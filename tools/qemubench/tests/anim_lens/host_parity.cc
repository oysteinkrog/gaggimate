// Host-only sanitizer check of the device dispatch glue via portable kernel
// twins. The registered non-Xtensa entry calls bandRef, so this test calls
// bandAsm directly. Run ./check_host.sh from this directory. build.sh scans
// .c and .cpp, leaving this .cc test out of the freestanding QEMU image.
#include "src/display/ui/default/bganim/AnimLens.cpp"
#include <cstdio>
#include <vector>

static int check_alignment_fallback() {
    if (!bg_anim_lens.init(480,480)) return 2;
    uint16_t *savedStage = stage8, *savedConst = pieConst;
    alignas(16) uint16_t scratch[56], constants[80], got[976], want[976];
    unsigned calls = 0, fallbacks = 0;
    for (int size : {0,55,100}) {
        const uint8_t p[BG_ANIM_PARAMS] = {50,static_cast<uint8_t>(size),62,0};
        bg_anim_lens.frame(1990,480,480,p);
        // These two rows include long feathers at the inner circle's top.
        const int y = lensY - innerRadius;
        for (int c = 0; c < 8; c++) {
            for (auto &v : constants) v = 0xa55a;
            pieConst = constants + 8 + c;
            for (int i = 0; i < 56; i++) pieConst[i] = savedConst[i];
            for (int a = 0; a < 8; a++) {
                stage8 = scratch + 8 + a;
                for (int o = 0; o < 8; o++) {
                    for (auto &v : scratch) v = 0xa55a;
                    for (int i = 0; i < 976; i++) got[i] = want[i] = 0xa55a;
                    bandAsm(got+8+o,y,2,480,1990,p);
                    bandRef(want+8+o,y,2,480,1990,p);
                    bool okay = true;
                    for (int i = 0; i < 976; i++) okay &= got[i] == want[i];
                    for (int i = 0; i < 80; i++)
                        okay &= constants[i] == (i >= 8+c && i < 64+c ? savedConst[i-8-c] : 0xa55a);
                    for (int i = 0; i < 56; i++)
                        if (c || a || i < 8+a || i >= 40+a) okay &= scratch[i] == 0xa55a;
                    if (!okay) {
                        std::printf("FAIL alignment size=%d constants=%d stage=%d dst=%d\n",size,c*2,a*2,o*2);
                        return 1;
                    }
                    calls++;
                    if (c || a) fallbacks++;
                }
            }
        }
    }
    stage8 = savedStage;
    pieConst = savedConst;
    bg_anim_lens.release();
    if (bganim::hotUsed() != 0) return 4;
    std::printf("PASS host alignment guard: %u band calls, %u fallbacks, table/dst offsets=0/2/4/6/8/10/12/14, size=0/55/100, mismatches=0, scratch untouched on fallback, guards=OK\n",calls,fallbacks);
    return 0;
}

int main() {
    const int alignment = check_alignment_fallback();
    if (alignment) return alignment;
    const int widths[] = {480,240,466,233};
    const uint32_t times[] = {0,1990,4960,7930,86400000u,0xffffffffu};
    unsigned calls = 0, rowsChecked = 0;
    for (int w : widths) {
        if (!bg_anim_lens.init(w,w)) return 2;
        if (bganim::hotUsed() != 9216) return 3;
        for (int setting = 0; setting < 16; setting++) {
            uint8_t p[BG_ANIM_PARAMS] = {50,55,62,0};
            if (setting < 8) for (int k = 0; k < 3; k++) p[k] = (setting & (1<<k)) ? 100 : 0;
            else for (int k = 0; k < 3; k++) p[k] = (setting * 37 + k * 19) % 101;
            const uint8_t theme[3][3] = {{0,0,0},{uint8_t(setting*17),uint8_t(255-setting*17),123},{255,255,255}};
            bganim::setThemeStops(theme,3);
            bganim::setThemeTone(setting & 1 ? 256 : 128,setting & 2 ? 76 : 255);
            for (uint32_t t : times) {
                bg_anim_lens.frame(t,w,w,p);
                for (int height : {1,2,4,8,16}) {
                    if ((w & 1) && height != 1) continue;
                    for (int offset = 0; offset < 8; offset += 2) {
                        std::vector<uint16_t> got(w*height+16,0x1357), want(w*height+16,0x1357);
                        for (int y = 0; y < w; y += height) {
                            const int rows = w-y < height ? w-y : height;
                            bandAsm(got.data()+offset,y,rows,w,t,p);
                            bg_anim_lens.bandRef(want.data()+offset,y,rows,w,t,p);
                            if (got != want) {
                                std::printf("FAIL width=%d set=%d t=%u offset=%d y=%d rows=%d\n",w,setting,t,offset,y,rows);
                                return 1;
                            }
                            calls++;
                            rowsChecked += rows;
                        }
                    }
                }
            }
        }
        bg_anim_lens.release();
        if (bganim::hotUsed() != 0) return 4;
    }
    std::printf("PASS host bandAsm/bandRef: %u calls, %u rows, widths=480/240/466/233, 16 parameter sets, 6 times, 4 alignments, 5 band heights, theme/tone changes, slab=9216 then 0\n",calls,rowsChecked);
}
