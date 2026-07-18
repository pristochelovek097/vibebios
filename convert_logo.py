from PIL import Image

# Open the image
img = Image.open("ICE_TEA_BIOS-master/Insyde/InsydeModulePkg/Universal/Console/Logo/InsydeBoot/InsydeBoot.bmp")
# Resize it to 128x128
img = img.resize((128, 128), Image.Resampling.LANCZOS)
img = img.convert("RGBA")

pixels = list(img.getdata())
with open("logo_data.h", "w") as f:
    f.write("const unsigned int logo_data[128*128] = {\n")
    for i, p in enumerate(pixels):
        # p is (R, G, B, A)
        r, g, b, a = p
        # ARGB format: A << 24 | R << 16 | G << 8 | B
        val = (255 << 24) | (r << 16) | (g << 8) | b
        f.write(f"0x{val:08X}, ")
        if i % 8 == 7:
            f.write("\n")
    f.write("};\n")
print("Converted to logo_data.h")
