# How the background animations are measured

Three node scripts here, plus the page itself. None of them needs a build and
none of them needs the device.

| script | question it answers |
|---|---|
| `motion.js` | How much does this animation move at a given Speed setting? |
| `grain_ratio.js` | When it moves, is it the picture moving or the speckle? |
| `page_vs_golden.js` | Does the firmware draw what the page designed? |

`motion_lib.js` holds the shared page loader and the pixel metrics. It is the
same loader `page_render.js` uses, cut down to what the measurements need.

## motion.js

```
node motion.js anim_bench.html out.json [--ids 1,2] [--speeds 0,50,100]
```

Movement is the **half change time**: how long the picture takes to change
half as much as two unrelated moments of the same animation differ. The target
for the fleet is 1200 ms at Speed 50 and the accept band is 950 to 1500.

Two properties of the harness matter and are not optional.

It plays each animation back continuously at 66 ms steps, which is one frame
at the 15 Hz a pixel row gets, rather than rendering at arbitrary times.
Several entries are stateful: Ripples spawns a drop when the clock passes a
stored deadline and drops ripples older than their lifetime, so a single render
at 41 s shows still water forever after. Starfield keeps a shooting star on a
deadline the same way.

The unrelated level is the mean of the top third of separations past 7.9 s, not
one long separation. Ripples and Steam are near periodic at about 7 s and land
back on themselves at some separations.

For sparse or grainy animations, read `thalfLpMs` rather than `thalfMs`: it
low passes with an 8x8 box average before differencing, so single pixel content
and near Nyquist grain do not inflate the score. Starfield, Orbits,
Harmonograph, Nebula and Floor need it.

## grain_ratio.js

```
node grain_ratio.js anim_bench.html [ids]
```

Splits each frame into its 8x8 box average and the residual, and reports how
fast the fine detail changes against how fast the picture does.

**Read the delta across a change, never the level.** Thirty of the 44 sit at or
above 1 today and none of the top ones is broken; the ratio measures sparseness
as well as fizz. What it catches that `motion.js` cannot is whole pixel stutter:
Truchet's ratio rose 0.111 when the calibration slowed it into the state where
28 percent of its 66 ms refreshes repeat the previous picture exactly, while its
half change time sat comfortably on target throughout. Flag a rise above about
0.15 on an animation whose rate went up.

## page_vs_golden.js

```
node page_vs_golden.js [--ids 1,2] [--json out.json]
```

Also `make pagecheck`, and it runs as the third stage of `make check`.

The goldens are rendered by the firmware, so `make check` can only prove the
firmware draws what it drew yesterday. This is the only check that notices the
port drifting away from the page entry, which is the approved design. It plays
the page the way `bench.cpp` plays the firmware, captures golden frames 30, 120
and 210, quantises both sides to RGB565 and counts differing pixels.

Per animation allowances live in `../page_exact.json`, default zero. Lower one
when you fix an animation. Never raise one to make a check pass.

## Headless Chrome screenshot width

Chrome on Windows clamps a headless window to about 504 CSS pixels, so a
screenshot asked for at 390 wide is a crop of a 504 wide render and looks like
horizontal overflow that is not there. Render inside a 390 pixel iframe for a
true phone viewport, and confirm layout with a DOM probe comparing `scrollWidth`
against `clientWidth` rather than by looking at the image.
