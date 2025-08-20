#include "gif_player.h"
#include <AnimatedGIF.h>
#include <SPIFFS.h>
#include <U8g2lib.h>
#include <config.h>

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

AnimatedGIF gif;
File gifFile;

// AnimatedGIF file callbacks for SPIFFS
static void *GIFOpenFile(const char *fname, int32_t *pSize)
{
    if (SPIFFS.exists(fname))
    {
        gifFile = SPIFFS.open(fname, FILE_READ);
        if (gifFile)
        {
            *pSize = gifFile.size();
            return &gifFile;
        }
    }
    return NULL;
}

static void GIFCloseFile(void *pHandle)
{
    File *f = static_cast<File *>(pHandle);
    if (f)
        f->close();
}

static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    File *f = static_cast<File *>(pFile->fHandle);
    if (!f)
        return 0;
    int32_t toRead = iLen;
    if ((pFile->iSize - pFile->iPos) < iLen)
        toRead = pFile->iSize - pFile->iPos;
    if (toRead <= 0)
        return 0;
    int32_t r = (int32_t)f->read(pBuf, toRead);
    pFile->iPos = (int32_t)f->position();
    return r;
}

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
    File *f = static_cast<File *>(pFile->fHandle);
    if (!f)
        return -1;
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
}

uint8_t ucFrameBuffer[(DISPLAY_WIDTH * DISPLAY_HEIGHT) + ((DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8)];
static bool s_gifOpened = false;
static String s_currentAnimPath;
// playback speed multiplier: 1.0 = normal
static float g_gifSpeed = 1.0f;

bool gifPlayerBegin()
{
    gif.begin(GIF_PALETTE_1BPP_OLED);
    gifPlayerSetSpeed(1.0f);
    return true;
}

bool gifPlayerStep()
{
    int iFrame;
    // Keep a C-string buffer for opening the file via AnimatedGIF
    char selectedBuf[64] = {0};
    char szTemp[128];

    // If not opened yet, locate and open the current GIF
    if (!s_gifOpened)
    {
        int lastIdx = 0;
        if (SPIFFS.exists("/anim_index.txt"))
        {
            File idxf = SPIFFS.open("/anim_index.txt", FILE_READ);
            if (idxf)
            {
                String s = idxf.readString();
                lastIdx = s.toInt();
                idxf.close();
            }
        }
        for (int i = lastIdx; i >= 1; --i)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "/anim_%03d.gif", i);
            if (SPIFFS.exists(buf))
            {
                strncpy(selectedBuf, buf, sizeof(selectedBuf) - 1);
                break;
            }
        }

        if (selectedBuf[0] == '\0')
        {
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr); // pick a small readable font
            u8g2.drawStr(0, 12, "Error: cannot find any anim_*.gif");
            u8g2.drawStr(0, 28, "Upload GIFs via web UI");
            u8g2.sendBuffer();
            return false;
        }

        // Try opening the GIF
        if (gif.open(selectedBuf, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, NULL))
        {
            s_gifOpened = true;
            s_currentAnimPath = String(selectedBuf);
            gif.setFrameBuf(ucFrameBuffer);
            gif.setDrawType(GIF_DRAW_COOKED);

            // Check canvas size once
            int cw = gif.getCanvasWidth();
            int ch = gif.getCanvasHeight();
            if (cw != DISPLAY_WIDTH || ch != DISPLAY_HEIGHT)
            {
                snprintf(szTemp, sizeof(szTemp), "Warn: GIF %d x %d != %d x %d", cw, ch, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_ncenB08_tr);
                u8g2.drawStr(0, 12, szTemp);
                u8g2.sendBuffer();
                delay(1500); // show message briefly
            }
        }
        else
        {
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            char buf[64];
            snprintf(buf, sizeof(buf), "Error: cannot open %s", selectedBuf);
            u8g2.drawStr(0, 12, buf);
            u8g2.drawStr(0, 28, "Check SPIFFS or filename");
            u8g2.sendBuffer();
            return false;
        }
    }

    // If opened, play a single frame and return
    if (s_gifOpened)
    {
        size_t bitsSize = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8;
        uint8_t *pOneBit = &ucFrameBuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];

        int frameDelay = 0;
        if (gif.playFrame(false, &frameDelay))
        {
            u8g2.clearBuffer();
            uint8_t *dst = u8g2.getBufferPtr();
            if (dst != nullptr)
            {
                memcpy(dst, pOneBit, bitsSize);
            }
            u8g2.sendBuffer();
            // Apply speed multiplier to GIF frame delay (frameDelay in ms)
            int delayMs = frameDelay;
            if (g_gifSpeed > 0.0f)
                delayMs = (int)((float)frameDelay / g_gifSpeed);
            if (delayMs < 1)
                delayMs = 1;
            delay(delayMs);
            return true;
        }
        else
        {
            // EOF or error: close and mark as not opened so next call can open next file
            gif.close();
            s_gifOpened = false;
            s_currentAnimPath = String();
            return false;
        }
    }
    return false;
}

void gifPlayerSetSpeed(float speed)
{
    // enforce a small positive lower bound
    if (speed <= 0.01f)
        speed = 0.01f;
    g_gifSpeed = speed;
}

float gifPlayerGetSpeed()
{
    return g_gifSpeed;
}

// Đóng GIF hiện tại và chuyển sang GIF tiếp theo, nếu hết thì vòng lại từ đầu
void gifPlayerNextGif()
{
    if (s_gifOpened)
    {
        gif.close();
        s_gifOpened = false;
        s_currentAnimPath = String();
    }

    // Đọc chỉ số hiện tại từ anim_index.txt
    int lastIdx = 0;
    if (SPIFFS.exists("/anim_index.txt"))
    {
        File idxf = SPIFFS.open("/anim_index.txt", FILE_READ);
        if (idxf)
        {
            String s = idxf.readString();
            lastIdx = s.toInt();
            idxf.close();
        }
    }

    // Tìm tổng số GIF hiện có
    int maxIdx = 0;
    for (int i = 1; i < 1000; ++i)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "/anim_%03d.gif", i);
        if (SPIFFS.exists(buf))
            maxIdx = i;
        else
            break;
    }

    // Tăng chỉ số, nếu vượt quá thì quay lại 1
    int nextIdx = lastIdx + 1;
    if (nextIdx > maxIdx)
        nextIdx = 1;

    // Lưu lại chỉ số mới vào anim_index.txt
    File idxf = SPIFFS.open("/anim_index.txt", FILE_WRITE);
    if (idxf)
    {
        idxf.print(nextIdx);
        idxf.close();
    }
}