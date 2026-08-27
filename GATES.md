# GATES — Spotify Knob

Port `ThingPulse/esp32-spotify-remote` to the Elecrow CrowPanel 2.1" ESP32-S3 Rotary
Display. Reuse the Spotify / OAuth / album-art layer. Replace the display and input
layer. Verify every hardware claim on the real board.

## Target hardware (authoritative — from `factory_soucecode/ESP32_Display_2_1-1`)

| Item | Value |
|---|---|
| MCU | ESP32-S3-N16R8 — 16MB flash, 8MB **octal** PSRAM |
| Port | `/dev/cu.usbmodem11301` (native USB-Serial/JTAG, MAC `1C:DB:D4:B3:57:24`) |
| Panel | ST7701S, 480x480 round, **RGB parallel** — `Arduino_ST7701_RGBPanel`, `st7701_type5_init_operations`, BGR, IPS=false |
| RGB bus | CS 16, SCK 2, SDA 1, DE 40, VSYNC 7, HSYNC 15, PCLK 41 |
| RGB data | R 46/3/8/18/17 · G 14/13/12/11/10/9 · B 5/45/48/47/21 |
| Timing | hsync fp10 pw4 bp20 · vsync fp10 pw4 bp20 |
| I2C | SDA 38, SCL 39 |
| I/O expander | PCF8574 @ `0x21` — P0 touch RST, P2 touch INT, P3 LCD power, P4 LCD reset, P5 encoder SW (`INPUT_PULLUP`) |
| Touch | CST8xx @ `0x15` (`Adafruit_CST8XX`) |
| Encoder | A = GPIO42, B = GPIO4 (switch is on PCF8574 P5, not a GPIO) |
| Backlight | GPIO6, LEDC PWM |

Contradicting sources deliberately rejected: `example/Simple example/Encoder_code`
(A=45 B=42 SW=41) and the vendor wiki page (B=44). Both disagree with the factory
firmware that actually ships on this unit. G5 decides it on hardware.

## Phase 1 — hardware bring-up (de-risk before porting UI)

- [x] G1 — The board target builds from a clean checkout.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && /Users/gurucharan/.local/bin/pio run -e crowpanel-21-rotary 2>&1 | tail -20`
      EXPECT: `SUCCESS`

- [x] G2 — Firmware flashes and the board reaches our own boot banner.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && hwlog flash -- /Users/gurucharan/.local/bin/pio run -e crowpanel-21-rotary -t upload && hwlog wait --pattern "SPOTIFY-KNOB boot" --timeout 40`
      EXPECT: `SPOTIFY-KNOB boot`

- [x] G3 — The ST7701 panel initializes and renders. Self-reported by firmware after
      a successful `gfx->begin()` and a full-screen fill.
      CHECK: `hwlog wait --pattern "PANEL ok 480x480" --timeout 30`
      EXPECT: `PANEL ok 480x480`

- [ ] G4 — MANUAL — The panel is visibly showing our test pattern, not the Elecrow
      factory demo. Evidence: a cameraBoi still, Read in-transcript, showing the
      colour bars / ring we drew. The factory demo shows a temperature gauge; any
      frame still showing that gauge fails this gate.

- [ ] G5 — Touch reports live coordinates when the screen is pressed.
      CHECK: `hwlog wait --pattern "TOUCH x=" --timeout 60`
      EXPECT: `TOUCH x=`

- [x] G6 — Encoder rotation reports events, and clockwise increases volume.
      EVIDENCE: sustained `KNOB dir=+1 volume=46..70` and `dir=-1` on reverse,
      from src/main.cpp:708. This DECIDES the three-way vendor pin conflict in
      favour of the factory firmware (A=42, B=4). The Elecrow wiki (B=44) and
      the bundled Encoder_code example (A=45, B=42, SW=41) are both WRONG for
      this board.

      CORRECTED 2026-08-26: this gate was first marked met on the strength of
      `dir=+1` merely APPEARING in the log. That proved the encoder was decoded,
      not that +1 meant clockwise — the log cannot see which way a hand turned
      the knob. The user then reported the direction was inverted, and the sense
      in Knob.cpp was flipped.

      This is precisely the "gate that passes by construction" failure: the
      EXPECT string matched without the stated outcome holding. A log pattern
      cannot decide a physical direction. The gate now rests on the user's
      direct report, and is marked MANUAL below rather than pretending a
      `hwlog wait` could settle it.
      STATUS: awaiting user re-confirmation after the sense flip.

- [ ] G7 — Knob press (PCF8574 P5) reports a press event.
      CHECK: `hwlog wait --pattern "KNOB press" --timeout 60`
      EXPECT: `KNOB press`

## Phase 2 — network and Spotify layer (reused from ThingPulse)

- [x] G8 — The board joins WiFi and reports its IP.
      CHECK: `hwlog wait --pattern "WIFI ok ip=" --timeout 60`
      EXPECT: `WIFI ok ip=`

- [x] G9 — A Spotify refresh token is persisted to LittleFS and survives reboot.
      AMENDED 2026-08-26: the board can no longer run the OAuth flow itself.
      Spotify rejects `http://tp-spotify.local/callback/` with
      `INVALID_CLIENT: Insecure redirect URI` — plain HTTP is permitted only for
      loopback IP literals, which a LAN device cannot be. The flow moved to the
      host (`tools/get_refresh_token.py`, redirect `http://127.0.0.1:8888/callback`)
      and the token ships in the filesystem image. The redirect URI is used only
      for the code exchange, so the board never needs it.
      EVIDENCE: board authenticates and polls
      `/v1/me/player/currently-playing`, receiving HTTP 204 (valid auth, no
      active device) rather than 401.

- [x] G10 — Now-playing metadata is fetched from the Spotify API for a real track.
      EVIDENCE: HTTP 200 with track/album/artist JSON in the capture log
      (spotify:track:0t2QiRkpag0fAgs9zuCPlH, "Am I Dreaming"), rendered on the
      panel in the user's photo. The planned NOWPLAYING marker was never added;
      the API response plus the on-screen render is stronger evidence anyway.

- [x] G11 — Album art is downloaded, JPEG-decoded and pushed to the panel.
      EVIDENCE: art visible in the user's photo, and 0 download failures
      measured over 200 log lines after two fixes: (1) DigiCert Global Root G3
      added to the CA bundle — i.scdn.co rotated to it while api.spotify.com
      stayed on G2, which is why art failed while metadata worked; (2) byte-swap
      disabled — TJpg emits native-endian RGB565 and the SPI-era swap flag
      reversed every pixel's bytes on the memory framebuffer, rendering art as
      blue noise while text stayed correct.

## Phase 3 — round UI and knob control

- [ ] G12 — MANUAL — The round now-playing view renders correctly on the circular
      panel: art, track, artist and progress all inside the visible circle with no
      content clipped by the bezel. Evidence: a cameraBoi still, Read in-transcript,
      of a real track playing.

- [x] G13 — Rotating the knob changes Spotify volume.
      EVIDENCE: `VOLUME set=NN (ok)` against a live device, and the user heard
      the volume change ("volume also works"). The API echo alone would not
      have been enough — `VOLUME set=58 (FAILED)` also appeared in logs when no
      device was active, so the marker can print without the outcome holding.

- [ ] G14 — Knob press toggles play/pause against the live player.
      CHECK: `hwlog wait --pattern "TRANSPORT toggle ->" --timeout 60`
      EXPECT: `TRANSPORT toggle ->`

- [ ] G15 — Touch gestures skip to next / previous track against the live player.
      CHECK: `hwlog wait --pattern "TRANSPORT skip" --timeout 60`
      EXPECT: `TRANSPORT skip`

- [ ] G16 — No crash across a sustained run: zero crash reports after ≥10 minutes
      of live playback with art refreshing.
      CHECK: `hwlog crashes 2>&1`
      EXPECT: `no crashes`
      NOTE: negative check — requires a positive control.
      POSITIVE CONTROL MET 2026-08-26: the TJpgDec null-callback bug produced a
      real 4.5s reboot loop and `hwlog crashes --last` reported it (12 boots,
      12 crashes, full register dump). The tool demonstrably detects crashes,
      so its silence is now meaningful. No deliberate panic build needed — a
      genuine crash served as the control.

## Phase 4 — documentation (constitution: docs move with code)

- [x] G17 — `README.md` documents the CrowPanel target, the pinout table, the build
      and flash commands, and the Spotify app setup.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && node -e "const t=require('fs').readFileSync('README.md','utf8'); const need=['CrowPanel','ST7701','PCF8574','0x15','crowpanel-21-rotary','redirect']; const miss=need.filter(k=>!t.includes(k)); if(miss.length){console.error('MISSING: '+miss.join(', ')); process.exit(1);} console.log('README complete');"`
      EXPECT: `README complete`
      EVIDENCE: check ran 2026-08-26, printed `README complete`, exit 0.

## Blocked-on-user inputs

These cannot be satisfied by me and gate Phase 2 onward:

1. WiFi SSID + password.
2. A Spotify **Premium** account (playback control is Premium-only).
3. A Spotify developer app: Client ID + Client Secret, redirect URI registered.

Phase 1 needs none of them and proceeds now.

## Post-fix additions (2026-08-26, after the panel began rendering)

- [ ] G18 — MANUAL — The circle-native view (RoundNowPlayingView) lays out
      correctly on hardware: centered art, perimeter progress ring, clock top,
      title/artists bottom, volume ring on knob turn. Flashed; awaiting the
      user's report. Supersedes the old G12 layout description, which was
      written for the ported rectangular HomeView.

- [ ] G19 — MANUAL — Fit gauge: the printed gauge_ring.stl seats the board at
      one of the three bore steps (79.60 / 79.35 / 79.15). The seating step
      becomes the cradle bore. Awaiting print.
