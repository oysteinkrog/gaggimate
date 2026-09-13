import { useCallback, useContext, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { GradientPicker } from 'react-linear-gradient-picker';
import { HexColorInput, HexColorPicker } from 'react-colorful';
import 'react-linear-gradient-picker/dist/index.css';
import './GradientEditor.css';
import { ApiServiceContext } from '../services/ApiService.js';
import { GradientBrowser } from './GradientBrowser.jsx';
import {
  BG_ANIMATIONS,
  BG_GRADIENT_ID_MAX,
  BG_GRADIENT_LIB_MAX,
  BG_GRADIENT_NAME_MAX,
  BG_LEGACY_CUSTOM_REF,
  BG_THEME_MAX_STOPS,
  builtinGradient,
  builtinThemeGroups,
  globalAssignFields,
  globalGradientRef,
  gradientCss,
  gradientForRef,
  isUniformStops,
  nextGradientId,
  parseGradientLibrary,
  parseThemeMap,
  refResolves,
  rgbToHex,
  sanitizeGradientName,
  serializeGradient,
  serializeGradientLibrary,
  serializeThemeMap,
  uniformPositions,
  wheelCss,
} from '../config/bgAnimations.js';

// Gradient picker plus an editor for the user's own gradients, in one of two
// scopes.
//
// scope={kind:'global'} edits bgAnimGradientRef, the gradient every animation
// draws with unless it has one of its own. That is the simple path: choose
// once, including one of your own gradients, and the whole fleet follows.
//
// scope={kind:'anim', animIdx} edits one slot of bgAnimThemeMap, an override
// for that animation alone. Its first choice is "Global (<name>)", which is
// what an untouched animation stores; on that choice the editor collapses to
// the name and one swatch, so an animation using the default does not look
// like a second gradient setting sitting under the first.
//
// The library itself (bgAnimGradients) is shared by both scopes and is edited
// wherever it is selected. Built-in themes cannot be edited in place; "Copy to
// my gradients" clones one into the library.
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

const previewOwner = { id: 'global', subs: new Set() };

function claimPreview(id) {
  if (previewOwner.id === id) return;
  previewOwner.id = id;
  previewOwner.subs.forEach(f => f());
}

function usePreviewOwner(id) {
  const [, bump] = useState(0);
  useEffect(() => {
    const f = () => bump(n => n + 1);
    previewOwner.subs.add(f);
    return () => {
      previewOwner.subs.delete(f);
    };
  }, []);
  return previewOwner.id === id;
}

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

export function GradientEditor({ scope, formData, setField, previewAnimIdx }) {
  const apiService = useContext(ApiServiceContext);
  const isGlobal = scope.kind === 'global';
  const animIdx = isGlobal ? null : scope.animIdx;
  const editorId = isGlobal ? 'global' : `anim-${scope.animIdx}`;
  const owns = usePreviewOwner(editorId);
  const library = useMemo(
    () => parseGradientLibrary(formData.bgAnimGradients),
    [formData.bgAnimGradients],
  );
  const refs = useMemo(() => parseThemeMap(formData.bgAnimThemeMap), [formData.bgAnimThemeMap]);
  // Always concrete, never '': what an animation with no override draws with.
  // It can be BG_LEGACY_CUSTOM_REF, the read-only stand-in for a pre-library
  // custom gradient the firmware's migration had to leave where it is.
  const globalRef = globalGradientRef(
    formData.bgAnimGradientRef,
    library,
    formData.bgAnimTheme,
    formData.bgAnimCustomTheme,
  );
  const customTheme = formData.bgAnimCustomTheme;
  const globalName = gradientForRef(globalRef, library, customTheme).name;
  const globalIsLegacy = globalRef === BG_LEGACY_CUSTOM_REF;
  // What the picker shows. In animation scope '' means "same as global", and
  // an override naming a deleted library entry reads as '' too, because that
  // is what the firmware draws.
  const rawRef = isGlobal ? globalRef : (refs[animIdx] ?? '');
  const ref = isGlobal || refResolves(rawRef, library) ? rawRef : '';
  // What is edited and previewed; '' resolves to the global.
  const editRef = ref === '' ? globalRef : ref;
  const current = gradientForRef(editRef, library, formData.bgAnimCustomTheme);
  const stops = current.stops;
  // An animation showing "Same as global" is looking at the global's gradient,
  // so editing the stops here would change every animation, which is not what
  // the reader of this panel is asking for. Read-only until they pick a
  // gradient of their own, or copy this one into the library.
  const editable = current.editable && (isGlobal || ref !== '');
  // The animation the panel previews while this editor is open. In global
  // scope that is whatever is playing, so the change can be seen.
  const previewIdx = previewAnimIdx ?? animIdx ?? 0;
  const anim = BG_ANIMATIONS[previewIdx];
  const wraps = anim?.id === 'plasma';
  const overrideCount = refs.filter(r => r !== '' && refResolves(r, library)).length;
  // The id a copy would take: the lowest one no entry carries and no stored ref
  // names, including a ref left dangling by a delete. Reserving the dangling
  // ones is what keeps reuse safe; handing out an id one of them still names
  // would make that ref resolve again, pointing a saved selection at a
  // gradient the user never chose there.
  //
  // The raw stored strings go in, not the parsed arrays, because parsing drops
  // exactly the refs that matter: a ref that no longer resolves, and a slot
  // past the end of this build's animation list.
  const freeId = useMemo(
    () => nextGradientId(library, [formData.bgAnimThemeMap, formData.bgAnimGradientRef]),
    [library, formData.bgAnimThemeMap, formData.bgAnimGradientRef],
  );
  const libraryFull = library.length >= BG_GRADIENT_LIB_MAX;
  // Out of ids is not reachable from a device: at most BG_GRADIENT_LIB_MAX
  // entries and one ref per animation reserve a few dozen ids out of 99999, so
  // the scan always finds one low down. It is handled anyway, because the
  // alternative is allocating an id the firmware's five-digit parser cannot
  // read, which it drops in silence.
  const canCopy = !libraryFull && freeId !== null;
  const copyBlockedReason = libraryFull
    ? `Up to ${BG_GRADIENT_LIB_MAX} gradients can be saved`
    : freeId === null
      ? `Every number up to ${BG_GRADIENT_ID_MAX} is already taken by a saved gradient, or is still named by a selection that points at a deleted one`
      : undefined;
  // Clearing every override is not undoable in one step, so it asks first.
  const [confirmClear, setConfirmClear] = useState(false);
  const tone = {
    brightness:
      formData.bgAnimBrightness === undefined ? 100 : parseInt(formData.bgAnimBrightness, 10),
    knee:
      formData.bgAnimHighlightKnee === undefined ? 100 : parseInt(formData.bgAnimHighlightKnee, 10),
  };

  const writeLibrary = next => setField('bgAnimGradients', serializeGradientLibrary(next));
  const writeRefs = next => setField('bgAnimThemeMap', serializeThemeMap(next));

  const takePreview = () => claimPreview(editorId);

  const assign = nextRef => {
    takePreview();
    if (isGlobal) {
      // globalAssignFields carries the rollback mirror: a built-in is written
      // into bgAnimTheme, which is the last fallback and what a build without
      // bgAnimGradientRef reads, while a library selection and the read-only
      // legacy stand-in leave it alone. The firmware's POST handler and both
      // of the display's writers apply the same policy, so a later library
      // selection cannot leave an appended index behind in the legacy field.
      for (const [key, value] of Object.entries(globalAssignFields(nextRef))) {
        setField(key, value);
      }
      return;
    }
    const next = refs.slice();
    next[animIdx] = nextRef;
    writeRefs(next);
  };

  // Clears every per-animation override so the global applies everywhere. This
  // replaces the old "Use for all animations", which wrote the same ref into
  // every slot and left the global with no effect at all.
  const clearOverrides = () => {
    writeRefs(refs.map(() => ''));
    setConfirmClear(false);
  };

  const updateStops = nextStops => {
    if (!editable) return;
    const sorted = nextStops.slice().sort((a, b) => a.pos - b.pos);
    writeLibrary(library.map(g => (g.id === current.id ? { ...g, stops: sorted } : g)));
  };

  const rename = name => {
    if (!editable) return;
    writeLibrary(library.map(g => (g.id === current.id ? { ...g, name } : g)));
  };

  const copyToLibrary = () => {
    if (!canCopy) return;
    const id = freeId;
    const base = editable ? `${current.name} copy` : current.name;
    const name = sanitizeGradientName(base);
    writeLibrary([...library, { id, name, stops: stops.map(s => ({ ...s })) }]);
    assign(`c${id}`);
  };

  const removeFromLibrary = () => {
    if (!editable) return;
    const gone = `c${current.id}`;
    writeLibrary(library.filter(g => g.id !== current.id));
    // Animations that used it fall back to the global, and the global to the
    // built-in theme, which is what the firmware does with a dangling
    // reference anyway. Clearing both here keeps the form showing what the
    // panel will draw.
    writeRefs(refs.map(r => (r === gone ? '' : r)));
    if (formData.bgAnimGradientRef === gone) setField('bgAnimGradientRef', '');
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
  const latestRef = useRef({ apiService, previewIdx, serialized });
  latestRef.current = { apiService, previewIdx, serialized };

  // A send that finds the socket closed (it reconnects on its own) is retried
  // shortly rather than left to the 5 s keepalive: the firmware keeps showing
  // the last preview it received for 15 s, so a lost message would leave the
  // panel on a stale gradient for that long.
  const retryRef = useRef(null);
  const sendPreview = useCallback(() => {
    const { apiService: api, previewIdx: a, serialized: s } = latestRef.current;
    clearTimeout(retryRef.current);
    retryRef.current = null;
    try {
      api?.send({ tp: 'req:bganim:preview', anim: a, stops: s });
    } catch {
      retryRef.current = setTimeout(sendPreview, PREVIEW_RETRY_MS);
    }
  }, []);

  useEffect(() => {
    if (!owns) return undefined;
    const t = setTimeout(sendPreview, PREVIEW_DEBOUNCE_MS);
    return () => clearTimeout(t);
  }, [owns, previewIdx, serialized, sendPreview]);

  useEffect(() => {
    if (!owns) return undefined;
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
  }, [owns, sendPreview]);

  // The built-ins, grouped for the picker, in the declared category order. The
  // browse dialog below is handed the same groups, so the two controls cannot
  // drift apart.
  const builtinGroups = useMemo(() => builtinThemeGroups(), []);

  const selectId = isGlobal ? 'bgAnimGradientRef' : `bgAnimGradientRef-${animIdx}`;

  // ---- the browse dialog --------------------------------------------------
  // Same choices as the select, as swatches. It never writes anything itself:
  // a chosen ref goes through assign(), the select's own path.
  const [browseOpen, setBrowseOpen] = useState(false);
  const browseBtnRef = useRef(null);
  const closeBrowse = useCallback(() => {
    setBrowseOpen(false);
    // Focus goes back where it came from, which is what a keyboard user needs
    // whether the dialog was cancelled or a gradient was chosen.
    browseBtnRef.current?.focus();
  }, []);
  const chooseFromBrowse = nextRef => {
    assign(nextRef);
    closeBrowse();
  };
  const browseGroups = useMemo(() => {
    const out = [];
    // An animation can be put back on the global gradient from here too, the
    // same '' the select's first option writes.
    if (!isGlobal) {
      const g = gradientForRef(globalRef, library, customTheme);
      out.push({
        label: 'Global',
        items: [{ ref: '', name: `Global (${g.name})`, stops: g.stops }],
      });
    }
    if (library.length > 0) {
      out.push({
        label: 'My gradients',
        items: library.map(g => ({ ref: `c${g.id}`, name: g.name, stops: g.stops })),
      });
    }
    for (const group of builtinGroups) {
      out.push({
        label: group.category,
        items: group.items.map(t => ({
          ref: String(t.index),
          name: t.name,
          stops: builtinGradient(t.index).stops,
        })),
      });
    }
    return out;
  }, [isGlobal, globalRef, customTheme, library, builtinGroups]);

  const browseButton = (
    <button
      type='button'
      ref={browseBtnRef}
      className='btn btn-sm'
      aria-haspopup='dialog'
      onClick={() => setBrowseOpen(true)}
    >
      Browse
    </button>
  );

  // Mounted whether it is open or closed (it renders nothing when closed), so
  // it stays inside the editor's preview-ownership capture handlers: opening it
  // claims the preview for this editor exactly as clicking the select does, and
  // closing it neither takes nor drops ownership.
  const browser = (
    <GradientBrowser
      isOpen={browseOpen}
      titleId={`gradient-browse-title-${editorId}`}
      title={
        isGlobal
          ? 'Choose a gradient for all animations'
          : `Choose a gradient for ${BG_ANIMATIONS[animIdx]?.name ?? 'this animation'}`
      }
      notice={
        isGlobal && globalIsLegacy
          ? `The gradient in use is ${globalName}, saved before gradients had names, so nothing below is marked as current. Choosing one replaces it.`
          : undefined
      }
      groups={browseGroups}
      currentRef={ref}
      onChoose={chooseFromBrowse}
      onClose={closeBrowse}
    />
  );

  if (!isGlobal && ref === '') {
    return (
      <div
        className='border-base-content/10 rounded-lg border p-3'
        onPointerDownCapture={takePreview}
        onFocusCapture={takePreview}
      >
        <label htmlFor={selectId} className='mb-1 block text-sm font-medium'>
          Gradient for {BG_ANIMATIONS[animIdx]?.name ?? 'this animation'}
        </label>
        <div className='flex flex-wrap items-center gap-3'>
          <select
            id={selectId}
            className='select select-bordered w-full sm:w-72'
            value=''
            onChange={e => assign(e.target.value)}
          >
            <option value=''>Global ({globalName})</option>
            {library.length > 0 && (
              <optgroup label='My gradients'>
                {library.map(g => (
                  <option key={g.id} value={`c${g.id}`}>
                    {g.name}
                  </option>
                ))}
              </optgroup>
            )}
            {builtinGroups.map(group => (
              <optgroup key={group.category} label={group.category}>
                {group.items.map(t => (
                  <option key={t.name} value={String(t.index)}>
                    {t.name}
                  </option>
                ))}
              </optgroup>
            ))}
          </select>
          {browseButton}
          <div
            className='h-8 min-w-40 flex-1 rounded-md border border-black/20'
            style={{ background: gradientCss(stops) }}
            aria-label={`Global gradient, ${globalName}`}
          />
        </div>
        <p className='text-base-content/60 mt-2 text-sm'>
          This animation uses the global gradient set above. Pick another here to give it one of its
          own.
        </p>
        {browser}
      </div>
    );
  }

  return (
    <div
      className='border-base-content/10 rounded-lg border p-3'
      onPointerDownCapture={takePreview}
      onFocusCapture={takePreview}
    >
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        <div className='form-control'>
          <label htmlFor={selectId} className='mb-1 block text-sm font-medium'>
            {isGlobal
              ? 'Gradient for all animations'
              : `Gradient for ${BG_ANIMATIONS[animIdx]?.name ?? 'this animation'}`}
          </label>
          <div className='flex items-center gap-2'>
            <select
              id={selectId}
              className='select select-bordered w-full'
              value={ref}
              onChange={e => assign(e.target.value)}
            >
              {!isGlobal && <option value=''>Global ({globalName})</option>}
              {/* The pre-library custom gradient, while the firmware has had to
                leave it where it is. Shown so the current value reads
                truthfully; it is not a choice, and picking anything else
                replaces it. */}
              {isGlobal && globalIsLegacy && (
                <option value={BG_LEGACY_CUSTOM_REF} disabled>
                  {globalName}
                </option>
              )}
              {library.length > 0 && (
                <optgroup label='My gradients'>
                  {library.map(g => (
                    <option key={g.id} value={`c${g.id}`}>
                      {g.name}
                    </option>
                  ))}
                </optgroup>
              )}
              {builtinGroups.map(group => (
                <optgroup key={group.category} label={group.category}>
                  {group.items.map(t => (
                    <option key={t.name} value={String(t.index)}>
                      {t.name}
                    </option>
                  ))}
                </optgroup>
              ))}
            </select>
            {browseButton}
          </div>
          {!isGlobal && (
            <p className='text-base-content/60 mt-2 text-sm'>
              {BG_ANIMATIONS[animIdx]?.name ?? 'This animation'} has its own gradient and ignores
              the global one. Choose Global ({globalName}) to put it back.
            </p>
          )}
        </div>
        <div className='flex flex-wrap items-end gap-2'>
          <button
            type='button'
            className='btn btn-sm'
            disabled={!canCopy}
            title={copyBlockedReason}
            onClick={copyToLibrary}
          >
            {editable ? 'Duplicate' : 'Copy to my gradients'}
          </button>
          {freeId === null && !libraryFull && (
            <p className='text-warning w-full text-sm'>
              A copy cannot be saved right now: every gradient number up to {BG_GRADIENT_ID_MAX} is
              taken by a saved gradient, or is still named by a selection pointing at a deleted one.
              Delete a saved gradient, or put the animations that point at a missing one back on the
              global gradient, to free a number.
            </p>
          )}
          {isGlobal && (
            <button
              type='button'
              className={`btn btn-sm sm:ml-auto ${confirmClear ? 'btn-warning' : ''}`}
              disabled={overrideCount === 0}
              title={
                overrideCount === 0
                  ? 'No animation has a gradient of its own'
                  : 'Puts every animation back on the global gradient. Your saved gradients are kept.'
              }
              onClick={() => (confirmClear ? clearOverrides() : setConfirmClear(true))}
              onBlur={() => setConfirmClear(false)}
            >
              {confirmClear
                ? `Reset all ${overrideCount}?`
                : `Reset ${overrideCount} animation${overrideCount === 1 ? '' : 's'} to global`}
            </button>
          )}
          {editable && (
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

      {editable && (
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
        {editable ? (
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
              {ref === '' && current.editable
                ? 'This is the global gradient, shared by every animation. Copy it to your gradients to give this animation one of its own.'
                : 'Built-in gradients cannot be changed. Copy one to your gradients to edit it.'}
            </p>
          </>
        )}
      </div>

      {/* What the panel makes of it: tone applied, and the loop if it loops. */}
      <div className={`mt-4 grid grid-cols-1 gap-3 ${wraps ? 'md:grid-cols-2' : ''}`}>
        <div>
          <div className='mb-1 text-xs opacity-70'>
            On the panel (animation brightness {tone.brightness}%, highlight rolloff {tone.knee}
            %)
          </div>
          <div
            className='h-6 w-full rounded border border-black/20'
            style={{ background: gradientCss(stops, tone) }}
          />
        </div>
        {wraps && (
          <div>
            <div className='mb-1 text-xs opacity-70'>
              {anim?.name ?? 'This animation'} loops the gradient, so its ends meet
            </div>
            <div
              className='h-6 w-full rounded border border-black/20'
              style={{ background: wheelCss(stops, tone) }}
            />
          </div>
        )}
      </div>
      <p className='text-base-content/60 mt-2 text-xs'>
        Stops run dark to bright. The panel shows {anim?.name ?? 'the animation'} with this gradient
        while you edit; save to keep it.
      </p>
      {browser}
    </div>
  );
}
