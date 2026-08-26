/*-------------------------------------------------------------------------------------------------
**
** Knob.h
**
**    Rotary encoder plus push switch for the CrowPanel 2.1" rotary display.
**
**    The two quadrature channels are real GPIO and are read by interrupt. The
**    push switch is NOT — it sits on PCF8574 P5, so it can only be reached by an
**    I2C transaction and must be polled from a task, never from an ISR.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>

#include "BoardPins.h"

enum class KnobEventType : uint8_t {
  None,
  Rotate,     ///< delta carries the signed detent count
  Press,      ///< short press, emitted on release
  LongPress,  ///< emitted once, while still held
};

struct KnobEvent {
  KnobEventType type  = KnobEventType::None;
  int8_t        delta = 0;
};

class Knob {
public:
  /// Attaches the encoder interrupts. Requires Expander to be started first,
  /// because the switch is read through it.
  bool begin();

  /// Drains one event. Returns false when the queue is empty. Call from a task.
  bool poll(KnobEvent &out);

  /// Accumulated detent position since boot. Signed, clockwise positive.
  int32_t position() const;

private:
  static void IRAM_ATTR isrChannelA();
  void pollSwitch();

  static constexpr uint32_t LONG_PRESS_MS   = 600;
  static constexpr uint32_t SW_DEBOUNCE_MS  = 30;
  static constexpr uint32_t SW_POLL_MS      = 20;

  uint32_t _pressedAt     = 0;
  uint32_t _lastSwPoll    = 0;
  uint32_t _lastSwChange  = 0;
  bool     _swDown        = false;
  bool     _longFired     = false;
  int32_t  _reported      = 0;
};

extern Knob knob;
