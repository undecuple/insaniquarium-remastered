#!/usr/bin/env python3
"""Paints the README banner and the GitHub social preview: a fish tank seen from inside, drawn as a painted-looking
background (domain-warped fractal noise, light rays from the surface with a zoom blur, bubbles, a sand strip, grain and
a vignette). No game art, sprites or logo lettering; the lettering is Lilita One (SIL OFL, from Google Fonts' own
repository, pinned by SHA-256, kept in work/fonts and not committed; credited in NOTICE.md). Everything is seeded and
nothing reads the clock, so the same run gives the same bytes.

  tools/repo_art.py            writes assets/banner.png, assets/banner@2x.png, assets/social-preview.png
  tools/repo_art.py --check    paints again and fails if a file on disk differs
  tools/repo_art.py --preview  writes the three images to work/art-preview/ instead (not tracked)

The README also has an animated banner, assets/banner-animated.gif: the same banner with fish swimming across and
bubbles rising behind the lettering. The fish are drawn here, shape by shape (no game art). It loops without a seam:
every motion goes round a whole number of times in the loop. --check and --preview cover it too.

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


def paint(w, h, seed=7, grain=5.5):
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
    if grain:
        img += (rng.randn(h, w, 1) * grain)
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


# ---- the animated banner -------------------------------------------------------------------------------------------
FRAMES, FRAME_MS = 100, 70   # 7 seconds; every motion below completes a whole number of turns in FRAMES


def fish_sprite(L, top, belly, fin, phase, S=4):
    """A fish facing right, L px long, drawn at S times the size and scaled down. `phase` swings the tail."""
    W, H = int(L * 1.25), int(L * 0.72)
    im = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cy = H * S / 2
    sway = np.sin(phase) * 0.14 * H * S
    outline = (6, 30, 46, 255)
    # tail, dorsal and pelvic fins
    d.polygon([(0.30 * W * S, cy - 0.07 * H * S), (0.01 * W * S, cy - 0.36 * H * S + sway), (0.08 * W * S, cy + sway * 0.4),
               (0.01 * W * S, cy + 0.36 * H * S + sway), (0.30 * W * S, cy + 0.07 * H * S)], fill=fin, outline=outline, width=S)
    d.polygon([(0.42 * W * S, cy - 0.26 * H * S), (0.56 * W * S, cy - 0.52 * H * S), (0.78 * W * S, cy - 0.26 * H * S)],
              fill=fin, outline=outline, width=S)
    d.polygon([(0.50 * W * S, cy + 0.24 * H * S), (0.58 * W * S, cy + 0.46 * H * S), (0.70 * W * S, cy + 0.26 * H * S)],
              fill=fin, outline=outline, width=S)
    box = [0.22 * W * S, cy - 0.34 * H * S, 0.99 * W * S, cy + 0.34 * H * S]
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).ellipse(box, fill=255)
    body = Image.new("RGBA", im.size, top)
    # a lighter belly and one pale stripe, both clipped to the body
    det = Image.new("RGBA", im.size, (0, 0, 0, 0))
    dd = ImageDraw.Draw(det)
    dd.ellipse([0.20 * W * S, cy + 0.02 * H * S, 1.01 * W * S, cy + 0.70 * H * S], fill=belly)
    dd.ellipse([0.50 * W * S, cy - 0.40 * H * S, 0.60 * W * S, cy + 0.40 * H * S], fill=(255, 255, 255, 80))
    body.alpha_composite(det)
    body.putalpha(mask)
    im.alpha_composite(body)
    d.ellipse(box, outline=outline, width=S)
    d.arc([0.66 * W * S, cy - 0.30 * H * S, 0.84 * W * S, cy + 0.30 * H * S], 100, 260, fill=outline, width=S)
    ex, ey, r = 0.90 * W * S, cy - 0.08 * H * S, 0.08 * H * S
    d.ellipse([ex - r, ey - r, ex + r, ey + r], fill=(255, 255, 255, 255), outline=outline, width=S)
    p = r * 0.5
    d.ellipse([ex - p + r * 0.2, ey - p, ex + p + r * 0.2, ey + p], fill=outline)
    return im.resize((W, H), Image.LANCZOS)


# size (px), y as a fraction of the height, colours (back, belly, fins), direction, start, swim cycles, bob cycles, phase
FISH = [
    (118, 0.74, (255, 168, 54, 255), (255, 226, 150, 255), (255, 120, 60, 235), 1, 0.00, 1, 2, 0.0),
    (78, 0.40, (64, 196, 200, 255), (200, 246, 232, 255), (30, 140, 170, 235), -1, 0.28, 1, 3, 1.7),
    (58, 0.60, (255, 120, 112, 255), (255, 214, 200, 255), (220, 70, 90, 235), 1, 0.52, 1, 2, 3.1),
    (96, 0.22, (110, 150, 235, 255), (214, 232, 255, 255), (70, 90, 200, 235), -1, 0.74, 1, 2, 4.4),
    (44, 0.84, (250, 214, 70, 255), (255, 244, 186, 255), (232, 160, 40, 235), 1, 0.88, 1, 3, 5.6),
]
BUBBLES = [(0.06, 7, 0.10), (0.15, 12, 0.55), (0.27, 5, 0.80), (0.41, 9, 0.30), (0.56, 6, 0.95), (0.63, 13, 0.05),
           (0.74, 8, 0.65), (0.83, 11, 0.40), (0.91, 5, 0.20), (0.97, 9, 0.75)]   # x fraction, radius, start


def animation():
    """The frames of the banner: the 1280x320 banner with swimming fish and rising bubbles behind the lettering."""
    w, h = 1280, 320
    base = paint(w, h, seed=8, grain=0)   # no grain: it would not compress in a GIF
    plain = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    lettering(plain, (60, 38, 1220, 290), [(LINE1, 90, "title"), (LINE2, 46, "title"), (FAN, 24, "sub")])
    frames = []
    for k in range(FRAMES):
        t = k / FRAMES
        im = base.copy()
        M = 200
        layer = Image.new("RGBA", (w + 2 * M, h), (0, 0, 0, 0))
        for size, yf, top, belly, fin, dr, start, laps, bobs, ph in FISH:
            sp = fish_sprite(size, top, belly, fin, 2 * np.pi * (3 * t) + ph)
            if dr < 0:
                sp = sp.transpose(Image.FLIP_LEFT_RIGHT)
            span = w + 2 * M
            x = ((start + dr * laps * t) % 1.0) * span - sp.width / 2
            y = yf * h + np.sin(2 * np.pi * (bobs * t) + ph) * 7 - sp.height / 2
            layer.alpha_composite(sp, (int(round(x)), int(round(y))))
        im.alpha_composite(layer.crop((M, 0, M + w, h)))
        S = 3
        bub = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
        bd = ImageDraw.Draw(bub)
        for xf, r, st in BUBBLES:
            span = h + 2 * r + 20
            y = h + r - ((st + t) % 1.0) * span
            x = xf * w + np.sin(2 * np.pi * (2 * t) + st * 9) * 6
            bd.ellipse([(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S], fill=(255, 255, 255, 22),
                       outline=(255, 255, 255, 150), width=max(2, int(r * S / 7)))
            hr = r * 0.22
            bd.ellipse([(x - r * 0.45 - hr) * S, (y - r * 0.45 - hr) * S, (x - r * 0.45 + hr) * S, (y - r * 0.45 + hr) * S],
                       fill=(255, 255, 255, 190))
        im.alpha_composite(bub.resize((w, h), Image.LANCZOS))
        im.alpha_composite(plain)
        frames.append(im.convert("RGB"))
    # one palette for all frames, so the unchanged pixels stay identical and the GIF only stores what moves
    sheet = Image.new("RGB", (w, h * 3))
    for i, k in enumerate((0, FRAMES // 3, 2 * FRAMES // 3)):
        sheet.paste(frames[k], (0, i * h))
    pal = sheet.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    return [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]


def save_gif(frames, path):
    """Every frame after the first stores only the pixels that changed (the rest is the transparent index 255 over
    the previous frame, which compresses to almost nothing)."""
    idx = [np.asarray(fr) for fr in frames]
    pal = frames[0].getpalette()
    out = [frames[0]]
    for k in range(1, len(frames)):
        im = Image.fromarray(np.where(idx[k] != idx[k - 1], idx[k], 255).astype(np.uint8), "P")
        im.putpalette(pal)
        out.append(im)
    out[0].save(path, save_all=True, append_images=out[1:], duration=FRAME_MS, loop=0, optimize=False, disposal=1,
                transparency=255)


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
    p = dest / "banner-animated.gif"
    tmp = dest / ".banner-animated.new.gif"
    save_gif(animation(), tmp)
    if check:
        same = p.exists() and p.read_bytes() == tmp.read_bytes()
        tmp.unlink()
        print(("ok     " if same else "DIFFERS ") + str(p))
        bad += not same
    else:
        tmp.replace(p)
        print(f"{p} {FRAMES} frames {p.stat().st_size // 1024} KB")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
