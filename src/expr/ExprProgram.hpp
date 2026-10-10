#pragma once

// ExprProgram.hpp -- the compiled form of an EXPR program, and its limits.
//
// A Program is immutable once built: the compiler (ExprCompiler.hpp) makes
// one, the module publishes it to the audio thread, and from then on nothing
// writes to it. Everything mutable -- registers, state, operator storage, the
// noise generator -- lives in an Instance (ExprRuntime.hpp), one per module,
// prepared from the Program before it is handed over.
//
// The executable form is compact register bytecode over a fixed library of
// primitives. The sample callback parses neither strings nor AST nodes; it
// walks this instruction list and nothing else.
//
// Header-only and free of <rack.hpp>, so the compiler and runtime can be
// tested without a Rack build (tests/expr).

#include <cstdint>
#include <string>
#include <vector>

namespace Lives {
namespace Expr {

// Language and schema version. Bumped when the grammar or the saved
// description changes meaning; patches carry it (Expr.cpp).
static const int kLanguageVersion = 2;

// Admission limits. These are what the compiler refuses to exceed, not a
// promise that every admitted program meets the CPU budget.
static const int    kMaxSlots        = 2;      // exposed parameters: EXPR's two letter knobs
static const int    kMaxInstructions = 1024;
static const int    kMaxRegisters    = 256;
static const int    kMaxStateSlots   = 128;    // explicit `state` scalars
static const int    kMaxOpStates     = 64;     // stateful call sites
static const int    kMaxNesting      = 64;
static const size_t kMaxSourceBytes  = 16384;

/// The fixed primitives. A program is a list of these over numbered
/// registers; operands a/b/c name registers, imm is an index or a jump
/// target depending on the opcode.
enum Op : uint8_t {
    OP_CONST,        // dst = constants[imm]
    OP_INPUT,        // dst = inputs[imm]            (Input enum below)
    OP_PARAM,        // dst = params[imm]            (slot 0..5)
    OP_LOAD_STATE,   // dst = stage[imm]             (sequential semantics)
    OP_STORE_STATE,  // stage[imm] = a
    OP_MOV,          // dst = a
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_MOD,
    OP_NEG, OP_ABS, OP_SIGN, OP_FLOOR, OP_CEIL, OP_ROUND, OP_FRAC,
    OP_EXP, OP_LOG, OP_LOG2, OP_LOG10, OP_SQRT,
    OP_SIN, OP_COS, OP_TAN, OP_ASIN, OP_ACOS, OP_ATAN, OP_ATAN2,
    OP_SINH, OP_COSH, OP_TANH,
    OP_MIN, OP_MAX, OP_CLAMP,
    OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE,
    OP_NOT, OP_BOOL,  // BOOL: dst = (a != 0) ? 1 : 0
    OP_JZ,           // if a == 0 goto imm
    OP_JNZ,          // if a != 0 goto imm
    OP_JMP,          // goto imm
    OP_NOISE,        // dst = seeded uniform [-1, 1]; op state imm
    OP_LAG,          // dst = one-pole toward a with tau b; op state imm
    OP_SLEW,         // dst = slew toward a, rise b, fall c (units/s); imm
    OP_PREV,         // dst = previous sample's a; op state imm
    OP_UNDEF,        // fault: the value is undefined here (imm: Undefined)
    OP_RET,          // return the point (a, b): X out and Y out
    OP_COUNT
};

/// Why OP_UNDEF fired: Desmos leaves a gap where these are undefined.
enum Undefined : uint8_t {
    UNDEF_NO_BRANCH,   // a piecewise without an else, outside every condition
    UNDEF_INDEX,       // a list index that is not a whole number in range
};

/// Which variable the equation is written in, and so how X and Y relate
/// to it (doc/decisions/0008-desmos-is-the-language.md).
enum Mode : uint8_t {
    MODE_X,        // y = f(x):          X = x,           Y = f(x)
    MODE_POLAR,    // r = f(θ):          X = r cos θ,     Y = r sin θ
    MODE_PARAM,    // (x(t), y(t)):      X = x(t),        Y = y(t)
};

/// What a program can read besides its parameters. Numbers are the imm of
/// OP_INPUT and the index into Inputs::v, so they are append-only.
enum Input : uint8_t {
    IN_X,      // function argument, from PHASE/X/manual (Expr.cpp)
    IN_U,      // normalized position 0..1 when PHASE is the source, else 0
    IN_T,      // seconds since the last RESET (or program start)
    IN_DT,     // 1 / sampleRate
    IN_SR,     // sampleRate
    IN_TRIG,   // 1 on the sample a TRIG edge landed, else 0
    IN_COUNT
};

struct Instruction {
    Op       op;
    uint16_t dst, a, b, c;
    int32_t  imm;
    uint16_t line, col;   // source position, for fault reports
};

/// A declared parameter: what a slot means for this program.
struct ParamDecl {
    std::string name;
    int    slot    = 0;     // 0-based physical slot
    double def     = 0.0;
    double min     = 0.0;
    double max     = 1.0;
    double cvScale = 1.0;   // parameter units per volt; may be negative
    bool   generic = false; // range was not declared; defaults were used
};

/// A persistent scalar: `state name = init`.
struct StateDecl {
    std::string name;
    double init = 0.0;
};

/// The immutable executable program plus the metadata the host needs to
/// bind its knobs and draw its display.
struct Program {
    std::string name;                      // from `name "..."`, or the source's first line
    std::string source;                    // the committed text, verbatim
    std::vector<Instruction> code;
    std::vector<double>      constants;
    std::vector<ParamDecl>   params;       // in declaration order
    std::vector<StateDecl>   states;
    int numRegisters = 0;
    int numOpStates  = 0;
    // A pure program is a memoryless function of x and its parameters: no
    // state, no stateful operators, no clock. Only those have a y(x) curve.
    bool pure = true;
    bool usesX = false;
    std::vector<std::string> warnings;
    Mode mode = MODE_X;
    // The domain, from a restriction at the end of the equation,
    // `{-8 ≤ x ≤ 8}`: the swept variable's range across one lap. Without
    // one the host uses its default (Expr.cpp). lo < hi when present.
    bool   hasDomain = false;
    double domainLo = 0.0, domainHi = 0.0;

    /// The declaration bound to a physical slot, or null.
    const ParamDecl* slot(int s) const {
        for (size_t i = 0; i < params.size(); i++)
            if (params[i].slot == s) return &params[i];
        return 0;
    }
};

}  // namespace Expr
}  // namespace Lives
