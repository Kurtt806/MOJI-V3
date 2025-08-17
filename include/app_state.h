#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

// Application screens/states
enum class AppState : uint8_t {
  CLOCK,
  EYES,
  QR,
  TEXT,
  DRAW,
  GIF,
  MENU,
  MODE,
  WIFI,
  SETTING,
  INFO,

  EXIT
};

// Change the global application state (implemented in main.cpp)
void changeState(AppState newState);

// Globals defined in main.cpp
extern AppState currentState;
extern AppState previousState;


// Chế độ hoạt động: Auto hoặc Static
enum class RunMode : uint8_t
{
  AUTO, 
  STATIC
};
extern RunMode runMode;

#endif // APP_STATE_H
