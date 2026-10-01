#!/usr/bin/env python3
"""Choose the row rotation that balances the shared Bayer dither tables, and
measure the row banding it removes (gm-404a).

The standard recursive Bayer matrix is balanced by column and not by row:
every column of the 8x8 matrix sums to 252 but its rows sum to 168 to 336,
and the 4x4 matrix has the same defect (columns 30, rows 20 to 40). On a flat
surface each row then carries a mean offset, and every eighth (or fourth) row
reads brighter than its neighbour.

The fix rotates column x of the matrix by an xor on the row index:

    new[y][x] = bayer[y ^ ROT[x]][x]

That keeps every column's values (so the column sums stay ideal) and, for the
right ROT, makes every row sum ideal too. This script enumerates every ROT
that balances both axes, with ROT[0] = 0 (an xor of every entry by the same
constant only relabels the rows), and ranks them by low-frequency power:

    score = sum over DFT bins f != 0 of |F(f)|^2 / |f|^2

on the matrix tiled periodically, |f| the toroidal frequency in cycles per
tile. Power near DC is what the eye sees as texture rather than grain, so
less of it is better. Balanced rows and columns already zero the pure row
and column stripe bins. Ties are broken by preferring a ROT that is a
permutation (every column gets a different shift, so no two columns keep
the same vertical phase), then lexicographically.

For BAYER8 this criterion picks 0 6 2 4 5 3 7 1, the rotation gm-dl3e proved
on Sundial (2% more weighted power than plain Bayer, no stripe component).

Usage:
    bayer_balance.py search             # candidates for both tables, and the C arrays
    bayer_balance.py profile DIR [DIR2] # row/column luma profiles of the goldens in
                                        # DIR (and DIR2, side by side), per animation
"""
import itertools
import os
import sys
from collections import defaultdict

import numpy as np

BAYER4 = np.array([0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]).reshape(4, 4)
BAYER8 = np.array([
    0, 32, 8, 40, 2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4, 36, 14, 46, 6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
    3, 35, 11, 43, 1, 33, 9, 41, 51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7, 39, 13, 45, 5, 37, 63, 31, 55, 23, 61, 29, 53, 21,
]).reshape(8, 8)


def rotate(b, rot):
    n = b.shape[0]
    return np.array([[b[y ^ rot[x], x] for x in range(n)] for y in range(n)])


def balanced_rotations(b):
    """Every ROT with ROT[0] = 0 whose rotated matrix has equal row sums.

    Meet in the middle: the row-sum vector is the sum of one rotated column
    per x, so split the columns in two halves and match the halves' sums.
    """
    n = b.shape[0]
    target = b.sum() // n
    cols = [np.array([[b[y ^ r, x] for y in range(n)] for r in range(n)]) for x in range(n)]
    h = n // 2
    left = defaultdict(list)
    for rs in itertools.product(range(n), repeat=h):
        if rs[0] != 0:
            continue
        s = sum(cols[x][rs[x]] for x in range(h))
        left[tuple(s)].append(rs)
    out = []
    for rs in itertools.product(range(n), repeat=n - h):
        s = sum(cols[h + i][rs[i]] for i in range(n - h))
        for lft in left.get(tuple(target - s), ()):
            out.append(lft + rs)
    return out


def lowfreq_power(m):
    n = m.shape[0]
    f = np.abs(np.fft.fft2(m - m.mean())) ** 2
    k = np.fft.fftfreq(n) * n
    u, v = np.meshgrid(k, k, indexing="ij")
    r2 = u * u + v * v
    nz = r2 > 0
    return float((f[nz] / r2[nz]).sum())


def rank_key(b, rot):
    return (round(lowfreq_power(rotate(b, rot)), 9), len(set(rot)) != len(rot), rot)


def c_array(name, m):
    vals = ", ".join(str(int(v)) for v in m.reshape(-1))
    return f"const uint8_t {name}[{m.size}] = {{{vals}}};"


def search():
    for name, b in (("BAYER4", BAYER4), ("BAYER8", BAYER8)):
        n = b.shape[0]
        ideal = b.sum() // n
        rots = balanced_rotations(b)
        rots.sort(key=lambda r: rank_key(b, r))
        base = lowfreq_power(b)
        print(f"{name}: plain Bayer row sums {list(b.sum(axis=1))}, column sums {list(b.sum(axis=0))}, ideal {ideal}")
        print(f"  {len(rots)} rotations with ROT[0]=0 balance both axes; best ten by low-frequency power")
        print("  (ratio to plain Bayer; perm = every column shifted differently):")
        for r in rots[:10]:
            p = lowfreq_power(rotate(b, r))
            perm = "perm" if len(set(r)) == n else "    "
            print(f"    ROT {' '.join(map(str, r))}  {p / base:.4f}  {perm}")
        best = rots[0]
        m = rotate(b, best)
        assert all(m.sum(axis=0) == ideal) and all(m.sum(axis=1) == ideal)
        assert sorted(m.reshape(-1)) == list(range(n * n))
        print(f"  chosen ROT {' '.join(map(str, best))}: rows {list(m.sum(axis=1))}, columns {list(m.sum(axis=0))}")
        print("  " + c_array(name, m))
        print()


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = []
    i = 0
    while len(parts) < 4:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1
    w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(data[i:i + w * h * 3], dtype=np.uint8).reshape(h, w, 3).astype(np.float64)


def profiles(directory, anim):
    rows, cols = [], []
    for t in ("030", "120", "210"):
        img = read_ppm(os.path.join(directory, f"{anim}-{t}.ppm"))
        luma = 0.299 * img[..., 0] + 0.587 * img[..., 1] + 0.114 * img[..., 2]
        rows.append(luma.mean(axis=1))
        cols.append(luma.mean(axis=0))
    return np.mean(rows, axis=0), np.mean(cols, axis=0)


def stats(p):
    """Mean and peak row-to-row step, and the amplitude of the profile's
    components at the dither periods (8, 4 and 2 rows): banding from the
    table shows up there and nowhere else."""
    steps = np.abs(np.diff(p))
    spec = np.abs(np.fft.rfft(p - p.mean())) * 2 / len(p)
    n = len(p)
    dith = float(np.sqrt(sum(spec[n // per] ** 2 for per in (8, 4, 2))))
    return steps.mean(), steps.max(), dith


def profile(dirs):
    anims = sorted({f.rsplit("-", 1)[0] for f in os.listdir(dirs[0]) if f.endswith(".ppm")})
    hdr = "anim        " + "  ".join(
        f"{'[' + os.path.basename(os.path.normpath(d)) + ']':>44}" for d in dirs)
    print(hdr)
    print("            " + "  ".join(
        f"{'row step':>9} {'peak':>5} {'col step':>9} {'peak':>5} {'row/col dither':>13}" for _ in dirs))
    for a in anims:
        cells = []
        for d in dirs:
            r, c = profiles(d, a)
            rs, rp, rd = stats(r)
            cs, cp, cd = stats(c)
            ratio = rd / cd if cd > 1e-9 else float("inf")
            cells.append(f"{rs:9.3f} {rp:5.2f} {cs:9.3f} {cp:5.2f} {ratio:13.2f}")
        print(f"{a:<12}" + "  ".join(cells))
    print()
    print("row step / col step: mean |difference| between neighbouring row (column) means")
    print("of luma (0-255), averaged over the three golden frames; peak is the largest.")
    print("row/col dither: amplitude of the row profile at periods 8, 4 and 2 over the")
    print("column profile's. Near 1 means rows and columns carry the same dither texture.")


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "search":
        search()
    elif len(sys.argv) >= 3 and sys.argv[1] == "profile":
        profile(sys.argv[2:])
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
