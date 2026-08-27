/*-------------------------------------------------------------------------------------------------
**
** BoardPins.h
**
**    Hardware definition for the Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display
**    (480x480 IPS round touch knob screen, ESP32-S3-N16R8).
**
**    Every value here is transcribed from Elecrow's own factory firmware
**    (factory_soucecode/ESP32_Display_2_1-1), which is the code that ships on the
**    unit. Two other vendor sources disagree and are deliberately NOT followed:
**
**      - example/Simple example/Encoder_code  claims A=45, B=42, SW=41
**      - the Elecrow wiki page                claims encoder B=44
**
**    Both contradict the factory firmware. Trust this file; it is the one that was
**    verified against the running board.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Panel geometry
// ---------------------------------------------------------------------------
static constexpr int16_t PANEL_WIDTH  = 480;
static constexpr int16_t PANEL_HEIGHT = 480;

static constexpr int16_t PANEL_CENTER_X = PANEL_WIDTH / 2;
static constexpr int16_t PANEL_CENTER_Y = PANEL_HEIGHT / 2;
static constexpr int16_t PANEL_RADIUS   = PANEL_WIDTH / 2;   // 240

// The addressable area is the full 480x480 square and the glass fills it — the
// round look comes from the board's bezel, not from a smaller active area. So
// layouts use the whole panel and are NOT inset. An earlier revision backed off
// to a 228px "safe radius" and simply wasted a visible ring of screen.
static constexpr int16_t PANEL_SAFE_RADIUS = PANEL_RADIUS;

// ---------------------------------------------------------------------------
// ST7701S RGB parallel bus
// ---------------------------------------------------------------------------
static constexpr int8_t LCD_CS    = 16;   // 3-wire SPI, init sequence only
static constexpr int8_t LCD_SCK   = 2;
static constexpr int8_t LCD_SDA   = 1;

static constexpr int8_t LCD_DE    = 40;
static constexpr int8_t LCD_VSYNC = 7;
static constexpr int8_t LCD_HSYNC = 15;
static constexpr int8_t LCD_PCLK  = 41;

static constexpr int8_t LCD_R0 = 46, LCD_R1 = 3,  LCD_R2 = 8,  LCD_R3 = 18, LCD_R4 = 17;
static constexpr int8_t LCD_G0 = 14, LCD_G1 = 13, LCD_G2 = 12, LCD_G3 = 11, LCD_G4 = 10, LCD_G5 = 9;
static constexpr int8_t LCD_B0 = 5,  LCD_B1 = 45, LCD_B2 = 48, LCD_B3 = 47, LCD_B4 = 21;

// Panel timing.
//
// SOURCE OF TRUTH: example/Arduino/RotaryScreen_2_1/RotaryScreen_2_1.ino in
// Elecrow's repo for this exact board. That file uses the SAME modern
// Arduino_GFX API this firmware uses, against a bundled copy of Arduino_GFX
// that is byte-identical to the 1.6.7 we build against. It is a reference
// implementation on an identical stack, not merely prior art.
//
// Two earlier attempts, both wrong, recorded so they are not retried:
//
//   1. factory_soucecode/ESP32_Display_2_1-1 — the porches below are right,
//      but it drives an older API and says nothing about polarity, clock or
//      bounce buffers. Sheared image.
//   2. example/esphome/*.yaml — h 20/10/10, v 8/10/10, 18MHz, pclk inverted.
//      Also sheared. ESPHome's st7701s component derives its peripheral
//      config differently, so its numbers do NOT transfer to Arduino_GFX.
//      Do not copy timings across driver stacks.
//
// The ST7701 fails soft: wrong values tear or shear the picture and never
// report an error. Do not tune these by feel.
static constexpr uint16_t LCD_HSYNC_FRONT_PORCH = 10;
static constexpr uint16_t LCD_HSYNC_PULSE_WIDTH = 4;
static constexpr uint16_t LCD_HSYNC_BACK_PORCH  = 20;
static constexpr uint16_t LCD_VSYNC_FRONT_PORCH = 10;
static constexpr uint16_t LCD_VSYNC_PULSE_WIDTH = 4;
static constexpr uint16_t LCD_VSYNC_BACK_PORCH  = 20;

// Sync polarity. Arduino_GFX INVERTS this on the way to ESP-IDF:
// Arduino_ESP32RGBPanel.cpp sets `hsync_idle_low = (hsync_polarity == 0)`.
// Passing 0 therefore makes sync idle LOW, which no working configuration for
// this panel does. The reference passes 1.
static constexpr uint16_t LCD_HSYNC_POLARITY = 1;
static constexpr uint16_t LCD_VSYNC_POLARITY = 1;

// Pixel clock: 12 MHz on the rising edge.
static constexpr int32_t  LCD_PCLK_HZ         = 12000000;
static constexpr uint16_t LCD_PCLK_ACTIVE_NEG = 0;

// DMA bounce buffers — two, 20 lines each.
//
// With 0 (the library default) the LCD GDMA fetches every pixel straight from
// PSRAM. The Arduino build here has CONFIG_LCD_RGB_RESTART_IN_VSYNC=y, so a
// missed DMA deadline makes the scanline pointer slip and then re-zero at each
// VSYNC — which renders as a STABLE diagonal shear rather than random flicker.
// That is exactly the artefact this board showed.
static constexpr size_t LCD_BOUNCE_BUFFER_PX = PANEL_WIDTH * 20;

// ---------------------------------------------------------------------------
// Backlight — LEDC PWM
// ---------------------------------------------------------------------------
static constexpr int8_t   BACKLIGHT_PIN        = 6;
static constexpr uint8_t  BACKLIGHT_CHANNEL    = 0;
static constexpr uint32_t BACKLIGHT_FREQ_HZ    = 19531;
static constexpr uint8_t  BACKLIGHT_RESOLUTION = 8;
static constexpr uint8_t  BACKLIGHT_DEFAULT    = 204;   // ~80%

// ---------------------------------------------------------------------------
// I2C — shared by the PCF8574 expander and the CST8xx touch controller
// ---------------------------------------------------------------------------
static constexpr int8_t   I2C_SDA_PIN = 38;
static constexpr int8_t   I2C_SCL_PIN = 39;
static constexpr uint32_t I2C_FREQ_HZ = 400000;

// ---------------------------------------------------------------------------
// PCF8574 I/O expander
//
// This part is load-bearing: LCD power and LCD reset both hang off it, so the
// panel cannot be brought up over GPIO alone.
// ---------------------------------------------------------------------------
static constexpr uint8_t PCF8574_ADDR = 0x21;

static constexpr uint8_t PCF_TOUCH_RST  = 0;   // P0
static constexpr uint8_t PCF_TOUCH_INT  = 2;   // P2
static constexpr uint8_t PCF_LCD_POWER  = 3;   // P3
static constexpr uint8_t PCF_LCD_RESET  = 4;   // P4  (active low)
static constexpr uint8_t PCF_ENCODER_SW = 5;   // P5  (input, pull-up, active low)

// ---------------------------------------------------------------------------
// CST8xx capacitive touch
// ---------------------------------------------------------------------------
static constexpr uint8_t TOUCH_I2C_ADDR = 0x15;

// ---------------------------------------------------------------------------
// Rotary encoder
//
// Only the two quadrature channels are real GPIO. The push switch is on the
// expander (PCF_ENCODER_SW), so it cannot be read with digitalRead().
// ---------------------------------------------------------------------------
static constexpr int8_t ENCODER_A_PIN = 42;
static constexpr int8_t ENCODER_B_PIN = 4;

// ---------------------------------------------------------------------------
// Ambient LEDs — NOT defined here on purpose.
//
// Elecrow's "Simple example/RGB_CODE" drives 5 WS2812s on GPIO48, but GPIO48 is
// the panel's B2 data line in the factory firmware. The two cannot both be true
// on one board, so that example is for a different revision. The ambient LEDs
// stay unclaimed until the pin is confirmed on this unit.
// ---------------------------------------------------------------------------
