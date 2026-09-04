#ifndef BGANIM_KBLOB_H
#define BGANIM_KBLOB_H

// Hot-loadable animation blob (display-kdev builds only, -DGM_KBLOB).
//
// A blob is one Anim*.cpp compiled with the firmware's own flags, linked
// against the running firmware's symbol table (tools/kblob/kb.py) and placed
// at the two fixed buffers below: text in IRAM, rodata/data/bss in DRAM. The
// device copies it in over HTTP (/api/debug/kblob) and dispatches into it
// through the ordinary BgAnimation descriptor, so a kernel iteration is a
// compile + upload + /api/debug/kbench round trip of a few seconds instead of
// a two-minute flash cycle, and the numbers come from the real core, the real
// caches and the real PSRAM bus rather than QEMU's model of them.
//
// What it needs from the build: memory protection off (sdkconfig.kdev.defaults
// sets CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n), because with it on the PMS makes
// the IRAM half of SRAM1 unwritable through its DRAM alias, and that alias is
// how the text gets written. Production keeps memory protection, which is why
// this exists as a separate env and not a knob.
//
// The blob runs from IRAM while the firmware's own kernels run from flash
// through the 16 KB instruction cache. Calibrate for that before reading a
// blob-vs-firmware delta as a code win: upload the unchanged source as a
// blob and the /api/debug/kbench `band` vs `blob` difference is the
// placement effect alone.

#ifdef GM_KBLOB

#include <stddef.h>
#include <stdint.h>

struct BgAnimation;

namespace kblob {

#ifndef GM_KBLOB_TEXT_CAP
#define GM_KBLOB_TEXT_CAP (12 * 1024)
#endif
#ifndef GM_KBLOB_DATA_CAP
#define GM_KBLOB_DATA_CAP (4 * 1024)
#endif

struct Info {
    uint32_t textBase = 0; // IRAM address the host links .text at
    uint32_t textCap = 0;
    uint32_t dataBase = 0; // DRAM address the host links .data/.bss at
    uint32_t dataCap = 0;
    uint32_t textSize = 0; // of the installed blob
    uint32_t dataSize = 0;
    uint32_t bssSize = 0;
    uint32_t gen = 0;      // increments per successful install
    bool loaded = false;
    char name[16] = {0};   // from the image header
    char err[80] = {0};    // why the last install was refused
    char fwSha[17] = {0};  // running firmware's ELF sha256, first 8 bytes, hex
};

Info info();

// The installed blob's animation descriptor, or nullptr.
const BgAnimation *anim();

// Validates and installs an image in the tools/kblob container format. The
// caller guarantees the previous blob is not resident on the render task
// (SleepAnimation::kblobDetach). On refusal the previous blob stays loaded
// only if nothing was written yet; info().err says why.
bool install(const uint8_t *img, size_t len);

} // namespace kblob

#endif // GM_KBLOB
#endif // BGANIM_KBLOB_H
