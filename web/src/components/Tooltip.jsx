import { useState, useRef, useEffect, useCallback } from 'preact/hooks';
import { createPortal } from 'preact/compat';
import { computePosition, flip, shift, offset, autoUpdate } from '@floating-ui/dom';

/**
 * Tooltip component that renders in a portal with automatic positioning.
 * Uses Floating UI for edge detection and repositioning.
 *
 * @param {Object} props
 * @param {string} props.content - Tooltip text content
 * @param {preact.ComponentChildren} props.children - Trigger element
 * @param {'top'|'bottom'|'left'|'right'} [props.placement='top'] - Preferred placement
 * @param {boolean} [props.showOnClick=false] - Also show on click or tap, for touch screens
 *   that have no hover. A tap outside the trigger or Escape closes it again.
 */
export function Tooltip({
  content,
  children,
  placement = 'top',
  disabled = false,
  showOnClick = false,
}) {
  const [isVisible, setIsVisible] = useState(false);
  const [position, setPosition] = useState({ x: 0, y: 0 });
  const [actualPlacement, setActualPlacement] = useState(placement);
  const triggerRef = useRef(null);
  const tooltipRef = useRef(null);
  // showOnClick only: whether the last press was a touch, and whether the
  // tooltip was already open when it began. A tap is followed by emulated
  // mouse events, including a mouseleave that would hide it again at once.
  const touchPressRef = useRef(false);
  const openAtPressRef = useRef(false);

  useEffect(() => {
    if (disabled && isVisible) {
      setIsVisible(false);
    }
  }, [disabled, isVisible]);

  useEffect(() => {
    if (!isVisible || !triggerRef.current || !tooltipRef.current || disabled) return;

    const cleanup = autoUpdate(triggerRef.current, tooltipRef.current, () => {
      computePosition(triggerRef.current, tooltipRef.current, {
        placement,
        strategy: 'fixed',
        middleware: [
          offset(8), // 8px gap from trigger
          flip(), // Flip to opposite side if no space
          shift({ padding: 8 }), // Shift along axis to stay in viewport
        ],
      }).then(({ x, y, placement: finalPlacement }) => {
        setPosition(pos => {
          // Only update if changed to prevent render loops
          if (pos.x === x && pos.y === y) return pos;
          return { x, y };
        });
        setActualPlacement(finalPlacement);
      });
    });

    return cleanup;
  }, [disabled, isVisible, placement]);

  useEffect(() => {
    if (!showOnClick || !isVisible) return;
    const onPointerDown = e => {
      if (triggerRef.current && !triggerRef.current.contains(e.target)) setIsVisible(false);
    };
    const onKeyDown = e => {
      if (e.key === 'Escape') setIsVisible(false);
    };
    document.addEventListener('pointerdown', onPointerDown);
    document.addEventListener('keydown', onKeyDown);
    return () => {
      document.removeEventListener('pointerdown', onPointerDown);
      document.removeEventListener('keydown', onKeyDown);
    };
  }, [showOnClick, isVisible]);

  const show = useCallback(() => {
    if (!disabled) setIsVisible(true);
  }, [disabled]);
  const hide = useCallback(() => setIsVisible(false), []);
  const onTriggerPointerEnter = e => {
    if (e.pointerType === 'mouse') touchPressRef.current = false;
  };
  const onTriggerPointerDown = e => {
    touchPressRef.current = e.pointerType !== 'mouse';
    openAtPressRef.current = isVisible;
  };
  const onTriggerMouseLeave = () => {
    if (!touchPressRef.current) hide();
  };
  const onTriggerClick = () => {
    // A second tap on the trigger closes it.
    if (touchPressRef.current && openAtPressRef.current) hide();
    else show();
  };

  const tooltip =
    isVisible &&
    createPortal(
      <div
        ref={tooltipRef}
        role='tooltip'
        className='tooltip-portal'
        data-placement={actualPlacement}
        style={{
          position: 'fixed',
          left: `${position.x}px`,
          top: `${position.y}px`,
        }}
      >
        {content}
      </div>,
      document.body,
    );

  return (
    <>
      <span
        ref={triggerRef}
        onMouseEnter={show}
        onMouseLeave={showOnClick ? onTriggerMouseLeave : hide}
        onFocus={show}
        onBlur={hide}
        onPointerEnter={showOnClick ? onTriggerPointerEnter : undefined}
        onPointerDown={showOnClick ? onTriggerPointerDown : undefined}
        onClick={showOnClick ? onTriggerClick : undefined}
        className='inline-flex'
      >
        {children}
      </span>
      {tooltip}
    </>
  );
}
