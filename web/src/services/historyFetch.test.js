// Run with Node's built-in test runner (this repo has no npm test script yet):
//   node --test src/services/historyFetch.test.js
//
// historyFetch.js has no sibling imports needing extension resolution, so
// this file runs under a plain `node --test`, unlike some analyzer modules
// (see scaleConnection.test.js for that caveat).

/* global globalThis */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { fetchHistoryWithRetry } from './historyFetch.js';

function makeResponse(status, headers = {}) {
  return {
    status,
    ok: status >= 200 && status < 300,
    headers: {
      get: name => headers[name] ?? null,
    },
  };
}

// Installs a fake global fetch for the duration of one test, recording call
// timestamps (relative to the fake clock below) so retry delays can be
// asserted without a real sleep.
function withFakeFetch(responses, run) {
  const originalFetch = globalThis.fetch;
  const calls = [];
  let i = 0;
  globalThis.fetch = async (input, init) => {
    calls.push({ input, init, t: Date.now() });
    const next = responses[Math.min(i, responses.length - 1)];
    i++;
    if (next instanceof Error) throw next;
    return next;
  };
  return run(calls).finally(() => {
    globalThis.fetch = originalFetch;
  });
}

test('fetchHistoryWithRetry returns immediately on a non-503 response', async () => {
  await withFakeFetch([makeResponse(200)], async calls => {
    const res = await fetchHistoryWithRetry('/api/history/index.bin', {}, { backoffMs: [1] });
    assert.equal(res.status, 200);
    assert.equal(calls.length, 1);
  });
});

test('fetchHistoryWithRetry retries a 503 and returns the eventual 200', async () => {
  await withFakeFetch([makeResponse(503), makeResponse(503), makeResponse(200)], async calls => {
    const res = await fetchHistoryWithRetry('/api/history/index.bin', {}, { backoffMs: [1, 1, 1] });
    assert.equal(res.status, 200);
    assert.equal(calls.length, 3);
  });
});

test('fetchHistoryWithRetry honours Retry-After over the configured backoff', async () => {
  const start = Date.now();
  await withFakeFetch(
    [makeResponse(503, { 'Retry-After': '0.02' }), makeResponse(200)],
    async calls => {
      const res = await fetchHistoryWithRetry(
        '/api/history/index.bin',
        {},
        { backoffMs: [5000] }, // would be very slow to wait for if Retry-After were ignored
      );
      assert.equal(res.status, 200);
      assert.equal(calls.length, 2);
      assert.ok(Date.now() - start < 1000, 'should have used the short Retry-After delay');
    },
  );
});

test('fetchHistoryWithRetry returns the last 503 once retries are exhausted (permanent failure)', async () => {
  await withFakeFetch(
    [makeResponse(503), makeResponse(503), makeResponse(503), makeResponse(503)],
    async calls => {
      const res = await fetchHistoryWithRetry(
        '/api/history/index.bin',
        {},
        { retries: 4, backoffMs: [1, 1, 1] },
      );
      assert.equal(res.status, 503);
      assert.equal(res.ok, false);
      assert.equal(calls.length, 4);
    },
  );
});

test('fetchHistoryWithRetry passes a non-503 error through on the first attempt', async () => {
  await withFakeFetch([makeResponse(404)], async calls => {
    const res = await fetchHistoryWithRetry('/api/history/missing.slog', {}, { backoffMs: [1] });
    assert.equal(res.status, 404);
    assert.equal(calls.length, 1);
  });
});

test('fetchHistoryWithRetry stops waiting and rejects when the signal is aborted mid-retry', async () => {
  const controller = new AbortController();
  await withFakeFetch([makeResponse(503), makeResponse(200)], async () => {
    const promise = fetchHistoryWithRetry(
      '/api/history/index.bin',
      { signal: controller.signal },
      { backoffMs: [10000] }, // long enough that the abort, not the timer, ends the wait
    );
    // Give the first attempt a tick to resolve and enter the retry wait.
    await new Promise(r => setTimeout(r, 5));
    controller.abort();
    await assert.rejects(promise, err => err.name === 'AbortError');
  });
});

test('fetchHistoryWithRetry rejects immediately when the signal is already aborted', async () => {
  const controller = new AbortController();
  controller.abort();
  await withFakeFetch([makeResponse(503), makeResponse(200)], async () => {
    await assert.rejects(
      fetchHistoryWithRetry(
        '/api/history/index.bin',
        { signal: controller.signal },
        { backoffMs: [10000] },
      ),
      err => err.name === 'AbortError',
    );
  });
});
