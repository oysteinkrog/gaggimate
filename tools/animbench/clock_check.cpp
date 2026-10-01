// Checks that an animation's motion does not depend on how long the device
// has been up (gm-bzu.50).
//
// tMs is millis(). The old frame() pattern, `const float t = tMs * speed`,
// loses precision as tMs grows: float(tMs) steps by 32 ms after 3.1 days and
// by 256 ms after 24.9 days, so the motion goes visibly jerky, and at the
// 49.7-day wrap every oscillator jumps back to its t = 0 phase. An animation
// on bganim::AnimClock (BgAnimClock.h) advances by the wrapped delta between
// frames instead, so the same sequence of frame-to-frame steps must render
// the same pictures whatever millis() read when it started.
//
// Method: for each animation, the same 40 frames 33 ms apart are rendered
// from three starting times: 1 minute, 10 days, and 660 ms before the wrap
// (so the run crosses it). Each run happens in a forked child, so every
// animation starts from a fresh process state (its clock lives in a static
// that release() deliberately keeps). Every frame of the 10-day and wrap
// runs must hash the same as the 1-minute run's frame. A run in which no
// frame differs from the one before also fails: the check would be vacuous
// for an animation that does not move.
//
// The default list is the animations that run on AnimClock. Each follow-up
// that moves another animation onto it adds that animation here. `--all`
// runs every registered animation and reports, without failing, the ones
// still on absolute time.
//
// Build:  make build/clock_check
// Usage:  ./build/clock_check [--all] [--size N] [anim ...]
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int FRAMES = 40;
constexpr uint32_t FRAME_MS = 33;
constexpr int BAND_H = 2; // production band height (BgAnim.h)

const char *const kOnClock[] = {"fireflies", "aurora", "starfield", "ember", "orbits", "steam", "mandala"};

struct Start {
    const char *name;
    uint32_t t0;
};
const Start kStarts[] = {
    {"1 minute", 60000u},
    {"10 days", 864000000u},
    {"across the wrap", 0xFFFFFFFFu - 20u * FRAME_MS},
};
constexpr int NSTARTS = sizeof(kStarts) / sizeof(kStarts[0]);

uint64_t fnv1a(const uint16_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n * sizeof(uint16_t); i++) {
        h = (h ^ b[i]) * 1099511628211ull;
    }
    return h;
}

// Renders FRAMES frames from t0 in a child process and returns their hashes.
// An empty result means the child failed (init or a crash).
std::vector<uint64_t> runFrom(int id, int size, uint32_t t0) {
    int fd[2];
    if (pipe(fd) != 0) {
        perror("pipe");
        exit(2);
    }
    const pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(2);
    }
    if (pid == 0) {
        close(fd[0]);
        const BgAnimation &a = bg_animation(id);
        uint8_t p[4];
        bg_parse_params(nullptr, id, p);
        if (!a.init(size, size)) {
            _exit(3);
        }
        std::vector<uint16_t> fb(static_cast<size_t>(size) * size);
        for (int f = 0; f < FRAMES; f++) {
            const uint32_t t = t0 + static_cast<uint32_t>(f) * FRAME_MS; // wraps on purpose
            a.frame(t, size, size, p);
            for (int y0 = 0; y0 < size; y0 += BAND_H) {
                const int rows = y0 + BAND_H <= size ? BAND_H : size - y0;
                a.band(fb.data() + static_cast<size_t>(y0) * size, y0, rows, size, t, p);
            }
            const uint64_t h = fnv1a(fb.data(), fb.size());
            if (write(fd[1], &h, sizeof(h)) != static_cast<ssize_t>(sizeof(h))) {
                _exit(4);
            }
        }
        _exit(0);
    }
    close(fd[1]);
    std::vector<uint64_t> out;
    uint64_t h;
    while (read(fd[0], &h, sizeof(h)) == static_cast<ssize_t>(sizeof(h))) {
        out.push_back(h);
    }
    close(fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || out.size() != static_cast<size_t>(FRAMES)) {
        out.clear();
    }
    return out;
}

bool onClock(const char *id) {
    for (const char *n : kOnClock) {
        if (strcmp(n, id) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    bool all = false;
    int size = 480;
    std::vector<std::string> names;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all") == 0) {
            all = true;
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            size = atoi(argv[++i]);
        } else {
            names.push_back(argv[i]);
        }
    }

    int failing = 0, checked = 0;
    for (int id = 0; id < bg_animation_count(); id++) {
        const char *animId = bg_animation(id).id;
        bool wanted;
        if (!names.empty()) {
            wanted = false;
            for (const std::string &n : names) {
                wanted = wanted || n == animId;
            }
        } else {
            wanted = all || onClock(animId);
        }
        if (!wanted) {
            continue;
        }
        // Only an animation that claims to be on the clock can fail.
        const bool enforced = !names.empty() || onClock(animId);
        checked++;

        std::vector<uint64_t> runs[NSTARTS];
        for (int s = 0; s < NSTARTS; s++) {
            runs[s] = runFrom(id, size, kStarts[s].t0);
        }
        std::string verdict;
        bool bad = false;
        if (runs[0].empty()) {
            verdict = "the 1 minute run failed (init failure or crash)";
            bad = true;
        } else {
            int moving = 0;
            for (int f = 1; f < FRAMES; f++) {
                moving += runs[0][f] != runs[0][f - 1];
            }
            if (moving == 0) {
                verdict = "no frame moved, so the check proves nothing";
                bad = true;
            }
            for (int s = 1; s < NSTARTS && !bad; s++) {
                if (runs[s].empty()) {
                    verdict = std::string("the ") + kStarts[s].name + " run failed";
                    bad = true;
                    break;
                }
                for (int f = 0; f < FRAMES; f++) {
                    if (runs[s][f] != runs[0][f]) {
                        verdict = std::string("frame ") + std::to_string(f) + " from " + kStarts[s].name +
                                  " differs from the 1 minute run";
                        bad = true;
                        break;
                    }
                }
            }
            if (!bad) {
                verdict = "OK (" + std::to_string(moving) + " of " + std::to_string(FRAMES - 1) + " steps moved)";
            }
        }
        if (bad && enforced) {
            failing++;
            printf("%-11s FAIL %s\n", animId, verdict.c_str());
        } else if (bad) {
            printf("%-11s still on absolute time: %s\n", animId, verdict.c_str());
        } else {
            printf("%-11s %s\n", animId, verdict.c_str());
        }
    }
    if (checked == 0) {
        printf("no animation matched\n");
        return 1;
    }
    printf("\n%s (%d failing, %dx%d)\n", failing ? "CLOCK CHECK FAILED" : "CLOCK CHECK OK", failing, size, size);
    return failing ? 1 : 0;
}
