with open('font_sun8x16.c', 'r') as f:
    lines = f.readlines()
with open('font_sun8x16_clean.h', 'w') as f:
    f.write('const u8 font_sun8x16[4096] = {\n')
    for line in lines:
        if '/*' in line and '*/' in line and '0x' in line:
            f.write(line)
    f.write('};\n')
