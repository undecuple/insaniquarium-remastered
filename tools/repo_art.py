#!/usr/bin/env python3
"""Paints the README banner and the GitHub social preview: a fish tank seen from inside, drawn as a painted-looking
background (domain-warped fractal noise, light rays from the surface with a zoom blur, bubbles, a sand strip, grain and
a vignette). No game art, sprites or logo lettering; the lettering is Lilita One (SIL OFL, from Google Fonts' own
repository, pinned by SHA-256, kept in work/fonts and not committed; credited in NOTICE.md). Everything is seeded and
nothing reads the clock, so the same run gives the same bytes.

  tools/repo_art.py            writes assets/banner.png, assets/banner@2x.png, assets/social-preview.png
  tools/repo_art.py --check    paints again and fails if a file on disk differs
  tools/repo_art.py --preview  writes the three images to work/art-preview/ instead (not tracked)

Needs numpy and Pillow (nix-shell -p 'python3.withPackages(ps:[ps.numpy ps.pillow])').
The social preview is 1280x640 and under 1 MB; GitHub has no API for it: upload it by hand
(Settings > General > Social preview). Every mark stays inside a 40 px margin (scaled for the 2x banner).
"""
import hashlib, sys, urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/lilitaone/LilitaOne-Regular.ttf"
FONT_SHA = "f5b641c45c69d772ee4eda687bc9fda411d5cad6b0b45371491da4580cbc8d59"
FONT = ROOT / "work" / "fonts" / "LilitaOne-Regular.ttf"

WATER_TOP, WATER_BOT = np.array([22, 102, 122]), np.array([11, 42, 60])   # #16667a -> #0b2a3c
LIGHT, SAND, CORAL, TEXT = (191, 238, 230), (217, 195, 138), (255, 122, 89), (244, 251, 249)

LINE1, LINE2 = "Remastered Mod", "for Insaniquarium! Deluxe"
SUB = "Mods and online co-op. Unofficial fan project."
FAN = "Unofficial fan project"


def font():
    if not FONT.exists() or hashlib.sha256(FONT.read_bytes()).hexdigest() != FONT_SHA:
        FONT.parent.mkdir(parents=True, exist_ok=True)
        data = urllib.request.urlopen(FONT_URL, timeout=30).read()
        if hashlib.sha256(data).hexdigest() != FONT_SHA:
            sys.exit("font download does not match the pinned SHA-256")
        FONT.write_bytes(data)
    return str(FONT)


def noise(rng, w, h, cells, octaves=5):
    """Fractal value noise in [0,1], w x h, with `cells` grid cells across the long side at the first octave."""
    out, amp, tot = np.zeros((h, w)), 1.0, 0.0
    for o in range(octaves):
        c = cells * 2 ** o
        gw, gh = max(2, int(c * w / max(w, h)) + 2), max(2, int(c * h / max(w, h)) + 2)
        g = Image.fromarray((rng.rand(gh, gw) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        out += np.asarray(g, dtype=float) / 255 * amp
        tot += amp
        amp *= 0.5
    return out / tot


def warped(rng, w, h, cells):
    """Noise sampled through a second noise field: the swirled, brushy look."""
    base = noise(rng, w, h, cells)
    dx, dy = noise(rng, w, h, cells * 0.7, 3), noise(rng, w, h, cells * 0.7, 3)
    yy, xx = np.mgrid[0:h, 0:w]
    sx = np.clip(xx + (dx - 0.5) * 0.35 * w / cells * 3, 0, w - 1).astype(int)
    sy = np.clip(yy + (dy - 0.5) * 0.35 * w / cells * 3, 0, h - 1).astype(int)
    return base[sy, sx]


def paint(w, h, seed=7):
    rng = np.random.RandomState(seed)
    sc = h / 640.0
    yy, xx = np.mgrid[0:h, 0:w]
    t = (yy / h)[..., None]
    img = WATER_TOP * (1 - t) + WATER_BOT * t
    # brushy brightness variation in the water
    n = warped(rng, w, h, 3)
    img = img * (0.78 + 0.5 * n[..., None])
    # light rays from a point above the surface: streaks by angle, fading with depth, then a soft blur
    ox, oy = w * 0.62, -h * 0.35
    ang = np.arctan2(xx - ox, yy - oy)
    streak = noise(rng, 600, 1, 14)[0]
    ray = streak[((ang + 1.6) / 3.2 * 599).clip(0, 599).astype(int)]
    ray = np.clip((ray - 0.45) * 3.0, 0, 1) * np.clip(1 - yy / (h * 0.95), 0, 1) ** 1.3
    ray = np.asarray(Image.fromarray((ray * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(6 * sc)), float) / 255
    img += ray[..., None] * (np.array(LIGHT) * 0.5)
    # sand strip with a wavy top edge, speckled
    xs = np.arange(w)
    edge = h - 74 * sc + np.sin(xs / w * 7.0 + 1.0) * 10 * sc + (noise(rng, w, 1, 9)[0] - 0.5) * 22 * sc
    sandm = (yy >= edge[None, :]).astype(float)
    sn = warped(rng, w, h, 14)
    sand = np.array(SAND) * (0.62 + 0.55 * sn[..., None]) * (1 - 0.25 * ((yy - edge) / (74 * sc)).clip(0, 1)[..., None])
    soft = np.asarray(Image.fromarray((sandm * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5 * sc)), float) / 255
    img = img * (1 - soft[..., None]) + sand * soft[..., None]
    # a few dark, blurred seaweed shapes against the sand edge
    weeds = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(weeds)
    for _ in range(11):
        x0 = rng.uniform(0.02, 0.98) * w
        base_y = h - 60 * sc
        hh = rng.uniform(70, 190) * sc
        ph = rng.uniform(0, 6)
        pts = [(x0 + np.sin(i * 0.8 + ph) * 11 * sc * (i / 6), base_y - i * hh / 12) for i in range(13)]
        d.line(pts, fill=150, width=int(8 * sc) + 1, joint="curve")
    weeds = np.asarray(weeds.filter(ImageFilter.GaussianBlur(2.2 * sc)), float) / 255
    img = img * (1 - 0.5 * weeds[..., None]) + np.array([20, 110, 80]) * 0.5 * weeds[..., None]
    # bubbles: faint body, bright rim, tiny highlight; drawn at 3x for smooth edges
    S = 3
    bub = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bub)
    for _ in range(60):
        r = rng.uniform(3, 15) * sc
        x, y = rng.uniform(0, w), rng.uniform(0, h - 90 * sc)
        box = [(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S]
        bd.ellipse(box, fill=(255, 255, 255, 22), outline=(255, 255, 255, 150), width=max(2, int(r * S / 7)))
        hr = r * 0.22
        bd.ellipse([(x - r * 0.45 - hr) * S, (y - r * 0.45 - hr) * S, (x - r * 0.45 + hr) * S, (y - r * 0.45 + hr) * S],
                   fill=(255, 255, 255, 190))
    bub = bub.resize((w, h), Image.LANCZOS)
    base = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")
    base.alpha_composite(bub)
    img = np.asarray(base.convert("RGB"), float)
    # grain and vignette
    img += (rng.randn(h, w, 1) * 5.5)
    v = ((xx / w - 0.5) ** 2 * 1.3 + (yy / h - 0.48) ** 2 * 1.6)
    img *= (1 - np.clip(v, 0, 1) ** 1.2 * 0.75)[..., None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")


def lettering(im, box, sizes):
    """Centered heavy lettering in `box` (x0,y0,x1,y1): a dark drop, a dark outline, a coral rim, a pale gradient fill."""
    w, h = im.size
    f = font()
    cx = (box[0] + box[2]) / 2
    y = box[1]
    for text, size, kind in sizes:
        ft = ImageFont.truetype(f, size)
        tw = ft.getlength(text)
        asc, desc = ft.getmetrics()
        mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(mask).text((cx - tw / 2, y), text, font=ft, fill=255)
        pad = max(2, size // 14)
        grow = mask.filter(ImageFilter.MaxFilter(2 * pad + 1 if (2 * pad + 1) % 2 else 2 * pad + 2))
        shadow = Image.new("RGBA", (w, h), (4, 20, 32, 0))
        shadow.putalpha(grow.filter(ImageFilter.GaussianBlur(pad)).point(lambda p: int(p * 0.7)))
        im.alpha_composite(shadow.transform(shadow.size, Image.AFFINE, (1, 0, 0, 0, 1, -size * 0.07)))
        rim = Image.new("RGBA", (w, h), CORAL + (255,) if kind == "title" else (7, 38, 58, 255))
        rim.putalpha(grow)
        im.alpha_composite(rim)
        ink = Image.new("RGBA", (w, h), (6, 30, 46, 255))
        ink.putalpha(mask.filter(ImageFilter.MaxFilter(3)))
        im.alpha_composite(ink)
        grad = Image.new("RGBA", (w, h))
        gp = np.zeros((h, w, 4), np.uint8)
        top, bot = ((255, 255, 255), (168, 232, 228)) if kind == "title" else (TEXT, (200, 240, 236))
        for yy in range(int(y), int(y + asc + desc) + 1):
            if 0 <= yy < h:
                tt = min(1, max(0, (yy - y) / (asc + desc)))
                gp[yy, :, :3] = [int(top[i] * (1 - tt) + bot[i] * tt) for i in range(3)]
                gp[yy, :, 3] = 255
        grad = Image.fromarray(gp)
        grad.putalpha(Image.fromarray(np.minimum(np.asarray(grad.split()[3]), np.asarray(mask))))
        im.alpha_composite(grad)
        y += asc + desc + size * 0.06
    return im


def quantize(im):
    return im.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)


def make():
    """name -> palette image. Positions are fractions of the height so the 2x banner is the 1x one, sharper."""
    out = {}
    # social preview: 1280x640, title large and centered, nothing smaller than 32 px
    im = paint(1280, 640)
    im = lettering(im, (40, 120, 1240, 520), [(LINE1, 128, "title"), (LINE2, 64, "title"), (SUB, 38, "sub")])
    out["social-preview"] = quantize(im)
    # banner: 1280x320 and 2560x640
    for name, s in (("banner", 1), ("banner@2x", 2)):
        im = paint(1280 * s, 320 * s, seed=8)
        im = lettering(im, (60 * s, 38 * s, 1220 * s, 290 * s),
                       [(LINE1, 90 * s, "title"), (LINE2, 46 * s, "title"), (FAN, 24 * s, "sub")])
        out[name] = quantize(im)
    return out


def main():
    check = "--check" in sys.argv
    dest = ROOT / ("work/art-preview" if "--preview" in sys.argv else "assets")
    dest.mkdir(parents=True, exist_ok=True)
    bad = 0
    for name, im in make().items():
        p = dest / f"{name}.png"
        tmp = dest / f".{name}.new.png"
        im.save(tmp, optimize=True)
        if check:
            same = p.exists() and p.read_bytes() == tmp.read_bytes()
            tmp.unlink()
            print(("ok     " if same else "DIFFERS ") + str(p))
            bad += not same
        else:
            tmp.replace(p)
            print(f"{p} {im.size[0]}x{im.size[1]} {p.stat().st_size // 1024} KB")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
