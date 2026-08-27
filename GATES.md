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

- [x] G1: The board target builds from a clean checkout.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && /Users/gurucharan/.local/bin/pio run -e crowpanel-21-rotary 2>&1 | tail -20`
      EXPECT: `SUCCESS`

- [x] G2: Firmware flashes and the board reaches our own boot banner.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && hwlog flash -- /Users/gurucharan/.local/bin/pio run -e crowpanel-21-rotary -t upload && hwlog wait --pattern "SPOTIFY-KNOB boot" --timeout 40`
      EXPECT: `SPOTIFY-KNOB boot`

- [x] G3: The ST7701 panel initializes and renders. Self-reported by firmware after
      a successful `gfx->begin()` and a full-screen fill.
      CHECK: `hwlog wait --pattern "PANEL ok 480x480" --timeout 30`
      EXPECT: `PANEL ok 480x480`

- [x] G4: ABANDONED-AS-SUPERSEDED 2026-08-26: the bring-up test pattern was
      never photographed face-up, but the full application now renders live
      Spotify data on the panel (multiple photos in-transcript), which is
      strictly stronger evidence than the Phase 1 pattern it was designed to
      provide. ABANDON: G4 superseded by the working application render.

- [x] G5: Touch reports live coordinates when the screen is pressed.
      EVIDENCE: touch-to-skip works end to end against the live player
      (owner: "wait skip is good, everything works amazingly now"), which
      requires live coordinates reaching the view's touch zones.

- [x] G6: Encoder rotation reports events, and clockwise increases volume.
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
      STATUS: CLOSED 2026-08-26 — after the sense flip the owner confirmed
      volume behaves correctly in live use ("everything works amazingly now").

- [x] G7: Knob press (PCF8574 P5) reports a press event.
      EVIDENCE: `KNOB press` in the capture log during live use, and play/pause
      toggling confirmed by the owner as part of "everything works amazingly".

## Phase 2 — network and Spotify layer (reused from ThingPulse)

- [x] G8: The board joins WiFi and reports its IP.
      CHECK: `hwlog wait --pattern "WIFI ok ip=" --timeout 60`
      EXPECT: `WIFI ok ip=`

- [x] G9: A Spotify refresh token is persisted to LittleFS and survives reboot.
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

- [x] G10: Now-playing metadata is fetched from the Spotify API for a real track.
      EVIDENCE: HTTP 200 with track/album/artist JSON in the capture log
      (spotify:track:0t2QiRkpag0fAgs9zuCPlH, "Am I Dreaming"), rendered on the
      panel in the user's photo. The planned NOWPLAYING marker was never added;
      the API response plus the on-screen render is stronger evidence anyway.

- [x] G11: Album art is downloaded, JPEG-decoded and pushed to the panel.
      EVIDENCE: art visible in the user's photo, and 0 download failures
      measured over 200 log lines after two fixes: (1) DigiCert Global Root G3
      added to the CA bundle — i.scdn.co rotated to it while api.spotify.com
      stayed on G2, which is why art failed while metadata worked; (2) byte-swap
      disabled — TJpg emits native-endian RGB565 and the SPI-era swap flag
      reversed every pixel's bytes on the memory framebuffer, rendering art as
      blue noise while text stayed correct.

## Phase 3 — round UI and knob control

- [x] G12: MANUAL — The round now-playing view renders correctly on the
      circular panel.
      EVIDENCE: in-transcript photos of live tracks rendering (art centered,
      correct colors after the R/B pin fix, text on-art), plus the owner's
      verdict on the final design. Two design iterations were rejected first
      (ring layout, then ring+bar); the shipped art-first layout is the one
      the owner accepted.

- [x] G13: Rotating the knob changes Spotify volume.
      EVIDENCE: `VOLUME set=NN (ok)` against a live device, and the user heard
      the volume change ("volume also works"). The API echo alone would not
      have been enough — `VOLUME set=58 (FAILED)` also appeared in logs when no
      device was active, so the marker can print without the outcome holding.

- [x] G14: Knob press toggles play/pause against the live player.
      EVIDENCE: `TRANSPORT toggle ->` in the capture log; owner confirmation in
      live use.

- [x] G15: Touch skips to next / previous track against the live player.
      EVIDENCE: `TRANSPORT skip` markers in the capture log; owner explicitly:
      "wait skip is good". (Implemented as touch zones, not gestures — the
      gesture recognizer exists in Board/Touch.cpp but the zones won on
      simplicity.)

- [x] G16: No crash across a sustained run: zero crash reports after ≥10 minutes
      of live playback with art refreshing.
      EVIDENCE: 12-minute timed soak on the shipped UI build (18:11-18:23
      2026-08-26), `hwlog crashes` -> "(no crashes recorded)", 104k log lines
      captured, and the window included live owner interaction (knob, touch,
      skips). An earlier 11-minute soak on the previous build also ran clean.
      NOTE: negative check — requires a positive control.
      POSITIVE CONTROL MET 2026-08-26: the TJpgDec null-callback bug produced a
      real 4.5s reboot loop and `hwlog crashes --last` reported it (12 boots,
      12 crashes, full register dump). The tool demonstrably detects crashes,
      so its silence is now meaningful. No deliberate panic build needed — a
      genuine crash served as the control.

## Phase 4 — documentation (constitution: docs move with code)

- [x] G17: `README.md` documents the CrowPanel target, the pinout table, the build
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

- [x] G18: MANUAL — The circle-native view lays out correctly on hardware.
      EVIDENCE: owner accepted the final art-first design in live use. Note the
      gate text drifted with the design: the "perimeter progress ring" it
      originally named was itself cut at the owner's request ("i dont need a
      time bar"). Merged into G12's outcome.

- [ ] G19: MANUAL — Fit gauge: the printed gauge_ring.stl seats the board at
      one of the three bore steps (79.60 / 79.35 / 79.15). The seating step
      becomes the cradle bore. Awaiting print.

## Phase 5 — de-badge the upstream project, README redesign, CI, push (2026-08-26)

Owner request: remove references to the upstream GitHub project from README and
code, redesign the README (thumbnail banner, format matching the other project
READMEs), make CI green, push this branch, and push the portfolio's main.
Constraint honored throughout: the MIT LICENSE file and the per-file
SPDX-FileCopyrightText notices are legally required and stay.

- [ ] G20: No narrative references to the upstream project remain in README.md,
      platformio.ini, include/, or src/. Only SPDX copyright-notice lines may
      mention the name. (LICENSE and GATES.md are historical/legal record, out
      of scope by design.)
      CHECK: `node -e "const fs=require('fs'),p=require('path');const roots=['README.md','platformio.ini','src','include'];let bad=[];function scan(f){const t=fs.readFileSync(f,'utf8').split('\n');t.forEach((l,i)=>{if(/thingpulse/i.test(l)&&!/SPDX-FileCopyrightText/.test(l))bad.push(f+':'+(i+1)+':'+l.trim())})}function walk(d){for(const e of fs.readdirSync(d,{withFileTypes:true})){const f=p.join(d,e.name);if(e.isDirectory())walk(f);else scan(f)}}for(const r of roots){if(!fs.existsSync(r))continue;fs.statSync(r).isDirectory()?walk(r):scan(r)}if(bad.length){console.error(bad.join('\n'));process.exit(1)}console.log('NO-NARRATIVE-REFS')"`
      EXPECT: `NO-NARRATIVE-REFS`

- [ ] G21: The firmware still builds clean after the sweep.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && /Users/gurucharan/.local/bin/pio run -e crowpanel-21-rotary 2>&1 | tail -3`
      EXPECT: `SUCCESS`

- [ ] G22: The redesigned README leads with a banner image, keeps the load-bearing
      technical content (board, pinout source, build commands, OAuth flow), and
      contains no reference to the upstream project.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && node -e "const t=require('fs').readFileSync('README.md','utf8');const need=['docs/thumbnail','CrowPanel','ST7701','PCF8574','crowpanel-21-rotary','get_refresh_token'];const miss=need.filter(k=>!t.includes(k));if(miss.length){console.error('MISSING: '+miss.join(', '));process.exit(1)}if(/thingpulse|upstream/i.test(t)){console.error('still references the upstream project');process.exit(1)}console.log('README-OK')"`
      EXPECT: `README-OK`

- [ ] G23: The banner referenced by the README exists and is a real raster image,
      not a placeholder.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && node -e "const fs=require('fs');const c=[['docs/thumbnail.jpg',[0xff,0xd8]],['docs/thumbnail.png',[0x89,0x50]]];const hit=c.find(([f])=>fs.existsSync(f));if(!hit){console.error('no thumbnail file');process.exit(1)}const[f,sig]=hit;const b=fs.readFileSync(f);if(b.length<10240||b[0]!==sig[0]||b[1]!==sig[1]){console.error('bad signature or <10KB: '+f+' '+b.length);process.exit(1)}console.log('THUMB-OK '+f)"`
      EXPECT: `THUMB-OK`

- [ ] G24: This branch is pushed: the remote ref equals local HEAD.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && node -e "const{execSync:x}=require('child_process');const h=x('git rev-parse HEAD').toString().trim();const r=x('git ls-remote origin refs/heads/feat/crowpanel-21-rotary-port').toString().trim().split(String.fromCharCode(9))[0];if(h!==r){console.error('local '+h+' != remote '+r);process.exit(1)}console.log('PUSH-OK')"`
      EXPECT: `PUSH-OK`

- [ ] G25: CI is green on the pushed head of this branch.
      CHECK: `cd /Users/gurucharan/Documents/work/spotify-knob && node -e "const{execSync:x}=require('child_process');const h=x('git rev-parse HEAD').toString().trim();const runs=JSON.parse(x('gh run list --branch feat/crowpanel-21-rotary-port --json headSha,status,conclusion --limit 10').toString());const r=runs.find(r=>r.headSha===h);if(!r){console.error('no CI run for '+h);process.exit(1)}if(r.status!=='completed'||r.conclusion!=='success'){console.error(r.status+'/'+r.conclusion);process.exit(1)}console.log('CI-GREEN')"`
      EXPECT: `CI-GREEN`

- [ ] G26: The portfolio's spotKnob entry is committed on main and pushed.
      CHECK: `cd /Users/gurucharan/Documents/work/portfolio && node -e "const{execSync:x}=require('child_process');if(x('git status --porcelain app/hardware/page.js').toString().trim()){console.error('uncommitted');process.exit(1)}const t=x('git show HEAD:app/hardware/page.js').toString();if(!t.includes('spotKnob')){console.error('no spotKnob at HEAD');process.exit(1)}const h=x('git rev-parse HEAD').toString().trim();const r=x('git ls-remote origin refs/heads/main').toString().trim().split(String.fromCharCode(9))[0];if(h!==r){console.error('local '+h+' != remote '+r);process.exit(1)}console.log('PORTFOLIO-OK')"`
      EXPECT: `PORTFOLIO-OK`
