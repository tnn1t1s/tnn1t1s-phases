#pragma once

// ExprExamples.hpp -- the equations in EXPR's right-click Examples menu.
//
// Rack-free so tests/expr compiles and runs every one. Patterns are
// equations transcribed from video frames (sources in doc/examples.md);
// each carries its own domain, and with both knobs at 1,
// their default, it is the transcribed formula exactly. Many swing several
// volts, so start GAIN low on an audio path. Basics are short equations on
// the default 0..2pi domain that close at the end of the lap with their
// knobs on whole numbers.

namespace Lives {
namespace Expr {

struct Example { const char* name; const char* source; };

static const Example kPatternExamples[] = {
    {"Kick and bursts (d, f)",
     "y = {mod(x + 3, 8) < 1: -2.5 - 0.833 exp((-mod(x + 3, 8))/(0.34d)), mod(x + 7, 8) < 0.5: 0.675 + 1.04 exp((-mod(mod(x + 7, 8), 0.175))/(0.025)) + 0.469 sin(110 mod(mod(x + 7, 8), 0.175)), mod(x + 2, 8) < 0.5: 0.675 + 1.04 exp((-mod(mod(x + 2, 8), 0.175))/(0.025)) + 0.469 sin(110 mod(mod(x + 2, 8), 0.175)), mod(x + 6, 8) < 1: -3.75 + 2.08 exp((-mod(x + 6, 8))/(1)), mod(x + 1, 8) < 1: -3.75 + 2.08 exp((-mod(x + 1, 8))/(1)), mod(x, 4) < 1: -3.89 + 3.78 exp((-mod(x, 4))/(0.2)) + 0.677 sin(25.1f mod(x, 4))} {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Sixteen-step rings (d, f)",
     "y = {mod(x + 3, 16) < 1: -2.5 - 2.5 exp((-mod(x + 3, 16))/(0.34)), mod(x + 7, 16) < 1: -3.75 + 2.5 exp((-mod(x + 7, 16))/(1)), mod(x + 9, 16) < 1: -3.75 + 2.5 exp((-mod(x + 9, 16))/(1)), mod(x, 2) < 1: -3.89 + 3.82 exp((-mod(x, 2))/(0.12d)) + 0.313 sin(25.1f mod(x, 2)), mod(x + 1, 16) < 1: -3.89 + 3.82 exp((-mod(x + 1, 16))/(0.12d)) + 0.313 sin(25.1f mod(x + 1, 16))} {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Ringing decays (d, f)",
     "y = {mod(x + 2, 4) < 0.6: -0.204 + 1.04 exp((-mod(x, 0.2))/(0.08)), mod(x + 3, 8) < 1: -3.75 + 2.08 exp((-mod(x + 3, 8))/(2)), mod(x, 2) < 2: -3.89 + 3.78 exp((-mod(x, 2))/(0.2d)) + 0.677 sin(37.7f mod(x, 2))} {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Bit dashes (g, k)",
     "y = {mod(x, 0.25) < 0.13g and mod(x + 8, 16) < 8: 0.53 mod(floor(4x) + floor(2x), 2) + 1.11 mod(floor(2x) + floor(x), 2) + 0.92 mod(floor(x) + floor((x)/(2)), 2) + 0.74 mod(floor((x)/(2)) + floor((x)/(4)), 2) + 1.49 e^(-2.24k mod(x, 2)) - 2.4, mod(x, 0.25) < 0.13g: -0.53 mod(floor(4x) + floor(2x), 2) - 1.11 mod(floor(2x) + floor(x), 2) - 0.92 mod(floor(x) + floor((x)/(2)), 2) - 0.74 mod(floor((x)/(2)) + floor((x)/(4)), 2) + 1.49 e^(-2.24k mod(x, 2)) + 0.9} {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Tanh and arches (p, w)",
     "y = {mod(x, 1.6p) < 0.8p: 2.4 tanh(2 sin((\xCF\x80 x)/(2))), -2.4 + 4.8 sqrt(max(0, 1 - 0.28w (mod(x, 4) - 2)^2))} {-16 \xE2\x89\xA4 x \xE2\x89\xA4 16}"},
    {"Exp ramps and arches (p, w)",
     "y = {mod(x, 1.6p) < 0.8p: 0.09 (exp(4 mod((x)/(8), 1)) - 1) - 2.4, -2.4 + 4.8 sqrt(max(0, 1 - 1.95w (mod(x, 2) - 1)^2))} {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Shrinking arches (g, d)",
     "y = {mod(x, 2) < g: min(2.4, -0.95 + 4.9 * 0.69^(floor((mod(x + 8, 16))/(4))) (1 - (2 mod((mod(x + 8, 16))/(4), 1) - 1)^2)), max(-2.4, 2.45 tanh(4d sin((\xCF\x80 x)/(2))) - 1.5)} {-16 \xE2\x89\xA4 x \xE2\x89\xA4 16}"},
    {"Parabola gates (g, d)",
     "y = {mod(x, 1) >= 0.55g: (1)/(5) x^2 - 5, mod(x, 1.6) < 0.8: 1.49 tanh(5d sin((\xCF\x80 x)/(2))) + 0.91, -2.4 + 2.98 sqrt(max(0, 1 - 3.11 (mod(x, 2) - 1)^2))} {-24 \xE2\x89\xA4 x \xE2\x89\xA4 24}"},
    {"Domes (w, b)",
     "y = 3 sqrt(max(0, 1 - (w)/(3) (mod(x, 4) - 2)^2)) + 0.3b sqrt(max(0, 1 - 20 (mod(x, 4) - 2)^2)) {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Log saw wobble (a, f)",
     "y = 1.53 log_2(1 + 6a mod((-x)/(4), 1)) - 2.14 + 0.26 sin(4\xCF\x80 f x) {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}"},
    {"Tan chirp (a, b)",
     "y = (a)/(10) tan(x) + sin(mod(x, sin(b x^2))) {-3 \xE2\x89\xA4 x \xE2\x89\xA4 3}"},
    {"Nested mods (a, b)",
     "y = mod(mod(mod(mod(sin(a x), cos(b x)), x^2), x), 1) - 0.5 {-2 \xE2\x89\xA4 x \xE2\x89\xA4 2}"},
    {"Tan of nested mod (a, b)",
     "y = tan(mod(a x, mod(sin(b x), x))) {-8.5 \xE2\x89\xA4 x \xE2\x89\xA4 8.5}"},
};

static const Example kBasicExamples[] = {
    {"Sine", "y = sin(x)"},
    {"Harmonics (knobs a, b)", "y = sin(ax) + sin(bx)/2"},
    {"Damped (knobs a, b)", "y = sin(ax) / cosh(b(x - \xCF\x80))"},
    {"Half and half", "y = {x < \xCF\x80: sin x, -sin(2x)}"},
    {"Pentatonic", "y = [0,2,4,7,9][floor(mod(5x/(2\xCF\x80), 5)) + 1] / 12"},
    {"Folded", "y = 2.4sin(1.32|2mod(x/\xCF\x80, 1) - 1|)"},
    {"Rose (polar, knob k)", "r = cos(k\xCE\xB8)"},
    {"Lissajous (parametric)", "(cos(3t), sin(2t))"},
};

static const int kPatternExampleCount = int(sizeof(kPatternExamples) / sizeof(kPatternExamples[0]));
static const int kBasicExampleCount = int(sizeof(kBasicExamples) / sizeof(kBasicExamples[0]));

}  // namespace Expr
}  // namespace Lives
