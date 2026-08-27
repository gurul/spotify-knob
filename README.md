<p align="center">
  <img src="docs/thumbnail.jpg" alt="spotKnob — the round display showing album art, with the rotary knob" width="720">
</p>

<h1 align="center">spotKnob</h1>

<p align="center">
  A WiFi Spotify controller on a round display with a clicking rotary knob.
</p>

<p align="center">
  <a href="#hardware">Hardware</a> ·
  <a href="#build-and-flash">Build & flash</a> ·
  <a href="#spotify-setup">Spotify setup</a> ·
  <a href="#controls">Controls</a> ·
  <a href="#debugging">Debugging</a>
</p>

---

Turn the knob for volume, press it for play/pause, swipe the glass to skip.
Album art, track, artist and progress render on the round face. It runs on the
**Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display** — a 480×480 round IPS panel
with a capacitive touch overlay and a rotary encoder, driven by an ESP32-S3 —
and talks to the Spotify Web API directly over WiFi. No phone, no companion
app, no cloud middleman.

## How the display works

The panel is an **ST7701S on a 16-bit RGB parallel bus**, which is a very
different animal from the usual SPI hobby display:

- An SPI display holds its own framebuffer. You send it commands and pixels.
- An RGB parallel panel holds nothing. The ESP32-S3's LCD peripheral scans a
  framebuffer out of PSRAM continuously, in real time, and the panel is just a
  shift register with a timing contract.

The backend is **Arduino_GFX**, which can drive that. The application's drawing
code, though, targets a ~15-method `TFT_eSPI`-shaped surface —
`src/Board/TFTCompat.h` presents exactly those methods over Arduino_GFX, so the
60 KB of view and renderer code compiles unmodified. `include/TFT_eSPI.h` is a
shim that satisfies the include; the real library is not a dependency.

The facade is deliberately narrow. Call a `TFT_eSPI` method it does not
implement and the build fails, which is the correct outcome — a silently
missing draw call is far worse than a compile error.

## Hardware

Every value below is transcribed from Elecrow's **factory firmware**
(`factory_soucecode/ESP32_Display_2_1-1`) or their **ESPHome config**, and the
ones marked ✔ have been confirmed on the physical board.

| Item | Value |
|---|---|
| MCU | ESP32-S3-N16R8 — 16 MB flash, 8 MB **octal** PSRAM ✔ |
| Panel | ST7701S, 480×480 round, RGB parallel 16-bit |
| RGB bus | CS 16, SCK 2, SDA 1, DE 40, VSYNC 7, HSYNC 15, PCLK 41 |
| RGB data | R 46/3/8/18/17 · G 14/13/12/11/10/9 · B 5/45/48/47/21 |
| Timing | hsync 20/10/10, vsync 8/10/10, 18 MHz pclk, inverted |
| I2C | SDA 38, SCL 39 ✔ |
| I/O expander | **PCF8574 @ 0x21** ✔ — P0 touch RST, P2 touch INT, P3 LCD power, P4 LCD reset, P5 encoder switch |
| Touch | CST8xx @ **0x15** ✔ |
| Encoder | A = GPIO42, B = GPIO4 ✔ (the switch is on the expander, **not** a GPIO) |
| Backlight | GPIO6, LEDC PWM ✔ |
| Enclosure | 79.00 mm ⌀ × 33.42 mm deep (from Elecrow's STEP file) |

### The expander is load-bearing

LCD power and LCD reset both hang off the PCF8574. **The panel cannot be brought
up over GPIO alone.** I2C and the expander must be alive before the RGB bus is
touched; `RoundDisplay::begin()` enforces that order. Getting it wrong yields a
dark panel and no error message.

The PCF8574 has no direction register — every pin is quasi-bidirectional, and
writing a 1 is also how you read a pin. `Expander` therefore keeps a shadow byte
and forces every input pin high on each write. Without that, the first write
after a read drives the encoder switch low and it reads as pressed forever.

### Vendor sources contradict each other

Three Elecrow sources disagree on the encoder pinout, and two disagree on panel
timings. Resolved on hardware:

| Source | Encoder claim | Verdict |
|---|---|---|
| Factory firmware | A=42, B=4, SW on PCF8574 P5 | **correct** ✔ |
| Elecrow wiki | B=44 | wrong |
| `Simple example/Encoder_code` | A=45, B=42, SW=41 | wrong |

For **panel timings the factory firmware is the wrong source** — its values
produce a sheared image, and its own comments contradict its literals
(`4 /* hsync_pulse_width(8) */`). The working numbers come from Elecrow's
ESPHome config. `src/Board/BoardPins.h` records which source won and why.

That same `Simple example` folder drives 5 WS2812s on GPIO48, which is the
panel's B2 data line in the factory firmware. Both cannot be true, so the
ambient LEDs are left unclaimed rather than guessed.

## Build and flash

Requires [PlatformIO](https://platformio.org/). Everything else is fetched
automatically.

```bash
pio run -e crowpanel-21-rotary                  # build
pio run -e crowpanel-21-rotary -t uploadfs      # filesystem: credentials + art cache
pio run -e crowpanel-21-rotary -t upload        # firmware
```

Run `uploadfs` and `upload` as **separate** commands. Combining targets in one
invocation confuses PlatformIO's dependency finder and the build fails looking
for `SPI.h`.

### The platform is pinned to a fork, on purpose

`platformio.ini` uses [pioarduino][pio] rather than the official `espressif32`
platform. The official platform tops out at Arduino core 2.0.17, which has no
`esp32-hal-periman.h`; Arduino_GFX 1.6.7 will not compile against it. pioarduino
is the maintained route to core 3.x.

[pio]: https://github.com/pioarduino/platform-espressif32

### Flashing while the board is crash-looping

The ESP32-S3's native USB re-enumerates on every reset, so a boot loop can make
`esptool` fail with `Failed to connect: No serial data received`. Hold **BOOT**,
tap **RESET**, release **BOOT** to force download mode.

## Spotify setup

You need a **Spotify Premium** account. Playback control is Premium-only; a free
account can read now-playing but cannot skip, pause, or set volume.

**1. Create an app** at the [Spotify Developer Dashboard][dash]. Enable **Web
API** only. Set the Redirect URI to exactly:

```
http://127.0.0.1:8888/callback
```

**2. Put the credentials in `data/user.ini`** (gitignored, never committed):

```ini
[wifi]
ssid = your-network
password = your-password

[spotify]
client_id = ...
client_secret = ...

[system]
timezone = PST8PDT,M3.2.0,M11.1.0
ui_date_time_format = us
```

**3. Get a refresh token**, then upload the filesystem:

```bash
python3 tools/get_refresh_token.py
pio run -e crowpanel-21-rotary -t uploadfs
```

[dash]: https://developer.spotify.com/dashboard

### Why OAuth runs on the host, not the board

Spotify's redirect-URI rule is HTTPS only, with plain HTTP permitted solely for
loopback IP literals. An embedded device on the LAN can satisfy neither half —
it is not loopback, and it cannot terminate TLS for a name it holds no
certificate for. Any attempt fails at the authorize endpoint with
`INVALID_CLIENT: Insecure redirect URI`.

So `tools/get_refresh_token.py` runs the exchange on your machine against a
loopback redirect and writes the refresh token into the filesystem image. The
board reads `/refresh-token.txt` at boot and never touches a browser.

This only affects the initial exchange. Refreshing an access token does not
involve the redirect URI, so the board needs nothing further.

### Certificate pinning

The firmware pins root CAs rather than trusting a store. `api.spotify.com` chains
to **DigiCert Global Root G2**; the album-art CDN `i.scdn.co` chains to
**DigiCert Global Root G3** (ECC). Both are in `gCombinedCerts`
(`src/SpotifyArtMgr.cpp`).

If album art stops downloading while metadata still works, suspect this first —
that asymmetry is the signature of a CA rotation on the image CDN alone. Check
what the chain actually uses:

```bash
echo | openssl s_client -connect i.scdn.co:443 -servername i.scdn.co 2>/dev/null \
  | openssl x509 -noout -issuer
```

## Controls

| Input | Action |
|---|---|
| Rotate knob | Volume, 2% per detent |
| Press knob | Play / pause |
| Hold knob | Re-read volume from the active device |
| Swipe left / right | Next / previous track |
| Tap | View-specific |

Rotation moves a local shadow immediately and pushes to Spotify on a **400 ms
debounce**. One API write per detent gets the account rate-limited within a
single flick of the knob.

## Debugging

Serial work goes through [`hwlog`][hwlog], which owns the port so a monitor never
fights a flash:

```bash
hwlog ports                                     # identify the board
hwlog start -p /dev/cu.usbmodem11301
hwlog flash -- pio run -e crowpanel-21-rotary -t upload
hwlog logs --boot -1 --tail 50
hwlog boots                                     # reboot loops are obvious here
hwlog crashes --last
```

[hwlog]: https://github.com/gurul/hardware-logging

`hwlog boots` earns its keep: a 4.5-second crash loop looks like ordinary
scrolling noise on a raw monitor, but shows up immediately as twelve boots with
twelve crashes.

### Decoding a backtrace by hand

If crash reports come back unsymbolized:

```bash
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line \
  -pfiaC -e .pio/build/crowpanel-21-rotary/firmware.elf 0x42008e45 0x420085bb
```

## Layout

```
src/
  Board/            everything specific to this board
    BoardPins.h       pinout, timings, and which vendor source won
    Expander.{h,cpp}  PCF8574: LCD power/reset, touch reset, knob switch
    RoundDisplay.*    ST7701S bring-up, ordering enforced
    Touch.{h,cpp}     CST8xx, reset over the expander, gesture recognition
    Knob.{h,cpp}      encoder by interrupt; switch polled over I2C
    TFTCompat.h       TFT_eSPI-shaped facade over Arduino_GFX
  Core/             connectivity, time sync, and shared utilities
  DisplayUI.*       view rendering — draws through TFTCompat
  UIViews/          the views and their renderers
  SpotifyPlayer.*   playback state, volume, and the API client
  SpotifyArtMgr.*   album-art download and cache, pinned root CAs
  Vault.*           credential loading
tools/
  get_refresh_token.py   host-side OAuth
GATES.md            acceptance ledger: what is proven, and by what evidence
```

The knob switch is polled, never read from an ISR: it lives on the PCF8574, so
reading it is an I2C transaction and cannot happen in interrupt context.

## Traps that cost real time

Recorded because each one was invisible until it wasn't.

**Null callback on the first JPEG.** `DisplayUI`'s constructor registered
`TJpgDec.setCallback()` at static-init time. Both are globals in different
translation units, so when `TJpgDec` constructed second it zeroed the callback —
and TJpg_Decoder calls it without a null check. Jump to `0x00000000`, 4.5-second
reboot loop. It had always survived on link-order luck; adding the `Board/`
globals changed the order and the latent bug surfaced. Registration moved to
`DisplayUI::init()`, at runtime.

**Stale CA bundle.** Album art failed TLS while metadata succeeded, because only
the image CDN had rotated to a root that was not pinned. See above.

**Core 3.x transitive includes.** `WiFiClientSecure.h` and `esp_mac.h` used to
arrive indirectly on core 2.x and must now be named explicitly.

## License

MIT — see [LICENSE](LICENSE).
