/*-------------------------------------------------------------------------------------------------
**
** Touch.h
**
**    CST8xx capacitive touch for the CrowPanel 2.1" rotary display.
**
**    The controller's reset and interrupt lines are on the PCF8574 (P0 and P2),
**    not GPIO, so the reset pulse has to go over I2C before the controller will
**    answer on 0x15. This class also raises the raw stream to the gestures the
**    round UI needs: tap, and horizontal swipe for track skip.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <Adafruit_CST8XX.h>
#include <Arduino.h>

#include "BoardPins.h"

enum class TouchGesture : uint8_t {
  None,
  Tap,
  SwipeLeft,
  SwipeRight,
  SwipeUp,
  SwipeDown,
};

struct TouchEvent {
  TouchGesture gesture = TouchGesture::None;
  int16_t      x       = 0;   ///< position where the gesture started
  int16_t      y       = 0;
};

class Touch {
public:
  /// Resets the controller through the expander, then probes it on I2C.
  bool begin();

  /// True while a finger is down; fills x/y with the current point.
  bool raw(int16_t &x, int16_t &y);

  /// Drains one recognized gesture. Returns false when nothing completed.
  bool poll(TouchEvent &out);

  bool present() const { return _present; }

private:
  /// A drag shorter than this is a tap, not a swipe.
  static constexpr int16_t  SWIPE_MIN_PX  = 60;
  /// Held longer than this without moving, it is neither.
  static constexpr uint32_t GESTURE_MAX_MS = 900;

  Adafruit_CST8XX _ts;
  bool            _present   = false;

  bool     _down     = false;
  int16_t  _startX   = 0;
  int16_t  _startY   = 0;
  int16_t  _lastX    = 0;
  int16_t  _lastY    = 0;
  uint32_t _startMs  = 0;
};

extern Touch touch;
