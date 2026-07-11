#!/usr/bin/env python3
"""
Converts PNG images to RGB565 uint16_t C-array headers for embedded displays.
Usage: python png_to_rgb565.py <image.png> <variable_name>
"""
import sys
import struct
from PIL import Image

def convert(png_path, var_name, output_path):
    img = Image.open(png_path).convert("RGBA")
    img = img.resize((32, 32), Image.LANCZOS)
    
    pixels = []
    for y in range(32):
        for x in range(32):
            r, g, b, a = img.getpixel((x, y))
            # If transparent, output black
            if a < 128:
                r, g, b = 0, 0, 0
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            pixels.append(rgb565)
    
    lines = []
    lines.append(f"// Auto-generated from {png_path}")
    lines.append(f"// 32x32 RGB565 pixel array")
    lines.append(f"const uint16_t {var_name}[1024] PROGMEM = {{")
    
    row_str = []
    for i, px in enumerate(pixels):
        row_str.append(f"0x{px:04X}")
        if (i + 1) % 16 == 0:
            lines.append("  " + ", ".join(row_str) + ",")
            row_str = []
    if row_str:
        lines.append("  " + ", ".join(row_str))
    lines.append("};")
    
    with open(output_path, "w") as f:
        f.write("\n".join(lines))
    print(f"Written to {output_path}")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2], sys.argv[3])
