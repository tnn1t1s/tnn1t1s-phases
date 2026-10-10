#pragma once

// ExprRuntime.hpp -- executing a Program, one sample at a time.
//
// An Instance is the mutable half of a program: registers, the explicit
// `state` scalars, the per-call-site operator storage (noise generators,
// lag/slew/prev memories) and the seed. One Instance per module, prepared
// from its Program on the UI thread and then owned by the audio thread.
//
// run() is the audio-thread contract: fixed-size arrays, a forward-only
// instruction walk bounded by the program length, no allocation, no
// exceptions. Every arithmetic fault -- divide by zero, log of a
// nonpositive, invalid power, zero modulus, a nonfinite result --
// aborts the sample with its source position.
//
// State writes are staged. run() works on a copy of the state and operator
// storage and commits it only if the whole sample succeeded, so a fault
// cannot poison a filter or a feedback path: the previous valid state is
// what the next sample starts from. The noise generators are part of that
// transaction.
//
// Sequential semantics: a state variable read before its assignment in the
// sample block yields the previous sample's value; read after, the new one.
// That is what the stage gives for free.

#include "expr/ExprProgram.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Lives {
namespace Expr {

/// What one sample's evaluation reads. v is indexed by Input; param by slot.
struct Inputs {
    double v[IN_COUNT];
    double param[kMaxSlots];
    Inputs() {
        for (int i = 0; i < IN_COUNT; i++) v[i] = 0.0;
        for (int i = 0; i < kMaxSlots; i++) param[i] = 0.0;
    }
};

struct Result {
    double value = 0.0;   // Y
    double x     = 0.0;   // X
    bool   fault = false;
    int    faultLine = 0, faultCol = 0;
    const char* faultWhat = "";
};

/// Storage for one stateful call site. Which fields mean what depends on
/// the operator: lag/slew/prev keep their memory in `a`; noise keeps its
/// generator in `rng`.
struct OpState {
    double   a   = 0.0;
    bool     set = false;   // prev/slew: has a first sample been seen
    uint32_t rng = 0;
};

struct Instance {
    const Program* prog = 0;      // non-owning; whoever holds the Instance holds the Program
    uint32_t seed = 1978;

    double  regs[kMaxRegisters];
    double  state[kMaxStateSlots];
    double  stage[kMaxStateSlots];
    OpState ops[kMaxOpStates];
    OpState opsStage[kMaxOpStates];
    uint32_t faultCount = 0;

    Instance() {
        for (int i = 0; i < kMaxRegisters; i++) regs[i] = 0.0;
        for (int i = 0; i < kMaxStateSlots; i++) state[i] = stage[i] = 0.0;
    }

    /// Bind to a program and initialize. UI thread, before handoff.
    void prepare(const Program* p, uint32_t s) {
        prog = p;
        seed = s;
        reset();
    }

    /// Program RESET: every state scalar back to its declared initial
    /// value, every operator memory cleared, every noise generator
    /// reseeded. Bounded by the program's declared counts.
    void reset() {
        if (!prog) return;
        const int ns = int(prog->states.size());
        for (int i = 0; i < ns; i++) state[i] = prog->states[i].init;
        for (int i = 0; i < prog->numOpStates; i++) {
            ops[i].a = 0.0;
            ops[i].set = false;
            // Each site gets its own stream: the seed stirred by the site
            // index with a golden-ratio constant, so two noise() calls in
            // one program never produce the same sequence.
            ops[i].rng = seed ^ (uint32_t(i + 1) * 0x9E3779B9u);
            if (ops[i].rng == 0) ops[i].rng = 0x1978u;
        }
        faultCount = 0;
    }

    /// Uniform [-1, 1] from the site's generator: a 32-bit LCG (Numerical
    /// Recipes constants), the high bits used. Integer, deterministic,
    /// identical on every platform.
    static double noiseStep(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return double(s >> 8) * (2.0 / 16777216.0) - 1.0;
    }

    /// One sample. Reads only `in`, writes only this Instance.
    Result run(const Inputs& in) {
        Result r;
        if (!prog) return r;
        const Instruction* code = prog->code.empty() ? 0 : &prog->code[0];
        const int n = int(prog->code.size());
        const int ns = int(prog->states.size());
        const int no = prog->numOpStates;
        for (int i = 0; i < ns; i++) stage[i] = state[i];
        for (int i = 0; i < no; i++) opsStage[i] = ops[i];

        const double* K = prog->constants.empty() ? 0 : &prog->constants[0];
        double* R = regs;
        const char* what = 0;
        int pc = 0;
        // Jumps only go forward, so n steps is the ceiling; the counter is
        // belt and braces against a malformed program.
        int steps = 0;
        while (pc < n && steps++ <= n) {
            const Instruction& I = code[pc++];
            double v = 0.0;
            switch (I.op) {
            case OP_CONST:      v = K[I.imm]; break;
            case OP_INPUT:      v = in.v[I.imm]; break;
            case OP_PARAM:      v = in.param[I.imm]; break;
            case OP_LOAD_STATE: v = stage[I.imm]; break;
            case OP_STORE_STATE: stage[I.imm] = R[I.a]; continue;
            case OP_MOV:        v = R[I.a]; break;
            case OP_ADD:        v = R[I.a] + R[I.b]; break;
            case OP_SUB:        v = R[I.a] - R[I.b]; break;
            case OP_MUL:        v = R[I.a] * R[I.b]; break;
            case OP_DIV:
                if (R[I.b] == 0.0) { what = "divide by zero"; goto fault; }
                v = R[I.a] / R[I.b];
                break;
            case OP_POW:
                v = std::pow(R[I.a], R[I.b]);
                if (!std::isfinite(v)) { what = "invalid power"; goto fault; }
                break;
            case OP_MOD: {
                const double m = R[I.b];
                // Floored, as Desmos: a negative modulus gives a result in (m, 0].
                if (m == 0.0) { what = "modulus of zero"; goto fault; }
                v = R[I.a] - m * std::floor(R[I.a] / m);
                break;
            }
            case OP_NEG:   v = -R[I.a]; break;
            case OP_ABS:   v = std::fabs(R[I.a]); break;
            case OP_SIGN:  v = R[I.a] > 0.0 ? 1.0 : (R[I.a] < 0.0 ? -1.0 : 0.0); break;
            case OP_FLOOR: v = std::floor(R[I.a]); break;
            case OP_CEIL:  v = std::ceil(R[I.a]); break;
            case OP_ROUND: v = std::floor(R[I.a] + 0.5); break;
            case OP_FRAC:  v = R[I.a] - std::floor(R[I.a]); break;
            case OP_EXP:   v = std::exp(R[I.a]); break;
            case OP_LOG:
                if (!(R[I.a] > 0.0)) { what = "log of a nonpositive"; goto fault; }
                v = std::log(R[I.a]); break;
            case OP_LOG2:
                if (!(R[I.a] > 0.0)) { what = "log2 of a nonpositive"; goto fault; }
                v = std::log2(R[I.a]); break;
            case OP_LOG10:
                if (!(R[I.a] > 0.0)) { what = "log10 of a nonpositive"; goto fault; }
                v = std::log10(R[I.a]); break;
            case OP_SQRT:
                if (R[I.a] < 0.0) { what = "sqrt of a negative"; goto fault; }
                v = std::sqrt(R[I.a]); break;
            case OP_SIN:   v = std::sin(R[I.a]); break;
            case OP_COS:   v = std::cos(R[I.a]); break;
            case OP_TAN:   v = std::tan(R[I.a]); break;
            case OP_ASIN:
                if (std::fabs(R[I.a]) > 1.0) { what = "asin outside [-1, 1]"; goto fault; }
                v = std::asin(R[I.a]); break;
            case OP_ACOS:
                if (std::fabs(R[I.a]) > 1.0) { what = "acos outside [-1, 1]"; goto fault; }
                v = std::acos(R[I.a]); break;
            case OP_ATAN:  v = std::atan(R[I.a]); break;
            case OP_ATAN2: v = std::atan2(R[I.a], R[I.b]); break;
            case OP_SINH:  v = std::sinh(R[I.a]); break;
            case OP_COSH:  v = std::cosh(R[I.a]); break;
            case OP_TANH:  v = std::tanh(R[I.a]); break;
            case OP_MIN:   v = R[I.a] < R[I.b] ? R[I.a] : R[I.b]; break;
            case OP_MAX:   v = R[I.a] > R[I.b] ? R[I.a] : R[I.b]; break;
            case OP_CLAMP: {
                double lo = R[I.b], hi = R[I.c];
                v = R[I.a] < lo ? lo : (R[I.a] > hi ? hi : R[I.a]);
                break;
            }
            case OP_LT: v = R[I.a] <  R[I.b] ? 1.0 : 0.0; break;
            case OP_LE: v = R[I.a] <= R[I.b] ? 1.0 : 0.0; break;
            case OP_GT: v = R[I.a] >  R[I.b] ? 1.0 : 0.0; break;
            case OP_GE: v = R[I.a] >= R[I.b] ? 1.0 : 0.0; break;
            case OP_EQ: v = R[I.a] == R[I.b] ? 1.0 : 0.0; break;
            case OP_NE: v = R[I.a] != R[I.b] ? 1.0 : 0.0; break;
            case OP_NOT:  v = R[I.a] == 0.0 ? 1.0 : 0.0; break;
            case OP_BOOL: v = R[I.a] != 0.0 ? 1.0 : 0.0; break;
            case OP_JZ:  if (R[I.a] == 0.0) pc = I.imm; continue;
            case OP_JNZ: if (R[I.a] != 0.0) pc = I.imm; continue;
            case OP_JMP: pc = I.imm; continue;
            case OP_NOISE:
                v = noiseStep(opsStage[I.imm].rng);
                break;
            case OP_LAG: {
                // One-pole toward the target; tau in seconds, 0 bypasses.
                // The coefficient is 1 - exp(-dt/tau), finite for any
                // positive tau and exactly 1 at tau = 0.
                OpState& s = opsStage[I.imm];
                const double x = R[I.a], tau = R[I.b], dt = in.v[IN_DT];
                if (!s.set) { s.a = x; s.set = true; }
                const double alpha = tau > 0.0 ? 1.0 - std::exp(-dt / tau) : 1.0;
                s.a += alpha * (x - s.a);
                v = s.a;
                break;
            }
            case OP_SLEW: {
                // Movement per sample bounded by rise/fall in units per
                // second; a zero rate holds in that direction.
                OpState& s = opsStage[I.imm];
                const double x = R[I.a], rise = R[I.b], fall = R[I.c], dt = in.v[IN_DT];
                if (rise < 0.0 || fall < 0.0) { what = "negative slew rate"; goto fault; }
                if (!s.set) { s.a = x; s.set = true; }
                double d = x - s.a;
                const double up = rise * dt, down = fall * dt;
                if (d > up) d = up;
                if (d < -down) d = -down;
                s.a += d;
                v = s.a;
                break;
            }
            case OP_PREV: {
                // The previous sample's argument; 0 on the first sample
                // after a reset.
                OpState& s = opsStage[I.imm];
                v = s.set ? s.a : 0.0;
                s.a = R[I.a];
                s.set = true;
                break;
            }
            case OP_UNDEF:
                what = I.imm == UNDEF_INDEX ? "list index out of range" : "no branch applies";
                goto fault;
            case OP_RET:
                if (!std::isfinite(R[I.a]) || !std::isfinite(R[I.b])) { what = "result is not finite"; goto fault; }
                r.x = R[I.a];
                r.value = R[I.b];
                // Commit the transaction.
                for (int i = 0; i < ns; i++) state[i] = stage[i];
                for (int i = 0; i < no; i++) ops[i] = opsStage[i];
                return r;
            default:
                what = "bad opcode";
                goto fault;
            }
            if (!std::isfinite(v)) { what = "result is not finite"; goto fault; }
            R[I.dst] = v;
            continue;
        fault:
            r.fault = true;
            r.faultWhat = what;
            r.faultLine = I.line;
            r.faultCol = I.col;
            faultCount++;
            return r;
        }
        // Fell off the end without OP_RET: the compiler never emits this.
        r.fault = true;
        r.faultWhat = "program did not return";
        faultCount++;
        return r;
    }
};

}  // namespace Expr
}  // namespace Lives
