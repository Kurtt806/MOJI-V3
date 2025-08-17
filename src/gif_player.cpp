#include "gif_player.h"
#include <AnimatedGIF.h>
#include <SPIFFS.h>
#include <U8g2lib.h>
#include <config.h> // Chứa DISPLAY_WIDTH, DISPLAY_HEIGHT

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

static AnimatedGIF gif;
static File gifFile;
static bool gifOpen = false;
static unsigned long nextFrameAt = 0;
static uint8_t frameBuf[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
static bool frameStarted = false;

// Callback đọc file
static int32_t GIFReadCallback(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  File *f = (File *)pFile->fHandle;
  if (pBuf) {
    int32_t n = f->read(pBuf, iLen);
    pFile->iPos += n;
    return n;
  }
  f->seek(pFile->iPos + iLen);
  pFile->iPos += iLen;
  return iLen;
}

// Callback seek file
static int32_t GIFSeekCallback(GIFFILE *pFile, int32_t iPosition) {
  File *f = (File *)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = iPosition;
  return iPosition;
}

// Callback mở file
static void *GIFOpenCallback(const char *szFilename, int32_t *pFileSize) {
  gifFile = SPIFFS.open(szFilename, FILE_READ);
  if (!gifFile) return NULL;
  if (pFileSize) *pFileSize = gifFile.size();
  return (void *)&gifFile;
}

// Callback đóng file
static void GIFCloseCallback(void *pHandle) {
  File *f = (File *)pHandle;
  if (f) f->close();
}

// Callback vẽ từng dòng của ảnh GIF
static void GIFDrawCallback(GIFDRAW *pDraw) {
  // Nếu chưa bắt đầu frame, xóa bộ đệm frameBuf về 0
  if (!frameStarted) {
    memset(frameBuf, 0x00, sizeof(frameBuf)); // Xóa sạch bộ đệm ảnh
    frameStarted = true; // Đánh dấu đã bắt đầu frame
  }

  // Tính toán vị trí dòng đích trên màn hình
  const int dstY = pDraw->iY + pDraw->y;
  // Nếu dòng nằm ngoài màn hình thì bỏ qua
  if (dstY < 0 || dstY >= DISPLAY_HEIGHT) return;

  // Lấy chỉ số màu trong suốt (transparent)
  const uint8_t transparent = pDraw->ucTransparent;
  int w = pDraw->iWidth; // Độ rộng dòng cần vẽ
  int srcX0 = 0;         // Vị trí bắt đầu trên nguồn
  int dstX0 = pDraw->iX; // Vị trí bắt đầu trên đích

  // Nếu vị trí bắt đầu trên đích < 0, điều chỉnh lại vị trí và độ rộng
  if (dstX0 < 0) { 
    srcX0 -= dstX0; // Dịch chuyển vị trí nguồn
    w += dstX0;     // Giảm độ rộng
    dstX0 = 0;      // Đặt lại vị trí bắt đầu trên đích
  }
  // Nếu dòng vượt quá chiều rộng màn hình, cắt bớt độ rộng
  if (dstX0 + w > DISPLAY_WIDTH) { 
    w = DISPLAY_WIDTH - dstX0; 
  }
  // Nếu độ rộng <= 0 thì không vẽ gì
  if (w <= 0) return;

  // Trỏ tới dữ liệu pixel nguồn, đã dịch chuyển srcX0
  const uint8_t *px = pDraw->pPixels + srcX0;
  // Trỏ tới bảng màu 24-bit (RGB)
  const uint8_t *pal24 = pDraw->pPalette24;

  // Lặp qua từng pixel trên dòng
  for (int i = 0; i < w; ++i) {
    int dstX = dstX0 + i; // Vị trí pixel trên màn hình
    uint8_t idx = px[i];  // Chỉ số màu của pixel
    // Nếu pixel là màu trong suốt thì bỏ qua
    if (idx == transparent) continue;

    uint8_t luminance = 0; // Độ sáng của pixel
    if (pal24) {
      // Tính độ sáng từ giá trị RGB theo công thức Y = 0.299R + 0.587G + 0.114B
      const uint8_t *c = &pal24[idx * 3];
      luminance = (uint8_t)((77 * c[0] + 150 * c[1] + 29 * c[2]) >> 8);
    }

    // Nếu độ sáng lớn hơn ngưỡng (ở đây là 10), vẽ pixel lên bộ đệm
    if (luminance > 10) {
      size_t bitIndex = (size_t)dstY * DISPLAY_WIDTH + dstX; // Tính vị trí bit trong frameBuf
      frameBuf[bitIndex >> 3] |= (1 << (bitIndex & 7));      // Đặt bit tương ứng thành 1 (pixel sáng)
    }
  }
}


bool gifPlayerBegin() {
  gif.begin(0); // Không dùng chế độ 1bpp nội bộ, để callback xử lý
  gif.setDrawType(GIF_DRAW_COOKED); // Nhận pixel RGB888
  return true;
}

static bool openGifIfNeeded() {
  if (gifOpen) return true;
  if (!SPIFFS.exists("/anim.gif")) {
    Serial.println("GIF file not found");
    return false;
  }
  if (!gif.open("/anim.gif", GIFOpenCallback, GIFCloseCallback, GIFReadCallback, GIFSeekCallback, GIFDrawCallback)) {
    Serial.println("GIF open failed");
    return false;
  }
  gifOpen = true;
  nextFrameAt = 0;
  return true;
}

bool gifPlayerStep() {
  if (!openGifIfNeeded()) return false;
  unsigned long now = millis();
  if (now < nextFrameAt) return false;

  frameStarted = false;
  int delayMs = 0;
  if (!gif.playFrame(false, &delayMs, NULL)) {
    gif.close();
    gifOpen = false;
    return false;
  }

  u8g2.clearBuffer();
  u8g2.drawXBM(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, frameBuf);
  u8g2.sendBuffer();

  nextFrameAt = millis() + (unsigned long)delayMs;
  return true;
}
