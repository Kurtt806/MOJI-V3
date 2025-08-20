#include <esp_efuse.h>
#include <Arduino.h> // Thư viện Arduino cơ bản
#include <SPIFFS.h>  // Thư viện hệ thống tệp SPIFFS
#include <Wire.h>    // Thư viện I2C
#include <U8g2lib.h> // Thư viện điều khiển màn hình OLED
#include <qrcode.h>
#include <BMI160Gen.h> // Thư viện cảm biến BMI160
#include <OneButton.h> // Thư viện xử lý nút nhấn đa chức năng
#include <WiFi.h>      // Thư viện WiFi cho ESP32
#include <config.h>    // File cấu hình phần cứng và thông số

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// QR Code object
QRCode qrcode;
uint64_t chipID = 0;

// ==== CONFIG ====
#include "wifi_manager.h"    // Quản lý WiFi (STA/AP/OFF)
#include "ota_manager.h"     // Quản lý cập nhật OTA
#include "web_server.h" // Web + WebSocket vẽ và nhận frame
#include "gif_player.h"      // Phát GIF từ SPIFFS

// ==== RTOS ====
SemaphoreHandle_t displayMutex; // Mutex bảo vệ truy cập màn hình
QueueHandle_t buttonQueue;      // Hàng đợi sự kiện nút nhấn

// ==== Kiểu dữ liệu ====
// Kiểu dữ liệu cho sự kiện nút nhấn
enum class ButtonEvent : uint8_t
{
  SINGLE,
  DOUBLE,
  LONG
};

// Trạng thái ứng dụng (các màn hình) - declared in include
#include "app_state.h"

// ==== Biến toàn cục ====
// AppState globals are defined here
AppState currentState = AppState::CLOCK;  // Trạng thái hiện tại của app
AppState previousState = AppState::CLOCK; // Trạng thái trước đó để phát hiện chuyển đổi
RunMode runMode = RunMode::AUTO;          // Chế độ hoạt động hiện tại
OneButton button(BUTTON_PIN, false);      // Đối tượng nút nhấn, active high

uint8_t menuIndex = 0;
// Default screen selection (selectable from MODE menu)
AppState defaultScreen = AppState::CLOCK;
uint8_t defaultScreenIndex = 0;
const AppState screenOptions[] = {AppState::CLOCK, AppState::EYES, AppState::QR, AppState::TEXT, AppState::DRAW, AppState::GIF};
const char *screenNames[] = {"CLOCK", "EYES", "QR", "TEXT", "DRAW", "GIF"};
const uint8_t screenCount = sizeof(screenOptions) / sizeof(screenOptions[0]);
int accelX, accelY, accelZ;
int gyroX, gyroY, gyroZ;
float angleX = 0, angleY = 0, angleZ = 0;
unsigned long lastUpdate = 0;


// --- Text font selection (for TEXT screen) ---
// Text fonts are defined in src/text_fonts.cpp
#include "text_fonts.h"
uint8_t textFontIndex = 3; // default to small

// Expose set/get functions for web server
void setTextFontIndex(uint8_t idx)
{
  if (idx < textFontCount)
  {
    textFontIndex = idx;
    // persist to SPIFFS
    File f = SPIFFS.open("/text_font", FILE_WRITE);
    if (f)
    {
      f.print((int)textFontIndex);
      f.close();
    }
  }
}

uint8_t getTextFontIndex()
{
  return textFontIndex;
}

void loadTextFontFromSPIFFS()
{
  if (!SPIFFS.exists("/text_font"))
    return;
  File f = SPIFFS.open("/text_font", FILE_READ);
  if (!f)
    return;
  String s = f.readStringUntil('\n');
  f.close();
  int idx = s.toInt();
  if (idx >= 0 && idx < (int)textFontCount)
    textFontIndex = idx;
}

// ==== Helper functions ====
bool MOJI_security(int key)
{
  chipID = ESP.getEfuseMac(); // Lấy Chip ID từ eFuse
  if (key != chipID)
  {
    while (1)
    {
      Serial.print("Chip ID: ");
      Serial.println(chipID);
      delay(1000);
    }
  }
  Serial.print("Chip ID: ");
  Serial.println(chipID);
  return true;
}
void changeState(AppState newState)
{
  previousState = currentState;
  currentState = newState;
}

// --- Persist default screen to SPIFFS ---
void loadDefaultScreenFromSPIFFS()
{
  if (!SPIFFS.exists("/default_screen"))
    return;
  File f = SPIFFS.open("/default_screen", FILE_READ);
  if (!f)
    return;
  String s = f.readStringUntil('\n');
  f.close();
  int idx = s.toInt();
  if (idx >= 0 && idx < (int)screenCount)
  {
    defaultScreenIndex = idx;
    defaultScreen = screenOptions[defaultScreenIndex];
  }
}

bool saveDefaultScreenToSPIFFS()
{
  File f = SPIFFS.open("/default_screen", FILE_WRITE);
  if (!f)
  {
    Serial.println("Failed to open /default_screen for writing");
    return false;
  }
  f.print(defaultScreenIndex);
  f.close();
  Serial.printf("Saved default screen index=%d (%s)\n", defaultScreenIndex, screenNames[defaultScreenIndex]);
  return true;
}
float convertRawGyro(int gRaw)
{
  return (gRaw * 250.0) / 32768.0;
}
float convertRawAccel(int aRaw)
{
  return (aRaw * 2.0) / 32768.0;
}

// ==== Button callbacks ====
void IRAM_ATTR checkTicks()
{
  button.tick();
}

// Callback cho từng loại nhấn nút
void singleClick()
{
  ButtonEvent ev = ButtonEvent::SINGLE; // Nhấn 1 lần
  xQueueSend(buttonQueue, &ev, 0);      // Gửi sự kiện vào hàng đợi
}
void doubleClick()
{
  ButtonEvent ev = ButtonEvent::DOUBLE; // Nhấn 2 lần
  xQueueSend(buttonQueue, &ev, 0);
}
void longPress()
{
  ButtonEvent ev = ButtonEvent::LONG; // Nhấn giữ lâu
  xQueueSend(buttonQueue, &ev, 0);
}

//=======================================================================
//=======================================================================
//=======================================================================
// ==== Các màn hình ====================================================
//=======================================================================
//=======================================================================
//=======================================================================
// Vẽ màn hình đồng hồ
void drawClock()
{
  u8g2.clearBuffer();

  time_t t = time(nullptr);
  // Nếu chưa sync NTP (t < 1640995200 là trước 2022)
  if (t < 1640995200)
  {
    const char *syncStr = "SYNC Wifi...";
    const char *logoStr = LOGO;
    const char *versionStr = VERSION;

    // Dùng font đang chọn cho logo (nếu hợp lệ), nếu không thì fallback
    u8g2.setFont(u8g2_font_bubble_tr);
    int logoWidth = u8g2.getStrWidth(LOGO);
    int logoX = (u8g2.getWidth() - logoWidth) / 2;
    // baseline y cho logo - đặt ở phần trên để các dòng còn lại không bị tràn
    u8g2.drawStr(logoX, 24, LOGO);

    // Font nhỏ hơn cho trạng thái và version
    u8g2.setFont(u8g2_font_6x10_tr);
    int syncWidth = u8g2.getStrWidth(syncStr);
    int syncX = (u8g2.getWidth() - syncWidth) / 2;
    int syncY = 44;
    u8g2.drawStr(syncX, syncY, syncStr);

    u8g2.setFont(u8g2_font_4x6_tf);
    int versionWidth = u8g2.getStrWidth(versionStr);
    int versionY = 60; // gần đáy màn hình, vẫn trong 64px
    u8g2.drawStr(105, versionY, versionStr);

    u8g2.sendBuffer();
    return;
  }

  struct tm tm;
  localtime_r(&t, &tm);

  // Ngày/thứ ở phía trên
  u8g2.setFont(u8g2_font_6x10_tr);
  const char *weekday[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
  char dateBuf[32];
  snprintf(dateBuf, sizeof(dateBuf), "%s %02d/%02d/%04d", weekday[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
  u8g2.drawStr(5, 10, dateBuf);

  // Giờ:phút lớn, căn giữa
  u8g2.setFont(u8g2_font_bubble_tn);
  char timeMain[8];
  snprintf(timeMain, sizeof(timeMain), "%02d:%02d", tm.tm_hour, tm.tm_min);
  int16_t tw = u8g2.getStrWidth(timeMain);
  int tx = (u8g2.getWidth() - tw) / 2;
  // Chọn vị trí dọc phù hợp với font lớn
  int ty = 40; // baseline y, có thể điều chỉnh tuỳ font

  u8g2.drawStr(tx, ty, timeMain);

  // Thanh tiến trình giây phía dưới
  const int barW = 100;
  const int barH = 8;
  const int barX = (u8g2.getWidth() - barW) / 2;
  const int barY = 52;
  // Khung ngoài
  u8g2.drawFrame(barX, barY, barW, barH);
  // Tính tỉ lệ điền (tm.tm_sec = 0..59) -> dùng (tm+1)/60 để khi giây = 59 gần đầy
  int fillW = (int)(((tm.tm_sec + 1) / 60.0f) * (barW - 2));
  if (fillW > 0)
  {
    u8g2.drawBox(barX + 1, barY + 1, fillW, barH - 2);
  }
  u8g2.sendBuffer();
}

// Vẽ mắt với điều khiển chuyển động
void drawEyes()
{
}

// Vẽ màn hình QR
void drawQR()
{
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.setFont(u8g2_font_6x10_tr);

  // Lấy địa chỉ IP hiện tại
  WifiStatus st = wifiGetStatus();
  String ipStr = st.ip.toString();
  String url = "http://" + ipStr + "/";

  // Khởi tạo mã QR với URL thực tế
  const int QR_VERSION = 3; // Phiên bản 3 cho QR code (29x29)
  int qrSize = qrcode_getBufferSize(QR_VERSION);
  uint8_t qrcodeData[qrSize]; // Kích thước động dựa trên phiên bản QR
  QRCode qrcode;              // Khai báo biến QRCode
  qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, url.c_str());

  // Vẽ mã QR lên màn hình
  int qrPix = 2; // Mỗi module QR là 2x2 pixel
  int qrW = qrcode.size * qrPix;
  int qrX = 128 - qrW - 2; // Căn phải
  int qrY = 64 - qrW - 2;  // Căn dưới
  for (int y = 0; y < qrcode.size; y++)
  {
    for (int x = 0; x < qrcode.size; x++)
    {
      if (qrcode_getModule(&qrcode, x, y))
      {
        u8g2.drawBox(qrX + x * qrPix, qrY + y * qrPix, qrPix, qrPix);
      }
    }
  }

  // Thêm văn bản hướng dẫn
  u8g2.drawStr(5, 10, "Quet QR");
  u8g2.drawStr(5, 20, "truy cap");
  u8g2.drawStr(5, 30, "trang web");
  u8g2.sendBuffer();
}

// Vẽ màn hình chữ
void drawText()
{
  u8g2.clearBuffer();
  u8g2.drawRFrame(0, 0, 128, 52, 4);

  // Nội dung chính: đọc từ SPIFFS file /display_text.txt nếu có
  String mainText = "TEXT";
  if (SPIFFS.exists("/display_text.txt"))
  {
    File f = SPIFFS.open("/display_text.txt", FILE_READ);
    if (f)
    {
      mainText = f.readStringUntil('\n');
      f.close();
      if (mainText.length() == 0)
        mainText = "TEXT";
    }
  }

  // Set font and prepare scrolling if text is wider than available area
  u8g2.setFont(textFonts[textFontIndex]);
  const int innerMargin = 6; // left/right padding inside the rounded frame
  const int availableW = u8g2.getWidth() - innerMargin * 2;
  int textW = u8g2.getStrWidth(mainText.c_str());
  int ty = 26; // baseline gần giữa khung 52px

  // Persistent scrolling state
  static String cachedText = "";
  static int scrollX = 0;
  static unsigned long lastScrollMs = 0;
  static unsigned long pauseUntil = 0;
  const unsigned long scrollInterval = 60; // ms per pixel move
  const int scrollStep = 1;                // pixels per step

  if (mainText != cachedText)
  {
    // New text: reset scroll state
    cachedText = mainText;
    textW = u8g2.getStrWidth(cachedText.c_str());
    if (textW <= availableW)
    {
      // center if fits
      scrollX = (u8g2.getWidth() - textW) / 2 - innerMargin; // we'll add innerMargin when drawing
    }
    else
    {
      // start just beyond right edge of inner area
      scrollX = availableW;
      pauseUntil = millis() + 800; // short pause before starting to scroll
    }
    lastScrollMs = millis();
  }

  if (textW <= availableW)
  {
    int tx = (u8g2.getWidth() - textW) / 2;
    u8g2.drawStr(tx, ty, cachedText.c_str());
  }
  else
  {
    unsigned long now = millis();
    if (now >= pauseUntil)
    {
      if (now - lastScrollMs >= scrollInterval)
      {
        scrollX -= scrollStep;
        lastScrollMs = now;
      }
      // when text fully scrolled out to left, reset to start position with a pause
      if (scrollX < -textW)
      {
        scrollX = availableW;
        pauseUntil = now + 600;
      }
    }

    int drawX = scrollX + innerMargin; // account for left padding
    u8g2.drawStr(drawX, ty, cachedText.c_str());
  }

  // Lấy thời gian hiện tại
  time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);

  // Thứ (VN)
  const char *weekday[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

  // Hiển thị thứ ở bên dưới góc trái
  u8g2.setFont(u8g2_font_6x10_tr);
  int baseY = 64 - 2; // baseline phía dưới màn hình
  u8g2.drawStr(2, baseY, weekday[tm.tm_wday]);

  // Hiển thị giờ:phút ở bên dưới góc phải
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  int tw2 = u8g2.getStrWidth(timeBuf);
  u8g2.drawStr(u8g2.getWidth() - tw2 - 2, baseY, timeBuf);

  u8g2.sendBuffer();
}

// Vẽ màn hình vẽ
void drawDraw()
{
  // Kiểm tra nếu mới chuyển vào chế độ DRAW, thử tải drawing đã lưu
  if (previousState != AppState::DRAW && currentState == AppState::DRAW)
  {
    webDrawLoadFromFlash(); // Thử tải, không quan tâm kết quả
  }

  // Nếu có bitmap mới từ WebSocket hoặc đã tải từ flash, vẽ trực tiếp ra OLED bằng u8g2
  uint8_t buf[DRAW_BITMAP_SIZE];
  if (webDrawCopyBitmap(buf, sizeof(buf)))
  {
    u8g2.clearBuffer();
    // Vẽ bitmap lên màn hình, giả sử kích thước 128x64
    u8g2.drawXBMP(0, 0, 128, 64, buf);
    u8g2.sendBuffer();
  }
  else
  {
    // Chưa có dữ liệu: hiển thị khung hướng dẫn và mã QR
    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.setFont(u8g2_font_6x10_tr);

    // Lấy địa chỉ IP hiện tại
    WifiStatus st = wifiGetStatus();
    String ipStr = st.ip.toString();
    String url = "http://" + ipStr + "/";

    // Khởi tạo mã QR với URL thực tế
    const int QR_VERSION = 3; // Phiên bản 3 cho QR code (29x29)
    int qrSize = qrcode_getBufferSize(QR_VERSION);
    uint8_t qrcodeData[qrSize]; // Kích thước động dựa trên phiên bản QR
    QRCode qrcode;              // Khai báo biến QRCode
    qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, url.c_str());

    // Vẽ mã QR lên màn hình
    int qrPix = 2; // Mỗi module QR là 2x2 pixel
    int qrW = qrcode.size * qrPix;
    int qrX = 128 - qrW - 2; // Căn phải
    int qrY = 64 - qrW - 2;  // Căn dưới
    for (int y = 0; y < qrcode.size; y++)
    {
      for (int x = 0; x < qrcode.size; x++)
      {
        if (qrcode_getModule(&qrcode, x, y))
        {
          u8g2.drawBox(qrX + x * qrPix, qrY + y * qrPix, qrPix, qrPix);
        }
      }
    }

    // Thêm văn bản hướng dẫn
    u8g2.drawStr(5, 20, "Quet QR");
    u8g2.drawStr(5, 30, "truy cap");
    u8g2.drawStr(5, 40, "trang web");
    u8g2.sendBuffer();
  }
}

// Vẽ màn hình GIF
void drawGif()
{
  // Thử phát 1 frame GIF nếu có
  gifPlayerStep();
}

// Vẽ menu lựa chọn
void drawMenu()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB08_tf); // Font tiêu đề / nội dung
  u8g2.drawStr(80, 12, "MENU");       // Tiêu đề menu
  u8g2.drawLine(0, 14, 127, 14);
  const char *items[5] = {"MODE", "WIFI", "SETTING", "INFO", "-------"};
  const int itemCount = 5;
  const int itemHeight = 14;
  const int menuTop = 24;

  // Tối đa hiển thị 4 mục, cuộn khi cần
  int visibleCount = 4;
  // Hành vi mong muốn: con trỏ chỉ di chuyển xuống 1 lần (ví dụ tới hàng thứ 2 hiển thị),
  // nếu người dùng tiếp tục cuộn, nội dung menu sẽ dịch lên để con trỏ vẫn nằm ở cùng vị trí trên màn hình.
  int cursorRow = 1; // 0-based row in visible area where cursor should 'stop' moving down
  int startIdx = 0;
  int mIndex = (int)menuIndex;
  if (mIndex > cursorRow)
    startIdx = mIndex - cursorRow;
  // Đảm bảo startIdx không vượt quá giới hạn để vẫn hiển thị đủ visibleCount phần tử
  int maxStart = itemCount - visibleCount;
  if (maxStart < 0)
    maxStart = 0;
  if (startIdx > maxStart)
    startIdx = maxStart;

  // Tính vị trí hiển thị thực tế (visual) của dòng được chọn: nếu index <= cursorRow
  // cho phép con trỏ di chuyển xuống theo index, ngược lại khóa con trỏ ở cursorRow
  int selectedVisualY = (mIndex > cursorRow) ? (menuTop + cursorRow * itemHeight) : (menuTop + mIndex * itemHeight);

  for (int i = 0; i < visibleCount && (startIdx + i) < itemCount; ++i)
  {
    int idx = startIdx + i;
    int y = menuTop + i * itemHeight;

    if (idx == mIndex)
    {
      // Vẽ nền trắng cho dòng được chọn (dùng chiều rộng màn hình động)
      int rectY = selectedVisualY - (itemHeight - 4); // điều chỉnh mở rộng ô phía trên cho đẹp
      int rectH = itemHeight + 2;
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, rectY, u8g2.getWidth(), rectH);
      // Vẽ mũi tên + text màu nền (0) => hiển thị chữ đen trên nền trắng
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_unifont_t_symbols);
      // Glyph và text cần một offset dọc hợp lý; +2..+4 tuỳ font
      u8g2.drawGlyph(2, selectedVisualY + 2, 0x25BA); // ►
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr(12, selectedVisualY + 2, items[idx]);
      // Khôi phục màu vẽ mặc định (1)
      u8g2.setDrawColor(1);
    }
    else
    {
      // Dòng không được chọn: vẽ text bình thường (màu 1)
      u8g2.setDrawColor(1);
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr(12, y + 2, items[idx]);
    }
  }
  u8g2.sendBuffer();
}

// Vẽ màn hình chế độ
void drawMode()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB08_tf); // Font for title/content
  u8g2.drawStr(80, 12, "MODE");       // Title
  u8g2.drawLine(0, 14, 127, 14);      // Horizontal line
  // MODE: show Default screen selection using the same font/cursor behavior as drawMenu()
  const int itemHeight = 14;
  const int menuTop = 24;

  // Menu-like cursor/scroll behavior
  int itemCount = (int)screenCount;
  int visibleCount = 4;
  int cursorRow = 1; // visual row where cursor 'stops'
  int startIdx = 0;
  int mIndex = (int)defaultScreenIndex;
  if (mIndex > cursorRow)
    startIdx = mIndex - cursorRow;
  int maxStart = itemCount - visibleCount;
  if (maxStart < 0)
    maxStart = 0;
  if (startIdx > maxStart)
    startIdx = maxStart;

  int selectedVisualY = (mIndex > cursorRow) ? (menuTop + cursorRow * itemHeight) : (menuTop + mIndex * itemHeight);

  for (int i = 0; i < visibleCount && (startIdx + i) < itemCount; ++i)
  {
    int idx = startIdx + i;
    int y = menuTop + i * itemHeight;

    if (idx == mIndex)
    {
      // highlight selected line
      int rectY = selectedVisualY - (itemHeight - 4);
      int rectH = itemHeight + 2;
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, rectY, u8g2.getWidth(), rectH);
      // draw arrow and text inverted
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_unifont_t_symbols);
      u8g2.drawGlyph(2, selectedVisualY + 2, 0x25BA); // ►
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr(12, selectedVisualY + 2, screenNames[idx]);
      u8g2.setDrawColor(1);
    }
    else
    {
      u8g2.setDrawColor(1);
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr(12, y + 2, screenNames[idx]);
    }
  }

  u8g2.sendBuffer();
}

// Vẽ màn hình WiFi
void drawWifi()
{
  WifiStatus st = wifiGetStatus(); // Lấy trạng thái WiFi
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  const char *modeStr = (st.mode == WifiModeEx::OFF) ? "OFF" : (st.mode == WifiModeEx::STA ? "STA" : "AP"); // Chuyển mode thành chuỗi
  u8g2.drawStr(0, 10, "WIFI");
  char line[32];
  snprintf(line, sizeof(line), "MODE: %s", modeStr); // Hiển thị mode
  u8g2.drawStr(0, 22, line);
  if (st.mode == WifiModeEx::STA)
  {
    snprintf(line, sizeof(line), "STA: %s", st.connected ? "OK" : "..."); // Trạng thái kết nối
    u8g2.drawStr(0, 34, line);
    if (st.connected)
    {
      snprintf(line, sizeof(line), "IP: %s", st.ip.toString().c_str()); // Hiển thị IP
      u8g2.drawStr(0, 46, line);
      snprintf(line, sizeof(line), "RSSI: %d", st.rssi); // Hiển thị RSSI
      u8g2.drawStr(0, 58, line);
    }
  }
  else if (st.mode == WifiModeEx::AP)
  {
    snprintf(line, sizeof(line), "AP IP: %s", st.ip.toString().c_str()); // Hiển thị IP AP
    u8g2.drawStr(0, 34, line);
  }
  u8g2.sendBuffer();
}

// Vẽ màn hình cài đặt
void drawSetting()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "SETTING");
  int state = -1;
  int progress = -1;
  const char *msg = nullptr;
  // call if available in ota_manager
  // (If these functions are not yet implemented in ota_manager, add them.)
  state = otaGetState();
  progress = otaGetProgress();
  msg = otaGetMessage();

  const char *stateStr = "UNK";
  switch (state)
  {
  case 0:
    stateStr = "IDLE";
    break;
  case 1:
    stateStr = "CHECK";
    break;
  case 2:
    stateStr = "DOWN";
    break;
  case 3:
    stateStr = "OK";
    break;
  case 4:
    stateStr = "FAIL";
    break;
  default:
    stateStr = "UNK";
    break;
  }

  char line[32];
  snprintf(line, sizeof(line), "OTA: %s", stateStr);
  u8g2.drawStr(0, 26, line);

  if (progress >= 0 && progress <= 100)
  {
    const int barW = 100;
    const int barH = 8;
    const int barX = 5;
    const int barY = 36;
    u8g2.drawFrame(barX, barY, barW, barH);
    int fillW = (progress * (barW - 2)) / 100;
    if (fillW > 0)
      u8g2.drawBox(barX + 1, barY + 1, fillW, barH - 2);

    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%d%%", progress);
    u8g2.drawStr(barX + barW + 4, barY + barH - 1, pbuf);
  }
  else
  {
    u8g2.drawStr(0, 44, "Progress: -");
  }

  // Show message if present
  if (msg && msg[0] != '\0')
  {
    // Limit to one line near bottom
    u8g2.drawStr(0, 58, msg);
  }

  u8g2.sendBuffer();
}

// Vẽ màn hình thông tin
void drawInfo()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, "VER: 3.0.1");   // Hiển thị phiên bản
  u8g2.drawStr(0, 22, "AUTHOR: KHOA"); // Hiển thị tác giả
  u8g2.sendBuffer();
}

/********************************************************************
 *
 *
 *
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// ==== Task xử lý nút ====
void ButtonTask(void *pv)
{
  ButtonEvent ev;
  for (;;)
  {
    // Nhận sự kiện nút nhấn từ hàng đợi
    if (xQueueReceive(buttonQueue, &ev, pdMS_TO_TICKS(5)) == pdPASS)
    {
      // Xử lý sự kiện nút nhấn
      if (ev == ButtonEvent::SINGLE)
      {
        if (currentState == AppState::MENU)
        {
          menuIndex = (menuIndex + 1) % 4; // Di chuyển xuống menu, 4 items (MODE, WIFI, SETTING, INFO)
        }
        else if (currentState == AppState::MODE)
        {
          // Trong màn hình MODE: 1 nhấn di chuyển lựa chọn Default Screen
          defaultScreenIndex = (defaultScreenIndex + 1) % screenCount;
        }
        else if (currentState == AppState::SETTING)
        {
          // Khi đang ở màn hình SETTING, nhấn 1 lần gọi OTA từ server
          Serial.println("Starting OTA HTTP update...");
          otaHttpUpdate(SERVER_BASE_URL);
        }
          else if (currentState == AppState::GIF)
          {
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
              gifPlayerNextGif();
              xSemaphoreGive(displayMutex);
            }
          }
          else
        {
          // Single-tap outside MENU/MODE/SETTING no longer cycles screens.
          // Screen changes should be made via the MENU (double-click to select)
          // or other explicit controls. Keep WiFi mode and other special
          // behaviors handled in their dedicated screens.
          // No-op here.
        }
      }
  else if (ev == ButtonEvent::DOUBLE)
      {
        if (currentState == AppState::MENU)
        {
          // Chọn mục trong menu
          switch (menuIndex)
          {
          case 0:
            changeState(AppState::MODE);
            break; // Chọn MODE
          case 1:
            changeState(AppState::WIFI);
            break; // Chọn WIFI
          case 2:
            changeState(AppState::SETTING);
            break; // Chọn SETTING
          case 3:
            changeState(AppState::INFO);
            break; // Chọn INFO
          }
        }
        else if (currentState == AppState::MODE)
        {
          // Double-click in MODE: confirm selection, persist and switch to it
          defaultScreenIndex = defaultScreenIndex % screenCount;
          defaultScreen = screenOptions[defaultScreenIndex];
          saveDefaultScreenToSPIFFS();
          changeState(defaultScreen);
        }
        // Ngoài menu: không làm gì với double click
      }
      else if (ev == ButtonEvent::LONG)
      {
        if (currentState == AppState::MENU)
        {
          changeState(defaultScreen); // Nhấn giữ trong MENU để về màn hình mặc định
        }
        else
        {
          // Long press outside MENU enters MENU. If already in MODE, toggle runMode.
          if (currentState == AppState::MODE)
          {
            runMode = (runMode == RunMode::AUTO) ? RunMode::STATIC : RunMode::AUTO;
          }
          else
          {
            changeState(AppState::MENU); // Nhấn giữ ở các màn hình khác để vào MENU
            menuIndex = 0;               // Đặt lại vị trí chọn đầu tiên
          }
        }
      }
    }
    // Xử lý nút nhấn
    button.tick();                 // Cần gọi liên tục để phát hiện sự kiện
    vTaskDelay(pdMS_TO_TICKS(20)); // Delay 20ms
  }
}

// ==== Hàm chuyển đổi giá trị cảm biến ====
void BMI160Task(void *pv)
{
  BMI160.setGyroRange(BMI160_GYRO_RANGE_250);
  BMI160.setAccelerometerRange(BMI160_ACCEL_RANGE_2G);
  float alpha = 0.98;
  int ax, ay, az, gx, gy, gz;
  lastUpdate = millis();
  for (;;)
  {
    BMI160.readMotionSensor(ax, ay, az, gx, gy, gz);
    gyroX = gx;
    gyroY = gy;
    float gX = convertRawGyro(gx);
    float gY = convertRawGyro(gy);
    float gZ = convertRawGyro(gz);
    float aX = convertRawAccel(ax);
    float aY = convertRawAccel(ay);
    float aZ = convertRawAccel(az);
    unsigned long now = millis();
    float dt = (now - lastUpdate) / 1000.0f;
    lastUpdate = now;
    // Góc từ Accelerometer (Pitch, Roll)
    float accAngleX = atan2f(aY, aZ) * 180 / PI;
    float accAngleY = atan2f(-aX, sqrtf(aY * aY + aZ * aZ)) * 180 / PI;

    // Complementary Filter
    angleX = alpha * (angleX + gX * dt) + (1 - alpha) * accAngleX;
    angleY = alpha * (angleY + gY * dt) + (1 - alpha) * accAngleY;
    // angleZ += gZ * dt;
    vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz
  }
}

// ==== Task xử lý Web ====
void WebTask(void *pv)
{
  for (;;)
  {
    otaLoop();
    webDrawServerLoop();
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay 10ms
  }
}

// ==== Task hiển thị ====
void DisplayTask(void *pv)
{
  u8g2.begin();
  loadDefaultScreenFromSPIFFS();
  changeState(defaultScreen);
  
  for (;;)
  {
    // Vẽ màn hình tương ứng với trạng thái hiện tại
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      switch (currentState)
      {
      case AppState::CLOCK:
        drawClock();
        break;
      case AppState::EYES:
        drawEyes();
        break;
      case AppState::QR:
        drawQR();
        break;
      case AppState::TEXT:
        drawText();
        break;
      case AppState::DRAW:
        drawDraw();
        break;
      case AppState::GIF:
        drawGif();
        break;
      case AppState::MENU:
        drawMenu();
        break;
      case AppState::MODE:
        drawMode();
        break;
      case AppState::WIFI:
        drawWifi();
        break;
      case AppState::SETTING:
        drawSetting();
        break;
      case AppState::INFO:
        drawInfo();
        break;
      case AppState::EXIT:
        u8g2.clearBuffer();
        u8g2.sendBuffer();
        break; // Màn hình trống khi EXIT
      }
      xSemaphoreGive(displayMutex);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  // bảo mật từng thiết bị
  // MOJI_security(MAC_KEY_YELLOW);

  displayMutex = xSemaphoreCreateMutex();             // Tạo mutex cho màn hình
  buttonQueue = xQueueCreate(5, sizeof(ButtonEvent)); // Tạo hàng đợi cho nút nhấn

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), checkTicks, CHANGE);
  button.attachClick(singleClick);
  button.attachDoubleClick(doubleClick);
  button.attachLongPressStart(longPress);

  SPIFFS.begin(false);
  // load persisted text font selection
  loadTextFontFromSPIFFS();
  

  Wire.begin(I2C_SDA, I2C_SCL);

  // Khởi động WiFi manager
  wifiManagerBegin();

  // Khởi động OTA (chỉ hoạt động khi WiFi STA kết nối)
  otaBegin();

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // Đồng bộ thời gian qua NTP

  // Khởi động Web server + WebSocket để nhận frame vẽ
  webDrawServerBegin();

  // Khởi động GIF player (SPIFFS + AnimatedGIF)
  gifPlayerBegin();

  // Tạo task xử lý nút và hiển thị
  xTaskCreatePinnedToCore(ButtonTask, "ButtonTask", 10000, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(DisplayTask, "DisplayTask", 10000, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(WebTask, "WebTask", 10000, NULL, 2, NULL, 1);

  if (BMI160.begin(BMI160GenClass::I2C_MODE, Wire, 0x69))
  {
    xTaskCreatePinnedToCore(BMI160Task, "BMI160Task", 4000, NULL, 2, NULL, 1);
  }
}
void loop()
{
}
