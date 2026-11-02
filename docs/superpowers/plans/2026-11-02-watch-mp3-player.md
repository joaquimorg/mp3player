# T-Watch MP3 Player Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Firmware for the LILYGO T-Watch-2020 V1 that plays 4 MP3 slots in loop over Bluetooth A2DP, with a Wi-Fi "Programação" mode serving a web page to upload/delete slots, plus a portable Windows tool that converts YouTube links and uploads them.

**Architecture:** Arduino + PlatformIO firmware. Pure logic (slot sequencing, upload validation) lives in a hardware-free library tested natively. Hardware units (storage, player, bt, netmode, ui) are separate files wired together by a mode state machine in `main.cpp`. Reprodução and Programação modes are mutually exclusive (BT xor Wi-Fi).

**Tech Stack:** PlatformIO (`espressif32` platform pinned to 6.x / Arduino-ESP32 2.0.x), `lewisxhe/TTGO_TWatch_Library` (display, touch, AXP202 power), `pschatzmann/ESP32-A2DP`, `pschatzmann/arduino-audio-tools`, `pschatzmann/arduino-libhelix`, LittleFS, ESP32 `WebServer`/`ESPAsyncWebServer`, Unity (native tests). PC tool: Windows batch + `yt-dlp.exe` + `ffmpeg.exe` + `curl.exe`.

**Spec:** `mp3player/docs/superpowers/specs/2026-11-02-watch-mp3-player-design.md`

## Global Constraints

- All files live under `D:\Work\TTGO-Watch\mp3player`.
- Board: T-Watch-2020 V1, ESP32-D0WDQ6, 16 MB flash, serial port `COM8`; `#define LILYGO_WATCH_2020_V1`.
- Exactly 4 slots, fixed files `/slot1.mp3` … `/slot4.mp3` (1-based in UI and files).
- Data partition ~12 MB; ~3 MB per slot target; PC tool encodes MP3 at 44.1 kHz, ~112 kbps.
- Modes: Reprodução = Wi-Fi off, BT on (A2DP source). Programação = BT off, Wi-Fi client on. Never both.
- Wi-Fi fallback AP name `TWATCH-SETUP` when no credentials or connection fails.
- MP3 is streamed from flash in blocks, never fully in RAM.
- Decoder on core 1, Bluetooth on core 0.
- UI language: Portuguese (Portugal). 240×240 touch display; screen blanks after inactivity.
- PC tool needs no Python/installer; only `yt-dlp.exe`, `ffmpeg.exe`, system `curl.exe`.
- The folder is not a git repo; there are no commit steps. Each task ends with its verification.

## Review Focus

1. Upload interrupted or aborted mid-transfer → the previous slot content must survive (write to `slotN.tmp`, rename only on success).
2. Upload larger than the free space or than `SLOT_MAX_BYTES` → rejected before any write, with a clear message.
3. Uploaded file is not an MP3 (e.g. a `.jpg` renamed) → rejected (ID3 or MPEG frame sync check), slot untouched.
4. Wrong Wi-Fi password or router off → after a timeout the watch opens `TWATCH-SETUP` instead of hanging.
5. Empty slots / BT device powered off or out of range during playback → player skips empty slots, stops cleanly when all are empty, and reconnects without crashing or leaking heap; switching mode while playing tears down BT cleanly.

---

## File Structure

```
mp3player/
  platformio.ini
  partitions.csv
  src/
    config.h            constants (NUM_SLOTS, SLOT_MAX_BYTES, pins via library)
    main.cpp            mode state machine, setup/loop
    settings.h/.cpp     NVS: wifi creds, last BT device, loop mode, volume
    storage.h/.cpp      LittleFS slots, atomic write, free space
    player.h/.cpp       MP3 decode task -> A2DP data callback
    bt.h/.cpp           scan, device list, connect, reconnect
    netmode.h/.cpp      Wi-Fi STA/AP, web server, upload/delete endpoints
    web_page.h          HTML/JS of the management page (PROGMEM)
    ui.h/.cpp           screens + touch
  lib/slotlogic/
    slot_logic.h/.cpp   pure logic (no Arduino includes)
  test/test_native/test_slot_logic.cpp
  tools/pc/
    obter-ferramentas.bat      downloads yt-dlp.exe, ffmpeg.exe
    youtube-para-relogio.bat   link -> mp3 -> upload
    LEIAME.txt
```

---

### Task 1: Project scaffold and board bring-up

**Files:**
- Create: `platformio.ini`, `partitions.csv`, `src/config.h`, `src/main.cpp`

**Interfaces:**
- Produces: `config.h` with `constexpr int NUM_SLOTS = 4; constexpr size_t SLOT_MAX_BYTES = 3 * 1024 * 1024;` and `slotPath(int n)` is NOT here (Task 2). `main.cpp` initialises `TTGOClass* watch = TTGOClass::getWatch()`.

- [ ] **Step 1: Write `platformio.ini`** with envs `watch` (platform `espressif32@~6.9.0`, board `esp32dev`, framework arduino, `board_build.partitions = partitions.csv`, `board_build.filesystem = littlefs`, `board_upload.flash_size = 16MB`, `board_build.flash_mode = dio`, `upload_port = COM8`, `monitor_port = COM8`, `monitor_speed = 115200`, `build_flags = -DLILYGO_WATCH_2020_V1 -DBOARD_HAS_PSRAM`, `lib_deps` = the 4 libraries above) and `native` (platform `native`, `test_framework = unity`, `lib_deps` none, `build_src_filter` excluding `src/`).
- [ ] **Step 2: Write `partitions.csv`** — `nvs` 0x5000 @0x9000; `otadata` 0x2000 @0xE000; `app0` factory 0x400000 @0x10000; `spiffs` (subtype spiffs, used for LittleFS) 0xBF0000 @0x410000.
- [ ] **Step 3: Write `main.cpp`** that powers the display (`watch->begin(); watch->openBL();`), prints "T-Watch MP3" on screen, mounts LittleFS with format-on-fail and logs `LittleFS total/used bytes` to Serial.
- [ ] **Step 4: Build and flash.** Run: `pio run -e watch -t upload` then `pio device monitor -e watch`. Expected: upload succeeds on COM8, screen shows the title, serial shows a LittleFS total of ~12 MB (> 11,000,000 bytes).

---

### Task 2: Slot logic (pure, native tests)

**Files:**
- Create: `lib/slotlogic/slot_logic.h`, `lib/slotlogic/slot_logic.cpp`
- Test: `test/test_native/test_slot_logic.cpp`

**Interfaces:**
- Produces (namespace `slotlogic`, no Arduino includes):
  - `enum class LoopMode : uint8_t { RepeatOne, AllSlots };`
  - `const char* slotPath(int slot);` → `"/slot1.mp3"`…`"/slot4.mp3"`, `nullptr` if out of 1..4.
  - `int nextSlot(int current, LoopMode mode, const bool hasFile[4]);` → returns the slot (1..4) to play when the current track ends; `RepeatOne` returns `current` if it has a file; `AllSlots` returns the next non-empty slot cyclically (may be `current`); returns `0` if all are empty.
  - `int prevSlot(int current, const bool hasFile[4]);` cyclic previous non-empty slot, `0` if none.
  - `enum class UploadCheck { Ok, BadSlot, TooBig, NoSpace, NotMp3 };`
  - `UploadCheck validateUpload(int slot, size_t size, size_t freeBytes, const uint8_t head[4]);` — `Ok` only if slot 1..4, `size <= 3*1024*1024`, `size <= freeBytes`, and `head` starts with `"ID3"` or an MPEG frame sync (`0xFF` followed by byte with top 3 bits set). Check order: BadSlot, NotMp3, TooBig, NoSpace.

- [ ] **Step 1: Write the failing tests** (one Unity test each): `slotPath(1)=="/slot1.mp3"`, `slotPath(0)==nullptr`, `slotPath(5)==nullptr`; `nextSlot(2,RepeatOne,{1,1,1,1})==2`; `nextSlot(4,AllSlots,all)==1`; `nextSlot(1,AllSlots,{1,0,0,1})==4`; `nextSlot(1,AllSlots,{1,0,0,0})==1`; `nextSlot(1,AllSlots,{0,0,0,0})==0`; `nextSlot(2,RepeatOne,{1,0,1,1})==3` (current empty falls back to next non-empty); `prevSlot(1,{1,1,0,1})==4`; `validateUpload` cases: valid `"ID3"` head → Ok; `{0xFF,0xFB,0x90,0x00}` → Ok; `"\xFF\xD8\xFF\xE0"` (JPEG) → NotMp3; size 3 MiB + 1 → TooBig; size 1 MiB with `freeBytes=500000` → NoSpace; slot 0 → BadSlot.
- [ ] **Step 2: Run** `pio test -e native` → FAIL (missing header).
- [ ] **Step 3: Implement** the functions in `slot_logic.cpp` to the signatures above.
- [ ] **Step 4: Run** `pio test -e native` → all PASS.

---

### Task 3: Settings and storage

**Files:**
- Create: `src/settings.h/.cpp`, `src/storage.h/.cpp`
- Modify: `src/main.cpp` (temporary serial self-test, removed in Task 5's wiring)

**Interfaces:**
- Consumes: `slotlogic::slotPath`, `validateUpload`, `UploadCheck`.
- Produces:
  - `settings`: `String wifiSsid(); String wifiPass(); void setWifi(const String&, const String&); String lastBtDevice(); void setLastBtDevice(const String&); slotlogic::LoopMode loopMode(); void setLoopMode(LoopMode); uint8_t volume(); void setVolume(uint8_t /*0-100, default 50*/);` stored in NVS namespace `mp3`.
  - `storage`: `bool begin(); bool hasFile(int slot); size_t slotSize(int slot); size_t freeBytes(); void hasFileAll(bool out[4]); bool remove(int slot); fs::File openRead(int slot);` and an atomic writer: `class SlotWriter { public: SlotWriter(int slot); bool write(const uint8_t*, size_t); bool commit(); void abort(); }` writing to `/slotN.tmp`, `commit()` renames over `/slotN.mp3` (removing the old file first), `abort()` deletes the tmp file. Starting a `SlotWriter` for a slot deletes a stale `.tmp`. Names: `String slotName(int slot)` (returns `"Faixa N"` if unset) and `void setSlotName(int slot, const String&)`, persisted in `/names.json`; `remove()` clears the name; Task 5 sets it from the uploaded file name after a successful `commit()`.

- [ ] **Step 1: Implement** both modules to the interfaces above.
- [ ] **Step 2: Add a temporary self-test in `main.cpp`**: write 1000 bytes of `"ID3"`+zeros to slot 1 via `SlotWriter`, `commit()`, log `hasFile(1)`/`slotSize(1)`; start a second writer for slot 1, write 10 bytes, `abort()`, log that `slotSize(1)` is still 1003 (**Review Focus 1**); `remove(1)`; log `hasFile(1)==false`; set and read back a Wi-Fi SSID and volume across `ESP.restart()`.
- [ ] **Step 3: Flash and monitor.** Expected serial: sizes as above, aborted write leaves slot intact, settings persist after reboot.

---

### Task 4: Bluetooth (scan, list, connect) and player

**Files:**
- Create: `src/bt.h/.cpp`, `src/player.h/.cpp`
- Modify: `src/main.cpp` (temporary test harness)

**Interfaces:**
- Consumes: `storage::openRead`, `storage::hasFileAll`, `slotlogic::nextSlot/prevSlot`, `settings::*`.
- Produces:
  - `bt`: `void begin();` (starts A2DP source, registers the data callback that pulls frames from `player`), `void startScan();` `void stopScan();` `std::vector<String> devices();` (names seen so far), `void connectTo(const String& name);` `bool connected();` `void end();` (full teardown so Wi-Fi can start). On connection loss it retries the last device (`settings::lastBtDevice()`) without blocking the UI.
  - `player`: `void begin();` (creates the decode task pinned to core 1), `void play(int slot); void pause(); void resume(); void next(); void prev(); void setVolume(uint8_t); int currentSlot(); bool playing(); int32_t pullFrames(Frame* out, int32_t count);` (called by the A2DP callback; returns silence frames when paused/idle), `void end();`. On end-of-track it uses `nextSlot` with the saved `LoopMode`; if `nextSlot` returns 0 it stops and stays idle (**Review Focus 5**). Decode uses libhelix in blocks read from the `fs::File`; resamples/handles mono by duplicating to stereo; output is 44.1 kHz 16-bit stereo.

- [ ] **Step 1: Implement** `bt` using ESP32-A2DP's SSID callback to collect discovered names and to connect by chosen name.
- [ ] **Step 2: Implement** `player` per the interface.
- [ ] **Step 3: Temporary harness in `main.cpp`**: write a known good test MP3 into slot 1 (use the PC tool path in Task 7, or `pio run -t uploadfs` with a file in `data/` for this step only), start scan for 10 s, log device names to Serial, auto-connect to the name given by a `#define TEST_BT_NAME`, then `player::play(1)`.
- [ ] **Step 4: Flash and verify with the user** (needs a Bluetooth speaker/headphones in pairing mode). Expected: the device name appears in the log, audio plays, the track loops, no watchdog reset over 5 minutes, free heap logged every 10 s stays flat (**±2 KB**). Then switch the speaker off and on: the log shows reconnect, no crash (**Review Focus 5**).

---

### Task 5: Network mode and web page

**Files:**
- Create: `src/netmode.h/.cpp`, `src/web_page.h`
- Modify: `src/settings.*` (no change expected beyond Task 3)

**Interfaces:**
- Consumes: `storage::*` (`SlotWriter`, `hasFile`, `slotSize`, `freeBytes`, `remove`), `slotlogic::validateUpload`, `settings::wifi*`.
- Produces: `netmode`: `void begin();` `void loop();` `void end();` `String ipText();` (IP when on the STA network, or `"192.168.4.1 (TWATCH-SETUP)"` in AP mode), `bool inSetupAp();`.
  - Behaviour: connect to the saved SSID with a **15 s timeout**; if no credentials or timeout → start AP `TWATCH-SETUP` and serve a setup page (`GET /` form, `POST /wifi` saves credentials then reboots into Programação mode) (**Review Focus 4**).
  - HTTP API on STA: `GET /` page; `GET /api/slots` → JSON `{"free":N,"slots":[{"n":1,"size":N,"name":"…"},…]}` (`size` 0 if empty); `POST /api/slot/<n>` multipart upload, field `file`; the handler reads the first 4 bytes into `validateUpload` **before writing**, answers `400` JSON `{"error":"…"}` in Portuguese for `NotMp3/TooBig/NoSpace/BadSlot` (**Review Focus 2, 3**), streams the rest through `SlotWriter`, `abort()` on any error or client disconnect, `commit()` only when all bytes arrived (**Review Focus 1**); `DELETE /api/slot/<n>` removes the slot.
  - The page (`web_page.h`) lists the 4 slots with name, size, "Enviar"(file input + progress bar) and "Apagar" buttons, and the free space; Portuguese text.

- [ ] **Step 1: Implement** `netmode` and the page.
- [ ] **Step 2: Temporary harness**: `main.cpp` calls `netmode::begin()` and logs `ipText()`.
- [ ] **Step 3: Flash and test with `curl` from the PC** (replace `IP`): 
  - `curl -F file=@good.mp3 http://IP/api/slot/2` → 200; `curl http://IP/api/slots` shows slot 2's size.
  - `curl -F file=@photo.jpg http://IP/api/slot/2` → 400 NotMp3 and slot 2 unchanged.
  - Upload a >3 MiB file → 400 TooBig.
  - Start a large upload and kill `curl` midway (Ctrl+C) → `/api/slots` shows the previous slot 2 size, no `.tmp` left after reboot.
  - `curl -X DELETE http://IP/api/slot/2` → slot empty.
  - Wrong password in settings → after ~15 s the `TWATCH-SETUP` AP appears; connect from phone, submit credentials, watch reboots and joins the network.
  - Open `http://IP/` in a browser and upload/delete a slot from the page.

---

### Task 6: UI and mode state machine

**Files:**
- Create: `src/ui.h/.cpp`
- Modify: `src/main.cpp` (replace all temporary harnesses with the real state machine)

**Interfaces:**
- Consumes: `player::*`, `bt::*`, `netmode::*`, `storage::hasFileAll`, `settings::*`.
- Produces: `ui`: `void begin(); void loop();` (touch handling, redraw, backlight timeout 15 s, wake on touch/button) and `enum class Mode { Play, Program };`; `main.cpp` owns `Mode` and the transitions `enterPlay()` / `enterProgram()`, each doing full teardown of the other side first (`player::pause` + `bt::end()` before `netmode::begin()`; `netmode::end()` before `bt::begin()`), and checking `ESP.getFreeHeap()` is logged on each switch (**Review Focus 5**).
- Screens (Portuguese):
  - **Reprodução**: 4 slot rows (name or "Vazio", ▶ marker on current), buttons ⏮ ⏯ ⏭, volume −/+, loop toggle ("Repetir faixa" / "Repetir todas"), top status "BT: <device>/Desligado", button "Programar" and button "Bluetooth".
  - **Bluetooth**: scan list (up to 6 names), tap to connect and save as last device, "Voltar".
  - **Programação**: shows `ipText()`, a hint "Abre este endereço no browser", button "Reprodução".
- Slot names come from the uploaded file name (stored by Task 5; if absent show "Faixa N").

- [ ] **Step 1: Implement** `ui` and the state machine; boot into Reprodução and auto-connect to the saved device.
- [ ] **Step 2: Flash and exercise on the watch** — Expected: all three screens reachable by touch, mode switches succeed ten times in a row without reboot, free heap after the tenth switch within 5 KB of the first, backlight turns off after 15 s and wakes on touch.
- [ ] **Step 3: End-to-end with the user:** upload 2 MP3s via the page, switch to Reprodução, pair the speaker from the Bluetooth screen, confirm audio, loop (both modes), next/prev, volume, power-cycle the watch and confirm it reconnects by itself.

---

### Task 7: Portable PC tool

**Files:**
- Create: `tools/pc/obter-ferramentas.bat`, `tools/pc/youtube-para-relogio.bat`, `tools/pc/LEIAME.txt`

**Interfaces:**
- Consumes: `POST http://<ip>/api/slot/<n>` (field `file`) from Task 5.
- Produces: two batch files.
  - `obter-ferramentas.bat`: uses `curl.exe -L` to download `yt-dlp.exe` (latest GitHub release) and a Windows `ffmpeg.exe` (the essentials build, extracted with `tar.exe`) into its own folder; skips files that already exist.
  - `youtube-para-relogio.bat`: asks for URL, slot (1-4) and watch IP; runs `yt-dlp.exe -x --audio-format mp3 --postprocessor-args "-ar 44100 -b:a 112k" --no-playlist -o "%TEMP%\twatch.%(ext)s"`; refuses if the resulting file is larger than 3,145,728 bytes (tells the user to pick a shorter video); uploads with `curl.exe -f -F "file=@..." http://IP/api/slot/N`; prints the watch's JSON error message on failure; deletes the temp file.
  - `LEIAME.txt`: 6 lines in Portuguese explaining: run `obter-ferramentas.bat` once; put the watch in Programação; run `youtube-para-relogio.bat`.

- [ ] **Step 1: Write** the three files.
- [ ] **Step 2: Run** `obter-ferramentas.bat`. Expected: `yt-dlp.exe` and `ffmpeg.exe` present; `yt-dlp.exe --version` prints a version.
- [ ] **Step 3: Run** `youtube-para-relogio.bat` with a short public video, the watch in Programação. Expected: success message, the slot appears on the page with a size below 3 MB, and plays on the speaker. Re-run with a >20 min video: the tool refuses before uploading.

---

### Task 8: Final verification

**Files:** none new; update `tools/pc/LEIAME.txt` if anything changed.

- [ ] **Step 1: Clean build and native tests.** Run `pio test -e native` (all PASS) and `pio run -e watch` (builds with no errors); record flash/RAM usage.
- [ ] **Step 2: Soak test with the user:** fill all 4 slots (two from YouTube, two from the page), play in `AllSlots` mode for 30 minutes; expected no reset, no audio dropouts longer than 1 s, battery level logged.
- [ ] **Step 3: Review Focus checklist:** re-run each of the five Review Focus scenarios and note the result.
