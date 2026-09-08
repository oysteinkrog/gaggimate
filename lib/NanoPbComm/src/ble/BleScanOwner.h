#ifndef NANOPBCOMM_BLE_SCAN_OWNER_H
#define NANOPBCOMM_BLE_SCAN_OWNER_H

#include <stdint.h>

// Who last started the one NimBLE scanner (gm-bzu.6).
//
// NimBLEDevice::getScan() is a singleton with one callback slot. The
// controller transport (BleClientTransport) and the scale library
// (RemoteScalesScanner, through BLEScalePlugin) both scan with it, and each
// installs its own callbacks when it starts. Whoever started last has the
// callbacks; the other side's onResult() is never called, whatever its own
// bookkeeping says. Neither side can read the slot back from NimBLE, so this
// token records it.
//
// Rules:
//   - A side sets itself as owner when it starts a scan, and None when it
//     stops one it owns. It never clears another owner's token.
//   - The controller transport re-installs its callbacks whenever it restarts
//     a stalled scan (takeScan), so a scan the scale left behind cannot leave
//     the controller undiscoverable.
//   - The controller transport's backoff retimes only a scan it owns.
//
// Written and read from the display's main loop and the BLE host task; a
// single byte, and a stale read costs one maintain() tick, so no lock.
enum class BleScanOwner : uint8_t { None = 0, Controller = 1, Scale = 2 };

inline volatile uint8_t g_bleScanOwner = 0;

inline BleScanOwner bleScanOwner() { return static_cast<BleScanOwner>(g_bleScanOwner); }
inline void bleScanSetOwner(BleScanOwner o) { g_bleScanOwner = static_cast<uint8_t>(o); }
// Clears the token only if `who` still holds it.
inline void bleScanRelease(BleScanOwner who) {
    if (bleScanOwner() == who) {
        bleScanSetOwner(BleScanOwner::None);
    }
}

#endif // NANOPBCOMM_BLE_SCAN_OWNER_H
