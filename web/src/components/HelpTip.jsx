import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCircleQuestion } from '@fortawesome/free-solid-svg-icons/faCircleQuestion';
import { Tooltip } from './Tooltip.jsx';

// A question-mark button that shows a short help text next to a setting.
// Hover or focus shows it on a desktop; a tap shows it on a phone.
export function HelpTip({ label, children }) {
  return (
    <Tooltip
      showOnClick
      content={
        <div className='max-w-xs text-left text-xs leading-snug sm:max-w-sm'>{children}</div>
      }
    >
      <button
        type='button'
        className='btn btn-ghost btn-circle btn-xs text-base-content/50 hover:text-base-content'
        aria-label={`Help: ${label}`}
      >
        <FontAwesomeIcon icon={faCircleQuestion} />
      </button>
    </Tooltip>
  );
}
