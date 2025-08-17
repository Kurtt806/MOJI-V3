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

#include "wifi_manager.h"    // Quản lý WiFi (STA/AP/OFF)
#include "ota_manager.h"     // Quản lý cập nhật OTA
#include "web_draw_server.h" // Web + WebSocket vẽ và nhận frame
#include "gif_player.h"      // Phát GIF từ SPIFFS

uint64_t chipID = 0;

// ==== CONFIG ====
// Khởi tạo đối tượng màn hình OLED
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
QRCode qrcode;

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
int accelX, accelY, accelZ;
int gyroX, gyroY, gyroZ;
float angleX = 0, angleY = 0, angleZ = 0;
unsigned long lastUpdate = 0;

// --- Clock font selection (changeable from web) ---
const uint8_t *clockFonts[] = {
    u8g2_font_bubble_tn,
    u8g2_font_logisoso24_tf,
    u8g2_font_ncenB08_tr,
    u8g2_font_6x10_tr};
const char *clockFontNames[] = {"bubble_tn", "logisoso24", "ncenB08", "6x10"};
const uint8_t clockFontCount = sizeof(clockFonts) / sizeof(clockFonts[0]);
uint8_t clockFontIndex = 0; // default font index

// ==== Helper functions ====
bool MOJI_security()
{
  chipID = ESP.getEfuseMac(); // Lấy Chip ID từ eFuse
  if (MAC_KEY != chipID)
  {
    while (1)
    {
      Serial.print("Invalid KEY in code ERR ");
      Serial.println(chipID);
      delay(1000);
    }
  }
  return true;
}
void changeState(AppState newState)
{
  previousState = currentState;
  currentState = newState;
}
float convertRawGyro(int gRaw)
{
  return (gRaw * 250.0) / 32768.0;
}
float convertRawAccel(int aRaw)
{
  return (aRaw * 2.0) / 32768.0;
}
void setClockFontIndex(uint8_t idx)
{
  if (idx < clockFontCount)
    clockFontIndex = idx;
}
uint8_t getClockFontIndex()
{
  return clockFontIndex;
}
const char *getClockFontName()
{
  return clockFontNames[clockFontIndex];
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

// ==== Các màn hình ====
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
  u8g2.setFont(clockFonts[clockFontIndex]);
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

// Vẽ mắt
void drawEyes()
{
  static unsigned long lastBlink = 0;
  static bool isBlinking = false;
  static unsigned long blinkDuration = 0;
  static int eyeOffset = 0; // Độ lệch liếc mắt
  static unsigned long lastGlance = 0;
  static int glanceDir = 0;    // -1: trái, 0: giữa, 1: phải
  static float dizzyAngle = 0; // Góc xoay cho hiệu ứng choáng váng
  unsigned long now = millis();
  static bool happyEyes = false;
  static unsigned long lastHappy = 0;
  static bool isAngry = false;
  static unsigned long angryStart = 0;
  static bool isDizzy = false;
  static unsigned long dizzyStart = 0;

  // Xác suất xuất hiện mắt hạnh phúc mỗi 3~6 giây
  if (!happyEyes && (now - lastHappy > 3000))
  {
    if (esp_random() % 1000 < 8)
    { // ~0.8% mỗi frame
      happyEyes = true;
      lastHappy = now;
    }
  }
  // Hiệu ứng mắt hạnh phúc kéo dài 1~2 giây
  if (happyEyes && (now - lastHappy > 1200 + esp_random() % 800))
  {
    happyEyes = false;
    lastHappy = now;
  }

  // Kiểm tra va chạm mạnh (gyroX vượt ngưỡng)
  bool shaken = (abs(gyroX) > 8000);

  // Kiểm tra choáng váng (gyroX vượt ngưỡng)
  bool dizzyDetected = (abs(gyroX) > 25000);

  // Giữ trạng thái giận giữ trong 3 giây nếu phát hiện va chạm mạnh
  if (!isAngry && shaken)
  {
    isAngry = true;
    angryStart = now;
  }
  if (isAngry && (now - angryStart > 3000))
  {
    isAngry = false;
  }

  // Giữ trạng thái choáng váng trong 3 giây nếu phát hiện choáng váng
  if (!isDizzy && dizzyDetected)
  {
    isDizzy = true;
    dizzyStart = now;
  }
  if (isDizzy && (now - dizzyStart > 3000))
  {
    isDizzy = false;
  }

  // Nếu choáng váng, tăng góc xoay liên tục
  if (isDizzy)
  {
    dizzyAngle += 0.3f;
    if (dizzyAngle > 2 * PI)
      dizzyAngle -= 2 * PI;
  }
  else
  {
    // Nếu không choáng, giảm dần về 0
    if (dizzyAngle > 0.05f)
      dizzyAngle *= 0.95f;
    else
      dizzyAngle = 0;
  }

  // Xác suất liếc mắt ngẫu nhiên mỗi 1~2 giây
  if (now - lastGlance > 1200)
  {
    if (esp_random() % 1000 < 10) // Xác suất ~1% mỗi frame
    {
      glanceDir = (esp_random() % 3) - 1;             // -1, 0, 1
      eyeOffset = glanceDir * (4 + esp_random() % 6); // Độ lệch 4~9 px
      lastGlance = now;
    }
    else
    {
      glanceDir = 0;
      eyeOffset = 0;
    }
  }

  // Nếu không đang chớp mắt, xác suất nhỏ để bắt đầu chớp
  if (!isBlinking && (now - lastBlink > 1000))
  {
    if (esp_random() % 1000 < 5) // Xác suất ~0.5% mỗi frame
    {
      isBlinking = true;
      blinkDuration = 80 + (esp_random() % 100); // Thời gian chớp 80~180ms
      lastBlink = now;
    }
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  if (isAngry)
  {
    // Mắt giận giữ: vẽ 2 đường xéo xuống vào nhau cho 2 mắt, giữ trạng thái 2 giây
    // Vẽ nhiều đường gần nhau để tăng độ dầy nét
    for (int i = 0; i < 3; ++i)
    {
      u8g2.drawLine(22 + eyeOffset, 28 + i, 42 + eyeOffset, 32 + i);  // Mắt trái xéo xuống phải
      u8g2.drawLine(82 + eyeOffset, 32 + i, 102 + eyeOffset, 28 + i); // Mắt phải xéo xuống trái
    }
    // Có thể thêm hiệu ứng đỏ hoặc vẽ thêm lông mày xéo để thể hiện giận dữ
  }
  else if (isBlinking)
  {
    // Vẽ mắt nhắm (chỉ là đường ngang, liếc cả mắt)
    u8g2.drawRBox(22 + eyeOffset, 28, 20, 4, 2);
    u8g2.drawRBox(82 + eyeOffset, 28, 20, 4, 2);
    if (now - lastBlink > blinkDuration)
    {
      isBlinking = false;
      lastBlink = now;
    }
  }
  else if (dizzyAngle > 0.05f)
  {
    // Vẽ mắt choáng váng: vẽ hình tròn xoắn ốc thay cho con ngươi, nhiều vòng hơn và xoáy nhanh hơn
    for (int i = 0; i < 2; ++i)
    {
      int cx = (i == 0) ? (32 + eyeOffset) : (92 + eyeOffset);
      int cy = 30;
      // Tăng số vòng xoáy và tốc độ xoáy
      for (float t = 0; t < 4 * PI; t += 0.12f) // Nhiều vòng hơn, bước nhỏ hơn
      {
        float r = 7 + t * 2.5f;
        int x = cx + (int)(cos(t + dizzyAngle * 2.5f) * r); // Xoáy nhanh hơn
        int y = cy + (int)(sin(t + dizzyAngle * 2.5f) * r);
        for (int dx = -1; dx <= 1; ++dx)
          for (int dy = -1; dy <= 1; ++dy)
            u8g2.drawPixel(x + dx, y + dy);
      }
    }
  }
  else
  {
    // Vẽ mắt mở (liếc cả mắt)
    u8g2.drawRBox(22 + eyeOffset, 10, 20, 40, 10);
    u8g2.drawRBox(82 + eyeOffset, 10, 20, 40, 10);
    // Nếu happyEyes, vẽ ngôi sao ở giữa mắt
    if (happyEyes)
    {
      // vẽ ngôi sao 4 cánh ở giữa mắt, màu đen
    }
  }
  u8g2.sendBuffer();
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
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(5, 20, "TEXT");
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

  // Menu items
  const char *items[2] = {"AUTO", "STATIC"};
  const int itemCount = 2;
  const int itemHeight = 14;  // Match drawMenu's item height
  const int menuTop = 24;     // Match drawMenu's menu top position
  const int visibleCount = 2; // Display up to 2 items (same as itemCount for now)

  // Scrolling logic to match drawMenu
  int cursorRow = 1; // Cursor stops moving down at row 1 (0-based)
  int startIdx = 0;
  int mIndex = (runMode == RunMode::AUTO) ? 0 : 1; // Map runMode to index
  if (mIndex > cursorRow)
    startIdx = mIndex - cursorRow;

  // Ensure startIdx doesn't exceed bounds
  int maxStart = itemCount - visibleCount;
  if (maxStart < 0)
    maxStart = 0;
  if (startIdx > maxStart)
    startIdx = maxStart;

  // Calculate visual Y position for selected item
  int selectedVisualY = (mIndex > cursorRow) ? (menuTop + cursorRow * itemHeight) : (menuTop + mIndex * itemHeight);

  // Draw menu items
  for (int i = 0; i < visibleCount && (startIdx + i) < itemCount; ++i)
  {
    int idx = startIdx + i;
    int y = menuTop + i * itemHeight;

    if (idx == mIndex)
    {
      // Draw highlighted selection (white background)
      int rectY = selectedVisualY - (itemHeight - 4); // Match drawMenu's adjustment
      int rectH = itemHeight + 2;                     // Match drawMenu's height
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, rectY, u8g2.getWidth(), rectH);
      // Draw arrow and text in black (color 0)
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_unifont_t_symbols);
      u8g2.drawGlyph(2, selectedVisualY + 2, 0x25BA); // ►, match drawMenu's offset
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr(12, selectedVisualY + 2, items[idx]); // Match drawMenu's text offset
      u8g2.setDrawColor(1);                              // Restore default color
    }
    else
    {
      // Draw unselected item
      u8g2.setDrawColor(1);
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr(12, y + 2, items[idx]); // Match drawMenu's text offset
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
          // Trong màn hình MODE: 1 nhấn chuyển giữa AUTO <-> STATIC
          runMode = (runMode == RunMode::AUTO) ? RunMode::STATIC : RunMode::AUTO;
        }
        else
        {
          switch (currentState)
          {
          case AppState::CLOCK:
            changeState(AppState::EYES);
            break;
          case AppState::EYES:
            changeState(AppState::QR);
            break;
          case AppState::QR:
            changeState(AppState::TEXT);
            break; // Chuyển sang màn hình chữ
          case AppState::TEXT:
            changeState(AppState::DRAW);
            break; // Chuyển sang màn hình vẽ
          case AppState::DRAW:
            changeState(AppState::GIF);
            break; // Chuyển sang GIF
          case AppState::GIF:
            changeState(AppState::CLOCK);
            break; // Quay lại đồng hồ
          case AppState::WIFI:
          {
            // Chuyển mode WiFi: OFF -> STA -> AP -> OFF
            WifiStatus st = wifiGetStatus();
            WifiModeEx next = (st.mode == WifiModeEx::OFF) ? WifiModeEx::STA : (st.mode == WifiModeEx::STA ? WifiModeEx::AP : WifiModeEx::OFF);
            wifiSetMode(next);
            break;
          }
          default:
            break;
          }
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
        // Ngoài menu: không làm gì với double click
      }
      else if (ev == ButtonEvent::LONG)
      {
        if (currentState == AppState::MENU)
        {
          changeState(AppState::CLOCK); // Nhấn giữ trong MENU để về CLOCK
        }
        else
        {
          changeState(AppState::MENU); // Nhấn giữ ở các màn hình khác để vào MENU
          menuIndex = 0;               // Đặt lại vị trí chọn đầu tiên
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
  BMI160.begin(BMI160GenClass::I2C_MODE, Wire, 0x69);
  BMI160.setGyroRange(BMI160_GYRO_RANGE_250);
  BMI160.setAccelerometerRange(BMI160_ACCEL_RANGE_2G);
  float alpha = 0.98; // hệ số lọc
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
    angleZ += gZ * dt;
    vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz
  }
}

// ==== Task xử lý Web ====
void WebTask(void *pv)
{
  for (;;)
  {
    // Mỗi 60s check OTA 1 lần
    static unsigned long t = 0;
    if (millis() - t > 60000)
    {
      t = millis();
      otaHttpUpdate(SERVER_BASE_URL "/firmware.json");
    }
    otaLoop();
    webDrawServerLoop();
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay 10ms
  }
}

// ==== Task hiển thị ====
void DisplayTask(void *pv)
{
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

  MOJI_security();

  displayMutex = xSemaphoreCreateMutex();             // Tạo mutex cho màn hình
  buttonQueue = xQueueCreate(5, sizeof(ButtonEvent)); // Tạo hàng đợi cho nút nhấn

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), checkTicks, CHANGE);
  button.attachClick(singleClick);
  button.attachDoubleClick(doubleClick);
  button.attachLongPressStart(longPress);

  SPIFFS.begin(false);

  Wire.begin();

  if (!u8g2.begin())
  {
    for (size_t i = 0; i < 5; i++)
    {
      u8g2.begin();
      delay(500);
    }
    Serial.println("Failed to initialize OLED!");
  }

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // Đồng bộ thời gian qua NTP

  // Khởi động WiFi manager
  wifiManagerBegin();

  // Khởi động OTA (chỉ hoạt động khi WiFi STA kết nối)
  otaBegin();

  // Khởi động Web server + WebSocket để nhận frame vẽ
  webDrawServerBegin();

  // Khởi động GIF player (SPIFFS + AnimatedGIF)
  gifPlayerBegin();

  // Tạo task xử lý nút và hiển thị
  xTaskCreatePinnedToCore(ButtonTask, "ButtonTask", 4048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(DisplayTask, "DisplayTask", 10000, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(BMI160Task, "BMI160Task", 4000, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(WebTask, "WebTask", 10000, NULL, 2, NULL, 1);
}
void loop()
{
}
