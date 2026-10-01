import { faFileExport } from '@fortawesome/free-solid-svg-icons/faFileExport';
import { faFileImport } from '@fortawesome/free-solid-svg-icons/faFileImport';
import { faEllipsisVertical } from '@fortawesome/free-solid-svg-icons/faEllipsisVertical';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { useCallback, useEffect, useRef, useState, useContext } from 'preact/hooks';
import { useRoute } from 'preact-iso';
import {
  ApiServiceContext,
  machine,
  prefetchSettings,
  updateSettingsCache,
  getCachedSettings,
} from '../../services/ApiService.js';
import {
  DASHBOARD_LAYOUTS,
  setDashboardLayout,
  setClock24h,
} from '../../utils/dashboardManager.js';
import { downloadJson } from '../../utils/download.js';
import { getStoredTheme, handleThemeChange } from '../../utils/themeManager.js';

import PageLayout from '../../components/PageLayout.jsx';
import PageHeader from '../../components/PageHeader.jsx';
import TabBar from '../../components/TabBar.jsx';

import lazy from 'preact-iso/lazy';

import { StickyFormFooter } from './StickyFormFooter.jsx';
import {
  GeneralTabSkeleton,
  DisplayTabSkeleton,
  MachineTabSkeleton,
  PluginsTabSkeleton,
  BluetoothTabSkeleton,
  SystemTabSkeleton,
} from '../../components/skeletons/SettingsSkeletons.jsx';
import { GeneralTab } from './tabs/GeneralTab.jsx';

const LazyDisplayTab = lazy(() => import('./tabs/DisplayTab.jsx').then(m => m.DisplayTab));
const LazyMachineTab = lazy(() => import('./tabs/MachineTab.jsx').then(m => m.MachineTab));
const LazyCalibrationTab = lazy(() =>
  import('./tabs/CalibrationTab.jsx').then(m => m.CalibrationTab),
);
const LazyPluginsTab = lazy(() => import('./tabs/PluginsTab.jsx').then(m => m.PluginsTab));
const LazyBluetoothTab = lazy(() => import('./tabs/BluetoothTab.jsx').then(m => m.BluetoothTab));
const LazySystemTab = lazy(() => import('./tabs/SystemTab.jsx').then(m => m.SystemTab));

const loadDisplayTab = () => import('./tabs/DisplayTab.jsx');
const loadMachineTab = () => import('./tabs/MachineTab.jsx');
const loadCalibrationTab = () => import('./tabs/CalibrationTab.jsx');
const loadPluginsTab = () => import('./tabs/PluginsTab.jsx');
const loadBluetoothTab = () => import('./tabs/BluetoothTab.jsx');
const loadSystemTab = () => import('./tabs/SystemTab.jsx');

// Icons
import { faSliders } from '@fortawesome/free-solid-svg-icons/faSliders';
import { faDisplay } from '@fortawesome/free-solid-svg-icons/faDisplay';
import { faTemperatureHalf } from '@fortawesome/free-solid-svg-icons/faTemperatureHalf';
import { faCrosshairs } from '@fortawesome/free-solid-svg-icons/faCrosshairs';
import { faPuzzlePiece } from '@fortawesome/free-solid-svg-icons/faPuzzlePiece';
import { faBluetoothB } from '@fortawesome/free-brands-svg-icons/faBluetoothB';
import { faRotate } from '@fortawesome/free-solid-svg-icons/faRotate';

const DEFAULT_SCALE_FACTOR_1 = -2500.0;
const DEFAULT_SCALE_FACTOR_2 = 2500.0;
const DEFAULT_HARDWARE_SCALE_SAMPLE_RATE_SPS = 10;
const DEFAULT_HARDWARE_SCALE_FILTER_ALPHA = 0.8;

function splitPidString(pidString) {
  if (!pidString) return { pid: pidString, kf: '0.000' };
  const parts = pidString.split(',');
  if (parts.length >= 4) {
    return { pid: parts.slice(0, 3).join(','), kf: parts[3] };
  }
  return { pid: pidString, kf: '0.000' };
}

function splitButtons(buttonBehavior) {
  if (!buttonBehavior) return {};
  const [button0, button1, button2] = buttonBehavior.split(',');
  return { button0, button1, button2 };
}

function parseAutoWakeupSchedules(autowakeupSchedules) {
  const defaultSchedule = [{ time: '07:00', days: [true, true, true, true, true, true, true] }];
  if (!autowakeupSchedules) {
    return defaultSchedule;
  }
  const schedules = [];
  if (typeof autowakeupSchedules === 'string' && autowakeupSchedules.trim()) {
    const scheduleStrings = autowakeupSchedules.split(';');
    for (const scheduleStr of scheduleStrings) {
      const [time, daysStr] = scheduleStr.split('|');
      if (time && daysStr && daysStr.length === 7) {
        const days = daysStr.split('').map(d => d === '1');
        schedules.push({ time, days });
      }
    }
  }
  return schedules.length > 0 ? schedules : defaultSchedule;
}

function transformFetchedSettings(fetchedSettings) {
  if (!fetchedSettings) return {};
  const buttonFields = fetchedSettings.buttonBehavior
    ? splitButtons(fetchedSettings.buttonBehavior)
    : {};
  const settingsWithToggle = {
    ...fetchedSettings,
    ...buttonFields,
    standbyDisplayEnabled:
      fetchedSettings.standbyDisplayEnabled !== undefined
        ? fetchedSettings.standbyDisplayEnabled
        : fetchedSettings.standbyBrightness > 0,
    dashboardLayout: fetchedSettings.dashboardLayout || DASHBOARD_LAYOUTS.ORDER_FIRST,
  };

  const sf1 = Number(fetchedSettings.scaleFactor1);
  const sf2 = Number(fetchedSettings.scaleFactor2);
  settingsWithToggle.scaleFactor1 =
    Number.isFinite(sf1) && Math.abs(sf1) > 0.001 ? sf1 : DEFAULT_SCALE_FACTOR_1;
  settingsWithToggle.scaleFactor2 =
    Number.isFinite(sf2) && Math.abs(sf2) > 0.001 ? sf2 : DEFAULT_SCALE_FACTOR_2;
  // The factors as loaded from the device, kept beside the editable drafts so
  // the load-cell calibration can tell a pending draft from the value the
  // device measures with (MachineTab). Not submitted: see buildSubmitFormData.
  settingsWithToggle.scaleFactor1Loaded = settingsWithToggle.scaleFactor1;
  settingsWithToggle.scaleFactor2Loaded = settingsWithToggle.scaleFactor2;
  const sampleRate = Number(fetchedSettings.hardwareScaleSampleRateSps);
  settingsWithToggle.hardwareScaleSampleRateSps =
    sampleRate === 80 ? 80 : DEFAULT_HARDWARE_SCALE_SAMPLE_RATE_SPS;
  const idleAlpha = Number(fetchedSettings.hardwareScaleIdleAlpha);
  settingsWithToggle.hardwareScaleIdleAlpha =
    Number.isFinite(idleAlpha) && idleAlpha > 0 && idleAlpha <= 1
      ? idleAlpha
      : DEFAULT_HARDWARE_SCALE_FILTER_ALPHA;
  const activeAlpha = Number(fetchedSettings.hardwareScaleActiveAlpha);
  settingsWithToggle.hardwareScaleActiveAlpha =
    Number.isFinite(activeAlpha) && activeAlpha > 0 && activeAlpha <= 1
      ? activeAlpha
      : DEFAULT_HARDWARE_SCALE_FILTER_ALPHA;

  if (fetchedSettings.pid) {
    const split = splitPidString(fetchedSettings.pid);
    settingsWithToggle.pid = split.pid;
    settingsWithToggle.kf = split.kf;
  }
  return settingsWithToggle;
}

// The device settings that are checkboxes. The form posts each as 0 or 1 and
// the device reads each only when it is present, so a partial POST leaves the
// rest as they were. onChange flips these on a click.
const CHECKBOX_KEYS = [
  'homekit',
  'boilerFillActive',
  'smartGrindActive',
  'scaleMenuButton',
  'homeAssistant',
  'momentaryButtons',
  'delayAdjust',
  'clock24hFormat',
  'autowakeupEnabled',
  'bgAnimAllScreens',
  'elementTintEnabled',
];

// Plain labels for the fields the device can refuse on save (WebUIPlugin.cpp's
// settings handler answers 422 with {error, code: 'invalid_fields', fields:
// [...]} naming the posted keys that failed validation; every other field in
// the same save is still stored). Matches the label the form shows for a key
// where one exists; a key with no entry here is shown as is.
const FIELD_LABELS = {
  apPassword: 'Access Point Password',
  bgAnimGradients: 'Custom gradients',
  bgAnimThemeMap: 'Animation colour assignments',
  autowakeupSchedules: 'Auto Wakeup Schedule',
};

function describeInvalidFields(fieldKeys) {
  const labels = fieldKeys.map(key => FIELD_LABELS[key] || key);
  if (labels.length === 1) {
    return (
      `"${labels[0]}" was refused and kept its old value. ` +
      `Every other change was saved. Fix it and save again.`
    );
  }
  return (
    `These fields were refused and kept their old values: ${labels.join(', ')}. ` +
    `Every other change was saved. Fix them and save again.`
  );
}

function buildSubmitFormData(formData, autowakeupSchedules, restart) {
  const formDataToSubmit = new FormData();

  for (const [key, value] of Object.entries(formData)) {
    if (value === undefined || value === null) continue;
    // Form-only bookkeeping, not a device setting.
    if (key === 'scaleFactor1Loaded' || key === 'scaleFactor2Loaded') continue;

    if (CHECKBOX_KEYS.includes(key)) {
      formDataToSubmit.set(key, value ? '1' : '0');
    } else {
      formDataToSubmit.set(key, String(value));
    }
  }

  formDataToSubmit.set('steamPumpPercentage', String(formData.steamPumpPercentage ?? 0));
  formDataToSubmit.set(
    'altRelayFunction',
    formData.altRelayFunction !== undefined ? String(formData.altRelayFunction) : '1',
  );
  formDataToSubmit.set(
    'buttonBehavior',
    `${formData.button0},${formData.button1},${formData.button2}`,
  );

  if (formData.pid && formData.kf !== undefined) {
    const combinedPid = `${formData.pid},${formData.kf}`;
    formDataToSubmit.set('pid', combinedPid);
  }

  const schedulesStr = autowakeupSchedules
    .map(schedule => `${schedule.time}|${schedule.days.map(d => (d ? '1' : '0')).join('')}`)
    .join(';');
  formDataToSubmit.set('autowakeupSchedules', schedulesStr);

  if (!formData.standbyDisplayEnabled) {
    formDataToSubmit.set('standbyBrightness', '0');
  }

  if (restart) {
    formDataToSubmit.append('restart', '1');
  }

  return formDataToSubmit;
}

export function Settings() {
  const apiService = useContext(ApiServiceContext);
  const { params } = useRoute();
  const tab = params.tab || 'general';
  const isFormTab = ['general', 'display', 'machine', 'plugins'].includes(tab);

  const [profiles, setProfiles] = useState([]);
  const [submitting, setSubmitting] = useState(false);
  // Set when the last save did not reach the device's flash (or did not
  // reach the device at all). The form keeps the edits so a retry resends
  // them. `restart` remembers which button was used, so the retry repeats it.
  const [saveError, setSaveError] = useState(null);
  const [formData, setFormData] = useState({});
  const [currentTheme, setCurrentTheme] = useState('light');
  const [showWifiPassword, setShowWifiPassword] = useState(false);
  const [showApPassword, setShowApPassword] = useState(false);
  const [autowakeupSchedules, setAutoWakeupSchedules] = useState([
    { time: '07:00', days: [true, true, true, true, true, true, true] },
  ]);

  const [fetchedSettings, setFetchedSettings] = useState(() => getCachedSettings());
  const [isLoading, setIsLoading] = useState(!fetchedSettings);

  useEffect(() => {
    if (!fetchedSettings) {
      prefetchSettings()
        .then(data => {
          setFetchedSettings(data);
          setIsLoading(false);
        })
        .catch(err => {
          console.error('Failed to prefetch settings:', err);
          setIsLoading(false);
        });
    }
  }, [fetchedSettings]);

  useEffect(() => {
    const loadProfiles = async () => {
      if (machine.value.connected) {
        const response = await apiService.request({ tp: 'req:profiles:list', minimal: true });
        setProfiles(response.profiles);
      }
    };
    loadProfiles();
  }, [machine.value.connected, apiService]);

  const formRef = useRef();
  const dropdownRef = useRef(null);
  const [dropdownOpen, setDropdownOpen] = useState(false);

  useEffect(() => {
    if (!dropdownOpen) return;

    const handleOutsideClick = event => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setDropdownOpen(false);
      }
    };

    document.addEventListener('click', handleOutsideClick);
    return () => document.removeEventListener('click', handleOutsideClick);
  }, [dropdownOpen]);

  useEffect(() => {
    if (fetchedSettings) {
      const settingsWithToggle = transformFetchedSettings(fetchedSettings);
      const parsedSchedules = parseAutoWakeupSchedules(fetchedSettings.autowakeupSchedules);
      setAutoWakeupSchedules(parsedSchedules);
      setClock24h(!!fetchedSettings.clock24hFormat);
      setFormData(settingsWithToggle);
    } else {
      setFormData({});
      setAutoWakeupSchedules([{ time: '07:00', days: [true, true, true, true, true, true, true] }]);
    }
  }, [fetchedSettings]);

  useEffect(() => {
    setCurrentTheme(getStoredTheme());
  }, []);

  const onChange = key => {
    return e => {
      let value = e.currentTarget.value;
      if (CHECKBOX_KEYS.includes(key)) {
        value = !formData[key];
      }
      if (key === 'clock24hFormat') {
        setClock24h(value);
      }
      if (key === 'standbyDisplayEnabled') {
        value = !formData.standbyDisplayEnabled;
        const newFormData = {
          ...formData,
          [key]: value,
        };
        if (!value) {
          newFormData.standbyBrightness = 0;
        }
        setFormData(newFormData);
        return;
      }
      if (key === 'dashboardLayout') {
        setDashboardLayout(value);
      }
      setFormData({
        ...formData,
        [key]: value,
      });
    };
  };

  const setField = useCallback((key, value) => {
    setFormData(prev => ({ ...prev, [key]: value }));
  }, []);

  const addAutoWakeupSchedule = () => {
    setAutoWakeupSchedules([
      ...autowakeupSchedules,
      {
        time: '07:00',
        days: [true, true, true, true, true, true, true],
      },
    ]);
  };

  const removeAutoWakeupSchedule = index => {
    if (autowakeupSchedules.length > 1) {
      const newSchedules = autowakeupSchedules.filter((_, i) => i !== index);
      setAutoWakeupSchedules(newSchedules);
    }
  };

  const updateAutoWakeupTime = (index, value) => {
    const newSchedules = [...autowakeupSchedules];
    newSchedules[index].time = value;
    setAutoWakeupSchedules(newSchedules);
  };

  const updateAutoWakeupDay = (scheduleIndex, dayIndex, enabled) => {
    const newSchedules = [...autowakeupSchedules];
    newSchedules[scheduleIndex].days[dayIndex] = enabled;
    setAutoWakeupSchedules(newSchedules);
  };

  const onSubmit = useCallback(
    async (e, restart = false) => {
      if (e) e.preventDefault();
      setSubmitting(true);
      const form = formRef.current;
      const formDataToSubmit = buildSubmitFormData(formData, autowakeupSchedules, restart);

      try {
        const response = await fetch(form.action, {
          method: 'post',
          body: formDataToSubmit,
        });
        const data = await response.json().catch(() => null);
        if (!response.ok || !data) {
          // The device answers {error, code} when it could not persist the
          // save. Keep formData as it is: those are the unsaved edits. A
          // 422 with code 'invalid_fields' means the rest of the save was
          // stored and only the listed fields were refused; name them.
          const invalidFields =
            data && data.code === 'invalid_fields' && Array.isArray(data.fields)
              ? data.fields
              : null;
          const hasInvalidFields = invalidFields && invalidFields.length > 0;
          const message = hasInvalidFields
            ? describeInvalidFields(invalidFields)
            : (data && data.error) || `The device answered with HTTP status ${response.status}.`;
          setSaveError({ message, restart, invalidFields: hasInvalidFields });
          return;
        }

        // The same normalisation as a fresh load, so the form shows what a
        // reload would show (split pid and buttons, the loaded scale factors,
        // the filter defaults, the parsed schedules).
        updateSettingsCache(data);
        setFormData(transformFetchedSettings(data));
        setAutoWakeupSchedules(parseAutoWakeupSchedules(data.autowakeupSchedules));
        setClock24h(!!data.clock24hFormat);
        setSaveError(null);
      } catch (error) {
        console.error('Failed to save settings:', error);
        setSaveError({
          message: 'The settings could not be sent to the device. Check the connection.',
          restart,
        });
      } finally {
        setSubmitting(false);
      }
    },
    [formData, autowakeupSchedules],
  );

  const onExport = useCallback(() => {
    downloadJson(formData, 'settings.json');
  }, [formData]);

  const onUpload = function (evt) {
    if (evt.target.files.length) {
      const file = evt.target.files[0];
      const reader = new FileReader();
      reader.onload = async e => {
        const data = JSON.parse(e.target.result);
        setFormData(data);
      };
      reader.readAsText(file);
    }
  };

  const settingsTabs = [
    { id: 'general', label: 'General', icon: faSliders },
    { id: 'display', label: 'Display', icon: faDisplay, preload: loadDisplayTab },
    { id: 'machine', label: 'Machine', icon: faTemperatureHalf, preload: loadMachineTab },
    { id: 'calibration', label: 'Calibration', icon: faCrosshairs, preload: loadCalibrationTab },
    { id: 'plugins', label: 'Plugins', icon: faPuzzlePiece, preload: loadPluginsTab },
    { id: 'bluetooth', label: 'Bluetooth', icon: faBluetoothB, preload: loadBluetoothTab },
    { id: 'system', label: 'System', icon: faRotate, preload: loadSystemTab },
  ];

  return (
    <PageLayout>
      <PageHeader
        title='Settings'
        noStack={true}
        tabs={<TabBar tabs={settingsTabs} activeTab={tab} basePath='/settings' />}
        actions={
          <div
            className={`action-dropdown relative ${dropdownOpen ? 'action-dropdown-open' : ''}`}
            ref={dropdownRef}
          >
            <button
              onClick={() => setDropdownOpen(open => !open)}
              className='btn btn-ghost btn-circle text-base-content/85 hover:bg-base-content/10'
              aria-label='More options'
              aria-expanded={dropdownOpen}
            >
              <FontAwesomeIcon icon={faEllipsisVertical} size='lg' />
            </button>
            <ul className='menu action-dropdown-menu bg-base-100 rounded-box border-base-content/10 right-0 z-50 mt-1 w-52 border p-2 shadow-lg'>
              <li>
                <button
                  type='button'
                  onClick={() => {
                    onExport();
                    setDropdownOpen(false);
                  }}
                  className='justify-start gap-2 font-medium'
                  aria-label='Export settings'
                >
                  <FontAwesomeIcon icon={faFileExport} />
                  <span>Export Settings</span>
                </button>
              </li>
              <li>
                <button
                  type='button'
                  onClick={() => {
                    document.getElementById('settingsImport')?.click();
                    setDropdownOpen(false);
                  }}
                  className='justify-start gap-2 font-medium'
                  aria-label='Import settings'
                >
                  <FontAwesomeIcon icon={faFileImport} />
                  <span>Import Settings</span>
                </button>
              </li>
            </ul>
            <input
              onChange={onUpload}
              className='hidden'
              id='settingsImport'
              type='file'
              accept='.json,application/json'
            />
          </div>
        }
      />

      <form
        id='settings-page-form'
        key='settings'
        ref={formRef}
        method='post'
        action='/api/settings'
        onSubmit={onSubmit}
        className={isFormTab ? '' : 'hidden'}
      >
        {tab === 'general' &&
          (isLoading ? (
            <GeneralTabSkeleton />
          ) : (
            <GeneralTab
              formData={formData}
              onChange={onChange}
              setField={setField}
              profiles={profiles}
              currentTheme={currentTheme}
              setCurrentTheme={setCurrentTheme}
              handleThemeChange={handleThemeChange}
              showWifiPassword={showWifiPassword}
              setShowWifiPassword={setShowWifiPassword}
              showApPassword={showApPassword}
              setShowApPassword={setShowApPassword}
            />
          ))}
        {tab === 'display' &&
          (isLoading ? (
            <DisplayTabSkeleton />
          ) : (
            <LazyDisplayTab formData={formData} onChange={onChange} setField={setField} />
          ))}
        {tab === 'machine' &&
          (isLoading ? (
            <MachineTabSkeleton />
          ) : (
            <LazyMachineTab formData={formData} onChange={onChange} setField={setField} />
          ))}
        {tab === 'plugins' &&
          (isLoading ? (
            <PluginsTabSkeleton />
          ) : (
            <LazyPluginsTab
              formData={formData}
              onChange={onChange}
              autowakeupSchedules={autowakeupSchedules}
              addAutoWakeupSchedule={addAutoWakeupSchedule}
              removeAutoWakeupSchedule={removeAutoWakeupSchedule}
              updateAutoWakeupTime={updateAutoWakeupTime}
              updateAutoWakeupDay={updateAutoWakeupDay}
            />
          ))}

        {isFormTab && saveError && (
          <div role='alert' className='alert alert-error mt-6'>
            <span>
              <strong>{saveError.invalidFields ? 'Partly saved.' : 'Not saved.'}</strong>{' '}
              {saveError.message}
              {!saveError.invalidFields && ' Your changes are still in the form.'}
            </span>
            <button
              type='button'
              className='btn btn-sm'
              disabled={submitting}
              onClick={e => onSubmit(e, saveError.restart)}
            >
              Retry
            </button>
          </div>
        )}
        {isFormTab && (
          <StickyFormFooter submitting={submitting} onRestart={e => onSubmit(e, true)} />
        )}
      </form>

      {tab === 'calibration' && <LazyCalibrationTab formData={formData} onChange={onChange} />}
      {tab === 'bluetooth' && (isLoading ? <BluetoothTabSkeleton /> : <LazyBluetoothTab />)}
      {tab === 'system' && (isLoading ? <SystemTabSkeleton /> : <LazySystemTab />)}
    </PageLayout>
  );
}
