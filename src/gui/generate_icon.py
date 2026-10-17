import math
from PIL import Image, ImageDraw

def create_idm_icon(size):
    # Create image with transparent background
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    margin = max(1, int(size * 0.04))
    r = (size - 2 * margin) / 2.0
    cx = size / 2.0
    cy = size / 2.0

    # 1. Draw outer soft shadow
    shadow_offset = max(1, int(size * 0.03))
    draw.ellipse([cx - r + shadow_offset, cy - r + shadow_offset, cx + r + shadow_offset, cy + r + shadow_offset], 
                 fill=(0, 0, 0, 45))

    # 2. Draw globe sphere base (Radial 3D lighting effect)
    for i in range(int(r), 0, -1):
        ratio = (r - i) / r
        # Deep blue at edges, cyan/cerulean at top-left highlight
        red = int(12 + ratio * 45)
        green = int(70 + ratio * 120)
        blue = int(160 + ratio * 90)
        # Shift light source towards top-left
        lx = cx - (r - i) * 0.35
        ly = cy - (r - i) * 0.35
        draw.ellipse([lx - i, ly - i, lx + i, ly + i], fill=(red, green, blue, 255))

    # 3. Draw globe grid lines (latitude & longitude)
    grid_color = (180, 225, 255, 90)
    line_w = max(1, int(size * 0.02))

    # Equator and latitude ellipses
    draw.ellipse([cx - r * 0.95, cy - r * 0.25, cx + r * 0.95, cy + r * 0.25], outline=grid_color, width=line_w)
    draw.ellipse([cx - r * 0.75, cy - r * 0.65, cx + r * 0.75, cy - r * 0.15], outline=grid_color, width=line_w)
    draw.ellipse([cx - r * 0.75, cy + r * 0.15, cx + r * 0.75, cy + r * 0.65], outline=grid_color, width=line_w)

    # Longitude ellipse
    draw.ellipse([cx - r * 0.45, cy - r * 0.92, cx + r * 0.45, cy + r * 0.92], outline=grid_color, width=line_w)
    draw.line([cx, cy - r, cx, cy + r], fill=grid_color, width=line_w)

    # 4. Draw iconic 3D green download arrow in center
    arrow_w = size * 0.42
    arrow_h = size * 0.58
    top_y = cy - arrow_h * 0.48
    stem_w = arrow_w * 0.46
    head_y = cy + arrow_h * 0.08
    tip_y = cy + arrow_h * 0.52

    arrow_points = [
        (cx - stem_w / 2, top_y),
        (cx + stem_w / 2, top_y),
        (cx + stem_w / 2, head_y),
        (cx + arrow_w / 2, head_y),
        (cx, tip_y),
        (cx - arrow_w / 2, head_y),
        (cx - stem_w / 2, head_y),
    ]

    # Arrow drop shadow
    shadow_pts = [(x + max(1, size * 0.025), y + max(1, size * 0.025)) for x, y in arrow_points]
    draw.polygon(shadow_pts, fill=(0, 30, 0, 120))

    # Arrow outer border (dark green 3D bevel)
    draw.polygon(arrow_points, fill=(10, 100, 30, 255))

    # Inset arrow body (glossy vibrant emerald green)
    inset = max(1, int(size * 0.025))
    inset_points = [
        (cx - stem_w / 2 + inset, top_y + inset),
        (cx + stem_w / 2 - inset, top_y + inset),
        (cx + stem_w / 2 - inset, head_y),
        (cx + arrow_w / 2 - inset * 1.5, head_y),
        (cx, tip_y - inset * 1.8),
        (cx - arrow_w / 2 + inset * 1.5, head_y),
        (cx - stem_w / 2 + inset, head_y),
    ]
    draw.polygon(inset_points, fill=(0, 204, 75, 255))

    # Arrow top glossy specular highlight
    specular_pts = [
        (cx - stem_w / 2 + inset, top_y + inset),
        (cx + stem_w / 2 - inset, top_y + inset),
        (cx + stem_w / 2 - inset, top_y + arrow_h * 0.28),
        (cx - stem_w / 2 + inset, top_y + arrow_h * 0.32),
    ]
    draw.polygon(specular_pts, fill=(180, 255, 200, 160))

    return img

sizes = [16, 32, 48, 64, 128, 256]
images = [create_idm_icon(s) for s in sizes]

# Save as multi-resolution icon
images[0].save('src/gui/idm.ico', format='ICO', sizes=[(s, s) for s in sizes], append_images=images[1:])
print("Successfully generated src/gui/idm.ico with sizes:", sizes)
