/*-------------------------------------------------------------------------------------------------
**
** RoundDisplay.h
**
**    ST7701S 480x480 round RGB panel for the CrowPanel 2.1" rotary display.
**
**    Bring-up order is not optional. The panel's power rail and reset line both
**    hang off the PCF8574, so I2C and the expander must be alive before the RGB
**    bus is touched. Calling begin() out of order gives a dark or noisy panel
**    with no error, which is the failure this class exists to prevent.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <Arduino_GFX_Library.h>

#include "BoardPins.h"

class RoundDisplay {
public:
  /// Powers, resets and initializes the panel, then lights the backlight.
  /// Requires Wire and Expander to be started first.
  bool begin();

  /// 0..255. Applied through LEDC on BACKLIGHT_PIN.
  void setBacklight(uint8_t level);
  uint8_t backlight() const { return _backlight; }

  Arduino_RGB_Display *gfx() { return _gfx; }

  /// True once begin() has completed successfully.
  bool ready() const { return _ready; }

  /// Draws a bring-up test pattern: RGB wedges, a white boundary ring on the
  /// safe radius, and crosshairs through the centre. Any geometry error in the
  /// panel timing shows up here as tearing or an off-centre ring.
  void drawTestPattern();

  /// True when (x, y) falls inside the visible circle.
  static bool inCircle(int16_t x, int16_t y);

private:
  void powerOnSequence();

  // The ST7701 needs its init sequence over a 3-wire SPI (CS/SCK/SDA) that is
  // separate from the RGB data bus carrying pixels.
  Arduino_DataBus       *_swspi     = nullptr;
  Arduino_ESP32RGBPanel *_bus       = nullptr;
  Arduino_RGB_Display   *_gfx       = nullptr;
  uint8_t                _backlight = 0;
  bool                   _ready     = false;
};

extern RoundDisplay display;
