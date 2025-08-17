#ifndef U8G2_ROBOEYES_H
#define U8G2_ROBOEYES_H

#include <Arduino.h>
#include <U8g2lib.h>

#define DEFAULT 0
#define TIRED   1
#define ANGRY   2
#define HAPPY   3

class U8g2RoboEyes {
  private:
    U8G2 *disp;
    int screenW, screenH;
    int frameInterval;
    unsigned long fpsTimer;

    // trạng thái mắt
    int eyeW, eyeH;
    int eyeX_L, eyeX_R, eyeY;
    bool eyeOpenL, eyeOpenR;
    bool cyclops;
    byte mood;

    // blink
    bool autoblink;
    int blinkInterval;
    unsigned long blinkTimer;

    // idle
    bool idle;
    unsigned long idleTimer;

    // animations
    bool flickerH, flickerV;
    int flickerAmpH, flickerAmpV;
    bool confused, laugh;
    unsigned long animStart;
    int animDuration;

  public:
    U8g2RoboEyes(U8G2 *u8g2) {
      disp = u8g2;
      screenW = 128;
      screenH = 64;
      frameInterval = 20;
      fpsTimer = 0;
      eyeW = 28;
      eyeH = 28;
      eyeX_L = 20;
      eyeX_R = 80;
      eyeY = 20;
      eyeOpenL = eyeOpenR = true;
      cyclops = false;
      mood = DEFAULT;
      autoblink = false;
      blinkInterval = 2000;
      blinkTimer = millis();
      idle = false;
      idleTimer = millis();

      flickerH = flickerV = false;
      flickerAmpH = flickerAmpV = 0;
      confused = laugh = false;
      animStart = 0;
      animDuration = 500;
    }

    void begin(int w, int h, int fps=30) {
      screenW = w; screenH = h;
      frameInterval = 1000 / fps;
      disp->clearBuffer();
      disp->sendBuffer();
    }

    // setters
    void setMood(byte m) { mood = m; }
    void setCyclops(bool c) { cyclops = c; }
    void setAutoblink(bool enable, int intervalMs=2000) {
      autoblink = enable; blinkInterval = intervalMs;
    }
    void setIdle(bool enable) { idle = enable; }

    // animations
    void blink() {
      eyeOpenL = eyeOpenR = false;
      blinkTimer = millis();
    }
    void animConfused(int duration=500) {
      confused = true; animStart = millis(); animDuration = duration;
    }
    void animLaugh(int duration=500) {
      laugh = true; animStart = millis(); animDuration = duration;
    }
    void setFlickerH(bool on, int amp=2) { flickerH = on; flickerAmpH = amp; }
    void setFlickerV(bool on, int amp=2) { flickerV = on; flickerAmpV = amp; }

    void update() {
      if (millis() - fpsTimer < frameInterval) return;
      fpsTimer = millis();

      // auto-blink
      if (autoblink && millis() - blinkTimer > blinkInterval) {
        blink();
      }
      // mở lại sau 200ms
      if (!eyeOpenL || !eyeOpenR) {
        if (millis() - blinkTimer > 200) {
          eyeOpenL = eyeOpenR = true;
        }
      }

      // idle
      if (idle && millis() - idleTimer > 3000) {
        eyeY = random(10, screenH - eyeH - 10);
        eyeX_L = random(5, screenW/2 - eyeW - 5);
        eyeX_R = random(screenW/2 + 5, screenW - eyeW - 5);
        idleTimer = millis();
      }

      // confused
      int offsetX = 0, offsetY = 0;
      if (confused) {
        if (millis() - animStart < animDuration) {
          offsetX = (millis()/50 % 2 == 0) ? -5 : 5;
        } else confused = false;
      }
      // laugh
      if (laugh) {
        if (millis() - animStart < animDuration) {
          offsetY = (millis()/50 % 2 == 0) ? -3 : 3;
        } else laugh = false;
      }
      // flicker
      if (flickerH) offsetX += (millis()/100 % 2 == 0) ? -flickerAmpH : flickerAmpH;
      if (flickerV) offsetY += (millis()/100 % 2 == 0) ? -flickerAmpV : flickerAmpV;

      drawEyes(offsetX, offsetY);
    }

  private:
    void drawEyes(int dx=0, int dy=0) {
      disp->clearBuffer();

      // mắt trái
      if (eyeOpenL) {
        disp->drawRBox(eyeX_L+dx, eyeY+dy, eyeW, eyeH, 6);
        drawMood(eyeX_L+dx, eyeY+dy, eyeW, eyeH);
      } else {
        disp->drawHLine(eyeX_L+dx, eyeY+dy+eyeH/2, eyeW);
      }

      // mắt phải
      if (!cyclops) {
        if (eyeOpenR) {
          disp->drawRBox(eyeX_R+dx, eyeY+dy, eyeW, eyeH, 6);
          drawMood(eyeX_R+dx, eyeY+dy, eyeW, eyeH);
        } else {
          disp->drawHLine(eyeX_R+dx, eyeY+dy+eyeH/2, eyeW);
        }
      }

      disp->sendBuffer();
    }

    void drawMood(int x, int y, int w, int h) {
      switch(mood) {
        case TIRED: disp->drawLine(x, y, x+w, y+h/3); break;
        case ANGRY: disp->drawLine(x, y, x+w, y); break;
        case HAPPY: disp->drawLine(x, y+h, x+w, y+h); break;
      }
    }
};

#endif
