"""A thunderstorm of letters. Light arrives at once; thunder crawls at 343 m/s.

An original scene in the style of the python.animate YouTube channel
(https://www.youtube.com/@python.animate): setup(cols, rows) once, then frame(t)
as a pure function of time, drawn with letters grouped by how much ink they
carry. That letter table (LEVELS) is the channel's. It repeats every DURATION
seconds, so it loops seamlessly.
"""
import numpy as np

TITLE = "ASCII Thunderstorm"
DURATION = 24.0
FPS = 30
ACCENT = (120, 160, 255)      # status stripe and time stamp
GLOW = 0.8

# letters grouped by how much ink they carry, dark -> bright
LEVELS = [" ", ".`'", ":;,-~", "=+*^", "ijltrxz/\\|",
          "oecnuv()", "0O8B&%", "#@$QW"]

SOUND = 0.343         # speed of sound, km/s
# lightning strikes: (time s, distance km, across the screen 0..1).
# Near ones are tall bolts; far ones are small and dim; beyond FAR_KM there is
# no bolt at all, only a glow inside the clouds (and you cannot hear it).
STRIKES = [(2.6, 2.4, 0.30), (4.6, 10.5, 0.86), (6.9, 5.8, 0.68),
           (7.6, 13.0, 0.20), (10.4, 9.0, 0.52), (12.8, 1.8, 0.45),
           (15.1, 12.0, 0.08), (17.2, 6.5, 0.76), (20.6, 3.5, 0.20),
           (22.3, 11.0, 0.62)]
NEAR_KM, FAR_KM = 1.5, 12.0
# distant rain storms hanging under the far clouds: (across 0..1, width 0..1, strength)
STORMS = [(0.14, 0.07, 0.9), (0.52, 0.10, 0.7), (0.84, 0.06, 1.0)]
DROPS = 0.03          # raindrops per letter cell: a drizzle
S = {}                # scene data, built once in setup()


def smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


# colour: night blue -> storm blue -> flash white

def palette(lum):
    xs = [0.0, 0.3, 0.7, 1.0]
    cols = [(6, 8, 20), (40, 55, 100), (150, 170, 230), (250, 250, 255)]
    return np.stack([np.interp(lum, xs, [c[i] for c in cols])
                     for i in range(3)], -1)


def hash2(x, y):
    return (np.sin(x * 12.9898 + y * 78.233) * 43758.5453) % 1.0


def vnoise(x, y):
    """smooth value noise"""
    ix, iy = np.floor(x), np.floor(y)
    fx, fy = smooth(x - ix), smooth(y - iy)
    a, b = hash2(ix, iy), hash2(ix + 1, iy)
    c, d = hash2(ix, iy + 1), hash2(ix + 1, iy + 1)
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(x, y):
    return (0.55 * vnoise(x, y) + 0.30 * vnoise(2 * x + 5.2, 2 * y + 1.3)
            + 0.15 * vnoise(4 * x + 9.1, 4 * y + 3.7))


# setup: the hills, every bolt and every raindrop, fixed once

def setup(cols, rows):
    rng = np.random.default_rng(7)
    i, j = np.mgrid[0:rows, 0:cols]
    horizon = int(rows * 0.84)
    jj = np.arange(cols)
    ridge = (horizon + 1.6 * np.sin(jj * 0.09 + 0.5) + 1.0 * np.sin(jj * 0.23 + 2.0)
             + 1.2 * hash2(jj * 1.0, 3.0))
    top = int(rows * 0.24)                      # where the nearest bolts leave the cloud
    sky = horizon - 2                           # the horizon line the clouds converge to
    bolts = []
    for ts, dist, across in STRIKES:
        f = float(np.clip((dist - NEAR_KM) / (FAR_KM - NEAR_KM - 1), 0, 1))   # 0 near .. 1 far
        cell = {}

        def put(r, c, w):
            if 0 <= r < rows and 0 <= c < cols:
                cell[(r, c)] = max(cell.get((r, c), 0.0), w)

        x = int(across * cols)
        end = int(ridge[min(max(x, 0), cols - 1)])
        tp = int(sky - (sky - top) * (1 - 0.85 * f))      # far bolts are short, near the horizon
        if dist < FAR_KM:
            for r in range(tp, end):
                nx = x + int(rng.choice([-2, -1, 0, 1, 2], p=[.05, .2, .5, .2, .05]))
                for c in range(min(x, nx), max(x, nx) + 1):
                    put(r, c, 1.0)
                x = nx
                if tp + 3 < r < end - 4 and rng.random() < 0.10:
                    d, ln = int(rng.choice([-1, 1])), int(rng.integers(4, 12))
                    bx, br = x, r
                    for k in range(ln):
                        br += 1 if rng.random() < 0.7 else 0
                        bx += d
                        put(br, bx, 0.55 * (1 - k / ln))
        glow = dict(cell)
        if f < 0.5:                                       # only near bolts have a soft glow
            for (r, c), w in cell.items():
                for off, gw in ((1, 0.25), (2, 0.10)):
                    for s_ in (-off, off):
                        if 0 <= c + s_ < cols:
                            glow[(r, c + s_)] = max(glow.get((r, c + s_), 0.0), gw * w)
        rr = np.array([k[0] for k in glow], dtype=int)
        cc = np.array([k[1] for k in glow], dtype=int)
        ww = np.array([glow[k] for k in glow])
        cy = tp + 0.35 * (end - tp)
        bolts.append(dict(rr=rr, cc=cc, ww=ww, cx=across * cols, cy=cy,
                          sx=0.2 * cols * (1 - 0.6 * f),
                          sy=max(0.6 * (end - tp), 4.0), f=f))
    n = int(cols * rows * DROPS)
    S.update(cols=cols, rows=rows, i=i, j=j, horizon=horizon, ridge=ridge,
             rtop=top, sky=sky, bolts=bolts,
             x0=rng.uniform(0, cols + rows * 0.5, n),
             sp=rng.uniform(18, 30, n), ph=rng.uniform(0, 1, n),
             thr=rng.uniform(0, 1, n), ln=rng.integers(1, 4, n))


def envelope(age):
    """a return stroke, a second one, and a weak third (lightning flickers)"""
    e = np.exp(-age / 0.05)
    if age >= 0.13:
        e += 0.8 * np.exp(-(age - 0.13) / 0.05)
    if age >= 0.27:
        e += 0.5 * np.exp(-(age - 0.27) / 0.07)
    return e


# one frame: a pure function of time, no state between frames

def frame(t):
    R, C = S["rows"], S["cols"]
    i, j, ridge, sky = S["i"], S["j"], S["ridge"], S["sky"]
    tc = t % DURATION
    inten = 0.475 + 0.225 * np.sin(2 * np.pi * t / DURATION - 1.2)    # 0.25 .. 0.70
    mmh = 1 + 7 * (inten - 0.25) / 0.45

    # lightning: far flashes only glow inside the clouds, near ones light the whole sky
    amb = 0.0
    loc = np.zeros((R, C))
    bolt = np.zeros((R, C))
    for T in (tc, tc + DURATION):               # a strike from the end can spill over
        for (ts, dist, _), b in zip(STRIKES, S["bolts"]):
            age = T - ts
            if not 0 <= age < 1.2:
                continue
            e = envelope(age)
            k = np.clip(2.4 / (1 + 0.5 * dist), 0.12, 1.3)          # far flashes are dimmer
            amb += k * (1 - 0.65 * b["f"]) * (0.6 * min(e, 1.4) + 0.4 * np.exp(-age / 0.35))
            loc += k * e * np.exp(-(((j - b["cx"]) / b["sx"]) ** 2
                                    + ((i - b["cy"]) / b["sy"]) ** 2))
            if age < 0.5 and len(b["ww"]):
                inten_b = (0.6 + 0.4 * min(e, 1.0)) * smooth((0.5 - age) / 0.12)
                np.maximum.at(bolt, (b["rr"], b["cc"]), b["ww"] * inten_b * (1 - 0.5 * b["f"]))
    amb = min(amb, 1.6)
    loc = np.minimum(loc, 1.5)

    # clouds seen from below, in perspective: a flat layer above us that runs off
    # to the horizon, so the pattern shrinks and bunches up with distance
    d = np.maximum(sky - i, 0) + 1.2                 # rows above the horizon line
    Z = (R * 0.9) / d                                # how far away that row of cloud is
    X = (j - C / 2) / (C * 0.5) * Z                  # sideways position on the cloud layer
    n = fbm(X * 1.6 + 0.06 * t, Z * 1.4 + 0.015 * t)
    dens = np.clip((n - 0.22) * 2.2, 0, 1)
    far = smooth((Z - 3.0) / 12.0)                   # 0 overhead .. 1 at the horizon
    lum = (0.012 + 0.17 * far * (0.6 + 0.4 * (1 - dens)) + dens * (0.27 - 0.14 * far)
           + 0.05 * smooth((i - 0.55 * R) / (0.3 * R))
           + dens * (0.40 * amb + 0.9 * loc) + (1 - dens) * 0.08 * amb * (1 - i / R))

    # distant rain storms: faint curtains hanging under the far clouds
    for sx0, sw, sstr in STORMS:
        cx = (sx0 * C + 0.5 * t) % C
        dxs = np.minimum(np.abs(j - cx), C - np.abs(j - cx))
        prof_x = np.exp(-((dxs / (sw * C)) ** 2))
        v = smooth((i - (sky - 8)) / 3.0) * (i < ridge)
        st = vnoise(j * 1.1 - 0.25 * i, 0.9 * t + sx0 * 50)
        lum = lum + 0.20 * sstr * prof_x * v * (0.4 + 0.8 * st) * (1 + 1.2 * amb)

    # the hills are a dark shape against the sky, lit only a little
    ground = i >= ridge
    lum = np.where(ground, 0.004 + 0.07 * amb, lum)

    # drizzle: a few faint short streaks drifting down
    rtop, hz = S["rtop"], S["horizon"]
    span = hz - rtop
    pos = (S["ph"] * span + S["sp"] * t) % span
    slope = 0.20 + 0.08 * np.sin(0.4 * t)
    vis = S["thr"] < inten
    rain = np.zeros((R, C))
    for k in range(3):
        row = rtop + pos - k
        col = np.round(S["x0"] - slope * (row - rtop)).astype(int)
        r = np.round(row).astype(int)
        ok = vis & (k < S["ln"]) & (r >= rtop) & (r < R) & (col >= 0) & (col < C)
        rc = np.clip(col, 0, C - 1)
        ok &= r < ridge[rc]
        w = (0.30 - 0.09 * k) * smooth((row - rtop) / 4.0) * (1 + 0.35 * min(amb, 1))
        np.maximum.at(rain, (r[ok], col[ok]), w[ok])
    lum = np.maximum(lum, rain)
    lum = np.maximum(lum, bolt)

    lum = np.clip(lum * (0.5 + 0.5 * smooth(t / 1.0)), 0, 1)    # fade in
    rgb = palette(lum)

    # which flash does the readout talk about: the latest one so far
    best = max(((ts + 24 * kk, dd) for ts, dd, _ in STRIKES for kk in (-1, 0)
                if ts + 24 * kk <= tc), key=lambda a: a[0])
    ts, dist = best
    wait = dist / SOUND - (tc - ts)
    word = "drizzle" if mmh < 3 else "light rain"
    hud = [f"{word} {mmh:3.1f} mm/h",
           f"flash {dist:4.1f} km away",
           ("too far to hear" if dist >= FAR_KM else
            f"thunder in {wait:4.1f} s" if wait > 0 else
            "thunder!" if wait > -1.5 else "quiet")]
    return lum, rgb, hud
