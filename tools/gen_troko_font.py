#!/usr/bin/env python3
"""Genera main/troko/troko_font_data.inc: atlas de glifos monoespaciados (DejaVu Sans Mono Bold)
usado por el renderizador de Troko. Solo hace falta ejecutarlo si quieres cambiar los caracteres."""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
OUT = sys.argv[2] if len(sys.argv) > 2 else "main/troko/troko_font_data.inc"
REF = 32  # tamano de referencia (px)
chars = [chr(c) for c in range(32, 127)] + list("ÁÉÍÓÚÑÜáéíóúñü¡¿°·▼▲…º–")
font = ImageFont.truetype(FONT, REF)
ascent, descent = font.getmetrics()
adv = font.getlength("M")
data = bytearray()
entries = []
for ch in sorted(set(chars), key=ord):
    if ch == " ":
        entries.append((ord(ch), len(data), 0, 0, 0, 0))
        continue
    l, t, r, b = font.getbbox(ch, anchor="ls")  # relativo a la linea base
    w, h = r - l, b - t
    im = Image.new("L", (w + 2, h + 2), 0)
    ImageDraw.Draw(im).text((-l + 1, -t + 1), ch, font=font, fill=255, anchor="ls")
    w2, h2 = im.size
    entries.append((ord(ch), len(data), w2, h2, l - 1, t - 1))
    data.extend(im.tobytes())
with open(OUT, "w") as f:
    f.write("// Generado por tools/gen_troko_font.py (DejaVu Sans Mono Bold, licencia Bitstream Vera/DejaVu)\n")
    f.write(f"static const int kFontRef = {REF};\n")
    f.write(f"static const float kFontAdvance = {adv / REF:.5f}f;  // ancho de avance / tamano\n")
    f.write(f"static const float kFontAscent = {ascent / REF:.5f}f;\n")
    f.write(f"static const float kFontDescent = {descent / REF:.5f}f;\n")
    f.write("struct GlyphInfo { uint32_t cp; uint32_t off; uint8_t w, h; int8_t left, top; };\n")
    f.write("static const GlyphInfo kGlyphs[] = {\n")
    for cp, off, w, h, l, t in entries:
        f.write(f"  {{{cp}, {off}, {w}, {h}, {l}, {t}}},\n")
    f.write("};\n")
    f.write(f"static const int kGlyphCount = {len(entries)};\n")
    f.write("static const uint8_t kGlyphData[] = {\n")
    for i in range(0, len(data), 24):
        f.write("  " + ",".join(str(x) for x in data[i:i+24]) + ",\n")
    f.write("};\n")
print(len(entries), "glifos,", len(data), "bytes; ascent", ascent, "descent", descent, "adv", adv)
