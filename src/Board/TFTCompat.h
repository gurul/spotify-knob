/*-------------------------------------------------------------------------------------------------
**
** TFTCompat.h
**
**    A TFT_eSPI-shaped facade over Arduino_GFX, so the ThingPulse drawing code
**    ports to the CrowPanel's RGB parallel panel without being rewritten.
**
**    TFT_eSPI cannot drive an RGB parallel panel at all — it speaks SPI to a
**    controller that holds its own framebuffer, whereas the ST7701 here is fed
**    a continuous pixel stream from PSRAM by the ESP32-S3's LCD peripheral. So
**    the backend genuinely had to change. What did NOT have to change is the
**    ~15-method drawing surface the application actually calls, which is what
**    this header preserves.
**
**    Every method below is used somewhere in DisplayUI.cpp or UIViews/. This
**    is deliberately not a general-purpose TFT_eSPI emulation: it covers the
**    real call sites and nothing more, so an unported call fails loudly at
**    compile time instead of silently drawing nothing.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <Arduino_GFX_Library.h>

#include "RoundDisplay.h"

// TFT_eSPI's colour constants. Same RGB565 values, so the ported drawing code
// keeps rendering identical colours on the new backend.
#ifndef TFT_BLACK
#define TFT_BLACK     0x0000
#define TFT_WHITE     0xFFFF
#define TFT_DARKGREY  0x7BEF
#define TFT_LIGHTGREY 0xD69A
#endif

// Panel geometry under TFT_eSPI's names. The round panel is square in memory,
// so width and height are equal — layout code must respect the inscribed
// circle (RoundDisplay::inCircle), not just these bounds.
#ifndef TFT_WIDTH
#define TFT_WIDTH  PANEL_WIDTH
#define TFT_HEIGHT PANEL_HEIGHT
#endif

/// Mirrors TFT_eSPI's setup_t closely enough for the diagnostics view, which is
/// the only consumer. Fields the RGB panel has no analogue for are reported as
/// zero rather than invented.
struct setup_t {
  const char *version    = "TFTCompat/Arduino_GFX";
  uint8_t     trans      = 0;
  uint16_t    tft_driver = 0x7701;
  int16_t     tft_width  = PANEL_WIDTH;
  int16_t     tft_height = PANEL_HEIGHT;
  uint8_t     serial     = 0;
  uint32_t    tft_spi_freq = 0;
};

class TFTCompat {
public:
  /// Binds to the already-initialized panel. RoundDisplay::begin() must have
  /// succeeded first — the power and reset lines it drives live on the I2C
  /// expander, so there is no way to recover that ordering from here.
  void attach(Arduino_RGB_Display *gfx) { _gfx = gfx; }

  /// Present so existing call sites compile. The real bring-up is
  /// RoundDisplay::begin(); this only asserts it already happened.
  void init() {
    if (_gfx == nullptr) {
      log_e("TFTCompat::init() before attach() — panel not brought up");
    }
  }

  // -- geometry ------------------------------------------------------------
  int16_t width()  { return _gfx ? _gfx->width()  : PANEL_WIDTH; }
  int16_t height() { return _gfx ? _gfx->height() : PANEL_HEIGHT; }

  void setRotation(uint8_t r) {
    if (_gfx) {
      _gfx->setRotation(r);
    }
  }

  // -- byte order ----------------------------------------------------------
  // TJpg_Decoder emits big-endian RGB565. TFT_eSPI handled that with a swap
  // flag; Arduino_GFX splits it into two different blit calls instead, so the
  // flag is kept here and selects between them in pushImage().
  void setSwapBytes(bool swap) { _swapBytes = swap; }
  bool getSwapBytes() const    { return _swapBytes; }

  // -- primitives ----------------------------------------------------------
  // OpenFontRender::setDrawer() binds against these three by name, so they are
  // required even though the application never calls them directly. Glyphs are
  // rasterized pixel by pixel between a startWrite/endWrite pair.
  void drawPixel(int32_t x, int32_t y, uint16_t color) {
    if (_gfx) _gfx->drawPixel(x, y, color);
  }

  void startWrite() {
    if (_gfx) _gfx->startWrite();
  }

  void endWrite() {
    if (_gfx) _gfx->endWrite();
  }

  void fillScreen(uint16_t color) {
    if (_gfx) _gfx->fillScreen(color);
  }

  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    if (_gfx) _gfx->fillRect(x, y, w, h, color);
  }

  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
    if (_gfx) _gfx->drawFastHLine(x, y, w, color);
  }

  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
    if (_gfx) _gfx->drawRoundRect(x, y, w, h, r, color);
  }

  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
    if (_gfx) _gfx->fillRoundRect(x, y, w, h, r, color);
  }

  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint16_t color) {
    if (_gfx) _gfx->fillTriangle(x0, y0, x1, y1, x2, y2, color);
  }

  void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color) {
    if (_gfx) _gfx->fillCircle(x, y, r, color);
  }

  /// Not a TFT_eSPI method — Arduino_GFX's annular arc, exposed for the round
  /// UI's perimeter progress/volume ring. Angles in degrees; 270 is 12 o'clock
  /// and increasing angles sweep clockwise on screen.
  void fillArc(int32_t x, int32_t y, int32_t r1, int32_t r2,
               float start, float end, uint16_t color) {
    if (_gfx) _gfx->fillArc(x, y, r1, r2, start, end, color);
  }

  /// Blits a 16-bit bitmap. Honours the swap-bytes flag set above.
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {
    if (_gfx == nullptr) {
      return;
    }
    if (_swapBytes) {
      _gfx->draw16bitBeRGBBitmap(x, y, data, w, h);
    } else {
      _gfx->draw16bitRGBBitmap(x, y, data, w, h);
    }
  }

  // -- diagnostics ---------------------------------------------------------
  void getSetup(setup_t &out) { out = setup_t{}; }

  /// The RGB panel path uses OpenFontRender for all text, so no TFT_eSPI
  /// bitmap fonts are ever loaded. Reporting zero is the truth, not a stub.
  uint16_t fontsLoaded() { return 0; }

  Arduino_RGB_Display *raw() { return _gfx; }

private:
  Arduino_RGB_Display *_gfx       = nullptr;
  bool                 _swapBytes = false;
};

/// Lets the ported ThingPulse sources keep their existing type name.
using TFT_eSPI = TFTCompat;
