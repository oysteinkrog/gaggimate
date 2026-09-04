import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { GradientPicker } from 'react-linear-gradient-picker';
import { HexColorInput, HexColorPicker } from 'react-colorful';
import 'react-linear-gradient-picker/dist/index.css';
import './GradientEditor.css';
import { ApiServiceContext } from '../services/ApiService.js';
import {
  BG_ANIMATIONS,
  BG_GRADIENT_LIB_MAX,
  BG_GRADIENT_NAME_MAX,
  BG_THEMES,
  BG_THEME_MAX_STOPS,
  effectiveRef,
  gradientCss,
  gradientForRef,
  isUniformStops,
  nextGradientId,
  parseGradientLibrary,
  parseThemeMap,
  rgbToHex,
  sanitizeGradientName,
  serializeGradient,
  serializeGradientLibrary,
  serializeThemeMap,
  uniformPositions,
  wheelCss,
} from '../config/bgAnimations.js';

// Per-animation gradient picker plus an editor for the user's own gradients.
//
// Two settings back it, both edited through setField so the ordinary settings
// save writes them: bgAnimThemeMap (which gradient each animation draws
// with) and bgAnimGradients (the named library). Built-in themes cannot be
// edited in place; "Copy to my gradients" clones one into the library.
//
// The stop editing itself is react-linear-gradient-picker (drag to move,
// click the bar to add, drag a stop downwards or double-click it to remove)
// with react-colorful as the colour picker for the active stop. The picker
// works in offsets 0..1 over a pixel width; positions here are 0..255.
//
// Whatever is selected is mirrored to the panel over the web socket while the
// editor is mounted (req:bganim:preview), so the device shows the animation
// being configured with the gradient being edited before anything is saved.
// The firmware holds a preview for 15 s per message; the editor re-sends on
// every change and every 5 s, and ends the preview when it unmounts.

const PREVIEW_DEBOUNCE_MS = 80;
const PREVIEW_KEEPALIVE_MS = 5000;
const PREVIEW_RETRY_MS = 300;
const BAR_HEIGHT = 40;
const MIN_BAR_WIDTH = 240;

function clamp(v, lo, hi) {
  return Math.min(hi, Math.max(lo, v));
}

// The picker hands colours back as it received them (hex), but be safe about
// an rgb()/rgba() string since the wire format only carries hex.
function toHex(color) {
  const c = String(color).trim();
  if (/^#[0-9a-fA-F]{6}$/.test(c)) return c.toLowerCase();
  const m = /^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/.exec(c);
  if (m) return rgbToHex([Number(m[1]), Number(m[2]), Number(m[3])]);
  return '#000000';
}

// What GradientPicker mounts below the bar for the active stop. It hands
// over color/onSelect(color, opacity); opacity has no meaning on the panel.
function StopColorPicker({ color, onSelect }) {
  return (
    <div className='gm-stop-picker'>
      <HexColorPicker color={color} onChange={c => onSelect(c, 1)} />
      <HexColorInput
        className='input input-bordered input-sm w-28 font-mono'
        color={color}
        onChange={c => onSelect(c, 1)}
        prefixed
      />
    </div>
  );
}

export function GradientEditor({ animIdx, formData, setField }) {
  const apiService = useContext(ApiServiceContext);
  const library = useMemo(
    () => parseGradientLibrary(formData.bgAnimGradients),
    [formData.bgAnimGradients],
  );
  const refs = useMemo(() => parseThemeMap(formData.bgAnimThemeMap), [formData.bgAnimThemeMap]);
  const ref = effectiveRef(refs, animIdx, library, formData.bgAnimTheme);
  const current = gradientForRef(ref, library);
  const stops = current.stops;
  const anim = BG_ANIMATIONS[animIdx];
  const wraps = anim?.id === 'plasma';
  const tone = {
    brightness:
      formData.bgAnimBrightness === undefined ? 100 : parseInt(formData.bgAnimBrightness, 10),
    knee:
      formData.bgAnimHighlightKnee === undefined ? 100 : parseInt(formData.bgAnimHighlightKnee, 10),
  };

  const writeLibrary = next => setField('bgAnimGradients', serializeGradientLibrary(next));
  const writeRefs = next => setField('bgAnimThemeMap', serializeThemeMap(next));

  const assign = nextRef => {
    const next = refs.slice();
    next[animIdx] = nextRef;
    writeRefs(next);
  };

  const assignAll = () => writeRefs(refs.map(() => ref));

  const updateStops = nextStops => {
    if (!current.editable) return;
    const sorted = nextStops.slice().sort((a, b) => a.pos - b.pos);
    writeLibrary(library.map(g => (g.id === current.id ? { ...g, stops: sorted } : g)));
  };

  const rename = name => {
    if (!current.editable) return;
    writeLibrary(library.map(g => (g.id === current.id ? { ...g, name } : g)));
  };

  const copyToLibrary = () => {
    if (library.length >= BG_GRADIENT_LIB_MAX) return;
    const id = nextGradientId(library);
    const base = current.editable ? `${current.name} copy` : current.name;
    const name = sanitizeGradientName(base);
    writeLibrary([...library, { id, name, stops: stops.map(s => ({ ...s })) }]);
    assign(`c${id}`);
  };

  const removeFromLibrary = () => {
    if (!current.editable) return;
    writeLibrary(library.filter(g => g.id !== current.id));
    // Animations that used it fall back to the global theme, which is what
    // the firmware does with a dangling reference anyway.
    writeRefs(refs.map(r => (r === ref ? '' : r)));
  };

  const reverse = () => {
    updateStops(stops.map(s => ({ ...s, pos: 255 - s.pos })));
  };

  const distribute = () => {
    const pos = uniformPositions(stops.length);
    updateStops(stops.map((s, i) => ({ ...s, pos: pos[i] })));
  };

  // ---- the picker ----------------------------------------------------------
  // GradientPicker wants its width in pixels; follow the container.
  const holderRef = useRef(null);
  const [barWidth, setBarWidth] = useState(400);
  useEffect(() => {
    const el = holderRef.current;
    if (!el || typeof ResizeObserver === 'undefined') return undefined;
    const ro = new ResizeObserver(entries => {
      const w = Math.floor(entries[0].contentRect.width);
      if (w > 0) setBarWidth(Math.max(MIN_BAR_WIDTH, w));
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const palette = stops.map(s => ({ color: s.color, offset: s.pos / 255 }));
  const [activeIdx, setActiveIdx] = useState(0);
  const activeStop = stops[clamp(activeIdx, 0, stops.length - 1)];

  const onPaletteChange = next => {
    const idx = next.findIndex(p => p.active);
    if (idx >= 0) setActiveIdx(idx);
    updateStops(
      next.map(p => ({
        color: toHex(p.color),
        pos: clamp(Math.round(Number(p.offset) * 255), 0, 255),
      })),
    );
  };

  const setActivePos = pct => {
    const idx = clamp(activeIdx, 0, stops.length - 1);
    const pos = clamp(Math.round((clamp(pct, 0, 100) * 255) / 100), 0, 255);
    updateStops(stops.map((s, i) => (i === idx ? { ...s, pos } : s)));
  };

  // The name field shows what is being typed; the library gets the sanitized
  // form on every keystroke, so trimming there cannot eat a space mid-word.
  const [nameDraft, setNameDraft] = useState(null);

  // ---- live preview on the panel -----------------------------------------
  // The timers read the latest values through a ref so neither effect has to
  // re-arm on every edit; only the debounce keys on the gradient itself.
  const serialized = serializeGradient({ stops });
  const latestRef = useRef({ apiService, animIdx, serialized });
  latestRef.current = { apiService, animIdx, serialized };

  // A send that finds the socket closed (it reconnects on its own) is retried
  // shortly rather than left to the 5 s keepalive: the firmware keeps showing
  // the last preview it received for 15 s, so a lost message would leave the
  // panel on a stale gradient for that long.
  const retryRef = useRef(null);
  const sendPreview = useCallback(() => {
    const { apiService: api, animIdx: a, serialized: s } = latestRef.current;
    clearTimeout(retryRef.current);
    retryRef.current = null;
    try {
      api?.send({ tp: 'req:bganim:preview', anim: a, stops: s });
    } catch {
      retryRef.current = setTimeout(sendPreview, PREVIEW_RETRY_MS);
    }
  }, []);

  useEffect(() => {
    const t = setTimeout(sendPreview, PREVIEW_DEBOUNCE_MS);
    return () => clearTimeout(t);
  }, [animIdx, serialized, sendPreview]);

  useEffect(() => {
    const t = setInterval(sendPreview, PREVIEW_KEEPALIVE_MS);
    return () => {
      clearInterval(t);
      clearTimeout(retryRef.current);
      retryRef.current = null;
      try {
        latestRef.current.apiService?.send({ tp: 'req:bganim:preview-end' });
      } catch {
        // Already disconnected; the firmware lapses the preview on its own.
      }
    };
  }, [sendPreview]);

  const libraryFull = library.length >= BG_GRADIENT_LIB_MAX;

  return (
    <div className='border-base-content/10 rounded-lg border p-3'>
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        <div className='form-control'>
          <label htmlFor='bgAnimGradientRef' className='mb-1 block text-sm font-medium'>
            Gradient for {anim?.name ?? 'this animation'}
          </label>
          <select
            id='bgAnimGradientRef'
            className='select select-bordered w-full'
            value={ref}
            onChange={e => assign(e.target.value)}
          >
            {library.length > 0 && (
              <optgroup label='My gradients'>
                {library.map(g => (
                  <option key={g.id} value={`c${g.id}`}>
                    {g.name}
                  </option>
                ))}
              </optgroup>
            )}
            <optgroup label='Built-in'>
              {BG_THEMES.map((t, i) => (
                <option key={t.name} value={String(i)}>
                  {t.name}
                </option>
              ))}
            </optgroup>
          </select>
        </div>
        <div className='flex flex-wrap items-end gap-2'>
          <button
            type='button'
            className='btn btn-sm'
            disabled={libraryFull}
            title={libraryFull ? `Up to ${BG_GRADIENT_LIB_MAX} gradients can be saved` : undefined}
            onClick={copyToLibrary}
          >
            {current.editable ? 'Duplicate' : 'Copy to my gradients'}
          </button>
          <button type='button' className='btn btn-sm' onClick={assignAll}>
            Use for all animations
          </button>
          {current.editable && (
            <button
              type='button'
              className='btn btn-sm btn-outline btn-error'
              onClick={removeFromLibrary}
            >
              Delete
            </button>
          )}
        </div>
      </div>

      {current.editable && (
        <div className='form-control mt-3'>
          <label htmlFor='bgAnimGradientName' className='mb-1 block text-sm font-medium'>
            Name
          </label>
          <input
            id='bgAnimGradientName'
            type='text'
            className='input input-bordered input-sm w-full md:w-1/2'
            maxLength={BG_GRADIENT_NAME_MAX}
            value={nameDraft ?? current.name}
            onFocus={e => setNameDraft(e.target.value)}
            onInput={e => {
              setNameDraft(e.target.value);
              rename(e.target.value);
            }}
            onBlur={() => setNameDraft(null)}
          />
        </div>
      )}

      <div className='gm-gradient mt-4' ref={holderRef}>
        {current.editable ? (
          <>
            <GradientPicker
              width={barWidth}
              paletteHeight={BAR_HEIGHT}
              palette={palette}
              minStops={2}
              maxStops={BG_THEME_MAX_STOPS}
              stopRemovalDrop={40}
              onPaletteChange={onPaletteChange}
              onColorStopSelect={stop => {
                if (typeof stop.id === 'number') setActiveIdx(stop.id);
              }}
            >
              <StopColorPicker />
            </GradientPicker>
            <div className='mt-2 flex flex-wrap items-center gap-3 text-sm'>
              <label className='flex items-center gap-1'>
                <span>Stop position</span>
                <input
                  type='number'
                  className='input input-bordered input-sm w-20'
                  min={0}
                  max={100}
                  step={1}
                  value={Math.round((activeStop.pos * 100) / 255)}
                  onChange={e => setActivePos(parseInt(e.target.value, 10) || 0)}
                />
                <span>%</span>
              </label>
              <span className='text-base-content/60'>
                {stops.length}/{BG_THEME_MAX_STOPS} stops. Click the bar to add one, drag a stop
                down or double-click it to remove it.
              </span>
              <span className='grow' />
              <button type='button' className='btn btn-sm' onClick={reverse}>
                Reverse
              </button>
              <button
                type='button'
                className='btn btn-sm'
                disabled={isUniformStops(stops)}
                onClick={distribute}
              >
                Space evenly
              </button>
            </div>
          </>
        ) : (
          <>
            <div
              className='w-full rounded-md border border-black/20'
              style={{ height: `${BAR_HEIGHT}px`, background: gradientCss(stops) }}
              aria-label='Gradient'
            />
            <p className='text-base-content/60 mt-2 text-sm'>
              Built-in gradients cannot be changed. Copy one to your gradients to edit it.
            </p>
          </>
        )}
      </div>

      {/* What the panel makes of it: tone applied, and the plasma wrap. */}
      <div className='mt-4 grid grid-cols-1 gap-3 md:grid-cols-2'>
        <div>
          <div className='mb-1 text-xs opacity-70'>
            On the panel (brightness {tone.brightness}%, highlight knee {tone.knee}%)
          </div>
          <div
            className='h-6 w-full rounded border border-black/20'
            style={{ background: gradientCss(stops, tone) }}
          />
        </div>
        <div>
          <div className='mb-1 text-xs opacity-70'>
            {wraps
              ? 'Plasma loops the gradient, so its ends meet'
              : 'Looped, as Plasma would draw it'}
          </div>
          <div
            className='h-6 w-full rounded border border-black/20'
            style={{ background: wheelCss(stops, tone) }}
          />
        </div>
      </div>
      <p className='text-base-content/60 mt-2 text-xs'>
        Stops run dark to bright. The panel shows this animation with the gradient while you edit;
        save to keep it.
      </p>
    </div>
  );
}
