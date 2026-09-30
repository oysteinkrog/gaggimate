// historyFetch.js
//
// The firmware's history worker processes one shot-history file request at a
// time from a queue of eight; past that it answers 503. A plain fetch treats
// a 503 as a final failure, so a page load that races a second browser tab
// (or the worker just being busy) can silently drop shots from a list or
// fail to load one. fetchHistoryWithRetry retries a 503 with bounded
// backoff, honouring the Retry-After header when the device sends one, and
// passes every other response (success or a non-503 error) straight
// through unchanged.

const DEFAULT_BACKOFF_MS = [500, 1000, 2000];

// Waits `ms`, rejecting early with an AbortError if `signal` fires first.
// Used between retries so a cancelled view (e.g. an unmounted page) does not
// keep waiting on a scheduled retry.
function sleep(ms, signal) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(new DOMException('Aborted', 'AbortError'));
      return;
    }
    const timer = setTimeout(() => {
      signal?.removeEventListener('abort', onAbort);
      resolve();
    }, ms);
    function onAbort() {
      clearTimeout(timer);
      reject(new DOMException('Aborted', 'AbortError'));
    }
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

// Picks the wait before the next attempt: the response's Retry-After header
// (seconds) when the device sent one and it parses as a non-negative number,
// otherwise the configured backoff for this attempt (the last entry repeats
// if there are more retries than backoff entries).
function retryDelayMs(response, attemptIndex, backoffMs) {
  const retryAfter = response.headers?.get?.('Retry-After');
  if (retryAfter != null) {
    const seconds = Number(retryAfter);
    if (Number.isFinite(seconds) && seconds >= 0) {
      return seconds * 1000;
    }
  }
  return backoffMs[Math.min(attemptIndex, backoffMs.length - 1)];
}

/**
 * Fetch a history endpoint, retrying a 503 (history worker queue full) with
 * bounded backoff. Every other response, success or error, is returned on
 * the first attempt.
 *
 * @param {string|URL} input - URL to fetch.
 * @param {RequestInit} [init] - Standard fetch options. `init.signal`, if given, both
 *   aborts the in-flight fetch and cancels any pending retry wait.
 * @param {Object} [options]
 * @param {number} [options.retries=4] - Total attempts, including the first.
 * @param {number[]} [options.backoffMs] - Delay before each retry, in order.
 * @returns {Promise<Response>} The first non-503 response, or the last 503 response
 *   once retries are exhausted (a caller's existing `!response.ok` check then reports
 *   it as an error, same as today).
 * @throws {DOMException} AbortError if `init.signal` fires before a final response,
 *   or whatever error a non-503 fetch failure (e.g. a network error) throws.
 */
export async function fetchHistoryWithRetry(input, init = {}, options = {}) {
  const { retries = 4, backoffMs = DEFAULT_BACKOFF_MS } = options;
  const { signal } = init;

  let lastResponse;
  for (let attempt = 0; attempt < retries; attempt++) {
    const response = await fetch(input, init);
    if (response.status !== 503) {
      return response;
    }
    lastResponse = response;
    if (attempt === retries - 1) break;
    await sleep(retryDelayMs(response, attempt, backoffMs), signal);
  }
  return lastResponse;
}
