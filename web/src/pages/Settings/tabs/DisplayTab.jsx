import { BG_ANIMATIONS, parseBgAnimParams, setBgAnimParam } from '../../../config/bgAnimations.js';
import Card from '../../../components/Card.jsx';
import { GradientEditor } from '../../../components/GradientEditor.jsx';
import { HelpTip } from '../../../components/HelpTip.jsx';

// The Display tab, in five sections: screen and brightness, the animation and
// its parameters, motion and performance, legibility, colours. Every control
// writes the same formData key it always did; the help that used to sit in
// long paragraphs under the controls now sits behind a question mark next to
// each control it is about.

// The stored value, or the firmware default when the device sent none.
function intOr(value, def) {
  return value === undefined ? def : parseInt(value, 10);
}

// A labelled card. Card's own title is hidden below the lg breakpoint, so the
// heading is drawn here to keep the sections labelled on a phone.
function DisplaySection({ title, intro, children }) {
  return (
    <Card>
      <h2 className='text-lg font-semibold'>{title}</h2>
      {intro && <p className='text-base-content/60 text-sm'>{intro}</p>}
      <div className='mt-2 grid grid-cols-1 gap-x-6 gap-y-4 md:grid-cols-2'>{children}</div>
    </Card>
  );
}

// One setting: label, help button, the current value and a reset button on
// the label row, then the control.
function Field({ label, htmlFor, help, value, isDefault, onReset, wide, note, children }) {
  return (
    <div className={`form-control min-w-0 ${wide ? 'md:col-span-2' : ''}`}>
      <div className='mb-1 flex min-h-7 items-center gap-1'>
        <label htmlFor={htmlFor} className='text-sm font-medium'>
          {label}
        </label>
        {help && <HelpTip label={label}>{help}</HelpTip>}
        {(value !== undefined || onReset) && (
          <span className='ml-auto flex shrink-0 items-center gap-1 pl-2'>
            {value !== undefined && (
              <span className='text-base-content/70 text-sm tabular-nums'>{value}</span>
            )}
            {onReset && (
              <button
                type='button'
                className='btn btn-ghost btn-xs'
                onClick={onReset}
                disabled={isDefault}
                aria-label={`Reset ${label} to its default`}
              >
                Reset
              </button>
            )}
          </span>
        )}
      </div>
      {children}
      {note && <div className='text-warning mt-1 text-xs'>{note}</div>}
    </div>
  );
}

// A switch with its label and help on one row.
function Toggle({ label, htmlFor, help, checked, onChange, wide }) {
  return (
    <div className={`flex min-h-7 items-center gap-1 ${wide ? 'md:col-span-2' : ''}`}>
      <label htmlFor={htmlFor} className='cursor-pointer text-sm font-medium'>
        {label}
      </label>
      {help && <HelpTip label={label}>{help}</HelpTip>}
      <input
        id={htmlFor}
        name={htmlFor}
        type='checkbox'
        className='toggle toggle-primary ml-auto shrink-0'
        checked={checked}
        onChange={onChange}
      />
    </div>
  );
}

// A 0-100 style range input bound to one formData key, with its value shown
// and a reset to the firmware default.
function RangeField({
  formData,
  onChange,
  setField,
  name,
  label,
  def,
  min,
  max,
  step,
  unit,
  help,
}) {
  const value = intOr(formData[name], def);
  return (
    <Field
      label={label}
      htmlFor={name}
      help={help}
      value={`${value}${unit}`}
      onReset={() => setField(name, String(def))}
      isDefault={value === def}
    >
      <input
        id={name}
        name={name}
        type='range'
        min={min}
        max={max}
        step={step}
        className='range range-sm w-full'
        value={value}
        onChange={onChange(name)}
      />
    </Field>
  );
}

function ScreenSection({ formData, onChange, setField }) {
  const vcom = intOr(formData.panelVcom, 45);
  return (
    <DisplaySection title='Screen and brightness'>
      <Field
        label='Main brightness (1 to 16)'
        htmlFor='mainBrightness'
        help='Backlight level while you use the machine. It dims the text along with the animation, so lowering it does not make anything easier to read. To tone down a bright animation, use animation brightness or highlight rolloff under Colours.'
      >
        <input
          id='mainBrightness'
          name='mainBrightness'
          type='number'
          className='input input-bordered w-full'
          placeholder='16'
          min='1'
          max='16'
          value={formData.mainBrightness}
          onChange={onChange('mainBrightness')}
        />
      </Field>
      <Field label='Display theme' htmlFor='themeMode'>
        <select
          id='themeMode'
          name='themeMode'
          className='select select-bordered w-full'
          value={formData.themeMode}
          onChange={onChange('themeMode')}
        >
          <option value={0}>Dark Theme</option>
          <option value={1}>Light Theme</option>
        </select>
      </Field>
      <Toggle
        wide
        label='Enable standby display'
        htmlFor='standbyDisplayEnabled'
        help='When this is off, the screen goes dark in standby: standby brightness is set to 0.'
        checked={!!formData.standbyDisplayEnabled}
        onChange={onChange('standbyDisplayEnabled')}
      />
      <Field
        label='Standby brightness (0 to 16)'
        htmlFor='standbyBrightness'
        help='Backlight level once the standby screen has been up for the standby brightness timeout. When standby display is off, this is 0.'
      >
        <input
          id='standbyBrightness'
          name='standbyBrightness'
          type='number'
          className='input input-bordered w-full'
          placeholder='8'
          min='0'
          max='16'
          disabled={!formData.standbyDisplayEnabled}
          value={formData.standbyDisplayEnabled ? formData.standbyBrightness : 0}
          onChange={onChange('standbyBrightness')}
        />
      </Field>
      <Field
        label='Standby brightness timeout'
        htmlFor='standbyBrightnessTimeout'
        help='Seconds the standby screen stays at main brightness before it drops to standby brightness.'
      >
        <div className='input-group'>
          <label htmlFor='standbyBrightnessTimeout' className='input w-full'>
            <input
              id='standbyBrightnessTimeout'
              name='standbyBrightnessTimeout'
              type='number'
              min='1'
              placeholder='60'
              value={formData.standbyBrightnessTimeout}
              onChange={onChange('standbyBrightnessTimeout')}
            />
            <span aria-label='seconds'>s</span>
          </label>
        </div>
      </Field>
      <Field
        wide
        label='Panel VCOM'
        htmlFor='panelVcom'
        value={`${vcom}, ${(0.1 + vcom * 0.0125).toFixed(2)} V`}
        onReset={() => setField('panelVcom', '45')}
        isDefault={vcom === 45}
        help='Trim this only if you see a faint shimmer on large flat areas. Move it a few steps at a time and stop where the shimmer is weakest. The right value differs per panel and drifts while the display warms up, so judge it on a cold screen. 45 is what the panel ships with.'
      >
        <input
          id='panelVcom'
          name='panelVcom'
          type='range'
          min='20'
          max='100'
          className='range range-sm w-full'
          value={vcom}
          onChange={onChange('panelVcom')}
        />
      </Field>
    </DisplaySection>
  );
}

// Animation picker plus the parameters of the selected animation only.
// Params live in formData.bgAnimParams as the same packed string the firmware
// stores ("p0,p1,p2,p3;..." indexed by animation id), edited via setField.
function AnimationSection({ formData, onChange, setField }) {
  const animIdx = Math.min(
    BG_ANIMATIONS.length - 1,
    Math.max(0, parseInt(formData.bgAnimId, 10) || 0),
  );
  const anim = BG_ANIMATIONS[animIdx];
  const values = parseBgAnimParams(formData.bgAnimParams)[animIdx];
  const setParam = (j, v) =>
    setField('bgAnimParams', setBgAnimParam(formData.bgAnimParams, animIdx, j, v));
  return (
    <DisplaySection title='Animation'>
      <Toggle
        wide
        label='Show the animation behind every screen'
        htmlFor='bgAnimAllScreens'
        help='When this is off, the animation plays on the standby screen only.'
        checked={!!formData.bgAnimAllScreens}
        onChange={onChange('bgAnimAllScreens')}
      />
      <Field wide label='Animation' htmlFor='bgAnimId'>
        <select
          id='bgAnimId'
          name='bgAnimId'
          className='select select-bordered w-full'
          value={animIdx}
          onChange={onChange('bgAnimId')}
        >
          {BG_ANIMATIONS.map((a, i) => (
            <option key={a.id} value={i}>
              {a.name}
            </option>
          ))}
        </select>
        {anim.description && (
          <p className='text-base-content/60 mt-1 text-sm'>{anim.description}</p>
        )}
      </Field>
      {anim.params.map((param, j) => {
        const def = param.def ?? 0;
        if (param.options) {
          const optionIdx = Math.min(
            param.options.length - 1,
            Math.floor((values[j] * param.options.length) / 101),
          );
          return (
            <Field
              key={`${anim.id}-${param.key}`}
              label={param.label}
              htmlFor={`bgAnim-${param.key}`}
            >
              <select
                id={`bgAnim-${param.key}`}
                className='select select-bordered w-full'
                value={optionIdx}
                onChange={e =>
                  // Store the option index scaled back onto 0-100.
                  setParam(
                    j,
                    Math.round(
                      (parseInt(e.target.value, 10) * 100) / Math.max(1, param.options.length - 1),
                    ),
                  )
                }
              >
                {param.options.map((label, k) => (
                  <option key={label} value={k}>
                    {label}
                  </option>
                ))}
              </select>
            </Field>
          );
        }
        return (
          <Field
            key={`${anim.id}-${param.key}`}
            label={param.label}
            htmlFor={`bgAnim-${param.key}`}
            value={values[j]}
            onReset={() => setParam(j, def)}
            isDefault={values[j] === def}
          >
            <input
              id={`bgAnim-${param.key}`}
              type='range'
              min='0'
              max='100'
              className='range range-sm w-full'
              value={values[j]}
              onChange={e => setParam(j, e.target.value)}
            />
          </Field>
        );
      })}
    </DisplaySection>
  );
}

const BANDWIDTH_HELP =
  'If the animation flickers or the image jumps, lower the frame rate or the panel refresh rate: both compete for the same memory bandwidth.';

function MotionSection({ formData, onChange, setField }) {
  const fps = parseInt(formData.bgAnimFps, 10) || 30;
  const range = { formData, onChange, setField };
  return (
    <DisplaySection title='Motion and performance' intro='Changes apply live after you save.'>
      <Field
        label='Animation frame rate'
        htmlFor='bgAnimFps'
        value={`${fps} fps`}
        onReset={() => setField('bgAnimFps', '30')}
        isDefault={fps === 30}
        help={`How many frames a second the animation draws. The panel refresh rate is separate. ${BANDWIDTH_HELP}`}
      >
        <input
          id='bgAnimFps'
          name='bgAnimFps'
          type='range'
          min='5'
          max='60'
          className='range range-sm w-full'
          value={fps}
          onChange={onChange('bgAnimFps')}
        />
      </Field>
      <Field
        label='Panel refresh rate'
        htmlFor='panelClockDiv'
        help={`How often the panel scans out. The animation has its own frame rate and the widgets redraw when they change. ${BANDWIDTH_HELP}`}
        note={
          formData.panelClockLive === false
            ? 'This build retimes the panel only when it starts up, so restart the display after saving.'
            : undefined
        }
      >
        <select
          id='panelClockDiv'
          name='panelClockDiv'
          className='select select-bordered w-full'
          value={parseInt(formData.panelClockDiv, 10) || 0}
          onChange={onChange('panelClockDiv')}
        >
          <option value={0}>Firmware default</option>
          <option value={6}>51 Hz, stable, slight gradient shimmer</option>
          <option value={7}>43 Hz, conservative</option>
          <option value={8}>38 Hz, most conservative</option>
        </select>
      </Field>
      <Field
        label='Animation resolution'
        htmlFor='bgAnimHalfRes'
        help='Full resolution is visibly sharper. Half resolution does not raise the frame rate by itself: doubling the rows costs what the smaller render saves (measured 2026-09-05). Interlace is the setting that does.'
      >
        <select
          id='bgAnimHalfRes'
          name='bgAnimHalfRes'
          className='select select-bordered w-full'
          value={intOr(formData.bgAnimHalfRes, 1)}
          onChange={onChange('bgAnimHalfRes')}
        >
          <option value={1}>Half (240x240, doubled on the way out)</option>
          <option value={0}>Full (480x480), sharper</option>
        </select>
      </Field>
      <Field
        label='Interlace animation'
        htmlFor='bgAnimInterlace'
        help='Interlace refreshes half the rows on each frame, which is not usually noticeable while something is moving. Half resolution with interlace on is what lets every animation run above 40 fps. At full resolution the heaviest animations drop to around 15 fps.'
      >
        <select
          id='bgAnimInterlace'
          name='bgAnimInterlace'
          className='select select-bordered w-full'
          value={intOr(formData.bgAnimInterlace, 1)}
          onChange={onChange('bgAnimInterlace')}
        >
          <option value={1}>
            On, refreshes half the rows each frame (roughly doubles the rate)
          </option>
          <option value={0}>Off, every row every frame</option>
        </select>
      </Field>
      <RangeField
        {...range}
        name='bgFadeOutMs'
        label='Screen fade out'
        def={120}
        min='0'
        max='1000'
        step='10'
        unit=' ms'
        help='How long the old screen takes to fade away on a screen change. 0 cuts.'
      />
      <RangeField
        {...range}
        name='bgFadeInMs'
        label='Screen fade in'
        def={120}
        min='0'
        max='1000'
        step='10'
        unit=' ms'
        help='How long the new screen takes to appear once it is ready. 0 cuts.'
      />
      <Field
        label='Screen fade curve'
        htmlFor='bgFadeCurve'
        help='Linear fades at a constant speed. Smooth starts and ends slowly.'
      >
        <select
          id='bgFadeCurve'
          name='bgFadeCurve'
          className='select select-bordered w-full'
          value={intOr(formData.bgFadeCurve, 0)}
          onChange={onChange('bgFadeCurve')}
        >
          <option value={0}>Linear, constant speed</option>
          <option value={1}>Smooth, eases in and out</option>
        </select>
      </Field>
    </DisplaySection>
  );
}

function LegibilitySection({ formData, onChange, setField }) {
  const range = { formData, onChange, setField };
  return (
    <DisplaySection
      title='Legibility'
      intro='How easy the screen is to read while an animation plays behind it.'
    >
      <RangeField
        {...range}
        name='bgAnimScrim'
        label='Text backdrop'
        def={0}
        min='0'
        max='100'
        unit='%'
        help='Dims the animation in a soft halo right behind the numbers and labels, and leaves the rest of the frame alone. 55% is the lightest setting that keeps white text comfortable to read on every animation and every theme, including the brightest. Stronger settings work but start to look like a dark plate.'
      />
      <Field
        label='Screen background panels'
        htmlFor='bgAnimClearPlates'
        help={
          <>
            <p>
              The brew, status and profile screens have a solid circle behind their dials, the info
              screen a solid panel, and the brew and grind screens a filled pill behind the scale
              weight. The other screens have none. With the animation behind every screen, these
              show up as dark shapes on some screens and not others, so by default they are hidden
              while the animation plays.
            </p>
            <p className='mt-1'>
              The pill behind the weight is also the mode switch. To keep it looking like a button,
              pick the custom option and give it a low opacity rather than hiding it.
            </p>
          </>
        }
      >
        <select
          id='bgAnimClearPlates'
          name='bgAnimClearPlates'
          className='select select-bordered w-full'
          value={intOr(formData.bgAnimClearPlates, 1)}
          onChange={onChange('bgAnimClearPlates')}
        >
          <option value={1}>Hide, animation fills every screen the same way</option>
          <option value={0}>Keep, solid panel behind the dials on some screens</option>
          <option value={2}>Custom colour and transparency</option>
        </select>
      </Field>
      {parseInt(formData.bgAnimClearPlates, 10) === 2 && (
        <>
          <Field label='Panel colour' htmlFor='bgAnimPlateColor'>
            <input
              id='bgAnimPlateColor'
              name='bgAnimPlateColor'
              type='color'
              className='input input-bordered h-12 w-full'
              value={formData.bgAnimPlateColor || '#000000'}
              onChange={onChange('bgAnimPlateColor')}
            />
          </Field>
          <RangeField
            {...range}
            name='bgAnimPlateOpacity'
            label='Panel opacity'
            def={35}
            min='0'
            max='100'
            unit='%'
            help='0% is fully transparent, the same as hiding the panels. 100% is fully opaque.'
          />
        </>
      )}
    </DisplaySection>
  );
}

const TASTE_HELP =
  'Not needed for legibility while the text backdrop is on; it is there for taste. It is separate from screen brightness, which dims the text along with the animation and so does not make anything easier to read.';

function ColoursSection({ formData, onChange, setField }) {
  const animIdx = Math.min(
    BG_ANIMATIONS.length - 1,
    Math.max(0, parseInt(formData.bgAnimId, 10) || 0),
  );
  const range = { formData, onChange, setField };
  return (
    <DisplaySection title='Colours'>
      <div className='min-w-0 md:col-span-2'>
        <GradientEditor animIdx={animIdx} formData={formData} setField={setField} />
      </div>
      <RangeField
        {...range}
        name='bgAnimBrightness'
        label='Animation brightness'
        def={100}
        min='10'
        max='100'
        unit='%'
        help={`Scales the whole animation theme down. ${TASTE_HELP}`}
      />
      <RangeField
        {...range}
        name='bgAnimHighlightKnee'
        label='Highlight rolloff'
        def={100}
        min='20'
        max='100'
        unit='%'
        help={`Compresses only the brightest parts and leaves the mid tones their colour, so it is usually the better one to reach for if a theme looks blown out. ${TASTE_HELP}`}
      />
      <Toggle
        wide
        label='Custom element tint'
        htmlFor='elementTintEnabled'
        help='Replaces the accent colour of the icons and text on the display with a colour you pick.'
        checked={!!formData.elementTintEnabled}
        onChange={onChange('elementTintEnabled')}
      />
      {!!formData.elementTintEnabled && (
        <Field label='Element tint colour' htmlFor='elementTintColor'>
          <input
            id='elementTintColor'
            name='elementTintColor'
            type='color'
            className='input input-bordered h-12 w-full'
            value={formData.elementTintColor || '#FFFFFF'}
            onChange={onChange('elementTintColor')}
          />
        </Field>
      )}
      <Field
        label='Touch feedback colour'
        htmlFor='touchDimColor'
        help='A pressed button or icon shifts toward this colour. Black reads as dimming; a bright colour reads as a highlight flash.'
      >
        <input
          id='touchDimColor'
          name='touchDimColor'
          type='color'
          className='input input-bordered h-12 w-full'
          value={formData.touchDimColor || '#000000'}
          onChange={onChange('touchDimColor')}
        />
      </Field>
    </DisplaySection>
  );
}

export function DisplayTab({ formData, onChange, setField }) {
  const props = { formData, onChange, setField };
  return (
    <div className='space-y-4 sm:space-y-6'>
      <ScreenSection {...props} />
      <AnimationSection {...props} />
      <MotionSection {...props} />
      <LegibilitySection {...props} />
      <ColoursSection {...props} />
    </div>
  );
}
