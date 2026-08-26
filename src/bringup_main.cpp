/*-------------------------------------------------------------------------------------------------
**
** bringup_main.cpp
**
**    Phase 1 entry point: proves the CrowPanel 2.1" hardware before any of the
**    Spotify application is ported onto it.
**
**    Every line it prints is an acceptance gate in GATES.md. The markers are
**    matched literally by `hwlog wait`, so do not reword them without editing
**    the ledger to match.
**
**    Built only when BRINGUP is defined; the real application owns main.cpp.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#ifdef BRINGUP

#include <Arduino.h>
#include <Wire.h>

#include "Board/BoardPins.h"
#include "Board/Expander.h"
#include "Board/Knob.h"
#include "Board/RoundDisplay.h"
#include "Board/Touch.h"

static void scanI2C() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("I2C scan complete, %d device(s)\n", found);
}

void setup() {
  Serial.begin(115200);

  // Native USB-Serial/JTAG drops everything written before the host opens the
  // port, so the boot marker would be lost on a cold start without this wait.
  const uint32_t waitUntil = millis() + 2500;
  while (!Serial && millis() < waitUntil) {
    delay(10);
  }
  delay(200);

  Serial.println();
  Serial.println("SPOTIFY-KNOB boot");
  Serial.printf("build %s %s\n", __DATE__, __TIME__);
  Serial.printf("PSRAM %u bytes, free heap %u\n",
                (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap());

  if (ESP.getPsramSize() == 0) {
    Serial.println("FATAL: no PSRAM — the 460KB framebuffer cannot be allocated");
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
  scanI2C();

  // P2 (touch INT) and P5 (encoder SW) are read back, so they must be held high.
  const uint8_t inputMask = (uint8_t)((1u << PCF_TOUCH_INT) | (1u << PCF_ENCODER_SW));
  if (expander.begin(inputMask)) {
    Serial.printf("EXPANDER ok 0x%02X\n", PCF8574_ADDR);
  } else {
    Serial.println("EXPANDER FAIL");
  }

  if (display.begin()) {
    display.drawTestPattern();
    Serial.printf("PANEL ok %dx%d\n", PANEL_WIDTH, PANEL_HEIGHT);
  } else {
    Serial.println("PANEL FAIL");
  }

  if (touch.begin()) {
    Serial.printf("TOUCH ok 0x%02X\n", TOUCH_I2C_ADDR);
  } else {
    Serial.println("TOUCH FAIL");
  }

  if (knob.begin()) {
    Serial.printf("KNOB ok A=%d B=%d SW=PCF.P%d\n",
                  ENCODER_A_PIN, ENCODER_B_PIN, PCF_ENCODER_SW);
  } else {
    Serial.println("KNOB FAIL");
  }

  Serial.println("bring-up ready — turn the knob, press it, touch the screen");
}

void loop() {
  KnobEvent ke;
  while (knob.poll(ke)) {
    switch (ke.type) {
      case KnobEventType::Rotate:
        // Clockwise must report dir=+1. If it reports -1, A and B are swapped
        // relative to BoardPins.h and the ledger's G6 has caught it.
        Serial.printf("KNOB dir=%+d delta=%d pos=%ld\n",
                      (ke.delta > 0) ? 1 : -1, (int)ke.delta, (long)knob.position());
        break;
      case KnobEventType::Press:
        Serial.println("KNOB press");
        break;
      case KnobEventType::LongPress:
        Serial.println("KNOB longpress");
        break;
      default:
        break;
    }
  }

  TouchEvent te;
  if (touch.poll(te)) {
    const char *name = "?";
    switch (te.gesture) {
      case TouchGesture::Tap:        name = "tap";    break;
      case TouchGesture::SwipeLeft:  name = "left";   break;
      case TouchGesture::SwipeRight: name = "right";  break;
      case TouchGesture::SwipeUp:    name = "up";     break;
      case TouchGesture::SwipeDown:  name = "down";   break;
      default: break;
    }
    Serial.printf("TOUCH x=%d y=%d gesture=%s\n", te.x, te.y, name);
  }

  delay(5);
}

#endif  // BRINGUP
