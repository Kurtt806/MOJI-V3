#pragma once

#include <Arduino.h>

// Initialize filesystem and GIF player
bool gifPlayerBegin();

// Play one step/frame if a GIF exists (e.g. /anim.gif); returns true if a frame was drawn
bool gifPlayerStep();
