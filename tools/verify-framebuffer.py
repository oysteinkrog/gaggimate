"""Byte-compare the device framebuffer against the pattern the host recomputes.

Same arithmetic as benchPatternPx() in SleepAnimation.cpp. Any misplaced band,
dropped descriptor, wrong destination offset or flipped bit shows up as an exact
mismatch count and coordinate.
"""
import subprocess
import sys

W = H = 480
M32 = 0xFFFFFFFF


def expected_row(y):
    out = bytearray(W * 2)
    for x in range(W):
        v = (x * 2654435761 + y * 40503) & M32
        px = ((v >> 11) ^ (v >> 27)) & 0xFFFF
        out[x * 2] = px & 0xFF
        out[x * 2 + 1] = px >> 8
    return bytes(out)


def grab(url='http://192.168.1.121/api/fbdump'):
    r = subprocess.run(['curl', '-s', '-m', '25', url], capture_output=True)
    return r.stdout


def check(d, label):
    if len(d) != W * H * 2:
        print(f'{label}: BAD LENGTH {len(d)} (expected {W*H*2})')
        return False
    bad = 0
    first = None
    bad_rows = []
    for y in range(H):
        exp = expected_row(y)
        act = d[y * W * 2:(y + 1) * W * 2]
        if act == exp:
            continue
        rowbad = sum(1 for i in range(0, W * 2, 2) if act[i:i + 2] != exp[i:i + 2])
        bad += rowbad
        bad_rows.append(y)
        if first is None:
            for x in range(W):
                if act[x * 2:x * 2 + 2] != exp[x * 2:x * 2 + 2]:
                    first = (x, y, act[x * 2] | (act[x * 2 + 1] << 8),
                             exp[x * 2] | (exp[x * 2 + 1] << 8))
                    break
    total = W * H
    if bad == 0:
        print(f'{label}: EXACT MATCH  {total} of {total} pixels')
        return True
    print(f'{label}: {bad} of {total} pixels differ ({100.0*bad/total:.4f}%), '
          f'{len(bad_rows)} rows affected')
    print(f'  first mismatch at x={first[0]} y={first[1]} got=0x{first[2]:04X} want=0x{first[3]:04X}')
    print(f'  affected rows: {bad_rows[:12]}{"..." if len(bad_rows) > 12 else ""}')
    return False


if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    dumps = []
    ok = True
    for i in range(n):
        d = grab()
        dumps.append(d)
        ok &= check(d, f'dump {i+1}')
    for i in range(1, len(dumps)):
        same = dumps[i] == dumps[0]
        print(f'dump {i+1} vs dump 1: {"identical" if same else "DIFFERS"}')
        ok &= same
    print('RESULT:', 'PASS' if ok else 'FAIL')
