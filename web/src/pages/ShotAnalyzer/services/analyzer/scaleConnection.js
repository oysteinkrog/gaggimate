/**
 * Scale-connection backfill and fallback detection.
 *
 * `activeScaleConnected` tracks any scale source (BLE or built-in) and
 * replaced the older BLE-only `bluetoothScaleConnected` bit. Binary shot
 * files back-fill it for format versions before the field existed (see
 * parseBinaryShot.js, version < 6). JSON shots - browser uploads, and
 * exports made before that backfill existed - never go through that
 * parser, so `activeScaleConnected` can be missing even when
 * `bluetoothScaleConnected` is present, or missing entirely on very old
 * recordings.
 *
 * `backfillActiveScaleConnected` applies the same rule to any sample array,
 * regardless of source. After it runs, a sample's state is one of:
 *   - true / false : known, from the device or backfilled from the BLE bit
 *   - undefined    : genuinely unknown (predates both fields)
 *
 * Scale-lost detection must not confuse "genuinely unknown" with
 * "connected". `isScaleLostInSamples` only trusts the explicit/backfilled
 * boolean when at least one sample carries it; otherwise it falls back to
 * `hasFlatWeightTrace`, a sample-based check for the failure this field
 * exists to catch: a scale that stopped reporting without ever recording
 * a disconnect.
 */

const FLAT_WEIGHT_TOLERANCE_G = 0.05;
const FLAT_WEIGHT_MIN_SAMPLES = 5;
const FLAT_WEIGHT_MIN_DURATION_MS = 2000;

/**
 * Mutates each sample's systemInfo in place, back-filling
 * activeScaleConnected from bluetoothScaleConnected wherever the former is
 * missing. Safe to call on binary shots too: it is a no-op wherever the
 * field is already boolean.
 */
export function backfillActiveScaleConnected(samples) {
  if (!Array.isArray(samples)) return;
  for (const sample of samples) {
    const sysInfo = sample?.systemInfo;
    if (!sysInfo) continue;
    if (typeof sysInfo.activeScaleConnected === 'boolean') continue;
    if (typeof sysInfo.bluetoothScaleConnected === 'boolean') {
      sysInfo.activeScaleConnected = sysInfo.bluetoothScaleConnected;
    }
  }
}

/**
 * True when every sample in the weight trace stays within tolerance of the
 * first reading for at least FLAT_WEIGHT_MIN_DURATION_MS - the symptom of a
 * scale that stopped reporting without ever setting a disconnect flag.
 */
export function hasFlatWeightTrace(samples) {
  if (!Array.isArray(samples) || samples.length < FLAT_WEIGHT_MIN_SAMPLES) return false;

  const firstWeight = samples[0]?.v;
  if (typeof firstWeight !== 'number' || Number.isNaN(firstWeight)) return false;

  const duration = samples.at(-1).t - samples[0].t;
  if (duration < FLAT_WEIGHT_MIN_DURATION_MS) return false;

  return samples.every(sample => {
    const weight = sample?.v;
    return typeof weight === 'number' && Math.abs(weight - firstWeight) <= FLAT_WEIGHT_TOLERANCE_G;
  });
}

/**
 * Scale-lost check for a set of samples (a whole shot or one phase).
 * An explicit `false` anywhere always means lost. If no sample carries a
 * known state at all, fall back to the flat-weight-trace heuristic instead
 * of silently reporting "not lost".
 */
export function isScaleLostInSamples(samples) {
  if (!Array.isArray(samples) || samples.length === 0) return false;

  let sawKnownState = false;
  for (const sample of samples) {
    const state = sample?.systemInfo?.activeScaleConnected;
    if (state === false) return true;
    if (state === true) sawKnownState = true;
  }

  if (sawKnownState) return false;
  return hasFlatWeightTrace(samples);
}
