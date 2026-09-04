#ifdef GM_KBLOB

#include "KBlob.h"
#include "BgAnim.h"
#include <Arduino.h> // log_i / log_w
#include <esp_app_desc.h>
#include <esp_memory_utils.h>
#include <esp_rom_crc.h>
#include <soc/soc.h> // MAP_IRAM_TO_DRAM
#include <stdio.h>
#include <string.h>

namespace kblob {
namespace {

// The .iram1.* rule in esp_common's linker fragment puts this in .iram0.text,
// so the CPU fetches from it like any IRAM_ATTR function. The loader writes it
// through the DRAM alias of the same SRAM (MAP_IRAM_TO_DRAM): the IRAM alias
// only accepts 32-bit data accesses, and with memory protection off the DRAM
// side is plain writable RAM.
alignas(16) uint8_t g_text[GM_KBLOB_TEXT_CAP] __attribute__((section(".iram1.kblob"), used));
alignas(16) uint8_t g_data[GM_KBLOB_DATA_CAP];

// Container written by tools/kblob/kb.py. Little-endian, 64 bytes, followed
// by the text image (padded to 4) and the initialised data image.
struct __attribute__((packed)) Header {
    char magic[4]; // "GMKB"
    uint32_t version;
    uint32_t textAddr;
    uint32_t textSize;
    uint32_t dataAddr;
    uint32_t dataSize; // initialised bytes (rodata + data, descriptor first)
    uint32_t bssSize;  // zeroed after dataSize
    uint32_t descOff;  // offset in the data image of the BgAnimation pointer
    uint8_t fwSha[8];  // sha256(firmware.elf)[0..8): must match the running app
    char name[16];
    uint32_t crc; // crc32 (zlib) over text image + data image
    uint32_t reserved;
};
static_assert(sizeof(Header) == 64, "kblob header layout");

constexpr uint32_t kVersion = 1;

Info g_info;
const BgAnimation *g_anim = nullptr;

bool refuse(const char *msg) {
    strlcpy(g_info.err, msg, sizeof(g_info.err));
    log_w("kblob: refused: %s", msg);
    return false;
}

void fillStatic() {
    g_info.textBase = reinterpret_cast<uint32_t>(g_text);
    g_info.textCap = sizeof(g_text);
    g_info.dataBase = reinterpret_cast<uint32_t>(g_data);
    g_info.dataCap = sizeof(g_data);
    if (g_info.fwSha[0] == 0) {
        esp_app_get_elf_sha256(g_info.fwSha, sizeof(g_info.fwSha));
    }
}

} // namespace

Info info() {
    fillStatic();
    return g_info;
}

const BgAnimation *anim() { return g_anim; }

bool install(const uint8_t *img, size_t len) {
    fillStatic();
    if (len < sizeof(Header)) {
        return refuse("short image");
    }
    Header h;
    memcpy(&h, img, sizeof(h));
    if (memcmp(h.magic, "GMKB", 4) != 0) {
        return refuse("bad magic");
    }
    if (h.version != kVersion) {
        return refuse("unsupported version");
    }
    // Sizes against the caps first, so the sums below cannot wrap.
    if (h.textAddr != g_info.textBase || h.textSize > g_info.textCap) {
        return refuse("text address or size does not fit this build");
    }
    if (h.dataAddr != g_info.dataBase || h.dataSize > g_info.dataCap || h.bssSize > g_info.dataCap - h.dataSize) {
        return refuse("data address or size does not fit this build");
    }
    const uint32_t textPadded = (h.textSize + 3u) & ~3u;
    if (sizeof(Header) + textPadded + h.dataSize != len) {
        return refuse("length does not match header");
    }
    char sha[17];
    for (int i = 0; i < 8; i++) {
        snprintf(sha + i * 2, 3, "%02x", h.fwSha[i]);
    }
    // esp_app_get_elf_sha256 hands out CONFIG_APP_RETRIEVE_LEN_ELF_SHA hex
    // digits; sdkconfig.kdev.defaults sets that to 16 so the whole header
    // field is compared, and a build that reports fewer is refused rather
    // than matched on a shorter prefix.
    if (strlen(g_info.fwSha) < 16) {
        return refuse("firmware reports fewer than 16 sha digits (CONFIG_APP_RETRIEVE_LEN_ELF_SHA)");
    }
    if (strncmp(sha, g_info.fwSha, 16) != 0) {
        return refuse("linked against a different firmware ELF");
    }
    const uint8_t *text = img + sizeof(Header);
    const uint8_t *data = text + textPadded;
    uint32_t crc = esp_rom_crc32_le(0, text, h.textSize);
    crc = esp_rom_crc32_le(crc, data, h.dataSize);
    if (crc != h.crc) {
        return refuse("payload crc mismatch");
    }
    if (h.dataSize < 4u || h.descOff > h.dataSize - 4u) {
        return refuse("descriptor offset outside data");
    }
    if (!esp_ptr_in_iram(g_text)) {
        return refuse("text buffer is not in IRAM (linker placement)");
    }

    // Past this point the old blob is gone whatever happens below.
    g_anim = nullptr;
    g_info.loaded = false;
    uint8_t *textW = reinterpret_cast<uint8_t *>(MAP_IRAM_TO_DRAM(reinterpret_cast<uint32_t>(g_text)));
    memcpy(textW, text, h.textSize);
    memcpy(g_data, data, h.dataSize);
    // The whole remainder, not bssSize: the header's figure is what the
    // linker reported and the loader must not depend on it covering the
    // alignment gap ahead of .bss (an early kb.py did not, and the stale
    // bytes it left in a table pointer crashed two blobs in frame()).
    memset(g_data + h.dataSize, 0, sizeof(g_data) - h.dataSize);
    // Nothing caches internal SRAM, so ordering the stores ahead of the first
    // fetch is all that is needed; isync drops any prefetched stale words.
    asm volatile("memw\n\tisync" ::: "memory");

    uint32_t descAddr;
    memcpy(&descAddr, g_data + h.descOff, sizeof(descAddr));
    if (descAddr < h.dataAddr || h.dataSize < sizeof(BgAnimation) || descAddr - h.dataAddr > h.dataSize - sizeof(BgAnimation)) {
        return refuse("descriptor pointer outside data");
    }
    const BgAnimation *a = reinterpret_cast<const BgAnimation *>(descAddr);
    if (a->init == nullptr || a->frame == nullptr || a->band == nullptr) {
        return refuse("descriptor missing init/frame/band");
    }
    const void *fns[] = {reinterpret_cast<const void *>(a->init), reinterpret_cast<const void *>(a->frame),
                         reinterpret_cast<const void *>(a->band), reinterpret_cast<const void *>(a->release),
                         reinterpret_cast<const void *>(a->bandRef)};
    for (const void *f : fns) {
        if (f != nullptr && !esp_ptr_executable(f)) {
            return refuse("descriptor function pointer is not executable");
        }
    }
    g_info.textSize = h.textSize;
    g_info.dataSize = h.dataSize;
    g_info.bssSize = h.bssSize;
    memcpy(g_info.name, h.name, sizeof(g_info.name));
    g_info.name[sizeof(g_info.name) - 1] = 0;
    g_info.err[0] = 0;
    g_info.gen++;
    g_info.loaded = true;
    g_anim = a;
    log_i("kblob: installed %s gen=%u text=%u data=%u bss=%u id=%s", g_info.name, static_cast<unsigned>(g_info.gen),
          static_cast<unsigned>(h.textSize), static_cast<unsigned>(h.dataSize), static_cast<unsigned>(h.bssSize), a->id);
    return true;
}

} // namespace kblob

#endif // GM_KBLOB
