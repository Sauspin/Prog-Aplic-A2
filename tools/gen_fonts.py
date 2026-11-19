#!/usr/bin/env python3
"""
Gera fontes suavizadas (anti-aliased) no formato VLW para a LovyanGFX,
a partir da fonte Inter (licença SIL OFL 1.1 - https://rsms.me/inter/).

As fontes embutidas da LovyanGFX só têm ASCII (sem acentos nem "°"),
por isso geramos as nossas, já com os caracteres do português.

Uso:
    pip install pillow
    python3 tools/gen_fonts.py [pasta_com_os_otf_da_Inter]

Saída: esp32_tft_touch_demo/fonts_inter.cpp e fonts_inter.h
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

FONT_DIR = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/opentype/inter"
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "esp32_tft_touch_demo")

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
PT_BR = "áàâãéêíóôõúüçÁÀÂÃÉÊÍÓÔÕÚÜÇ°ºª·•–—…×−"
DIGITS = " 0123456789:.,-+%°/CFhkmsVWAPa"

# (nome C, arquivo, tamanho em px, caracteres)
FONTS = [
    ("inter_12", "Inter-Medium.otf", 12, ASCII + PT_BR),
    ("inter_14", "Inter-SemiBold.otf", 14, ASCII + PT_BR),
    ("inter_20", "Inter-SemiBold.otf", 20, ASCII + PT_BR),
    ("inter_30", "Inter-Bold.otf", 30, DIGITS),
    ("inter_52", "InterDisplay-Bold.otf", 52, " 0123456789:"),
]


def build_vlw(path, size, chars):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        adv = int(round(font.getlength(ch)))
        l, t, r, b = font.getbbox(ch, anchor="ls")
        w, h = max(0, r - l), max(0, b - t)
        if ch == " " or w == 0 or h == 0:
            glyphs.append((ord(ch), 0, 0, adv, 0, 0, b""))
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor="ls")
        glyphs.append((ord(ch), h, w, adv, -t, l, img.tobytes()))

    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    for code, h, w, adv, dy, dx, _ in glyphs:
        out += struct.pack(">7i", code, h, w, adv, dy, dx, 0)
    for g in glyphs:
        out += g[6]
    return bytes(out)


def main():
    cpp = ['// Arquivo gerado por tools/gen_fonts.py - não edite à mão.',
           '// Fonte: Inter (c) The Inter Project Authors - SIL Open Font License 1.1',
           '#include "fonts_inter.h"', '']
    hdr = ['// Arquivo gerado por tools/gen_fonts.py - não edite à mão.',
           '#pragma once', '#include <stdint.h>', '#include <stddef.h>', '']
    total = 0
    for name, fname, size, chars in FONTS:
        data = build_vlw(os.path.join(FONT_DIR, fname), size, chars)
        total += len(data)
        hdr.append(f"extern const uint8_t {name}_vlw[];")
        hdr.append(f"extern const size_t  {name}_vlw_len;")
        cpp.append(f"// {fname} {size}px - {len(data)} bytes")
        cpp.append(f"alignas(4) const uint8_t {name}_vlw[] = {{")
        for i in range(0, len(data), 24):
            cpp.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
        cpp.append("};")
        cpp.append(f"const size_t {name}_vlw_len = sizeof({name}_vlw);")
        cpp.append("")
    with open(os.path.join(OUT_DIR, "fonts_inter.cpp"), "w", encoding="utf-8") as f:
        f.write("\n".join(cpp))
    with open(os.path.join(OUT_DIR, "fonts_inter.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(hdr) + "\n")
    print(f"ok - {total / 1024:.1f} KB de fontes")


if __name__ == "__main__":
    main()
