import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faUndo } from '@fortawesome/free-solid-svg-icons/faUndo';
import { BG_ANIMATIONS, parseBgAnimParams, setBgAnimParam } from '../../../config/bgAnimations.js';
import Section from '../../../components/Card.jsx';
import { GradientEditor } from '../../../components/GradientEditor.jsx';
import {
  InputGroupField,
  SettingsFormField,
  ToggleField,
} from '../../../components/SettingsFormField.jsx';

const FIELD_GRID = 'grid grid-cols-1 gap-4 md:grid-cols-2';

const valueWithDefault = (value, fallback) =>
  value === undefined ? fallback : parseInt(value, 10);

function SettingsGroup({ title, children }) {
  return (
    <section className='border-base-content/10 border-t pt-5 first:border-t-0 first:pt-0'>
      <h3 className='mb-3 text-base font-semibold'>{title}</h3>
      {children}
    </section>
  );
}

// The standby animation selector's value. -1 is "same as main", which is
// also what an id past the end of this build's roster means: the firmware
// falls back to the main animation for one of those rather than indexing off
// the end of the registry, so the form shows the same thing it does.
function standbyAnimValue(stored) {
  const id = parseInt(stored, 10);
  if (!Number.isFinite(id) || id < 0 || id >= BG_ANIMATIONS.length) return -1;
  return id;
}

function AnimationParamField({ anim, animIdx, formData, param, paramIdx, setField, value }) {
  if (param.options) {
    return (
      <SettingsFormField
        key={`${anim.id}-${param.key}`}
        label={param.label}
        htmlFor={`bgAnim-${param.key}`}
        noMargin
      >
        <select
          id={`bgAnim-${param.key}`}
          className='select select-bordered w-full'
          value={Math.min(
            param.options.length - 1,
            Math.floor((value * param.options.length) / 101),
          )}
          onChange={e =>
            setField(
              'bgAnimParams',
              setBgAnimParam(
                formData.bgAnimParams,
                animIdx,
                paramIdx,
                // Store the option index scaled back onto 0 to 100.
                Math.round(
                  (parseInt(e.target.value, 10) * 100) / Math.max(1, param.options.length - 1),
                ),
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
      </SettingsFormField>
    );
  }

  const defaultValue = param.def ?? 0;

  return (
    <SettingsFormField
      key={`${anim.id}-${param.key}`}
      label={`${param.label} (${value})`}
      htmlFor={`bgAnim-${param.key}`}
      noMargin
    >
      <div className='flex items-center gap-2'>
        <input
          id={`bgAnim-${param.key}`}
          type='range'
          min='0'
          max='100'
          className='range min-w-0 flex-1'
          value={value}
          onChange={e =>
            setField(
              'bgAnimParams',
              setBgAnimParam(formData.bgAnimParams, animIdx, paramIdx, e.target.value),
            )
          }
        />
        <button
          type='button'
          className='btn btn-ghost btn-square btn-sm text-base-content/70 shrink-0'
          aria-label={`Reset ${param.label} to ${defaultValue}`}
          title='Reset to default'
          disabled={value === defaultValue}
          onClick={() =>
            setField(
              'bgAnimParams',
              setBgAnimParam(formData.bgAnimParams, animIdx, paramIdx, defaultValue),
            )
          }
        >
          <FontAwesomeIcon icon={faUndo} aria-hidden='true' />
        </button>
      </div>
    </SettingsFormField>
  );
}

// Animation picker, placement and parameter controls for the selected
// animation only. Params live in formData.bgAnimParams as the same packed
// string the firmware stores ("p0,p1,p2,p3;..." indexed by animation id) and
// are edited via setField.
function BackgroundAnimationSettings({ formData, onChange, setField }) {
  const animIdx = Math.min(
    BG_ANIMATIONS.length - 1,
    Math.max(0, parseInt(formData.bgAnimId, 10) || 0),
  );
  const anim = BG_ANIMATIONS[animIdx];
  const values = parseBgAnimParams(formData.bgAnimParams)[animIdx];
  const bgAnimFps = parseInt(formData.bgAnimFps, 10) || 30;
  const panelVcom = valueWithDefault(formData.panelVcom, 45);
  const bgAnimHalfRes = valueWithDefault(formData.bgAnimHalfRes, 1);
  const bgAnimInterlace = valueWithDefault(formData.bgAnimInterlace, 0);
  const bgAnimClearPlates = valueWithDefault(formData.bgAnimClearPlates, 1);
  const bgAnimScrim = valueWithDefault(formData.bgAnimScrim, 0);
  const bgFadeOutMs = valueWithDefault(formData.bgFadeOutMs, 120);
  const bgFadeInMs = valueWithDefault(formData.bgFadeInMs, 120);
  const bgFadeCurve = valueWithDefault(formData.bgFadeCurve, 0);
  const bgAnimBrightness = valueWithDefault(formData.bgAnimBrightness, 100);
  const bgAnimHighlightKnee = valueWithDefault(formData.bgAnimHighlightKnee, 100);
  const bgAnimPlateOpacity = valueWithDefault(formData.bgAnimPlateOpacity, 35);

  return (
    <div className='space-y-6'>
      <SettingsGroup title='Animation placement'>
        <div className={FIELD_GRID}>
          <SettingsFormField
            label='Animation'
            htmlFor='bgAnimId'
            noMargin
            helpText={anim.description}
          >
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
          </SettingsFormField>
          <SettingsFormField
            label='Standby screen animation'
            htmlFor='bgAnimStandbyId'
            noMargin
            helpText='Standby can use the main animation or its own selection.'
            tooltip='It keeps its own parameters and its own gradient. With show animation behind all screens turned off, only the standby animation is ever seen.'
            tooltipLabel='Standby screen animation'
          >
            <select
              id='bgAnimStandbyId'
              name='bgAnimStandbyId'
              className='select select-bordered w-full'
              value={standbyAnimValue(formData.bgAnimStandbyId)}
              onChange={onChange('bgAnimStandbyId')}
            >
              <option value={-1}>Same as main</option>
              {BG_ANIMATIONS.map((a, i) => (
                <option key={`standby-${a.id}`} value={i}>
                  {a.name}
                </option>
              ))}
            </select>
          </SettingsFormField>
          <div className='md:col-span-2'>
            <ToggleField
              label='Show animation behind all screens'
              htmlFor='bgAnimAllScreens'
              checked={!!formData.bgAnimAllScreens}
              onChange={onChange('bgAnimAllScreens')}
              helpText='When off, only the standby screen uses the background animation.'
            />
          </div>
        </div>
      </SettingsGroup>

      <SettingsGroup title={`${anim.name} tuning`}>
        <div className={FIELD_GRID}>
          {anim.params.map((param, j) => (
            <AnimationParamField
              key={`${anim.id}-${param.key}`}
              anim={anim}
              animIdx={animIdx}
              formData={formData}
              param={param}
              paramIdx={j}
              setField={setField}
              value={values[j]}
            />
          ))}
        </div>
        <div className='mt-4'>
          <GradientEditor animIdx={animIdx} formData={formData} setField={setField} />
        </div>
      </SettingsGroup>

      <SettingsGroup title='Motion and performance'>
        <div className={FIELD_GRID}>
          <SettingsFormField
            label={`Animation frame rate (${bgAnimFps} fps)`}
            htmlFor='bgAnimFps'
            noMargin
            helpText='Lower this or the panel refresh rate if the animation flickers or the image jumps.'
            tooltip='They compete for the same memory bandwidth. Frame rate changes apply live after saving.'
            tooltipLabel='Animation frame rate'
          >
            <input
              id='bgAnimFps'
              name='bgAnimFps'
              type='range'
              min='5'
              max='60'
              className='range w-full'
              value={bgAnimFps}
              onChange={onChange('bgAnimFps')}
            />
          </SettingsFormField>
          <SettingsFormField
            label='Panel refresh rate'
            htmlFor='panelClockDiv'
            noMargin
            helpText={
              formData.panelClockLive === false
                ? 'This build retimes the panel only when it starts up, so restart the display after saving.'
                : 'How often the panel scans out. The animation has its own frame rate and widgets refresh when they change.'
            }
            tooltip={
              formData.panelClockLive === false
                ? 'Animation frame rate and panel refresh rate compete for the same memory bandwidth. This panel refresh change applies after the display restarts.'
                : 'Animation frame rate and panel refresh rate compete for the same memory bandwidth. Panel refresh changes apply live after saving.'
            }
            tooltipLabel='Panel refresh rate'
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
          </SettingsFormField>
          <SettingsFormField
            label='Animation resolution'
            htmlFor='bgAnimHalfRes'
            noMargin
            helpText='Trades sharpness for smoothness; full resolution is sharper but heavier.'
            tooltip='Half resolution plus interlacing is what lets every animation run above 40 fps; full resolution can drop the heaviest animations to around 15 fps. Half resolution alone does not raise frame rate because doubling rows costs what the smaller render saves, measured 2026-09-05.'
            tooltipLabel='Animation resolution'
          >
            <select
              id='bgAnimHalfRes'
              name='bgAnimHalfRes'
              className='select select-bordered w-full'
              value={bgAnimHalfRes}
              onChange={onChange('bgAnimHalfRes')}
            >
              <option value={1}>Half (240x240, doubled on the way out)</option>
              <option value={0}>Full (480x480), sharper</option>
            </select>
          </SettingsFormField>
          <SettingsFormField
            label='Interlace animation'
            htmlFor='bgAnimInterlace'
            noMargin
            helpText='Interlacing is the setting that improves smoothness.'
            tooltip='It refreshes half the rows on each frame, which roughly doubles the rate. That is not usually noticeable while something is moving.'
            tooltipLabel='Interlace animation'
          >
            <select
              id='bgAnimInterlace'
              name='bgAnimInterlace'
              className='select select-bordered w-full'
              value={bgAnimInterlace}
              onChange={onChange('bgAnimInterlace')}
            >
              <option value={1}>
                On, refreshes half the rows each frame (roughly doubles the rate)
              </option>
              <option value={0}>Off, every row every frame</option>
            </select>
          </SettingsFormField>
          <SettingsFormField
            label={`Screen fade out (${bgFadeOutMs} ms)`}
            htmlFor='bgFadeOutMs'
            noMargin
            helpText='How long the old screen takes to fade away on a screen change. 0 cuts.'
          >
            <input
              id='bgFadeOutMs'
              name='bgFadeOutMs'
              type='range'
              min='0'
              max='1000'
              step='10'
              className='range w-full'
              value={bgFadeOutMs}
              onChange={onChange('bgFadeOutMs')}
            />
          </SettingsFormField>
          <SettingsFormField
            label={`Screen fade in (${bgFadeInMs} ms)`}
            htmlFor='bgFadeInMs'
            noMargin
            helpText='How long the new screen takes to appear once it is ready. 0 cuts.'
          >
            <input
              id='bgFadeInMs'
              name='bgFadeInMs'
              type='range'
              min='0'
              max='1000'
              step='10'
              className='range w-full'
              value={bgFadeInMs}
              onChange={onChange('bgFadeInMs')}
            />
          </SettingsFormField>
          <SettingsFormField label='Screen fade curve' htmlFor='bgFadeCurve' noMargin>
            <select
              id='bgFadeCurve'
              name='bgFadeCurve'
              className='select select-bordered w-full'
              value={bgFadeCurve}
              onChange={onChange('bgFadeCurve')}
            >
              <option value={0}>Linear, constant speed</option>
              <option value={1}>Smooth, eases in and out</option>
            </select>
          </SettingsFormField>
        </div>
      </SettingsGroup>

      <SettingsGroup title='Reading the screen'>
        <div className={FIELD_GRID}>
          <SettingsFormField
            label={`Text backdrop (${bgAnimScrim}%)`}
            htmlFor='bgAnimScrim'
            noMargin
            helpText='Use this first for readability. 55% is the lightest setting that keeps white text comfortable to read on every animation and every theme, including the brightest.'
            tooltip='It dims the animation in a soft halo directly behind numbers and labels and leaves the rest of the frame alone, which is why it is on by default. Stronger settings work but start to look like a dark plate.'
            tooltipLabel='Text backdrop'
          >
            <input
              id='bgAnimScrim'
              name='bgAnimScrim'
              type='range'
              min='0'
              max='100'
              className='range w-full'
              value={bgAnimScrim}
              onChange={onChange('bgAnimScrim')}
            />
          </SettingsFormField>
          <SettingsFormField
            label={`Animation brightness (${bgAnimBrightness}%)`}
            htmlFor='bgAnimBrightness'
            noMargin
            helpText='Scales the whole animation down everywhere; use it for taste, not text contrast.'
            tooltip='With the text backdrop on, brightness is not needed for legibility. It is separate from screen brightness, which dims text along with the animation and does not make the screen easier to read.'
            tooltipLabel='Animation brightness'
          >
            <input
              id='bgAnimBrightness'
              name='bgAnimBrightness'
              type='range'
              min='10'
              max='100'
              className='range w-full'
              value={bgAnimBrightness}
              onChange={onChange('bgAnimBrightness')}
            />
          </SettingsFormField>
          <SettingsFormField
            label={`Highlight rolloff (${bgAnimHighlightKnee}%)`}
            htmlFor='bgAnimHighlightKnee'
            noMargin
            helpText='Compresses only the brightest parts, usually the better fix for a blown out theme.'
            tooltip='Animation brightness and highlight rolloff change how the animation itself looks everywhere. Rolloff leaves mid tones their colour, and both controls are for taste rather than legibility when the backdrop is on.'
            tooltipLabel='Highlight rolloff'
          >
            <input
              id='bgAnimHighlightKnee'
              name='bgAnimHighlightKnee'
              type='range'
              min='20'
              max='100'
              className='range w-full'
              value={bgAnimHighlightKnee}
              onChange={onChange('bgAnimHighlightKnee')}
            />
          </SettingsFormField>
        </div>
      </SettingsGroup>

      <SettingsGroup title='Colours and surfaces'>
        <div className={FIELD_GRID}>
          <SettingsFormField
            label='Screen background panels'
            htmlFor='bgAnimClearPlates'
            noMargin
            helpText='By default these surfaces are hidden while animation plays so screens do not gain inconsistent dark shapes.'
            tooltip='Brew, status and profile screens have a solid circle behind their dials; the info screen has a solid panel; brew and grind screens have a filled pill behind the scale weight. Other screens have none, so those surfaces appear on some screens and not others. The pill behind the weight is also the mode switch, so pick custom colour and transparency at a low opacity if it should still read as a button, rather than hiding it outright.'
            tooltipLabel='Screen background panels'
          >
            <select
              id='bgAnimClearPlates'
              name='bgAnimClearPlates'
              className='select select-bordered w-full'
              value={bgAnimClearPlates}
              onChange={onChange('bgAnimClearPlates')}
            >
              <option value={1}>Hide, animation fills every screen the same way</option>
              <option value={0}>Keep, solid panel behind the dials on some screens</option>
              <option value={2}>Custom colour and transparency</option>
            </select>
          </SettingsFormField>
          {bgAnimClearPlates === 2 && (
            <>
              <SettingsFormField label='Panel colour' htmlFor='bgAnimPlateColor' noMargin>
                <input
                  id='bgAnimPlateColor'
                  name='bgAnimPlateColor'
                  type='color'
                  className='input input-bordered h-12 w-full'
                  value={formData.bgAnimPlateColor || '#000000'}
                  onChange={onChange('bgAnimPlateColor')}
                />
              </SettingsFormField>
              <SettingsFormField
                label={`Panel opacity (${bgAnimPlateOpacity}%)`}
                htmlFor='bgAnimPlateOpacity'
                noMargin
                helpText='Use a low opacity if the scale weight pill should still read as a button.'
                tooltip='The pill behind the weight is also the mode switch, so custom low opacity keeps that affordance without restoring the solid dark plate.'
                tooltipLabel='Panel opacity'
              >
                <input
                  id='bgAnimPlateOpacity'
                  name='bgAnimPlateOpacity'
                  type='range'
                  min='0'
                  max='100'
                  className='range w-full'
                  value={bgAnimPlateOpacity}
                  onChange={onChange('bgAnimPlateOpacity')}
                />
              </SettingsFormField>
            </>
          )}
          <div className='md:col-span-2'>
            <ToggleField
              label='Custom element tint'
              htmlFor='elementTintEnabled'
              checked={!!formData.elementTintEnabled}
              onChange={onChange('elementTintEnabled')}
              helpText='Sets the display icons and accent colour.'
            />
          </div>
          {!!formData.elementTintEnabled && (
            <SettingsFormField label='Element tint colour' htmlFor='elementTintColor' noMargin>
              <input
                id='elementTintColor'
                name='elementTintColor'
                type='color'
                className='input input-bordered h-12 w-full'
                value={formData.elementTintColor || '#FFFFFF'}
                onChange={onChange('elementTintColor')}
              />
            </SettingsFormField>
          )}
          <SettingsFormField
            label='Touch feedback colour'
            htmlFor='touchDimColor'
            noMargin
            helpText='Pressed elements shift toward this colour.'
          >
            <input
              id='touchDimColor'
              name='touchDimColor'
              type='color'
              className='input input-bordered h-12 w-full'
              value={formData.touchDimColor || '#000000'}
              onChange={onChange('touchDimColor')}
            />
          </SettingsFormField>
        </div>
      </SettingsGroup>

      <SettingsGroup title='Panel trim'>
        <div className={FIELD_GRID}>
          <SettingsFormField
            label={`Panel VCOM (${panelVcom}, ${(0.1 + panelVcom * 0.0125).toFixed(2)} V)`}
            htmlFor='panelVcom'
            noMargin
            helpText='Advanced panel voltage trim. Leave it at the shipped value unless large flat areas shimmer.'
            tooltip='Move it a few steps at a time and stop where shimmer is weakest. The right value differs per panel and drifts as the display warms up, so judge it on a cold screen; 45 is the shipped value.'
            tooltipLabel='Panel VCOM'
          >
            <input
              id='panelVcom'
              name='panelVcom'
              type='range'
              min='20'
              max='100'
              className='range w-full'
              value={panelVcom}
              onChange={onChange('panelVcom')}
            />
          </SettingsFormField>
        </div>
      </SettingsGroup>
    </div>
  );
}

export function DisplayTab({ formData, onChange, setField }) {
  return (
    <div className='space-y-4 sm:space-y-6'>
      <Section title='Display'>
        <SettingsGroup title='Screen and standby'>
          <div className={FIELD_GRID}>
            <SettingsFormField
              label='Main brightness (1-16)'
              htmlFor='mainBrightness'
              noMargin
              tooltip='Screen brightness dims the text and animation together, so it changes overall brightness but does not make animated backgrounds easier to read.'
              tooltipLabel='Main brightness'
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
            </SettingsFormField>
            <SettingsFormField label='Display theme' htmlFor='themeMode' noMargin>
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
            </SettingsFormField>
            <div className='md:col-span-2'>
              <ToggleField
                label='Enable standby display'
                htmlFor='standbyDisplayEnabled'
                checked={!!formData.standbyDisplayEnabled}
                onChange={onChange('standbyDisplayEnabled')}
              />
            </div>
            <SettingsFormField
              label='Standby brightness (0-16)'
              htmlFor='standbyBrightness'
              helpText='When standby display is off, brightness is saved as 0.'
              noMargin
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
            </SettingsFormField>
            <InputGroupField
              label='Standby brightness timeout'
              htmlFor='standbyBrightnessTimeout'
              unit='s'
              unitAriaLabel='seconds'
              noMargin
            >
              <input
                id='standbyBrightnessTimeout'
                name='standbyBrightnessTimeout'
                type='number'
                min='1'
                placeholder='60'
                value={formData.standbyBrightnessTimeout}
                onChange={onChange('standbyBrightnessTimeout')}
              />
            </InputGroupField>
          </div>
        </SettingsGroup>
      </Section>

      <Section title='Background Animation'>
        <BackgroundAnimationSettings formData={formData} onChange={onChange} setField={setField} />
      </Section>
    </div>
  );
}
