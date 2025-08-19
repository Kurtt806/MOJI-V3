#ifndef U8G2_ROBOEYES_H
#define U8G2_ROBOEYES_H

#include <Arduino.h>
#include <U8g2lib.h>

// Mood constants
#define MOOD_DEFAULT 0
#define MOOD_TIRED 1
#define MOOD_ANGRY 2
#define MOOD_HAPPY 3
#define MOOD_SURPRISED 4
#define MOOD_SLEEPY 5

// Animation constants
#define ANIM_NONE 0
#define ANIM_CONFUSED 1
#define ANIM_LAUGH 2
#define ANIM_SHAKE 3
#define ANIM_BOUNCE 4

class U8g2RoboEyes
{
private:
  // Display và config cơ bản
  U8G2 *disp;
  uint8_t screenW, screenH;
  uint16_t frameInterval;
  unsigned long fpsTimer;

  // Thuộc tính mắt - sử dụng struct để nhóm dữ liệu liên quan
  struct EyeProperties
  {
    uint8_t width = 28;
    uint8_t height = 40;
    uint8_t cornerRadius = 10;
    int16_t leftX = 20;
    int16_t rightX = 80;
    int16_t y = 20;
    bool openLeft = true;
    bool openRight = true;
  } eye;

  // Vị trí gốc để reset
  struct BasePosition
  {
    int16_t leftX = 20;
    int16_t rightX = 80;
    int16_t y = 20;
  } basePos;

  // Trạng thái và mood
  uint8_t mood = MOOD_DEFAULT;
  bool cyclops = false;

  // Blink system - tối ưu timing
  struct BlinkSystem
  {
    bool autoEnabled = false;
    uint16_t interval = 2000;
    uint16_t duration = 200;
    unsigned long timer = 0;
    bool isBlinking = false;
  } blinkSys;

  // Idle movement
  struct IdleSystem
  {
    bool enabled = false;
    uint16_t interval = 3000;
    unsigned long timer = 0;
    uint8_t intensity = 1;
  } idle;

  // Animation system - tối ưu hơn
  struct Animation
  {
    uint8_t type = ANIM_NONE;
    unsigned long startTime = 0;
    uint16_t duration = 500;
    uint8_t intensity = 1;
    int16_t offsetX = 0;
    int16_t offsetY = 0;
  } anim;

  // Flicker effects
  struct Flicker
  {
    bool horizontal = false;
    bool vertical = false;
    uint8_t ampH = 2;
    uint8_t ampV = 2;
  } flicker;

  // Smooth movement system
  struct SmoothMove
  {
    bool enabled = false;
    float smoothFactor = 0.3f;
    int16_t targetLeftX = 20;
    int16_t targetRightX = 80;
    int16_t targetY = 20;
  } smoothMove;

public:
  // Constructor với initialization list để tối ưu
  U8g2RoboEyes(U8G2 *u8g2) : disp(u8g2),
                             screenW(128),
                             screenH(64),
                             frameInterval(33), // ~30fps
                             fpsTimer(0)
  {
    blinkSys.timer = millis();
    idle.timer = millis();
  }

  // Khởi tạo với validation
  void begin(uint8_t w = 128, uint8_t h = 64, uint8_t fps = 30)
  {
    screenW = constrain(w, 64, 255);
    screenH = constrain(h, 32, 255);
    frameInterval = 1000 / constrain(fps, 1, 60);

    // Reset về vị trí trung tâm cho màn hình mới
    centerEyes();

    disp->clearBuffer();
    disp->sendBuffer();
  }

  // === SETTERS - Inline cho performance ===
  inline void setMood(uint8_t m)
  {
    if (m <= MOOD_SLEEPY)
      mood = m;
  }

  inline void setCyclops(bool c)
  {
    cyclops = c;
  }

  void setAutoblink(bool enable, uint16_t intervalMs = 2000, uint16_t durationMs = 200)
  {
    blinkSys.autoEnabled = enable;
    blinkSys.interval = constrain(intervalMs, 500, 10000);
    blinkSys.duration = constrain(durationMs, 50, 1000);
  }

  inline void setIdle(bool enable, uint8_t intensity = 1)
  {
    idle.enabled = enable;
    idle.intensity = constrain(intensity, 1, 5);
  }

  void setEyeSize(uint8_t w, uint8_t h, uint8_t cornerRadius = 6)
  {
    eye.width = constrain(w, 10, 50);
    eye.height = constrain(h, 10, 50);
    eye.cornerRadius = constrain(cornerRadius, 0, min(w, h) / 2);
  }

  // === ANIMATIONS - Cải thiện system ===
  void blink()
  {
    eye.openLeft = eye.openRight = false;
    blinkSys.timer = millis();
    blinkSys.isBlinking = true;
  }

  void startAnimation(uint8_t type, uint16_t duration = 500, uint8_t intensity = 1)
  {
    if (type <= ANIM_BOUNCE)
    {
      anim.type = type;
      anim.startTime = millis();
      anim.duration = constrain(duration, 100, 5000);
      anim.intensity = constrain(intensity, 1, 10);
    }
  }

  // Convenience methods
  inline void animConfused(uint16_t duration = 500)
  {
    startAnimation(ANIM_CONFUSED, duration);
  }

  inline void animLaugh(uint16_t duration = 500)
  {
    startAnimation(ANIM_LAUGH, duration);
  }

  inline void animShake(uint16_t duration = 300)
  {
    startAnimation(ANIM_SHAKE, duration);
  }

  inline void animBounce(uint16_t duration = 600)
  {
    startAnimation(ANIM_BOUNCE, duration);
  }

  void setFlicker(bool horizontal, bool vertical, uint8_t ampH = 2, uint8_t ampV = 2)
  {
    flicker.horizontal = horizontal;
    flicker.vertical = vertical;
    flicker.ampH = constrain(ampH, 1, 10);
    flicker.ampV = constrain(ampV, 1, 10);
  }

  // === POSITION CONTROL - Tối ưu validation ===
  void setEyePosition(int16_t leftX, int16_t rightX, int16_t y)
  {
    eye.leftX = constrain(leftX, 0, screenW - eye.width);
    eye.rightX = constrain(rightX, 0, screenW - eye.width);
    eye.y = constrain(y, 0, screenH - eye.height);
    smoothMove.enabled = false; // Stop smooth movement
  }

  void setEyeOffset(int16_t offsetX, int16_t offsetY)
  {
    setEyePosition(
        basePos.leftX + offsetX,
        basePos.rightX + offsetX,
        basePos.y + offsetY);
  }

  void enableSmoothMovement(float factor = 0.3f)
  {
    smoothMove.enabled = true;
    smoothMove.smoothFactor = constrain(factor, 0.1f, 1.0f);
  }

  void moveEyesTo(int16_t targetLeftX, int16_t targetRightX, int16_t targetY)
  {
    if (smoothMove.enabled)
    {
      smoothMove.targetLeftX = constrain(targetLeftX, 0, screenW - eye.width);
      smoothMove.targetRightX = constrain(targetRightX, 0, screenW - eye.width);
      smoothMove.targetY = constrain(targetY, 0, screenH - eye.height);
    }
    else
    {
      setEyePosition(targetLeftX, targetRightX, targetY);
    }
  }

  void centerEyes()
  {
    int16_t centerY = (screenH - eye.height) / 2;
    int16_t leftX = screenW / 4 - eye.width / 2;
    int16_t rightX = 3 * screenW / 4 - eye.width / 2;

    basePos.leftX = leftX;
    basePos.rightX = rightX;
    basePos.y = centerY;

    setEyePosition(leftX, rightX, centerY);
  }

  // === MOTION REACTION - Cải thiện thuật toán ===
  void reactToMotion(float angleX, float angleY, float intensity = 1.0f)
  {
    // Clamp intensity và tính toán offset
    intensity = constrain(intensity, 0.0f, 5.0f);
    int16_t offsetX = (int16_t)(angleX * intensity * 8);
    int16_t offsetY = (int16_t)(angleY * intensity * 8);

    setEyeOffset(offsetX, offsetY);

    // Dynamic mood và effects dựa trên intensity
    if (intensity > 3.0f)
    {
      setMood(MOOD_ANGRY);
      setFlicker(true, true, (uint8_t)intensity, (uint8_t)intensity / 2);
      startAnimation(ANIM_SHAKE, 200);
    }
    else if (intensity > 2.0f)
    {
      setMood(MOOD_SURPRISED);
      setFlicker(true, false, 2);
    }
    else if (intensity > 1.0f)
    {
      setMood(MOOD_TIRED);
      setFlicker(true, false, 1);
    }
    else
    {
      setMood(MOOD_DEFAULT);
      setFlicker(false, false);
    }
  }

  // === MAIN UPDATE LOOP - Tối ưu performance ===
  void update()
  {
    // Frame rate limiting
    unsigned long currentTime = millis();
    if (currentTime - fpsTimer < frameInterval)
      return;
    fpsTimer = currentTime;

    // Update systems
    updateBlink(currentTime);
    updateIdle(currentTime);
    updateAnimation(currentTime);
    updateSmoothMovement();

    // Calculate final offsets
    int16_t totalOffsetX = anim.offsetX;
    int16_t totalOffsetY = anim.offsetY;

    // Add flicker effects
    if (flicker.horizontal)
    {
      totalOffsetX += (currentTime / 100 % 2 == 0) ? -flicker.ampH : flicker.ampH;
    }
    if (flicker.vertical)
    {
      totalOffsetY += (currentTime / 100 % 2 == 0) ? -flicker.ampV : flicker.ampV;
    }

    drawEyes(totalOffsetX, totalOffsetY);
  }

  // === GETTERS ===
  void getEyePositions(int16_t &leftX, int16_t &rightX, int16_t &y) const
  {
    leftX = eye.leftX;
    rightX = eye.rightX;
    y = eye.y;
  }

  inline uint8_t getMood() const { return mood; }
  inline bool isCyclops() const { return cyclops; }
  inline bool isBlinking() const { return blinkSys.isBlinking; }

private:
  // === PRIVATE UPDATE METHODS ===
  void updateBlink(unsigned long currentTime)
  {
    if (blinkSys.autoEnabled && currentTime - blinkSys.timer > blinkSys.interval && !blinkSys.isBlinking)
    {
      blink();
    }

    // Reopen eyes after blink duration
    if (blinkSys.isBlinking && currentTime - blinkSys.timer > blinkSys.duration)
    {
      eye.openLeft = eye.openRight = true;
      blinkSys.isBlinking = false;
    }
  }

  void updateIdle(unsigned long currentTime)
  {
    if (!idle.enabled || currentTime - idle.timer <= idle.interval)
      return;

    // Random eye movement với giới hạn dựa trên intensity
    uint8_t moveRange = idle.intensity * 15;
    int16_t newY = random(max(0, basePos.y - moveRange),
                          min(screenH - eye.height, basePos.y + moveRange));
    int16_t newLeftX = random(max(0, basePos.leftX - moveRange / 2),
                              min(screenW / 2 - eye.width, basePos.leftX + moveRange / 2));
    int16_t newRightX = random(max(screenW / 2, basePos.rightX - moveRange / 2),
                               min(screenW - eye.width, basePos.rightX + moveRange / 2));

    moveEyesTo(newLeftX, newRightX, newY);
    idle.timer = currentTime;
  }

  void updateAnimation(unsigned long currentTime)
  {
    if (anim.type == ANIM_NONE)
      return;

    unsigned long elapsed = currentTime - anim.startTime;
    if (elapsed >= anim.duration)
    {
      anim.type = ANIM_NONE;
      anim.offsetX = anim.offsetY = 0;
      return;
    }

    // Calculate animation offsets based on type
    float progress = (float)elapsed / anim.duration;

    switch (anim.type)
    {
    case ANIM_CONFUSED:
      anim.offsetX = (currentTime / 80 % 2 == 0) ? -anim.intensity * 3 : anim.intensity * 3;
      break;

    case ANIM_LAUGH:
      anim.offsetY = (currentTime / 60 % 2 == 0) ? -anim.intensity * 2 : anim.intensity * 2;
      break;

    case ANIM_SHAKE:
      anim.offsetX = (currentTime / 40 % 2 == 0) ? -anim.intensity * 4 : anim.intensity * 4;
      anim.offsetY = (currentTime / 60 % 2 == 0) ? -anim.intensity : anim.intensity;
      break;

    case ANIM_BOUNCE:
      anim.offsetY = -abs(sin(progress * PI * 4)) * anim.intensity * 5;
      break;
    }
  }

  void updateSmoothMovement()
  {
    if (!smoothMove.enabled)
      return;

    // Smooth interpolation
    eye.leftX += (smoothMove.targetLeftX - eye.leftX) * smoothMove.smoothFactor;
    eye.rightX += (smoothMove.targetRightX - eye.rightX) * smoothMove.smoothFactor;
    eye.y += (smoothMove.targetY - eye.y) * smoothMove.smoothFactor;

    // Stop when close enough
    if (abs(eye.leftX - smoothMove.targetLeftX) < 1 &&
        abs(eye.rightX - smoothMove.targetRightX) < 1 &&
        abs(eye.y - smoothMove.targetY) < 1)
    {
      smoothMove.enabled = false;
    }
  }

  // === DRAWING METHODS ===
  void drawEyes(int16_t dx = 0, int16_t dy = 0)
  {
    disp->clearBuffer();

    // Left eye
    drawSingleEye(eye.leftX + dx, eye.y + dy, eye.openLeft);

    // Right eye (if not cyclops)
    if (!cyclops)
    {
      drawSingleEye(eye.rightX + dx, eye.y + dy, eye.openRight);
    }

    disp->sendBuffer();
  }

  void drawSingleEye(int16_t x, int16_t y, bool isOpen)
  {
    if (isOpen)
    {
      // Draw eye shape với corner radius
      if (eye.cornerRadius > 0)
      {
        disp->drawRBox(x, y, eye.width, eye.height, eye.cornerRadius);
      }
      else
      {
        disp->drawBox(x, y, eye.width, eye.height);
      }

      // Draw mood expression
      drawMoodExpression(x, y);

      // Draw pupil for some moods
      if (mood == MOOD_SURPRISED)
      {
        int16_t pupilX = x + eye.width / 2 - 2;
        int16_t pupilY = y + eye.height / 2 - 2;
        disp->drawDisc(pupilX, pupilY, 4);
      }
    }
    else
    {
      // Closed eye - horizontal line
      disp->drawHLine(x, y + eye.height / 2, eye.width);
    }
  }

  void drawMoodExpression(int16_t x, int16_t y)
  {
    switch (mood)
    {
    case MOOD_TIRED:
      // Droopy eyelid
      disp->drawLine(x, y, x + eye.width, y + eye.height / 3);
      break;

    case MOOD_ANGRY:
      // Angry brow
      disp->drawLine(x, y, x + eye.width, y);
      disp->drawLine(x, y - 2, x + eye.width / 2, y);
      break;

    case MOOD_HAPPY:
      // Happy line at bottom
      disp->drawLine(x + 2, y + eye.height - 2, x + eye.width - 2, y + eye.height - 2);
      break;

    case MOOD_SLEEPY:
      // Multiple droopy lines
      disp->drawLine(x, y + 2, x + eye.width, y + eye.height / 2);
      disp->drawLine(x, y + eye.height / 4, x + eye.width, y + eye.height / 2 + 2);
      break;
    }
  }
};

#endif