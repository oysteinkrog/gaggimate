import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEye } from '@fortawesome/free-solid-svg-icons/faEye';
import { faEyeSlash } from '@fortawesome/free-solid-svg-icons/faEyeSlash';
import { timezones } from '../../../config/zones.js';
import {
  BG_ANIMATIONS,
  parseBgAnimParams,
  setBgAnimParam,
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
