/*-------------------------------------------------------------------------------------------------
**
** TFT_eSPI.h  — SHIM, not the real library.
**
**    The real TFT_eSPI is NOT a dependency of this port and is not in
**    platformio.ini. It speaks SPI to a controller that owns its own
**    framebuffer, which cannot drive the CrowPanel's ST7701 RGB parallel
**    panel — that panel is fed a continuous pixel stream from PSRAM by the
**    ESP32-S3 LCD peripheral.
**
**    This header exists so the ported sources keep their original
**    `#include <TFT_eSPI.h>` line and their `TFT_eSPI *` types. It redirects
**    to the Arduino_GFX-backed facade in src/Board/TFTCompat.h.
**
**    If you are looking for why a TFT_eSPI call does not compile: TFTCompat
**    implements only the methods this application actually uses. That is
**    deliberate — an unported call should fail at build time, not silently
**    draw nothing at runtime. Add the method to TFTCompat.h.
**
** SPDX-License-Identifier: MIT
** ------------------------------------------------------------------------------------------------
*/

#pragma once

#include "../src/Board/TFTCompat.h"
