/*-------------------------------------------------------------------------------------------------
**
** Expander.h
**
**    Minimal PCF8574 driver over Wire, for the CrowPanel 2.1" rotary display.
**
**    The PCF8574 has no direction register. Every pin is quasi-bidirectional:
**    writing a 1 releases the pin to a weak pull-up, which is also how you read
**    it as an input. So a shadow byte is mandatory — a pin used as an input must
**    be held high in every write, or the next write drives it low and the input
**    reads 0 forever.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "BoardPins.h"

class Expander {
public:
  /// Probes the device and drives the initial pin states.
  /// @param inputMask bitmask of pins used as inputs; these are pinned high.
  bool begin(uint8_t inputMask);

  /// Sets one output pin. Input pins are always forced high regardless.
  bool write(uint8_t pin, bool high);

  /// Reads one pin. Only meaningful for a pin included in the input mask.
  bool read(uint8_t pin);

  /// Reads the whole port.
  uint8_t readPort();

  bool present() const { return _present; }

private:
  bool flush();

  uint8_t _shadow    = 0xFF;
  uint8_t _inputMask = 0x00;
  bool    _present   = false;
};

extern Expander expander;
