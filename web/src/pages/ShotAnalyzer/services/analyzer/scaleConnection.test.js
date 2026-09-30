// Run with Node's built-in test runner (this repo has no npm test script yet):
//   node --test src/pages/ShotAnalyzer/services/analyzer/scaleConnection.test.js
//
// Note: several sibling analyzer modules import each other without a file
// extension (e.g. './metricStats'), which Vite resolves but plain Node ESM
// does not. That is a pre-existing, repo-wide convention outside this file's
// scope, so a bare `node --test` run on this file currently needs a
// specifier-resolution loader (or a bundler-aware runner such as vitest) to
// follow those imports. All assertions here were verified passing under such
// a loader; this file itself requires no special handling.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { calculateShotMetrics } from './shotAnalysis.js';
import {
  backfillActiveScaleConnected,
  hasFlatWeightTrace,
  isScaleLostInSamples,
} from './scaleConnection.js';

const SAMPLE_INTERVAL_MS = 250;

/** Builds a minimal, valid sample array for calculateShotMetrics.
 *  `weightAt` gets the sample index and returns the weight (v) reading.
 *  `systemInfoAt` gets the sample index and returns that sample's systemInfo. */
function buildSamples({ count, weightAt, systemInfoAt, flow = 2 }) {
  const samples = [];
  for (let i = 0; i < count; i++) {
    samples.push({
      t: i * SAMPLE_INTERVAL_MS,
      tt: 93,
      ct: 93,
      tp: 9,
      cp: 9,
      fl: flow,
      tf: flow,
      pf: 0,
      vf: 0,
      v: weightAt(i),
      ev: weightAt(i),
      pr: 0,
      systemInfo: systemInfoAt(i),
      phaseNumber: 0,
    });
  }
  return samples;
}

const SETTINGS = { scaleDelayMs: 0, sensorDelayMs: 200, isAutoAdjusted: false };

function buildShot(samples) {
  return { id: 'fixture', samples, sampleInterval: SAMPLE_INTERVAL_MS };
}

// --- backfillActiveScaleConnected -------------------------------------------------

test('backfillActiveScaleConnected fills activeScaleConnected from the old BLE-only bit', () => {
  const samples = [{ systemInfo: { bluetoothScaleConnected: false } }];
  backfillActiveScaleConnected(samples);
  assert.equal(samples[0].systemInfo.activeScaleConnected, false);
});

test('backfillActiveScaleConnected never overwrites an explicit value', () => {
  const samples = [{ systemInfo: { activeScaleConnected: true, bluetoothScaleConnected: false } }];
  backfillActiveScaleConnected(samples);
  assert.equal(samples[0].systemInfo.activeScaleConnected, true);
});

test('backfillActiveScaleConnected leaves a sample with neither field as undefined', () => {
  const samples = [{ systemInfo: { shotStartedVolumetric: true } }];
  backfillActiveScaleConnected(samples);
  assert.equal(samples[0].systemInfo.activeScaleConnected, undefined);
});

test('backfillActiveScaleConnected tolerates samples with no systemInfo', () => {
  const samples = [{ t: 0 }, { t: 250, systemInfo: { bluetoothScaleConnected: true } }];
  assert.doesNotThrow(() => backfillActiveScaleConnected(samples));
  assert.equal(samples[1].systemInfo.activeScaleConnected, true);
});

// --- hasFlatWeightTrace ------------------------------------------------------------

test('hasFlatWeightTrace is true when the weight reading never moves', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: () => 18,
    systemInfoAt: () => ({}),
  });
  assert.equal(hasFlatWeightTrace(samples), true);
});

test('hasFlatWeightTrace is false once the weight is actually climbing', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: i => i * 1.5,
    systemInfoAt: () => ({}),
  });
  assert.equal(hasFlatWeightTrace(samples), false);
});

test('hasFlatWeightTrace ignores a too-short trace (too few samples or too short a span)', () => {
  const tooFewSamples = buildSamples({ count: 3, weightAt: () => 18, systemInfoAt: () => ({}) });
  assert.equal(hasFlatWeightTrace(tooFewSamples), false);

  const tooShortSpan = buildSamples({ count: 5, weightAt: () => 18, systemInfoAt: () => ({}) });
  assert.equal(hasFlatWeightTrace(tooShortSpan), false);
});

// --- isScaleLostInSamples -----------------------------------------------------------

test('isScaleLostInSamples: an explicit false anywhere always means lost', () => {
  const samples = buildSamples({
    count: 6,
    weightAt: i => i * 1.5, // rising, i.e. clearly not a "flat trace" symptom
    systemInfoAt: i => ({ activeScaleConnected: i < 3 }),
  });
  assert.equal(isScaleLostInSamples(samples), true);
});

test('isScaleLostInSamples: an explicit true throughout means not lost, even with a flat trace', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: () => 18, // flat, but the telemetry says the scale is fine
    systemInfoAt: () => ({ activeScaleConnected: true }),
  });
  assert.equal(isScaleLostInSamples(samples), false);
});

test('isScaleLostInSamples: no known state at all falls back to the flat-weight-trace heuristic', () => {
  const lost = buildSamples({ count: 12, weightAt: () => 18, systemInfoAt: () => ({}) });
  assert.equal(isScaleLostInSamples(lost), true);

  const fine = buildSamples({ count: 12, weightAt: i => i * 1.5, systemInfoAt: () => ({}) });
  assert.equal(isScaleLostInSamples(fine), false);
});

test('isScaleLostInSamples: empty input is not lost', () => {
  assert.equal(isScaleLostInSamples([]), false);
  assert.equal(isScaleLostInSamples(undefined), false);
});

// --- calculateShotMetrics acceptance scenario ---------------------------------------
// "An imported pre-branch JSON shot with a flat weight trace shows the scale-lost
// marker" (gm-bzu.59). A pre-branch shot is one recorded before activeScaleConnected
// existed: its systemInfo carries neither that field nor the older
// bluetoothScaleConnected bit, so only the sample-based fallback can catch it.

test('calculateShotMetrics flags a pre-branch JSON shot with a flat weight trace as scale-lost', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: () => 18, // scale stuck reporting the same weight all shot
    systemInfoAt: () => ({
      shotStartedVolumetric: true,
      currentlyVolumetric: true,
      volumetricAvailable: true,
      // no bluetoothScaleConnected, no activeScaleConnected: genuinely pre-branch.
    }),
  });

  const result = calculateShotMetrics(buildShot(samples), null, SETTINGS);

  assert.equal(result.isBrewByWeight, true);
  assert.equal(result.globalScaleLost, true, 'expected the shot-level scale-lost marker');
  assert.ok(result.phases.length > 0);
  assert.equal(result.phases[0].scaleLost, true, 'expected the phase-level scale-lost marker');
});

test('calculateShotMetrics backfills from the old BLE-only bit before checking for scale loss', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: () => 18,
    systemInfoAt: i => ({
      shotStartedVolumetric: true,
      currentlyVolumetric: true,
      volumetricAvailable: true,
      // Legacy field only: scale drops out partway through the shot.
      bluetoothScaleConnected: i < 6,
    }),
  });

  const result = calculateShotMetrics(buildShot(samples), null, SETTINGS);

  assert.equal(result.globalScaleLost, true);
  // The backfill mutates the samples in place, so the raw stat reads correctly too.
  assert.equal(samples[6].systemInfo.activeScaleConnected, false);
  assert.equal(result.total.sys_scale, false);
});

test('calculateShotMetrics does not flag a normal shot that happens to end on a flat drip', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: () => 18, // flat, but the telemetry says the scale stayed connected
    systemInfoAt: () => ({
      shotStartedVolumetric: true,
      currentlyVolumetric: true,
      volumetricAvailable: true,
      activeScaleConnected: true,
    }),
  });

  const result = calculateShotMetrics(buildShot(samples), null, SETTINGS);

  assert.equal(result.globalScaleLost, false);
  assert.equal(result.phases[0].scaleLost, false);
});

test('calculateShotMetrics does not flag a pre-branch shot whose weight is actually rising', () => {
  const samples = buildSamples({
    count: 12,
    weightAt: i => i * 1.5,
    systemInfoAt: () => ({
      shotStartedVolumetric: true,
      currentlyVolumetric: true,
      volumetricAvailable: true,
    }),
  });

  const result = calculateShotMetrics(buildShot(samples), null, SETTINGS);

  assert.equal(result.globalScaleLost, false);
  assert.equal(result.phases[0].scaleLost, false);
});
