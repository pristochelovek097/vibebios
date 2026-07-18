from PIL import Image
import sys

# Если пользователь передал аргумент, используем его, иначе logo.bmp
filename = sys.argv[1] if len(sys.argv) > 1 else "logo.bmp"

try:
    img = Image.open(filename)
except Exception as e:
    print(f"Error opening {filename}: {e}")
    sys.exit(1)

# Preserve aspect ratio, scale down so it fits in a 200x200 bounding box
# 200x200 = 40,000 pixels max, easily fits in 128KB ROM
img.thumbnail((200, 200), Image.Resampling.LANCZOS)
width, height = img.size
img = img.convert("RGBA")

pixels = list(img.getdata())
with open("logo_data.h", "w") as f:
    f.write(f"const unsigned int LOGO_WIDTH = {width};\n")
    f.write(f"const unsigned int LOGO_HEIGHT = {height};\n")
    f.write(f"const unsigned int logo_data[{width*height}] = {{\n")
    for i, p in enumerate(pixels):
        r, g, b, a = p
        val = (a << 24) | (r << 16) | (g << 8) | b
        f.write(f"0x{val:08X}, ")
        if i % 8 == 7:
            f.write("\n")
    f.write("};\n")
print(f"Converted {filename} to logo_data.h ({width}x{height})")
