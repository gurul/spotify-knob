/*-------------------------------------------------------------------------------------------------
** Expander.cpp — PCF8574 driver. See Expander.h for why the shadow byte matters.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "Expander.h"

Expander expander;

bool Expander::begin(uint8_t inputMask) {
  _inputMask = inputMask;

  // Start with every pin high. That is the safe state: outputs are inactive and
  // inputs are released to their pull-ups.
  _shadow = 0xFF;

  Wire.beginTransmission(PCF8574_ADDR);
  _present = (Wire.endTransmission() == 0);
  if (!_present) {
    log_e("PCF8574 not found at 0x%02X — LCD power and reset are unreachable", PCF8574_ADDR);
    return false;
  }

  return flush();
}

bool Expander::flush() {
  // Inputs must read back, so force them high on every write.
  const uint8_t out = _shadow | _inputMask;

  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(out);
  return Wire.endTransmission() == 0;
}

bool Expander::write(uint8_t pin, bool high) {
  if (pin > 7) {
    return false;
  }
  if (high) {
    _shadow |= (uint8_t)(1u << pin);
  } else {
    _shadow &= (uint8_t)~(1u << pin);
  }
  return flush();
}

uint8_t Expander::readPort() {
  if (Wire.requestFrom((uint8_t)PCF8574_ADDR, (uint8_t)1) != 1) {
    return 0xFF;
  }
  return (uint8_t)Wire.read();
}

bool Expander::read(uint8_t pin) {
  if (pin > 7) {
    return true;
  }
  return (readPort() & (uint8_t)(1u << pin)) != 0;
}
