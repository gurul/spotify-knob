/*-------------------------------------------------------------------------------------------------
** RoundDisplay.cpp — ST7701S RGB panel bring-up. See RoundDisplay.h for ordering.
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#include "RoundDisplay.h"

#include "Expander.h"

RoundDisplay display;

void RoundDisplay::powerOnSequence() {
  // Reset asserted (active low) before power, so the controller never sees a
  // clock edge while its rail is still rising.
  expander.write(PCF_LCD_RESET, false);
  delay(20);

  expander.write(PCF_LCD_POWER, true);
  delay(100);

  expander.write(PCF_LCD_RESET, true);
  delay(120);   // ST7701 needs >=120ms after reset release before commands
}

bool RoundDisplay::begin() {
  if (!expander.present()) {
    log_e("expander absent — cannot power the panel");
    return false;
  }

  powerOnSequence();

  // 3-wire SPI, used only to push the ST7701 init sequence. No DC and no MISO:
  // the controller takes 9-bit commands where the 9th bit is the D/C flag.
  _swspi = new Arduino_SWSPI(
      GFX_NOT_DEFINED /* DC */, LCD_CS, LCD_SCK, LCD_SDA, GFX_NOT_DEFINED /* MISO */);

  _bus = new Arduino_ESP32RGBPanel(
      LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
      LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
      LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
      LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
      LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
      LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
      LCD_PCLK_ACTIVE_NEG,
      LCD_PCLK_HZ,
      false /* useBigEndian */,
      0 /* de_idle_high */,
      0 /* pclk_idle_high */,
      LCD_BOUNCE_BUFFER_PX);

  if (_swspi == nullptr || _bus == nullptr) {
    log_e("RGB bus allocation failed");
    return false;
  }

  // RST is GFX_NOT_DEFINED because reset is on the expander, already driven above.
  _gfx = new Arduino_RGB_Display(
      PANEL_WIDTH, PANEL_HEIGHT,
      _bus,
      0 /* rotation */,
      true /* auto_flush */,
      _swspi,
      GFX_NOT_DEFINED /* RST */,
      st7701_type5_init_operations, sizeof(st7701_type5_init_operations));

  if (_gfx == nullptr) {
    log_e("panel allocation failed");
    return false;
  }

  // The 480x480x16bpp framebuffer is 460 KB and lives in PSRAM. Without octal
  // PSRAM enabled this call fails, and it is the usual cause of a dark panel.
  if (!_gfx->begin()) {
    log_e("gfx->begin() failed — check that octal PSRAM is enabled");
    return false;
  }

  _gfx->fillScreen(RGB565_BLACK);

  setBacklight(BACKLIGHT_DEFAULT);
  _ready = true;

  return true;
}

void RoundDisplay::setBacklight(uint8_t level) {
  static bool attached = false;

  if (!attached) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(BACKLIGHT_PIN, BACKLIGHT_FREQ_HZ, BACKLIGHT_RESOLUTION);
#else
    ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ_HZ, BACKLIGHT_RESOLUTION);
    ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
#endif
    attached = true;
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BACKLIGHT_PIN, level);
#else
  ledcWrite(BACKLIGHT_CHANNEL, level);
#endif

  _backlight = level;
}

bool RoundDisplay::inCircle(int16_t x, int16_t y) {
  const int32_t dx = (int32_t)x - PANEL_CENTER_X;
  const int32_t dy = (int32_t)y - PANEL_CENTER_Y;
  return (dx * dx + dy * dy) <= ((int32_t)PANEL_SAFE_RADIUS * PANEL_SAFE_RADIUS);
}

void RoundDisplay::drawTestPattern() {
  if (!_ready) {
    return;
  }

  // Bands run edge to edge across the whole 480x480, not clipped to a circle.
  // This is also the diagnostic for how much of the panel the bezel actually
  // shows: if the corner markers below are visible, the addressable square is
  // fully exposed and layouts can use it.
  const uint16_t bands[3] = {RGB565_RED, RGB565_GREEN, RGB565_BLUE};
  for (int16_t y = 0; y < PANEL_HEIGHT; y++) {
    _gfx->drawFastHLine(0, y, PANEL_WIDTH, bands[(y / 60) % 3]);
  }

  // Corner markers. Whether these survive the bezel decides whether the UI can
  // use the corners or must stay inside the inscribed circle.
  const int16_t m = 28;
  _gfx->fillRect(0, 0, m, m, RGB565_WHITE);
  _gfx->fillRect(PANEL_WIDTH - m, 0, m, m, RGB565_WHITE);
  _gfx->fillRect(0, PANEL_HEIGHT - m, m, m, RGB565_WHITE);
  _gfx->fillRect(PANEL_WIDTH - m, PANEL_HEIGHT - m, m, m, RGB565_WHITE);

  // Inscribed circle at the full radius, plus crosshairs. Wrong panel timing
  // shows up here as an ellipse, a shear, or an off-centre ring.
  _gfx->drawCircle(PANEL_CENTER_X, PANEL_CENTER_Y, PANEL_RADIUS - 1, RGB565_BLACK);
  _gfx->drawCircle(PANEL_CENTER_X, PANEL_CENTER_Y, PANEL_RADIUS - 2, RGB565_BLACK);
  _gfx->drawFastHLine(PANEL_CENTER_X - 40, PANEL_CENTER_Y, 80, RGB565_WHITE);
  _gfx->drawFastVLine(PANEL_CENTER_X, PANEL_CENTER_Y - 40, 80, RGB565_WHITE);

  _gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  _gfx->setTextSize(3);
  _gfx->setCursor(PANEL_CENTER_X - 96, PANEL_CENTER_Y + 70);
  _gfx->print("SPOTIFY KNOB");
}
