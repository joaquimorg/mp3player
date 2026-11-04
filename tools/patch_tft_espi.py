"""PlatformIO pre-build script: patches the TFT_eSPI copy used by TTGO_TWatch_Library.

Problem: TFT_eSprite::fillRect() fills the first row of a rectangle and copies it to the other rows with
memcpy(). With the sprite in PSRAM (the 240x240 screen buffer) and a rectangle starting at an ODD x, that
memcpy leaves a stray black pixel column at x-1 (an ESP32 PSRAM / unaligned-copy problem). It showed as a thin
vertical line before some words and next to some icons. The patch fills every row with plain 16-bit stores
through a volatile pointer (so the compiler cannot turn the loop back into memcpy/memset).

The library is downloaded by PlatformIO into .pio/libdeps, so the fix is applied here, at build time, and is
idempotent. Only the 16-bit branch of TFT_eSprite::fillRect is changed.
"""
Import("env")  # noqa: F821  (provided by PlatformIO)
import os

MARK = "T-Watch MP3 patch"
NEW_BLOCK = """if (_bpp == 16) {
        // T-Watch MP3 patch: no memcpy/memset into PSRAM (stray black column at x-1 for odd x).
        color = (color >> 8) | (color << 8);
        for (int32_t row = 0; row < h; row++) {
            volatile uint16_t* d = _img + yp + (int32_t)_iwidth * row;
            for (int32_t i = 0; i < w; i++) d[i] = (uint16_t) color;
        }
    """


def patch_file(path):
    with open(path, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8", "replace")
    crlf = "\r\n" in text
    text = text.replace("\r\n", "\n")
    fn = text.find("void TFT_eSprite::fillRect(int32_t x")
    if fn < 0:
        print("patch_tft_espi: fillRect not found in %s (library changed?)" % path)
        return
    start = text.find("if (_bpp == 16) {", fn)
    end = text.find("} else if (_bpp == 8) {", start)
    if start < 0 or end < 0:
        print("patch_tft_espi: unexpected fillRect layout in %s" % path)
        return
    if MARK in text[start:end]:
        return  # already patched
    text = text[:start] + NEW_BLOCK + text[end:]
    if crlf:
        text = text.replace("\n", "\r\n")
    with open(path, "wb") as f:
        f.write(text.encode("utf-8"))
    print("patch_tft_espi: patched TFT_eSprite::fillRect in %s" % path)


libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))  # noqa: F821
for root, _dirs, files in os.walk(libdeps):
    if os.path.basename(root) == "TFT_eSPI" and "TFT_eSPI.cpp" in files:
        patch_file(os.path.join(root, "TFT_eSPI.cpp"))
