import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEye } from '@fortawesome/free-solid-svg-icons/faEye';
import { faEyeSlash } from '@fortawesome/free-solid-svg-icons/faEyeSlash';
import { timezones } from '../../../config/zones.js';
import {
  BG_ANIMATIONS,
  BG_THEMES,
  BG_THEME_CUSTOM,
  BG_THEME_MAX_STOPS,
  parseBgAnimParams,
  parseCustomTheme,
  serializeCustomTheme,
  setBgAnimParam,
  themeStopsFor,
} from '../../../config/bgAnimations.js';
import { DASHBOARD_LAYOUTS } from '../../../utils/dashboardManager.js';
import Section from '../../../components/Card.jsx';
import {
  InputGroupField,
  SettingsFormField,
  ToggleField,
} from '../../../components/SettingsFormField.jsx';

function ButtonBehaviorSelect({ id, label, value, onChange, profiles }) {
  return (
    <SettingsFormField label={label} htmlFor={id} noMargin >
      <select
        id={id}
        name={id}
        className='select select-bordered w-full'
        value={value}
        onChange={onChange}
      >
        <option value='none'>None</option>
        <option value='brew'>Brew button</option>
        <option value='steam'>Steam button</option>
        <option value='water'>Water button</option>
        <option value='flush'>Flush</option>
        {profiles.map(p => (
          <option key={p.id} value={p.id}>
            Profile: {p.label}
          </option>
        ))}
      </select>
    </SettingsFormField>
  );
}

// Global color theme picker: built-in gradient themes plus a custom editor
// (2-8 hex stops, dark -> bright). The selected theme colors every animation.
function ColorThemeSettings({ formData, onChange, setField }) {
  const themeId = Math.min(
    BG_THEME_CUSTOM,
    Math.max(0, parseInt(formData.bgAnimTheme, 10) || 0),
  );
  const isCustom = themeId === BG_THEME_CUSTOM;
  const customStops = parseCustomTheme(formData.bgAnimCustomTheme);
  const editStops = customStops.length >= 2 ? customStops : BG_THEMES[0].stops.slice();
  const previewStops = themeStopsFor(themeId, formData.bgAnimCustomTheme);
  const setStops = stops => setField('bgAnimCustomTheme', serializeCustomTheme(stops));
  return (
    <>
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        <SettingsFormField label='Color theme' htmlFor='bgAnimTheme' noMargin>
          <select
            id='bgAnimTheme'
            name='bgAnimTheme'
            className='select select-bordered w-full'
            value={themeId}
            onChange={e => {
              setField('bgAnimTheme', parseInt(e.target.value, 10));
              // Seed the custom editor from the last built-in theme selected.
              if (parseInt(e.target.value, 10) === BG_THEME_CUSTOM && customStops.length < 2) {
                setStops(themeStopsFor(Math.min(themeId, BG_THEMES.length - 1), ''));
              }
            }}
          >
            {BG_THEMES.map((t, i) => (
              <option key={t.name} value={i}>
                {t.name}
              </option>
            ))}
            <option value={BG_THEME_CUSTOM}>Custom…</option>
          </select>
        </SettingsFormField>
        <SettingsFormField label='Preview' htmlFor='bgAnimThemePreview' noMargin>
          <div
            id='bgAnimThemePreview'
            className='border-base-content/10 h-10 w-full rounded-lg border'
            style={{ background: `linear-gradient(to right, ${previewStops.join(', ')})` }}
          />
        </SettingsFormField>
      </div>
      {isCustom && (
        <div className='mt-3 flex flex-wrap items-center gap-2'>
          {editStops.map((stop, i) => (
            <input
              key={i}
              type='color'
              className='h-10 w-12 cursor-pointer rounded border-0 bg-transparent p-0'
              value={stop}
              aria-label={`Custom stop ${i + 1}`}
              onChange={e => {
                const next = editStops.slice();
                next[i] = e.target.value;
                setStops(next);
              }}
            />
          ))}
          <button
            type='button'
            className='btn btn-sm'
            disabled={editStops.length >= BG_THEME_MAX_STOPS}
            onClick={() => setStops([...editStops, editStops[editStops.length - 1]])}
          >
            + Stop
          </button>
          <button
            type='button'
            className='btn btn-sm'
            disabled={editStops.length <= 2}
            onClick={() => setStops(editStops.slice(0, -1))}
          >
            − Stop
          </button>
          <span className='text-base-content/60 text-sm'>dark → bright</span>
        </div>
      )}
    </>
  );
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
    <div className='border-base-content/5 mt-6 border-t pt-6'>
      <h3 className='text-md text-base-content mb-2 font-semibold'>Background Animation</h3>
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
                value={Math.min(param.options.length - 1, Math.floor((values[j] * param.options.length) / 101))}
                onChange={e =>
                  setField(
                    'bgAnimParams',
                    setBgAnimParam(
                      formData.bgAnimParams,
                      animIdx,
                      j,
                      // Store the option index scaled back onto 0-100.
                      Math.round((parseInt(e.target.value, 10) * 100) / Math.max(1, param.options.length - 1)),
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
      {anim.description && (
        <p className='text-base-content/60 mt-2 text-sm'>{anim.description}</p>
      )}
      <div className='mt-4'>
        <ColorThemeSettings formData={formData} onChange={onChange} setField={setField} />
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
        <SettingsFormField label='Panel refresh rate' htmlFor='panelClockDiv' noMargin>
          <select
            id='panelClockDiv'
            name='panelClockDiv'
            className='select select-bordered w-full'
            value={parseInt(formData.panelClockDiv, 10) || 0}
            onChange={onChange('panelClockDiv')}
          >
            <option value={0}>Firmware default</option>
            <option value={5}>61 Hz, smoothest, flickers if starved</option>
            <option value={6}>51 Hz, stable, slight gradient shimmer</option>
            <option value={7}>43 Hz, conservative</option>
            <option value={8}>38 Hz, most conservative</option>
          </select>
        </SettingsFormField>
        <SettingsFormField label='Animation resolution' htmlFor='bgAnimHalfRes' noMargin>
          <select
            id='bgAnimHalfRes'
            name='bgAnimHalfRes'
            className='select select-bordered w-full'
            value={formData.bgAnimHalfRes === undefined ? 1 : parseInt(formData.bgAnimHalfRes, 10)}
            onChange={onChange('bgAnimHalfRes')}
          >
            <option value={1}>Half (240x240, doubled), every animation at 40+ fps</option>
            <option value={0}>Full (480x480), sharper, 15 to 25 fps on the heavy ones</option>
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
            <option value={1}>On, refreshes half the rows each frame (roughly doubles the rate)</option>
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
      </div>
      <p className='text-base-content/60 mt-2 text-sm'>
        If the animation flickers or the image jumps, lower the frame rate or the refresh rate:
        both compete for the same memory bandwidth. Changes apply live after saving.
      </p>
      <p className='text-base-content/60 mt-2 text-sm'>
        Resolution and interlacing trade sharpness for smoothness. Half resolution plus
        interlacing is what lets every animation run above 40 fps; full resolution is visibly
        sharper but the heaviest animations drop to around 15 fps. Interlacing refreshes half
        the rows on each frame, which is not usually noticeable while something is moving.
      </p>
      <p className='text-base-content/60 mt-2 text-sm'>
        The brew, status and profile screens carry a solid circle behind their dials, the info
        screen a solid panel, and the brew and grind screens a filled pill behind the scale
        weight, while the other screens carry none. With the animation running behind every
        screen those show up as dark shapes on some screens and not others, so by default they
        are hidden while the animation plays. The pill behind the weight is also the mode
        switch, so if you want it to still read as a button, pick the custom option and give it
        a low opacity rather than hiding it outright.
      </p>
      <div className='mt-4'>
        <ToggleField
          label='Show animation behind all screens (not just standby)'
          htmlFor='bgAnimAllScreens'
          checked={!!formData.bgAnimAllScreens}
          onChange={onChange('bgAnimAllScreens')}
        />
      </div>
    </div>
  );
}

function PasswordField({ id, label, placeholder, value, onChange, shown, setShown, ...rest }) {
  return (
    <label className='input w-full'>
      <input
        id={id}
        name={id}
        type={shown ? 'text' : 'password'}
        placeholder={placeholder ?? label}
        value={value}
        onChange={onChange}
        {...rest}
      />
      <button
        type='button'
        className='hover:text-primary cursor-pointer focus:outline-none'
        aria-label='Show Password'
        onClick={() => setShown(!shown)}
      >
        <FontAwesomeIcon icon={shown ? faEyeSlash : faEye} />
      </button>
    </label>
  );
}

export function GeneralTab({
  formData,
  onChange,
  setField,
  profiles,
  currentTheme,
  setCurrentTheme,
  handleThemeChange,
  showWifiPassword,
  setShowWifiPassword,
  showApPassword,
  setShowApPassword,
}) {
  return (
    <div className='space-y-4 sm:space-y-6 lg:grid lg:grid-cols-2 lg:gap-4'>
      {/* User Preferences */}
      <Section title='User Preferences' className='h-full' >
        <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
          <SettingsFormField label='Startup Mode' htmlFor='startup-mode' noMargin>
            <select
              id='startup-mode'
              name='startupMode'
              className='select select-bordered w-full'
              onChange={onChange('startupMode')}
            >
              <option value='standby' selected={formData.startupMode === 'standby'}>
                Standby
              </option>
              <option value='brew' selected={formData.startupMode === 'brew'}>
                Brew
              </option>
            </select>
          </SettingsFormField>
          <SettingsFormField label='Startup Profile' htmlFor='startup-profile' noMargin>
            <select
              id='startup-profile'
              name='startupProfile'
              className='select select-bordered w-full'
              value={formData.startupProfile || ''}
              onChange={onChange('startupProfile')}
            >
              <option value=''>Last used profile</option>
              {profiles.map(profile => (
                <option key={profile.id} value={profile.id}>
                  {profile.label}
                </option>
              ))}
            </select>
          </SettingsFormField>
          <InputGroupField
            label='Standby Timeout'
            htmlFor='standbyTimeout'
            unit='s'
            unitAriaLabel='seconds'
            noMargin
          >
            <input
              id='standbyTimeout'
              name='standbyTimeout'
              type='number'
              placeholder='0'
              value={formData.standbyTimeout}
              onChange={onChange('standbyTimeout')}
            />
          </InputGroupField>
        </div>

        {/* Predictive Scale Delay */}
        <div className='border-base-content/5 mt-6 border-t pt-6'>
          <h3 className='text-md text-base-content mb-2 font-semibold'>Predictive Scale Delay</h3>
          <p className='text-base-content/85 mb-4 text-sm opacity-70'>
            Shuts off the process ahead of time based on the flow rate to account for any dripping
            or delays in the control.
          </p>
          <div className='mb-4'>
            <ToggleField
              label='Auto Adjust'
              htmlFor='delayAdjust'
              checked={!!formData.delayAdjust}
              onChange={onChange('delayAdjust')}
            />
          </div>
          <div className='grid grid-cols-1 gap-4 sm:grid-cols-2'>
            <InputGroupField
              label='Brew'
              htmlFor='brewDelay'
              unit='ms'
              unitAriaLabel='milliseconds'
              noMargin
            >
              <input
                id='brewDelay'
                name='brewDelay'
                type='number'
                step='any'
                className='grow'
                placeholder='0'
                value={formData.brewDelay}
                onChange={onChange('brewDelay')}
              />
            </InputGroupField>
            <InputGroupField
              label='Grind'
              htmlFor='grindDelay'
              unit='ms'
              unitAriaLabel='milliseconds'
              noMargin
            >
              <input
                id='grindDelay'
                name='grindDelay'
                type='number'
                step='any'
                className='grow'
                placeholder='0'
                value={formData.grindDelay}
                onChange={onChange('grindDelay')}
              />
            </InputGroupField>
          </div>
        </div>

        {/* Buttons */}
        <div className='border-base-content/5 mt-6 border-t pt-6'>
          <h3 className='text-md text-base-content mb-2 font-semibold'>Physical Buttons</h3>
          <p className='text-base-content/85 mb-4 text-sm opacity-70'>
            Define behavior for physical buttons when pressed. Make sure they are wired to the Alt
            Relay Header.
          </p>
          <div className='mb-4'>
            <ToggleField
              label='Momentary Buttons'
              htmlFor='momentaryButtons'
              checked={!!formData.momentaryButtons}
              onChange={onChange('momentaryButtons')}
            />
          </div>
          <div className='grid grid-cols-1 gap-4 md:grid-cols-3'>
            <ButtonBehaviorSelect
              id='button0'
              label='Brew Button Behavior'
              value={formData.button0}
              onChange={onChange('button0')}
              profiles={profiles}
            />
            <ButtonBehaviorSelect
              id='button1'
              label='Steam Button Behavior'
              value={formData.button1}
              onChange={onChange('button1')}
              profiles={profiles}
            />
            <ButtonBehaviorSelect
              id='button2'
              label='Water Button Behavior'
              value={formData.button2}
              onChange={onChange('button2')}
              profiles={profiles}
            />
          </div>
        </div>
      </Section>

      {/* Display Settings */}
      <Section title='Display Settings' className='h-full' >
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

        <BackgroundAnimationSettings formData={formData} onChange={onChange} setField={setField} />
      </Section>

      {/* Web Settings */}
      <Section title='Web Settings' className='h-full' >
        <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
          <SettingsFormField label='Theme' htmlFor='webui-theme' noMargin>
            <select
              id='webui-theme'
              name='webui-theme'
              className='select select-bordered w-full'
              value={currentTheme}
              onChange={e => {
                setCurrentTheme(e.target.value);
                handleThemeChange(e);
              }}
            >
              <option value='system'>System</option>
              <option value='light'>Light</option>
              <option value='dark'>Dark</option>
              <option value='coffee'>Coffee</option>
              <option value='nord'>Nord</option>
            </select>
          </SettingsFormField>
          <SettingsFormField label='Dashboard Layout' htmlFor='dashboardLayout' noMargin>
            <select
              id='dashboardLayout'
              name='dashboardLayout'
              className='select select-bordered w-full'
              value={formData.dashboardLayout || DASHBOARD_LAYOUTS.ORDER_FIRST}
              onChange={e => {
                onChange('dashboardLayout')(e);
              }}
            >
              <option value={DASHBOARD_LAYOUTS.ORDER_FIRST}>Process Controls First</option>
              <option value={DASHBOARD_LAYOUTS.ORDER_LAST}>Chart First</option>
            </select>
          </SettingsFormField>
        </div>
      </Section>

      {/* Network / System Preferences */}
      <Section title='System & Network' className='h-full' >
        <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
          <SettingsFormField label='Wi-Fi SSID' htmlFor='wifiSsid' noMargin>
            <input
              id='wifiSsid'
              name='wifiSsid'
              type='text'
              className='input input-bordered w-full'
              placeholder='Wi-Fi SSID'
              value={formData.wifiSsid}
              onChange={onChange('wifiSsid')}
            />
          </SettingsFormField>
          <SettingsFormField label='Wi-Fi Password' htmlFor='wifiPassword' noMargin>
            <PasswordField
              id='wifiPassword'
              label='Wi-Fi Password'
              value={formData.wifiPassword}
              onChange={onChange('wifiPassword')}
              shown={showWifiPassword}
              setShown={setShowWifiPassword}
            />
          </SettingsFormField>
          <SettingsFormField
            label='Access Point Password'
            htmlFor='apPassword'
            helpText='Used for the GaggiMate hotspot when no Wi-Fi is configured (min. 8 characters).'
            noMargin
          >
            <PasswordField
              id='apPassword'
              label='Access Point Password'
              minLength={8}
              maxLength={63}
              value={formData.apPassword}
              onChange={onChange('apPassword')}
              shown={showApPassword}
              setShown={setShowApPassword}
            />
          </SettingsFormField>
          <SettingsFormField label='Hostname' htmlFor='mdnsName' noMargin>
            <input
              id='mdnsName'
              name='mdnsName'
              type='text'
              className='input input-bordered w-full'
              placeholder='Hostname'
              value={formData.mdnsName}
              onChange={onChange('mdnsName')}
            />
          </SettingsFormField>
          <SettingsFormField label='Time Zone' htmlFor='timezone' noMargin>
            <select
              id='timezone'
              name='timezone'
              className='select select-bordered w-full'
              onChange={onChange('timezone')}
            >
              {timezones.map(tz => (
                <option key={tz} value={tz} selected={formData.timezone === tz}>
                  {tz}
                </option>
              ))}
            </select>
          </SettingsFormField>
        </div>

        {/* Clock */}
        <div className='border-base-content/5 mt-6 border-t pt-6'>
          <ToggleField
            label='Use 24h Format'
            htmlFor='clock24hFormat'
            checked={!!formData.clock24hFormat}
            onChange={onChange('clock24hFormat')}
          />
        </div>
      </Section>
    </div>
  );
}
