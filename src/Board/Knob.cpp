/*-------------------------------------------------------------------------------------------------
** Knob.cpp — quadrature encoder (GPIO, interrupt) + push switch (expander, polled).
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "Knob.h"

#include "Expander.h"

Knob knob;

namespace {

// Written from the ISR, read from a task. Only ever incremented/decremented by
// one detent, so a plain volatile int32_t is enough on a 32-bit core.
volatile int32_t g_position = 0;
volatile uint32_t g_lastEdgeUs = 0;

// Rejects contact bounce. A real detent on this encoder is far slower than this.
constexpr uint32_t ROTATE_DEBOUNCE_US = 1200;

}  // namespace

void IRAM_ATTR Knob::isrChannelA() {
  const uint32_t now = micros();
  if ((now - g_lastEdgeUs) < ROTATE_DEBOUNCE_US) {
    return;
  }
  g_lastEdgeUs = now;

  // Quadrature decode: sample B on an A edge. Which combination means "forward"
  // depends on how the encoder is wired, and cannot be derived from the pin
  // numbers — it has to be settled by turning the physical knob.
  //
  // Settled 2026-08-26 on hardware: with A=42/B=4 as wired on this board,
  // (a != b) is COUNTER-clockwise. Clockwise must increase volume, so the sense
  // is inverted here rather than by negating at the call sites, which would
  // leave knob.position() reading backwards.
  const int a = digitalRead(ENCODER_A_PIN);
  const int b = digitalRead(ENCODER_B_PIN);

  if (a != b) {
    g_position--;
  } else {
    g_position++;
  }
}

bool Knob::begin() {
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);

  g_position   = 0;
  _reported    = 0;
  g_lastEdgeUs = micros();

  attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), isrChannelA, CHANGE);

  if (!expander.present()) {
    log_w("expander absent — knob press will not be detected");
    return false;
  }

  // Switch is active low with a pull-up, so it idles high.
  _swDown = false;

  return true;
}

int32_t Knob::position() const {
  return g_position;
}

void Knob::pollSwitch() {
  const uint32_t now = millis();
  if ((now - _lastSwPoll) < SW_POLL_MS) {
    return;
  }
  _lastSwPoll = now;

  if (!expander.present()) {
    return;
  }

  // Active low.
  const bool down = !expander.read(PCF_ENCODER_SW);

  if (down != _swDown) {
    if ((now - _lastSwChange) < SW_DEBOUNCE_MS) {
      return;
    }
    _lastSwChange = now;
    _swDown       = down;

    if (down) {
      _pressedAt     = now;
      _longFired     = false;
      _veryLongFired = false;
    }
  }
}

bool Knob::poll(KnobEvent &out) {
  // Rotation first — it is the high-traffic event.
  const int32_t pos = g_position;
  if (pos != _reported) {
    int32_t delta = pos - _reported;

    // Clamp so one very fast spin cannot overflow the int8_t payload.
    if (delta > 127) {
      delta = 127;
    } else if (delta < -127) {
      delta = -127;
    }

    _reported += delta;
    out.type  = KnobEventType::Rotate;
    out.delta = (int8_t)delta;
    return true;
  }

  const bool wasDown = _swDown;
  pollSwitch();
  const uint32_t now = millis();

  // Long press fires once, while still held.
  if (_swDown && !_longFired && (now - _pressedAt) >= LONG_PRESS_MS) {
    _longFired = true;
    out.type   = KnobEventType::LongPress;
    out.delta  = 0;
    return true;
  }

  // Very long press fires once, later in the same hold.
  if (_swDown && _longFired && !_veryLongFired && (now - _pressedAt) >= VERY_LONG_PRESS_MS) {
    _veryLongFired = true;
    out.type       = KnobEventType::VeryLongPress;
    out.delta      = 0;
    return true;
  }

  // Short press fires on release, and only if the long press did not already.
  if (wasDown && !_swDown && !_longFired) {
    out.type  = KnobEventType::Press;
    out.delta = 0;
    return true;
  }

  out.type = KnobEventType::None;
  return false;
}
