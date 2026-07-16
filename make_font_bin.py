import re

with open('font_sun8x16_clean.h', 'r') as f:
    text = f.read()

# Extract hex values
hex_vals = re.findall(r'0x[0-9a-fA-F]{2}', text)

with open('font.bin', 'wb') as f:
    f.write(bytes([int(x, 16) for x in hex_vals]))
