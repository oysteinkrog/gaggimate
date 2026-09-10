import { BG_ANIMATIONS, parseBgAnimParams, setBgAnimParam } from '../../../config/bgAnimations.js';
import Section from '../../../components/Card.jsx';
import { GradientEditor } from '../../../components/GradientEditor.jsx';
import {
  InputGroupField,
  SettingsFormField,
  ToggleField,
} from '../../../components/SettingsFormField.jsx';

// The standby animation selector's value. -1 is "same as main", which is
// also what an id past the end of this build's roster means: the firmware
// falls back to the main animation for one of those rather than indexing off
// the end of the registry, so the form shows the same thing it does.
function standbyAnimValue(stored) {
  const id = parseInt(stored, 10);
  if (!Number.isFinite(id) || id < 0 || id >= BG_ANIMATIONS.length) return -1;
  return id;
}

// Animation picker + parameter sliders for the selected animation only.
// Params live in formData.bgAnimParams as the same packed string the firmware
// stores ("p0,p1,p2,p3;..." indexed by animation id) — edited via setField.
function BackgroundAnimationSettings({ formData, onChange, setField }) {
  const animIdx = Math.min(
    BG_ANIMATIONS.length - 1,
    Math.max(0, parseInt(formData.bgAnimId, 10) || 0),
  );
  const anim = BG_ANIMATIONS[animIdx];
  const values = parseBgAnimParams(formData.bgAnimParams)[animIdx];
  return (
    <div>
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        <SettingsFormField label='Animation' htmlFor='bgAnimId' noMargin>
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
        {anim.params.map((param, j) =>
          param.options ? (
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
                  Math.floor((values[j] * param.options.length) / 101),
                )}
                onChange={e =>
                  setField(
                    'bgAnimParams',
                    setBgAnimParam(
                      formData.bgAnimParams,
                      animIdx,
                      j,
                      // Store the option index scaled back onto 0-100.
                      Math.round(
                        (parseInt(e.target.value, 10) * 100) /
                          Math.max(1, param.options.length - 1),
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
          ) : (
            <SettingsFormField
              key={`${anim.id}-${param.key}`}
              label={`${param.label} (${values[j]})`}
              htmlFor={`bgAnim-${param.key}`}
              noMargin
            >
              <input
                id={`bgAnim-${param.key}`}
                type='range'
                min='0'
                max='100'
                className='range w-full'
                value={values[j]}
                onChange={e =>
                  setField(
                    'bgAnimParams',
                    setBgAnimParam(formData.bgAnimParams, animIdx, j, e.target.value),
                  )
                }
              />
            </SettingsFormField>
          ),
        )}
      </div>
      {anim.description && <p className='text-base-content/60 mt-2 text-sm'>{anim.description}</p>}
      <div className='mt-4'>
        <GradientEditor animIdx={animIdx} formData={formData} setField={setField} />
      </div>
      <div className='mt-4 grid grid-cols-1 gap-4 md:grid-cols-2'>
        <SettingsFormField
          label={`Animation frame rate (${parseInt(formData.bgAnimFps, 10) || 30} fps)`}
          htmlFor='bgAnimFps'
          noMargin
        >
          <input
            id='bgAnimFps'
            name='bgAnimFps'
            type='range'
            min='5'
            max='60'
            className='range w-full'
            value={parseInt(formData.bgAnimFps, 10) || 30}
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
              : 'How often the panel scans out. The animation has its own frame rate (the slider) and the widgets refresh when they change.'
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
        </SettingsFormField>
        <SettingsFormField
          label={`Panel VCOM (${formData.panelVcom === undefined ? 45 : parseInt(formData.panelVcom, 10)}, ${(
            0.1 +
            (formData.panelVcom === undefined ? 45 : parseInt(formData.panelVcom, 10)) * 0.0125
          ).toFixed(2)} V)`}
          htmlFor='panelVcom'
          noMargin
          helpText='Trim this only if you see a faint shimmer on large flat areas. Move it a few steps at a time and stop where the shimmer is weakest. The right value differs per panel and drifts while the display warms up, so judge it on a cold screen. 45 is what the panel ships with.'
        >
          <input
            id='panelVcom'
            name='panelVcom'
            type='range'
            min='20'
            max='100'
            className='range w-full'
            value={formData.panelVcom === undefined ? 45 : parseInt(formData.panelVcom, 10)}
            onChange={onChange('panelVcom')}
          />
        </SettingsFormField>
        <SettingsFormField
          label='Animation resolution'
          htmlFor='bgAnimHalfRes'
          noMargin
          helpText='Half resolution does not raise the frame rate: doubling the rows costs what the smaller render saves (measured 2026-09-05). Interlace is the setting that does.'
        >
          <select
            id='bgAnimHalfRes'
            name='bgAnimHalfRes'
            className='select select-bordered w-full'
            value={formData.bgAnimHalfRes === undefined ? 1 : parseInt(formData.bgAnimHalfRes, 10)}
            onChange={onChange('bgAnimHalfRes')}
          >
            <option value={1}>Half (240x240, doubled on the way out)</option>
            <option value={0}>Full (480x480), sharper</option>
          </select>
        </SettingsFormField>
        <SettingsFormField label='Interlace animation' htmlFor='bgAnimInterlace' noMargin>
          <select
            id='bgAnimInterlace'
            name='bgAnimInterlace'
            className='select select-bordered w-full'
            value={
              formData.bgAnimInterlace === undefined ? 1 : parseInt(formData.bgAnimInterlace, 10)
            }
            onChange={onChange('bgAnimInterlace')}
          >
            <option value={1}>
              On, refreshes half the rows each frame (roughly doubles the rate)
            </option>
            <option value={0}>Off, every row every frame</option>
          </select>
        </SettingsFormField>
        <SettingsFormField label='Screen background panels' htmlFor='bgAnimClearPlates' noMargin>
          <select
            id='bgAnimClearPlates'
            name='bgAnimClearPlates'
            className='select select-bordered w-full'
            value={
              formData.bgAnimClearPlates === undefined
                ? 1
                : parseInt(formData.bgAnimClearPlates, 10)
            }
            onChange={onChange('bgAnimClearPlates')}
          >
            <option value={1}>Hide, animation fills every screen the same way</option>
            <option value={0}>Keep, solid panel behind the dials on some screens</option>
            <option value={2}>Custom colour and transparency</option>
          </select>
        </SettingsFormField>
        {parseInt(formData.bgAnimClearPlates, 10) === 2 && (
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
              label={`Panel opacity (${
                formData.bgAnimPlateOpacity === undefined
                  ? 35
                  : parseInt(formData.bgAnimPlateOpacity, 10)
              }%)`}
              htmlFor='bgAnimPlateOpacity'
              noMargin
            >
              <input
                id='bgAnimPlateOpacity'
                name='bgAnimPlateOpacity'
                type='range'
                min='0'
                max='100'
                className='range w-full'
                value={
                  formData.bgAnimPlateOpacity === undefined
                    ? 35
                    : parseInt(formData.bgAnimPlateOpacity, 10)
                }
                onChange={onChange('bgAnimPlateOpacity')}
              />
            </SettingsFormField>
          </>
        )}
        <SettingsFormField
          label={`Text backdrop (${
            formData.bgAnimScrim === undefined ? 0 : parseInt(formData.bgAnimScrim, 10)
          }%)`}
          htmlFor='bgAnimScrim'
          noMargin
        >
          <input
            id='bgAnimScrim'
            name='bgAnimScrim'
            type='range'
            min='0'
            max='100'
            className='range w-full'
            value={formData.bgAnimScrim === undefined ? 0 : parseInt(formData.bgAnimScrim, 10)}
            onChange={onChange('bgAnimScrim')}
          />
        </SettingsFormField>
        <SettingsFormField
          label={`Screen fade out (${
            formData.bgFadeOutMs === undefined ? 120 : parseInt(formData.bgFadeOutMs, 10)
          } ms)`}
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
            value={formData.bgFadeOutMs === undefined ? 120 : parseInt(formData.bgFadeOutMs, 10)}
            onChange={onChange('bgFadeOutMs')}
          />
        </SettingsFormField>
        <SettingsFormField
          label={`Screen fade in (${
            formData.bgFadeInMs === undefined ? 120 : parseInt(formData.bgFadeInMs, 10)
          } ms)`}
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
            value={formData.bgFadeInMs === undefined ? 120 : parseInt(formData.bgFadeInMs, 10)}
            onChange={onChange('bgFadeInMs')}
          />
        </SettingsFormField>
        <SettingsFormField label='Screen fade curve' htmlFor='bgFadeCurve' noMargin>
          <select
            id='bgFadeCurve'
            name='bgFadeCurve'
            className='select select-bordered w-full'
            value={formData.bgFadeCurve === undefined ? 0 : parseInt(formData.bgFadeCurve, 10)}
            onChange={onChange('bgFadeCurve')}
          >
            <option value={0}>Linear, constant speed</option>
            <option value={1}>Smooth, eases in and out</option>
          </select>
        </SettingsFormField>
        <SettingsFormField
          label={`Animation brightness (${
            formData.bgAnimBrightness === undefined ? 100 : parseInt(formData.bgAnimBrightness, 10)
          }%)`}
          htmlFor='bgAnimBrightness'
          noMargin
        >
          <input
            id='bgAnimBrightness'
            name='bgAnimBrightness'
            type='range'
            min='10'
            max='100'
            className='range w-full'
            value={
              formData.bgAnimBrightness === undefined
                ? 100
                : parseInt(formData.bgAnimBrightness, 10)
            }
            onChange={onChange('bgAnimBrightness')}
          />
        </SettingsFormField>
        <SettingsFormField
          label={`Highlight rolloff (${
            formData.bgAnimHighlightKnee === undefined
              ? 100
              : parseInt(formData.bgAnimHighlightKnee, 10)
          }%)`}
          htmlFor='bgAnimHighlightKnee'
          noMargin
        >
          <input
            id='bgAnimHighlightKnee'
            name='bgAnimHighlightKnee'
            type='range'
            min='20'
            max='100'
            className='range w-full'
            value={
              formData.bgAnimHighlightKnee === undefined
                ? 100
                : parseInt(formData.bgAnimHighlightKnee, 10)
            }
            onChange={onChange('bgAnimHighlightKnee')}
          />
        </SettingsFormField>
      </div>
      <p className='text-base-content/60 mt-2 text-sm'>
        If the animation flickers or the image jumps, lower the frame rate or the refresh rate: both
        compete for the same memory bandwidth. Changes apply live after saving.
      </p>
      <p className='text-base-content/60 mt-2 text-sm'>
        Resolution and interlacing trade sharpness for smoothness. Half resolution plus interlacing
        is what lets every animation run above 40 fps; full resolution is visibly sharper but the
        heaviest animations drop to around 15 fps. Interlacing refreshes half the rows on each
        frame, which is not usually noticeable while something is moving.
      </p>
      <p className='text-base-content/60 mt-2 text-sm'>
        The brew, status and profile screens carry a solid circle behind their dials, the info
        screen a solid panel, and the brew and grind screens a filled pill behind the scale weight,
        while the other screens carry none. With the animation running behind every screen those
        show up as dark shapes on some screens and not others, so by default they are hidden while
        the animation plays. The pill behind the weight is also the mode switch, so if you want it
        to still read as a button, pick the custom option and give it a low opacity rather than
        hiding it outright.
      </p>
      <p className='text-base-content/60 mt-2 text-sm'>
        The last three settings are about reading the screen while an animation plays behind it. The
        text backdrop dims the animation in a soft halo directly behind the numbers and labels and
        leaves the rest of the frame alone, which is why it is on by default. 55% is the lightest
        setting that keeps white text comfortable to read on every animation and every theme,
        including the brightest; stronger settings work but start to look like a dark plate.
        Animation brightness and highlight rolloff change how the animation itself looks everywhere.
        Brightness scales the whole theme down, rolloff compresses only the brightest parts and
        leaves the mid tones their colour, so rolloff is usually the better one to reach for if a
        theme looks blown out. Neither is needed for legibility with the backdrop on; they are there
        for taste. Note that these are separate from screen brightness, which dims the text along
        with the animation and so does not make anything easier to read.
      </p>
      <div className='mt-4'>
        <ToggleField
          label='Show animation behind all screens (not just standby)'
          htmlFor='bgAnimAllScreens'
          checked={!!formData.bgAnimAllScreens}
          onChange={onChange('bgAnimAllScreens')}
        />
      </div>
      <div className='mt-4'>
        <SettingsFormField
          label='Standby screen animation'
          htmlFor='bgAnimStandbyId'
          noMargin
          helpText='The standby screen can play a different animation from the rest of the display. It keeps its own parameters and its own gradient. With the animation shown behind all screens turned off, only this one is ever seen.'
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
      </div>
      <div className='mt-4'>
        <ToggleField
          label='Custom element tint (icons and accent colour on the display)'
          htmlFor='elementTintEnabled'
          checked={!!formData.elementTintEnabled}
          onChange={onChange('elementTintEnabled')}
        />
      </div>
      {!!formData.elementTintEnabled && (
        <div className='mt-4'>
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
        </div>
      )}
      <div className='mt-4'>
        <SettingsFormField
          label='Touch feedback colour (pressed elements shift toward this)'
          htmlFor='touchDimColor'
          noMargin
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
    </div>
  );
}

export function DisplayTab({ formData, onChange, setField }) {
  return (
    <div className='space-y-4 sm:space-y-6'>
      <Section title='Display'>
        <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
          <SettingsFormField label='Main Brightness (1-16)' htmlFor='mainBrightness' noMargin>
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
          <SettingsFormField label='Display Theme' htmlFor='themeMode' noMargin>
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
        </div>

        {/* Standby Display */}
        <div className='border-base-content/5 mt-6 border-t pt-6'>
          <div className='mb-4'>
            <ToggleField
              label='Enable standby display'
              htmlFor='standbyDisplayEnabled'
              checked={!!formData.standbyDisplayEnabled}
              onChange={onChange('standbyDisplayEnabled')}
            />
          </div>
          <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
            <SettingsFormField
              label='Standby Brightness (0-16)'
              htmlFor='standbyBrightness'
              helpText='When the toggle is off, brightness will be set to 0'
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
              label='Standby Brightness Timeout'
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
        </div>
      </Section>

      <Section title='Background Animation'>
        <BackgroundAnimationSettings formData={formData} onChange={onChange} setField={setField} />
      </Section>
    </div>
  );
}
