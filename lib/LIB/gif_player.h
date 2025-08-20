#pragma once

#include <Arduino.h>

// Initialize filesystem and GIF player
bool gifPlayerBegin();

// Play one step/frame if a GIF exists (prefers /anim.gif, otherwise /anim_###.gif); returns true if a frame was drawn
bool gifPlayerStep();

// Set/get playback speed multiplier (1.0 = normal, 2.0 = twice as fast, 0.5 = half speed)
void gifPlayerSetSpeed(float speed);
float gifPlayerGetSpeed();

// Advance to next GIF file (cycle). Safe to call from other tasks; the caller
// should take the display mutex if required by the application.
void gifPlayerNextGif();