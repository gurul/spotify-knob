/*-------------------------------------------------------------------------------------------------
** Touch.cpp — CST8xx touch, reset over the expander, gestures raised from raw points.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "Touch.h"

#include "Expander.h"

Touch touch;

bool Touch::begin() {
  if (!expander.present()) {
    log_e("expander absent — cannot reset the touch controller");
    return false;
  }

  // INT high (idle), then pulse RST low. Without this the controller stays held
  // in reset and never acknowledges on 0x15.
  expander.write(PCF_TOUCH_INT, true);
  expander.write(PCF_TOUCH_RST, false);
  delay(20);
  expander.write(PCF_TOUCH_RST, true);
  delay(150);   // CST8xx needs time to run its own boot before it answers

  _present = _ts.begin(&Wire, TOUCH_I2C_ADDR);
  if (!_present) {
    log_e("CST8xx not responding at 0x%02X", TOUCH_I2C_ADDR);
  }

  return _present;
}

bool Touch::raw(int16_t &x, int16_t &y) {
  if (!_present || _ts.touched() == 0) {
    return false;
  }

  CST_TS_Point p = _ts.getPoint(0);
  x = p.x;
  y = p.y;
  return true;
}

bool Touch::poll(TouchEvent &out) {
  int16_t x = 0;
  int16_t y = 0;
  const bool down = raw(x, y);
  const uint32_t now = millis();

  if (down) {
    if (!_down) {
      _down    = true;
      _startX  = x;
      _startY  = y;
      _startMs = now;
    }
    _lastX = x;
    _lastY = y;
    return false;   // gestures resolve on release
  }

  if (!_down) {
    return false;
  }

  // Finger just lifted — classify.
  _down = false;

  if ((now - _startMs) > GESTURE_MAX_MS) {
    return false;   // a long rest, not a gesture
  }

  const int16_t dx = _lastX - _startX;
  const int16_t dy = _lastY - _startY;

  out.x = _startX;
  out.y = _startY;

  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy)) {
    out.gesture = (dx < 0) ? TouchGesture::SwipeLeft : TouchGesture::SwipeRight;
    return true;
  }

  if (abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx)) {
    out.gesture = (dy < 0) ? TouchGesture::SwipeUp : TouchGesture::SwipeDown;
    return true;
  }

  out.gesture = TouchGesture::Tap;
  return true;
}
