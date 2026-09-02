// Semantic-trace micro-interpreter for the exact EE.* (Xtensa PIE) sequence
// in blend_group8.S.
//
// This is NOT a general Xtensa/PIE emulator -- it models only the six
// mnemonics blend_group8.S actually uses (ee.vld.128.ip, ee.vst.128.ip,
// ee.andq, ee.orq, ee.vmul.u16, ee.vadds.s16) plus ssai (the SAR-set
// instruction), each exactly as documented for this ISA:
//
//   ee.andq  qD, qA, qB   -- eight lanes, qD[i] = qA[i] & qB[i]  (16-bit)
//   ee.orq   qD, qA, qB   -- eight lanes, qD[i] = qA[i] | qB[i]  (16-bit)
//   ee.vmul.u16 qD, qA, qB -- eight lanes, qD[i] = truncate16((u32)qA[i] *
//                             (u32)qB[i] >> SAR)  -- SAR is a HARDWARE
//                             REGISTER threaded through `ssai`, not an
//                             instruction operand, which is exactly the
//                             correctness risk this file exists to check:
//                             every vmul.u16 in the trace picks up whatever
//                             SAR the most recent ssai left behind.
//   ee.vadds.s16 qD, qA, qB -- eight lanes, signed 16-bit SATURATING add
//                             (the only vector add this ISA has -- no
//                             unsigned, no non-saturating variant).
//   ee.vld.128.ip qD, aN, imm -- qD = next 8 lanes from the stream bound
//                             to pointer aN; aN "advances" by imm bytes
//                             (modeled as popping the stream, not as an
//                             address -- see Interp::run).
//   ee.vst.128.ip qS, aN, imm -- push qS's 8 lanes to the stream bound to
//                             pointer aN.
//   ssai N                  -- SAR := N (0..31).
//
// The instruction list itself is PARSED DIRECTLY FROM blend_group8.S at
// test time (see parseAsmFile below), not hand-copied into a separate
// data structure -- that is what makes this a proof about the actual file
// that was fed to the real assembler and xtensa_report.py, not about a
// close-but-independently-maintained transcription of it. If
// blend_group8.S ever changes, this interpreter automatically re-executes
// whatever is there; it cannot silently drift out of sync with it.
//
// What this DOES prove: that the exact mnemonic+operand+SAR sequence in
// blend_group8.S, interpreted per each instruction's documented semantics,
// reproduces blend565_ref bit-for-bit for every lane, over randomized
// inputs -- i.e. the arithmetic is right IF the documented semantics are
// right and IF the real hardware's SAR/ssai timing matches what this
// interpreter assumes (SAR takes effect immediately for the next
// instruction, with no pipeline delay).
//
// What this does NOT prove: that the real ESP32-S3 PIE coprocessor
// actually implements ee.vmul.u16/ee.vadds.s16/ssai this way (no way to
// execute this on the host, and no device/QEMU run was available for this
// pass -- see the report), or that there is no hardware erratum, or that
// register-allocation/scheduling assumptions elsewhere (e.g. whatever
// wraps this asm block in blend_pie_kernel.cpp) are correct. This is the
// strongest proof available without real execution, not a replacement for
// one.
#pragma once
#include "blend_ref.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace blendopt::interp {

struct Instr {
    std::string op;
    std::vector<std::string> args; // raw operand tokens, e.g. {"q5","q0","q4"} or {"q7","a7","16"} or {"11"}
};

// Parse blend_group8.S: keep only recognized instruction lines, in order,
// stripping labels, directives (lines starting with '.'), comments
// ("// ..."), and the trailing "ret.n".
inline std::vector<Instr> parseAsmFile(const std::string &path) {
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("cannot open " + path);
    }
    static const std::vector<std::string> kKnownOps = {
        "ee.vld.128.ip", "ee.vst.128.ip", "ee.andq", "ee.orq", "ee.vmul.u16", "ee.vadds.s16", "ssai",
    };
    std::vector<Instr> out;
    std::string line;
    while (std::getline(f, line)) {
        // Strip "//" comment.
        auto cpos = line.find("//");
        if (cpos != std::string::npos) {
            line = line.substr(0, cpos);
        }
        // Trim.
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        if (line.empty() || line[0] == '.' || line.back() == ':') {
            continue;
        }
        // Split mnemonic from operand list.
        auto sp = line.find_first_of(" \t");
        std::string op = (sp == std::string::npos) ? line : line.substr(0, sp);
        bool known = false;
        for (const auto &k : kKnownOps) {
            if (op == k) {
                known = true;
                break;
            }
        }
        if (!known) {
            continue; // e.g. "ret.n" -- not part of the vector arithmetic trace
        }
        std::vector<std::string> args;
        if (sp != std::string::npos) {
            std::string rest = line.substr(sp + 1);
            std::stringstream ss(rest);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                auto a0 = tok.find_first_not_of(" \t");
                auto a1 = tok.find_last_not_of(" \t");
                if (a0 != std::string::npos) {
                    args.push_back(tok.substr(a0, a1 - a0 + 1));
                }
            }
        }
        out.push_back({op, args});
    }
    return out;
}

using Lane8 = std::array<uint16_t, 8>;

// Interprets one blendGroup8General invocation: eight independent (fg, bg,
// a) lanes in, one blended Lane8 out. `program` is the instruction list
// parsed straight from blend_group8.S.
inline Lane8 runGroup8(const std::vector<Instr> &program, const Lane8 &fg, const Lane8 &bg, const Lane8 &a,
                        const Lane8 &inv) {
    // Constant table, in the exact load order blend_group8.S consumes
    // from a7 (ct): ones, maskR, constR2048, maskG, constG32, maskB.
    static const Lane8 kOnes = {1, 1, 1, 1, 1, 1, 1, 1};
    static const Lane8 kMaskR = {0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800};
    static const Lane8 kConstR2048 = {2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048};
    static const Lane8 kMaskG = {0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0};
    static const Lane8 kConstG32 = {32, 32, 32, 32, 32, 32, 32, 32};
    static const Lane8 kMaskB = {0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F};

    // Streams bound to each pointer register (see blend_group8.S's header
    // comment for the a2..a7 argument convention). Each ee.vld.128.ip pops
    // the front chunk; ee.vst.128.ip on a3 is where the result lands.
    std::vector<Lane8> streamA2 = {bg};                                                          // rd  (read dst/bg)
    std::vector<Lane8> streamA4 = {fg};                                                           // col (fg)
    std::vector<Lane8> streamA5 = {a};                                                            // av  (alpha)
    std::vector<Lane8> streamA6 = {inv};                                                          // iv  (inv)
    std::vector<Lane8> streamA7 = {kOnes, kMaskR, kConstR2048, kMaskG, kConstG32, kMaskB};         // ct
    size_t idxA2 = 0, idxA4 = 0, idxA5 = 0, idxA6 = 0, idxA7 = 0;
    Lane8 storedResult{};
    bool stored = false;

    std::array<Lane8, 8> q{}; // q0..q7
    int sar = 0;

    auto qIndex = [](const std::string &tok) -> int {
        if (tok.size() < 2 || tok[0] != 'q') {
            throw std::runtime_error("expected q-register operand, got: " + tok);
        }
        return std::stoi(tok.substr(1));
    };

    for (const auto &ins : program) {
        if (ins.op == "ssai") {
            sar = std::stoi(ins.args.at(0));
        } else if (ins.op == "ee.vld.128.ip") {
            const int qd = qIndex(ins.args.at(0));
            const std::string &ptr = ins.args.at(1);
            Lane8 val{};
            if (ptr == "a2") {
                val = streamA2.at(idxA2++);
            } else if (ptr == "a4") {
                val = streamA4.at(idxA4++);
            } else if (ptr == "a5") {
                val = streamA5.at(idxA5++);
            } else if (ptr == "a6") {
                val = streamA6.at(idxA6++);
            } else if (ptr == "a7") {
                val = streamA7.at(idxA7++);
            } else {
                throw std::runtime_error("ee.vld.128.ip from unexpected pointer register: " + ptr);
            }
            q[static_cast<size_t>(qd)] = val;
        } else if (ins.op == "ee.vst.128.ip") {
            const int qs = qIndex(ins.args.at(0));
            const std::string &ptr = ins.args.at(1);
            if (ptr != "a3") {
                throw std::runtime_error("ee.vst.128.ip to unexpected pointer register: " + ptr);
            }
            storedResult = q[static_cast<size_t>(qs)];
            stored = true;
        } else if (ins.op == "ee.andq" || ins.op == "ee.orq") {
            const int qd = qIndex(ins.args.at(0));
            const int qa = qIndex(ins.args.at(1));
            const int qb = qIndex(ins.args.at(2));
            Lane8 result{};
            for (int i = 0; i < 8; i++) {
                result[static_cast<size_t>(i)] = (ins.op == "ee.andq")
                                                      ? static_cast<uint16_t>(q[static_cast<size_t>(qa)][static_cast<size_t>(i)] &
                                                                              q[static_cast<size_t>(qb)][static_cast<size_t>(i)])
                                                      : static_cast<uint16_t>(q[static_cast<size_t>(qa)][static_cast<size_t>(i)] |
                                                                              q[static_cast<size_t>(qb)][static_cast<size_t>(i)]);
            }
            q[static_cast<size_t>(qd)] = result;
        } else if (ins.op == "ee.vmul.u16") {
            const int qd = qIndex(ins.args.at(0));
            const int qa = qIndex(ins.args.at(1));
            const int qb = qIndex(ins.args.at(2));
            Lane8 result{};
            for (int i = 0; i < 8; i++) {
                const uint32_t prod = static_cast<uint32_t>(q[static_cast<size_t>(qa)][static_cast<size_t>(i)]) *
                                       static_cast<uint32_t>(q[static_cast<size_t>(qb)][static_cast<size_t>(i)]);
                result[static_cast<size_t>(i)] = static_cast<uint16_t>((prod >> sar) & 0xFFFFu);
            }
            q[static_cast<size_t>(qd)] = result;
        } else if (ins.op == "ee.vadds.s16") {
            const int qd = qIndex(ins.args.at(0));
            const int qa = qIndex(ins.args.at(1));
            const int qb = qIndex(ins.args.at(2));
            Lane8 result{};
            for (int i = 0; i < 8; i++) {
                const int16_t la = static_cast<int16_t>(q[static_cast<size_t>(qa)][static_cast<size_t>(i)]);
                const int16_t lb = static_cast<int16_t>(q[static_cast<size_t>(qb)][static_cast<size_t>(i)]);
                int32_t sum = static_cast<int32_t>(la) + static_cast<int32_t>(lb);
                if (sum > 32767) {
                    sum = 32767;
                } else if (sum < -32768) {
                    sum = -32768;
                }
                result[static_cast<size_t>(i)] = static_cast<uint16_t>(static_cast<int16_t>(sum));
            }
            q[static_cast<size_t>(qd)] = result;
        } else {
            throw std::runtime_error("interpreter does not model opcode: " + ins.op);
        }
    }
    if (!stored) {
        throw std::runtime_error("program never executed ee.vst.128.ip to a3 -- no result produced");
    }
    return storedResult;
}

} // namespace blendopt::interp
