import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
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
  sampleGradient,
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
// Whatever is selected is mirrored to the panel over the web socket while the
// editor is mounted (req:bganim:preview), so the device shows the animation
// being configured with the gradient being edited before anything is saved.
// The firmware holds a preview for 15 s per message; the editor re-sends on
// every change and every 5 s, and ends the preview when it unmounts.

const PREVIEW_DEBOUNCE_MS = 80;
const PREVIEW_KEEPALIVE_MS = 5000;

function clamp(v, lo, hi) {
  return Math.min(hi, Math.max(lo, v));
}

function pinEnds(stops) {
  const next = stops.map(s => ({ ...s }));
  next[0].pos = 0;
  next[next.length - 1].pos = 255;
  for (let i = 1; i < next.length; i++) {
    if (next[i].pos < next[i - 1].pos) next[i].pos = next[i - 1].pos;
  }
  return next;
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

  const [selected, setSelected] = useState(0);
  const selectedIdx = clamp(selected, 0, stops.length - 1);
  const selectedStop = stops[selectedIdx];

  const writeLibrary = next => setField('bgAnimGradients', serializeGradientLibrary(next));
  const writeRefs = next => setField('bgAnimThemeMap', serializeThemeMap(next));

  const assign = nextRef => {
    const next = refs.slice();
    next[animIdx] = nextRef;
    writeRefs(next);
    setSelected(0);
  };

  const assignAll = () => writeRefs(refs.map(() => ref));

  const updateStops = nextStops => {
    if (!current.editable) return;
    writeLibrary(library.map(g => (g.id === current.id ? { ...g, stops: pinEnds(nextStops) } : g)));
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
    setSelected(0);
  };

  const setStop = (idx, patch) => {
    const next = stops.map((s, i) => (i === idx ? { ...s, ...patch } : s));
    if (patch.pos !== undefined && idx > 0 && idx < stops.length - 1) {
      next[idx].pos = clamp(patch.pos, stops[idx - 1].pos, stops[idx + 1].pos);
    }
    updateStops(next);
  };

  const addStopAt = pos => {
    if (stops.length >= BG_THEME_MAX_STOPS) return;
    pos = clamp(Math.round(pos), 1, 254);
    const color = rgbToHex(sampleGradient(stops, pos));
    let idx = 1;
    while (idx < stops.length - 1 && stops[idx].pos <= pos) idx++;
    const next = stops.slice();
    next.splice(idx, 0, { color, pos });
    updateStops(next);
    setSelected(idx);
  };

  const removeStop = idx => {
    if (stops.length <= 2) return;
    updateStops(stops.filter((_, i) => i !== idx));
    setSelected(Math.max(0, idx - 1));
  };

  const reverse = () => {
    updateStops(
      stops
        .slice()
        .reverse()
        .map(s => ({ ...s, pos: 255 - s.pos })),
    );
    setSelected(stops.length - 1 - selectedIdx);
  };

  const distribute = () => {
    const pos = uniformPositions(stops.length);
    updateStops(stops.map((s, i) => ({ ...s, pos: pos[i] })));
  };

  // ---- drag handling on the gradient bar ---------------------------------
  const barRef = useRef(null);
  const dragRef = useRef(null); // { idx, moved }
  const dragEndedAtRef = useRef(0); // a release after a drag must not read as a bar click

  const posFromEvent = e => {
    const rect = barRef.current.getBoundingClientRect();
    return ((e.clientX - rect.left) / rect.width) * 255;
  };

  const onHandlePointerDown = (idx, e) => {
    e.preventDefault();
    e.stopPropagation();
    setSelected(idx);
    if (idx === 0 || idx === stops.length - 1) return; // ends are pinned
    dragRef.current = { idx, moved: false };
    e.currentTarget.setPointerCapture(e.pointerId);
  };

  const onHandlePointerMove = e => {
    const drag = dragRef.current;
    if (!drag) return;
    drag.moved = true;
    setStop(drag.idx, { pos: Math.round(posFromEvent(e)) });
  };

  const onHandlePointerUp = e => {
    if (!dragRef.current) return;
    if (dragRef.current.moved) dragEndedAtRef.current = Date.now();
    dragRef.current = null;
    e.currentTarget.releasePointerCapture(e.pointerId);
  };

  const onBarClick = e => {
    if (!current.editable) return;
    if (Date.now() - dragEndedAtRef.current < 300) return;
    addStopAt(posFromEvent(e));
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

  const sendPreview = useCallback(() => {
    const { apiService: api, animIdx: a, serialized: s } = latestRef.current;
    try {
      api?.send({ tp: 'req:bganim:preview', anim: a, stops: s });
    } catch {
      // Socket not connected; the next keepalive tries again.
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

      {/* Editing bar: raw colours, handles underneath. */}
      <div className='mt-4 pb-5'>
        <div
          ref={barRef}
          className={`relative h-10 w-full rounded-md border border-black/20 ${
            current.editable ? 'cursor-copy' : ''
          }`}
          style={{ background: gradientCss(stops), touchAction: 'none' }}
          onClick={onBarClick}
          role={current.editable ? 'button' : undefined}
          aria-label={current.editable ? 'Click to add a colour stop' : 'Gradient'}
        >
          {stops.map((s, i) => {
            const pinned = i === 0 || i === stops.length - 1;
            const isSel = i === selectedIdx;
            return (
              <button
                key={i}
                type='button'
                className={`absolute top-full z-10 h-5 w-5 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 shadow ${
                  isSel ? 'border-primary ring-primary/40 ring-2' : 'border-white'
                } ${pinned || !current.editable ? 'cursor-default' : 'cursor-ew-resize'}`}
                style={{ left: `${(s.pos * 100) / 255}%`, background: s.color }}
                aria-label={`Stop ${i + 1}, ${s.color} at ${Math.round((s.pos * 100) / 255)}%`}
                onPointerDown={e => (current.editable ? onHandlePointerDown(i, e) : setSelected(i))}
                onPointerMove={onHandlePointerMove}
                onPointerUp={onHandlePointerUp}
                onPointerCancel={onHandlePointerUp}
                onClick={e => e.stopPropagation()}
              />
            );
          })}
        </div>
      </div>

      {current.editable ? (
        <div className='mt-2 flex flex-wrap items-center gap-2'>
          <input
            type='color'
            className='h-9 w-11 cursor-pointer rounded border-0 bg-transparent p-0'
            value={selectedStop.color}
            aria-label='Selected stop colour'
            onInput={e => setStop(selectedIdx, { color: e.target.value })}
          />
          <input
            type='text'
            className='input input-bordered input-sm w-24 font-mono'
            value={selectedStop.color}
            aria-label='Selected stop colour as hex'
            onChange={e => {
              const v = e.target.value.trim();
              if (/^#?[0-9a-fA-F]{6}$/.test(v)) {
                setStop(selectedIdx, { color: `#${v.replace(/^#/, '').toLowerCase()}` });
              }
            }}
          />
          <label className='flex items-center gap-1 text-sm'>
            <span>Position</span>
            <input
              type='number'
              className='input input-bordered input-sm w-20'
              min={0}
              max={100}
              step={1}
              disabled={selectedIdx === 0 || selectedIdx === stops.length - 1}
              value={Math.round((selectedStop.pos * 100) / 255)}
              onChange={e => {
                const pct = clamp(parseInt(e.target.value, 10) || 0, 0, 100);
                setStop(selectedIdx, { pos: Math.round((pct * 255) / 100) });
              }}
            />
            <span>%</span>
          </label>
          <button
            type='button'
            className='btn btn-sm'
            disabled={stops.length <= 2}
            onClick={() => removeStop(selectedIdx)}
          >
            Remove stop
          </button>
          <span className='text-base-content/60 text-sm'>
            {stops.length}/{BG_THEME_MAX_STOPS} stops, click the bar to add one
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
      ) : (
        <p className='text-base-content/60 mt-2 text-sm'>
          Built-in gradients cannot be changed. Copy one to your gradients to edit it.
        </p>
      )}

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
