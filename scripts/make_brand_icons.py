#!/usr/bin/env python3
"""Every app icon from one picture: resources/images/3idea_logo.png (the 3idea logo, 2000 px, white background).

The "Imagine / Create / Print" line is left out (it can't be read at icon size); the bulb and "Idea" go on a white
rounded square so the icon shows on dark and light taskbars. Needs Pillow.

    python3 scripts/make_brand_icons.py
"""
import base64
import io
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parent.parent
IMAGES = ROOT / "resources" / "images"
MSIX = ROOT / "scripts" / "msix" / "assets"
TAGLINE = (0.5125, 0.71875)  # the tagline starts at these fractions of the width and height


def mark() -> Image.Image:
    """The logo without its tagline, white made see-through, trimmed."""
    src = Image.open(IMAGES / "3idea_logo.png").convert("RGB")
    w, h = src.size
    ImageDraw.Draw(src).rectangle([int(w * TAGLINE[0]), int(h * TAGLINE[1]), w, h], fill=(255, 255, 255))
    px = src.load()
    out = Image.new("RGBA", src.size)
    dst = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            a = max(0, min(255, (250 - min(r, g, b)) * 3))  # the "white" is 254, not 255
            if a:
                k = a / 255
                dst[x, y] = tuple(max(0, min(255, round((c - 255 * (1 - k)) / k))) for c in (r, g, b)) + (a,)
    return out.crop(out.getbbox())


def fit(img: Image.Image, size: int, pad: float) -> Image.Image:
    """img centred in a size x size see-through square, pad (fraction) round it."""
    inner = round(size * (1 - 2 * pad))
    scaled = ImageOps.contain(img, (inner, inner), Image.LANCZOS)
    out = Image.new("RGBA", (size, size))
    out.alpha_composite(scaled, ((size - scaled.width) // 2, (size - scaled.height) // 2))
    return out


def tile(img: Image.Image, size: int) -> Image.Image:
    """The mark on a white rounded square (drawn 4x and shrunk, for smooth corners)."""
    big = size * 4
    out = Image.new("RGBA", (big, big))
    ImageDraw.Draw(out).rounded_rectangle([0, 0, big - 1, big - 1], radius=round(big * 0.22), fill=(255, 255, 255, 255))
    out.alpha_composite(fit(img, big, 0.1))
    return out.resize((size, size), Image.LANCZOS)


def main() -> None:
    m = mark()
    t = tile(m, 1024)
    for name, size in [("Infinium_32px", 32), ("Infinium_64", 64), ("Infinium_128px", 128), ("Infinium-mac_128px", 128),
                       ("Infinium_154", 154), ("Infinium_154_title", 154), ("Infinium", 154), ("InfiniumTitle", 154),
                       ("Infinium_192px", 192)]:
        t.resize((size, size), Image.LANCZOS).save(IMAGES / f"{name}.png", optimize=True)
    fit(m, 192, 0.04).save(IMAGES / "Infinium_192px_transparent.png", optimize=True)
    gray = t.resize((192, 192), Image.LANCZOS)
    g = ImageOps.grayscale(gray.convert("RGB")).convert("RGBA")
    g.putalpha(gray.getchannel("A"))
    g.save(IMAGES / "Infinium_192px_grayscale.png", optimize=True)

    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    for name in ("Infinium.ico", "InfiniumTitle.ico", "Infinium-mac_256px.ico"):
        t.resize((256, 256), Image.LANCZOS).save(IMAGES / name, sizes=sizes)
    t.save(IMAGES / "Infinium.icns")

    png = io.BytesIO()
    fit(m, 512, 0.02).save(png, "PNG", optimize=True)
    (IMAGES / "studio_logo.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 512 512">'
        f'<image width="512" height="512" xlink:href="data:image/png;base64,{base64.b64encode(png.getvalue()).decode()}"/></svg>\n')

    # Microsoft Store package (MSIX): the white square too, as dark blue "Idea" is lost on a dark Start menu
    for name, size in [("Square150x150Logo", 150), ("Square44x44Logo", 44),
                       ("Square44x44Logo.targetsize-44_altform-unplated", 44), ("StoreLogo", 50)]:
        t.resize((size, size), Image.LANCZOS).save(MSIX / f"{name}.png", optimize=True)


if __name__ == "__main__":
    main()
