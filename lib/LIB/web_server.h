#pragma once

#include <Arduino.h>
#include <config.h>

// Size of the monochrome OLED buffer (1bpp)
constexpr size_t DRAW_BITMAP_SIZE = BITMAP_SIZE; // 128*64/8 = 1024

// Start HTTP (port 80) and WebSocket (port 81) servers
void webDrawServerBegin();

// Call periodically to handle WebSocket events
void webDrawServerLoop();

//Đưa con trỏ tới bộ đệm bitmap nhận được mới nhất (kích thước draw_bitmap_size)
//Nội dung bộ đệm có giá trị cho đến khi nhận được khung tiếp theo.
//trả về nullptr nếu chưa có khung.
const uint8_t* webDrawGetBitmap();

// Try to copy latest bitmap into provided buffer; returns true on success.
bool webDrawCopyBitmap(uint8_t* out, size_t len);

// Load saved drawing from flash into current bitmap buffer; returns true on success.
bool webDrawLoadFromFlash();
