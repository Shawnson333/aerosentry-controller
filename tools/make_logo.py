from PIL import Image

src = Image.open(r"C:\Users\sjohn\Downloads\grok_1793227431048.jpg").convert("RGB")
corner = src.getpixel((8, 8))
ui = (0x0A, 0x16, 0x28)

def dist(pixel):
    return abs(pixel[0] - corner[0]) + abs(pixel[1] - corner[1]) + abs(pixel[2] - corner[2])

xs = []
ys = []
for y in range(src.height):
    for x in range(src.width):
        if dist(src.getpixel((x, y))) > 28:
            xs.append(x)
            ys.append(y)
pad = 16
left = max(0, min(xs) - pad)
top = max(0, min(ys) - pad)
right = min(src.width, max(xs) + pad + 1)
bottom = min(src.height, max(ys) + pad + 1)
crop = src.crop((left, top, right, bottom))
print("corner", corner, "crop", crop.size)

max_w, max_h = 780, 470
scale = min(max_w / crop.width, max_h / crop.height)
mark = crop.resize(
    (max(1, round(crop.width * scale)), max(1, round(crop.height * scale))),
    Image.Resampling.LANCZOS,
)
blended = Image.new("RGB", mark.size, ui)
pixels = mark.load()
out_px = blended.load()
for y in range(mark.height):
    for x in range(mark.width):
        pixel = pixels[x, y]
        out_px[x, y] = ui if dist(pixel) <= 36 else pixel

canvas = Image.new("RGB", (800, 480), ui)
canvas.paste(blended, ((800 - blended.width) // 2, (480 - blended.height) // 2))
print("mark", blended.size)

out = r"c:\Users\sjohn\.platformio\Projects\aerosentry-controller\src\logo_img.c"
raw = []
for y in range(canvas.height):
    for x in range(canvas.width):
        red, green, blue = canvas.getpixel((x, y))
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        raw.append(value & 0xFF)
        raw.append(value >> 8)

lines = []
row = []
for byte in raw:
    row.append(f"0x{byte:02x}")
    if len(row) == 16:
        lines.append("  " + ", ".join(row) + ",")
        row = []
if row:
    lines.append("  " + ", ".join(row) + ",")

text = f"""#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t aerosentry_logo_map[] = {{
{chr(10).join(lines)}
}};

const lv_img_dsc_t aerosentry_logo = {{
    .header.always_zero = 0,
    .header.w = 800,
    .header.h = 480,
    .data_size = 800 * 480 * 2,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = aerosentry_logo_map,
}};
"""
with open(out, "w", newline="\n") as handle:
    handle.write(text)
print("wrote", len(raw), "bytes")
