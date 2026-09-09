// The hot-loaded kernel blob's half of SleepAnimation (GM_KBLOB,
// display-kdev; bganim/KBlob.h, tools/kblob): the install handshake, the
// blob's animation slot, and the cycle-count bench behind /api/debug/kbench.
// The render loop reaches it through the hooks in SleepAnimationProbe.h.
#ifndef GAGGIMATE_SIM
#ifdef GM_KBLOB

#include "SleepAnimation.h"
#include "SleepAnimationInternal.h"
#include "SleepAnimationProbe.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <display/ui/default/bganim/BgAnimCommon.h>
#include <display/ui/default/bganim/KBlob.h>
#include <esp_cpu.h> // esp_cpu_get_cycle_count, for the kbench
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

// Frame-boundary slot: the blob loader's detach handshake and the cycle-count
// bench both need the animation statics to themselves.
void SleepAnimation::probeFrameBoundary() {
    serviceBlobDetach();
    if (kbenchReq.load() >= 0) {
        runKBench();
    }
}

void SleepAnimation::probeResidentSet(int id) { blobResident.store(id == KBLOB_SLOT); }

bool SleepAnimation::probeBlobActive() const {
    return useBlob.load(std::memory_order_relaxed) && !blobInstalling.load(std::memory_order_relaxed) && kblob::anim() != nullptr;
}

const BgAnimation *SleepAnimation::probeBlobAnim(int slot) const { return slot == KBLOB_SLOT ? kblob::anim() : nullptr; }

bool SleepAnimation::probeReleaseBlobResident() {
    if (residentAnimId != KBLOB_SLOT) {
        return false;
    }
    // Never through bg_animation(): a slot with no blob behind it clamps to
    // Plasma there, and Plasma's release() is not what holds these.
    const BgAnimation *b = kblob::anim();
    if (b != nullptr && b->release != nullptr) {
        b->release();
    }
    residentAnimId = -1;
    blobResident.store(false);
    return true;
}

bool SleepAnimation::kblobBeginInstall(uint32_t timeoutMs) {
    // Order matters: the guard first, so a useblob=1 arriving from here on is
    // dropped, then the switch off, then the handshake.
    blobInstalling.store(true);
    useBlob.store(false);
    if (!running) {
        if (blobResident.load()) {
            blobInstalling.store(false);
            return false;
        }
        return true;
    }
    blobDetachReq.store(true);
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
    while (blobDetachReq.load()) {
        if (esp_timer_get_time() > deadline) {
            blobInstalling.store(false);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (blobResident.load()) {
        blobInstalling.store(false);
        return false;
    }
    return true;
}

void SleepAnimation::serviceBlobDetach() {
    if (!blobDetachReq.load()) {
        return;
    }
    if (residentAnimId == KBLOB_SLOT) {
        releaseResident();
    }
    if (initializedAnimId == KBLOB_SLOT) {
        initializedAnimId = -1;
    }
    blobDetachReq.store(false);
}

namespace {
uint32_t fnv1a(const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}
} // namespace

void SleepAnimation::runKBench() {
    const int id = kbenchReq.exchange(-1);
    const int n = kbenchN.load();
    const int frames = kbenchFrames.load();
    const uint32_t which = kbenchWhich.load();
    const int w = display->width();
    const int h = display->height();
    KBenchResult r;
    r.anim = id;
    r.frames = frames;
    r.n = n;
    r.which = which;
    const uint32_t seq = kbenchSeq.load() + 2;
    r.seq = seq;
    auto publish = [&]() {
        kbenchSeq.store(seq - 1, std::memory_order_release);
        kbench = r;
        kbenchSeq.store(seq, std::memory_order_release);
    };
    const BgAnimation &fw = bg_animation(id);
    const BgAnimation *blob = kblob::anim();
    const size_t bandBytes = static_cast<size_t>(w) * BAND_H * sizeof(uint16_t);
    // Internal like the production band buffers; PSRAM only if that fails,
    // which the log then says.
    uint16_t *buf = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, bandBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, bandBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        log_w("SleepAnimation: kbench band buffer fell back to PSRAM");
    }
    const int bandsPerFrame = (h + BAND_H - 1) / BAND_H;
    // Per-band output hashes of the firmware band(): the firmware bandRef()
    // and both blob kernels are compared against them. The blob's own
    // bandRef() is compared with the blob band() of the same band directly.
    uint32_t *hashes = nullptr;
    if ((which & KB_BAND) != 0 && (which & ~KB_BAND) != 0) {
        const size_t hashBytes = sizeof(uint32_t) * static_cast<size_t>(frames) * bandsPerFrame;
        hashes = static_cast<uint32_t *>(heap_caps_malloc(hashBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (buf == nullptr) {
        publish();
        return;
    }
    uint8_t pFw[4];
    bg_parse_params(nullptr, id, pFw);
    releaseResident();
    initializedAnimId = -1;
    // Nothing is resident now, so anything still in the slab was leaked by
    // an earlier init()/release() pair. Report it and start clean.
    r.hotLeakBefore = static_cast<uint32_t>(bganim::hotUsed());
    if (r.hotLeakBefore != 0) {
        log_w("SleepAnimation: kbench found %u B of hot slab allocated with nothing resident, resetting",
              static_cast<unsigned>(bganim::hotReset()));
    }
    bool haveHashes = false; // the firmware band() ran and filled hashes[]
    // obj 0 is the firmware descriptor (variants 0 and 1), obj 1 the blob
    // (variants 2 and 3). Each object inits once and both of its kernels
    // are timed on the same frame() state (see the header).
    for (int obj = 0; obj < 2; obj++) {
        const BgAnimation *a = obj == 0 ? &fw : blob;
        const int vBand = obj * 2;
        const int vRef = obj * 2 + 1;
        const bool doBand = (which & (1u << vBand)) != 0 && a != nullptr;
        const bool doRef = (which & (1u << vRef)) != 0 && a != nullptr && a->bandRef != nullptr;
        if (!doBand && !doRef) {
            continue;
        }
        // Each descriptor's own parameter defaults: for a blob that is a
        // variant of animation `id` they are the same values, and for a blob
        // that is a new animation they are the only ones that make sense.
        uint8_t p[4];
        for (int i = 0; i < 4; i++) {
            p[i] = obj == 0 ? pFw[i] : (a->params[i].key != nullptr ? a->params[i].def : 0);
        }
        if (a->release != nullptr) {
            residentAnimId = obj == 0 ? id : KBLOB_SLOT;
            blobResident.store(obj == 1);
        }
        r.v[vBand].ran = doBand;
        r.v[vRef].ran = doRef;
        if (!a->init(w, h)) {
            r.v[vBand].initFailed = doBand;
            r.v[vRef].initFailed = doRef;
            releaseResident();
            continue;
        }
        for (int f = 0; f < frames; f++) {
            const uint32_t tMs = 123456u + static_cast<uint32_t>(f) * 33u;
            a->frame(tMs, w, h, p);
            int bi = 0;
            for (int y0 = 0; y0 < h; y0 += BAND_H, bi++) {
                const int rows = (y0 + BAND_H <= h) ? BAND_H : (h - y0);
                const int idx = f * bandsPerFrame + bi;
                uint32_t hsh[2] = {0, 0}; // [0] band(), [1] bandRef()
                // Alternate which kernel meets the band cold, as animtest
                // does, so neither one's first run always follows the
                // other's warm-up of the same tables.
                for (int k2 = 0; k2 < 2; k2++) {
                    const bool isRef = ((bi & 1) == 0) ? (k2 == 1) : (k2 == 0);
                    if (isRef ? !doRef : !doBand) {
                        continue;
                    }
                    const auto fn = isRef ? a->bandRef : a->band;
                    KBenchVariant &v = r.v[isRef ? vRef : vBand];
                    uint32_t best = ~0u;
                    uint32_t first = 0;
                    for (int k = 0; k < n; k++) {
                        const uint32_t c0 = esp_cpu_get_cycle_count();
                        fn(buf, y0, rows, w, tMs, p);
                        const uint32_t dc = esp_cpu_get_cycle_count() - c0;
                        if (k == 0) {
                            first = dc;
                        }
                        if (dc < best) {
                            best = dc;
                        }
                        v.sumCyc += dc;
                    }
                    v.minCyc += best;
                    v.firstCyc += first;
                    v.bands++;
                    if (hashes != nullptr) {
                        hsh[isRef ? 1 : 0] = fnv1a(buf, static_cast<size_t>(w) * rows * sizeof(uint16_t));
                    }
                }
                if (hashes == nullptr) {
                    continue;
                }
                auto note = [&](KBenchVariant &v, uint32_t expect, uint32_t got) {
                    if (expect == got) {
                        return;
                    }
                    if (v.mismatchBands == 0) {
                        v.firstMismatchFrame = f;
                        v.firstMismatchY = y0;
                    }
                    v.mismatchBands++;
                };
                if (obj == 0) {
                    if (doBand) {
                        hashes[idx] = hsh[0];
                        if (doRef) {
                            note(r.v[1], hsh[0], hsh[1]);
                        }
                    }
                } else {
                    if (haveHashes && doBand) {
                        note(r.v[2], hashes[idx], hsh[0]);
                    }
                    if (haveHashes && doRef) {
                        note(r.v[3], hashes[idx], hsh[1]);
                    }
                    if (doBand && doRef && hsh[0] != hsh[1]) {
                        r.v[3].mismatchVsBlob++;
                    }
                }
            }
        }
        releaseResident();
        const uint32_t leaked = static_cast<uint32_t>(bganim::hotUsed());
        if (obj == 0) {
            r.hotLeakFw = leaked;
        } else {
            r.hotLeakBlob = leaked;
        }
        if (leaked != 0) {
            log_w("SleepAnimation: kbench %s %s left %u B in the hot slab after release(), resetting",
                  obj == 0 ? "firmware" : "blob", a->id, static_cast<unsigned>(leaked));
            bganim::hotReset();
        }
        if (obj == 0 && doBand) {
            haveHashes = hashes != nullptr;
        }
    }
    heap_caps_free(buf);
    heap_caps_free(hashes);
    publish();
    log_i("SleepAnimation: kbench %s n=%d frames=%d band=%llu ref=%llu blob=%llu blobref=%llu min cycles", fw.id, n, frames,
          static_cast<unsigned long long>(r.v[0].minCyc), static_cast<unsigned long long>(r.v[1].minCyc),
          static_cast<unsigned long long>(r.v[2].minCyc), static_cast<unsigned long long>(r.v[3].minCyc));
}


#endif // GM_KBLOB
#endif // GAGGIMATE_SIM
