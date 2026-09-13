import { useEffect, useRef } from 'preact/hooks';
import { gradientCss } from '../config/bgAnimations.js';

// The browse dialog beside the gradient picker: every gradient as a swatch,
// grouped the way the picker groups its options, because nobody picks a colour
// from a list of sixty names.
//
// It is a view, not a second source of truth. The <select> still owns the
// value: this dialog hands a ref back through onChoose, and the editor writes
// it through the same assign() the select's onChange calls, so the mirror
// policy and the per-animation map cannot drift between the two controls.
// tools/gradient_browse_check.mjs drives both paths with the same gradient and
// compares what each one writes.
//
// A swatch is drawn with gradientCss(), which is what the editor's own bar
// uses. The firmware-exact sampler in config/gradientRamp.js is the better
// oracle for what the panel stores, and it is deliberately not used here: a
// swatch sits next to the bar that appears once the gradient is chosen, so the
// two must agree with each other above all. The two paths differ by a few
// least significant bits of RGB565 on a handful of ramp entries, which no
// 40 px swatch can show, and drawing one of them differently would be a
// difference the user can see.
//
// Rendered by the editor whether it is open or not (it returns null when
// closed, like VisualizerUploadModal), so the editor's preview-ownership
// capture handlers wrap it and a check can reach its props.

// Enough of the focusable set for what this dialog contains.
const FOCUSABLE = 'button:not([disabled]), [href], input, select, textarea';

export function GradientBrowser({
  isOpen,
  titleId,
  title,
  notice,
  groups,
  currentRef,
  onChoose,
  onClose,
}) {
  const panelRef = useRef(null);

  // Escape closes it from anywhere, including before anything inside has been
  // focused. Captured, so a key handler on the page underneath cannot eat it.
  useEffect(() => {
    if (!isOpen) return undefined;
    const onKey = e => {
      if (e.key !== 'Escape') return;
      e.stopPropagation();
      onClose();
    };
    document.addEventListener('keydown', onKey, true);
    const scroll = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    return () => {
      document.removeEventListener('keydown', onKey, true);
      document.body.style.overflow = scroll;
    };
  }, [isOpen, onClose]);

  // Opening moves focus to the current gradient, so a keyboard user lands on
  // what is selected rather than at the top of sixty swatches. The editor puts
  // focus back on the Browse button when this closes.
  useEffect(() => {
    if (!isOpen) return;
    const panel = panelRef.current;
    if (!panel) return;
    const target = panel.querySelector('[aria-current="true"]') ?? panel.querySelector(FOCUSABLE);
    target?.focus();
  }, [isOpen]);

  if (!isOpen) return null;

  // Tab stays inside while the dialog is up.
  const onKeyDown = e => {
    if (e.key !== 'Tab') return;
    const panel = panelRef.current;
    if (!panel) return;
    const items = Array.from(panel.querySelectorAll(FOCUSABLE));
    if (items.length === 0) return;
    const first = items[0];
    const last = items[items.length - 1];
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault();
      first.focus();
    }
  };

  return (
    <div
      className='fixed inset-0 z-50 flex items-end justify-center bg-black/50 sm:items-center sm:p-4'
      onClick={onClose}
    >
      <div
        ref={panelRef}
        role='dialog'
        aria-modal='true'
        aria-labelledby={titleId}
        className='bg-base-100 flex max-h-[90vh] w-full max-w-3xl flex-col overflow-hidden rounded-t-2xl shadow-xl sm:max-h-[85vh] sm:rounded-2xl'
        onClick={e => e.stopPropagation()}
        onKeyDown={onKeyDown}
      >
        <div className='border-base-content/10 flex items-start gap-3 border-b p-3'>
          <div className='min-w-0 grow'>
            <h3 id={titleId} className='truncate text-base font-medium'>
              {title}
            </h3>
            {notice && <p className='text-base-content/60 mt-1 text-xs'>{notice}</p>}
          </div>
          <button type='button' className='btn btn-sm btn-ghost' onClick={onClose}>
            Close
          </button>
        </div>
        <div className='grow overflow-y-auto p-3'>
          {groups.map(group => (
            <div key={group.label} role='group' aria-label={group.label} className='mb-4 last:mb-0'>
              <div className='text-base-content/60 mb-2 text-xs font-medium tracking-wide uppercase'>
                {group.label}
              </div>
              <div className='grid grid-cols-2 gap-2 sm:grid-cols-3 md:grid-cols-4'>
                {group.items.map(item => {
                  const isCurrent = item.ref === currentRef;
                  return (
                    <button
                      key={item.ref}
                      type='button'
                      data-ref={item.ref}
                      aria-current={isCurrent ? 'true' : undefined}
                      className={`flex flex-col gap-1 rounded-lg border p-1 text-left ${
                        isCurrent
                          ? 'border-primary ring-primary ring-2'
                          : 'border-base-content/10 hover:border-base-content/40'
                      }`}
                      onClick={() => onChoose(item.ref)}
                    >
                      <span
                        className='h-10 w-full rounded border border-black/20'
                        style={{ background: gradientCss(item.stops) }}
                      />
                      <span className='truncate px-1 text-xs'>
                        {item.name}
                        {isCurrent && <span className='text-primary'> (current)</span>}
                      </span>
                    </button>
                  );
                })}
              </div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
