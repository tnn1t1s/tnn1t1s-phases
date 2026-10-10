// PHASES EXPR -- a live-compiled equation on the wire.
//
// Type an expression, or a stateful equation block, press Apply, and it is
// a Rack voltage while the patch keeps running. The program is compiled on
// the UI thread (src/expr/ExprCompiler.hpp), prepared whole -- bytecode,
// state, seeded noise -- and handed to the audio thread through one atomic
// pointer, which takes it at a sample boundary and hands the old one back
// the same way for the UI thread to free. A program that fails to compile
// changes nothing: the previous one keeps playing, and the editor says
// where the error is.
//
// The wire is the transport, as for every follower: PHASE in, cable first
// and then the family module touching this one (Lives::resolveWire), and
// the resolved wire is republished so the chain continues. 0-10 V maps
// across one slice of the domain: the domain is the equation's trailing
// restriction, `{-8 ≤ x ≤ 8}`, or 0..2π without one; LOOP cuts it into
// equal slices and POS picks one (src/expr/ExprDomain.hpp). A cable
// on X is a direct function argument instead (1 V = one x unit, plus the
// manual X knob); with neither, the X knob alone drives a static
// evaluation. The active source is on the status strip.
//
// This module extends the position-wire design in two explicit ways,
// recorded in doc/decisions/0007: a stationary wire can legitimately
// produce a nonzero DC voltage (a constant program is a voltage), and a
// stateful program runs in sample time whether or not the wire moves.
// There is no motor ramp here; EXPR is not a media player.
//
// Events are explicit. TRIG and RST are Schmitt edges; a wrap on the wire
// is never promoted to a trigger, because a position wire alone cannot tell
// a wrap from a reset, a scrub or a source switch. Per sample: accept a
// prepared program, read inputs, apply RST, apply TRIG, run, condition,
// record history.
//
// Output is volts only: a program result of 1 is 1 V at unity gain, then
// offset, a hard or soft-knee limiter, optional lag or slew, and a final
// finite/range guard (src/expr/ExprCondition.hpp). No pitch modes, no
// scales; patch those downstream.

#include <rack.hpp>
#include "AgentModule.hpp"
#include "SvgHelper.hpp"
#include "lives/PhaseWire.hpp"
#include "expr/ExprCompiler.hpp"
#include "expr/ExprCondition.hpp"
#include "expr/ExprDomain.hpp"
#include "expr/ExprExamples.hpp"
#include "expr/ExprHistory.hpp"
#include "expr/ExprMath.hpp"
#include "expr/ExprPreview.hpp"
#include "expr/ExprRuntime.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace rack;

extern Plugin* pluginInstance;

namespace X = Lives::Expr;

// The program a new module plays: one sine per lap on the default domain,
// so a freshly dropped EXPR beside PHASE makes a signal before anyone opens
// the editor.
static const char* kDefaultSource = "sin(x)";

static const double kTwoPi = 6.28318530717958647692;

/// The DOMAIN knob's former positions, {low, high}, kept to convert
/// patches saved with it into a restriction on the equation.
struct LegacyDomain { double lo, hi; };
static const LegacyDomain kLegacyDomains[] = {
    {0, kTwoPi}, {-M_PI, M_PI}, {0, 1}, {-1, 1}, {0, 10}, {-10, 10}, {0, 16}, {-32, 32},
};
static const int kLegacyDomainCount = sizeof(kLegacyDomains) / sizeof(kLegacyDomains[0]);

/// LOOP's labels, one per X::kLoopDivisions entry.
static const std::vector<std::string> kLoopLabels = {
    "1/16", "1/12", "1/8", "1/6", "1/4", "1/3", "1/2", "whole domain"};
/// LOOP fully clockwise: the whole domain (default).
static const float kLoopWhole = float(X::kLoopCount - 1);

/// POS: which slice LOOP plays, shown as "2 of 4".
struct PosQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

/// A program and its execution state, moved between threads as one unit.
struct Prepared {
    std::shared_ptr<const X::Program> program;
    X::Instance instance;
    uint32_t revision = 0;
};

/// What the UI needs to know about a slot: a copy of the declaration, so
/// the knob quantities never reach into a program the audio thread may be
/// retiring. Written by apply() on the UI thread, read on the UI thread.
struct SlotMeta {
    bool used = false;
    std::string name;
    double def = 0, min = 0, max = 1, cvScale = 1;
    bool generic = false;
};

/// A knob's value: its 0..1 position across [min, max], plus CV, rounded
/// to a whole number unless the knob is set to fine steps, then clamped.
/// Whole numbers keep sin(bx) closed on a 0..2π lap (DR-0008).
static double knobValue(double min, double max, double knob, double cv, bool fine) {
    double v = min + knob * (max - min) + cv;
    if (!fine) v = std::round(v);
    if (v < min) v = min;
    if (v > max) v = max;
    return v;
}

struct Expr;

/// The slot knob: stored 0..1, displayed in the declared range and units.
struct SlotQuantity : ParamQuantity {
    Expr* expr() const;
    const SlotMeta& meta() const;
    int slotIndex() const;
    std::string getLabel() override;
    std::string getDescription() override;
    float getDisplayValue() override;
    void setDisplayValue(float v) override;
    std::string getDisplayValueString() override;
};

struct Expr : AgentModule {
    // Rack saves params and inputs by id. Ids keep the layout of the six-slot
    // panel with X and TRIG (invariant): slots 3 to 6, X and TRIG stay
    // configured but have no control, so saved patches load into the same
    // ids. GAIN_CV and LIMIT_CV come after the slot CVs, and LOOP_CV and
    // POS_CV after those, for the same reason.
    static constexpr int kSavedSlots = 6;
    enum ParamId { P_PARAM, X_PARAM = P_PARAM + kSavedSlots, GAIN_PARAM, LIMIT_PARAM, DOMAIN_PARAM,
                   LOOP_PARAM, POS_PARAM, NUM_PARAMS };
    enum InputId { PHASE_INPUT, X_INPUT, TRIG_INPUT, RST_INPUT, CV_INPUT,
                   GAIN_CV_INPUT = CV_INPUT + kSavedSlots, LIMIT_CV_INPUT,
                   LOOP_CV_INPUT, POS_CV_INPUT, NUM_INPUTS };
    // X carries the curve's x coordinate (r cos θ in polar, x(t) in
    // parametric, the swept x itself on a y = curve), conditioned like Y,
    // so X and Y together draw the curve on an X-Y scope.
    enum OutputId { Y_OUTPUT, X_OUTPUT, NUM_OUTPUTS };
    enum LightId { FAULT_LIGHT, NUM_LIGHTS };

    enum Source { SRC_NONE, SRC_CABLE, SRC_ADJACENT };

    // --- program handoff ----------------------------------------------------
    // UI -> audio: publish() swaps the new program in; the audio thread takes
    // it with exchange(nullptr). Latest wins: a program still pending when
    // the next is published is freed by the UI thread, unseen.
    std::atomic<Prepared*> pending{nullptr};
    // audio thread only
    Prepared* active   = nullptr;
    Prepared* outgoing = nullptr;    // crossfading out
    int fadeRemaining = 0, fadeLength = 0;
    X::Conditioner condOut, condOutX;   // the outgoing program's conditioning, Y and X
    // audio -> UI: retired programs, freed in the widget's step(). A ring
    // the UI drains before every publish, so the audio thread finds room
    // for the two it can retire per take; if it ever does not, it leaves
    // the pending program where it is and tries next sample.
    static const int kRetireSlots = 8;
    std::atomic<Prepared*> retired[kRetireSlots];
    std::atomic<int> retireHead{0};   // audio writes
    std::atomic<int> retireTail{0};   // UI writes

    // --- the committed program, UI side ---------------------------------------
    std::shared_ptr<const X::Program> uiProgram;   // for the display and knobs
    SlotMeta slots[X::kMaxSlots];
    std::string source;        // last committed valid source (saved)
    std::string draft;         // the editor's uncommitted text (saved separately)
    // The floating equation panel: open or not, and where, in px of the
    // window. UI thread only; saved with the patch.
    struct EquationPanelState { bool open = false; float x = 80, y = 80, w = 720, h = 200; } eqPanel;
    // The wide layout: the controls in a column on the left and the display
    // across the rest. UI thread only; saved with the patch.
    bool wideLayout = false;
    uint32_t revision = 0;     // last submitted
    bool compileOk = true;
    std::string compileMessage;
    int compileLine = 0, compileCol = 0;
    std::vector<std::string> compileWarnings;
    std::atomic<uint32_t> activeRevision{0};
    std::atomic<int> programIsPure{1};

    // --- settings (UI writes, audio reads; atomics so no sample tears) --------
    /// The slice LOOP and POS select, knob plus CV: k of n. Non-const: Rack
    /// does not const-qualify Param::getValue(). kLoopCvPerVolt and
    /// kPosCvPerVolt (parameters): 10 V sweeps the knob's whole travel, as
    /// PHASE's START and LEN CVs do.
    static constexpr float kLoopCvPerVolt = float(X::kLoopCount - 1) / 10.f;
    static constexpr float kPosCvPerVolt = 0.1f;
    struct Slice { int k, n; };
    Slice slice() {
        float loop = params[LOOP_PARAM].getValue();
        float pos = params[POS_PARAM].getValue();
        if (inputs[LOOP_CV_INPUT].isConnected())
            loop += kLoopCvPerVolt * inputs[LOOP_CV_INPUT].getVoltage();
        if (inputs[POS_CV_INPUT].isConnected())
            pos += kPosCvPerVolt * inputs[POS_CV_INPUT].getVoltage();
        const int n = X::loopDivisions(loop);
        return {X::sliceOf(pos, n), n};
    }
    // Gain and limit are panel knobs (GAIN_PARAM, LIMIT_PARAM) plus their
    // CV; the audio thread publishes the sum here for the conditioner and
    // the display.
    std::atomic<float> gainNow{1.f}, limitNow{5.f};
    std::atomic<float> offset{0.f}, knee{0.f};
    std::atomic<int>   smooth{X::Conditioner::SMOOTH_NONE};
    std::atomic<float> tauSec{0.010f}, riseVps{100.f}, fallVps{100.f};
    std::atomic<int>   faultPolicy{X::Conditioner::FAULT_ZERO};
    std::atomic<float> crossfadeMs{10.f};
    uint32_t seed = 1978;             // read at prepare time only
    int historyWindowSec = 4;         // display only
    bool showTarget = true;           // display only
    /// LOOP repeats its slice at full-domain speed instead of stretching it
    /// across the lap (menu, saved). Off by default, so a patch saved
    /// without it plays as it did.
    std::atomic<bool> loopRepeats{true};

    // --- audio state -------------------------------------------------------------
    dsp::SchmittTrigger rstTrig;
    X::Conditioner cond, condX;      // Y and X
    double tSec = 0.0;
    float prevU = 0.f;
    int prevSource = SRC_NONE;
    X::HistoryRing<8192> history;

    // --- for the display and tooltips (audio writes, UI reads) -------------------
    std::atomic<float> dispX{0.f}, dispY{0.f}, dispU{0.f}, dispTarget{0.f};
    std::atomic<int>   dispSource{SRC_NONE};
    std::atomic<uint32_t> faultCount{0};
    std::atomic<const char*> faultWhat{""};
    std::atomic<int> faultLine{0};
    std::atomic<float> liveParam[X::kMaxSlots];
    // Per slot: false rounds the knob to whole numbers (the default).
    std::atomic<bool> fineSteps[X::kMaxSlots];

    Expr() {
        for (int i = 0; i < kRetireSlots; i++) retired[i].store(nullptr);
        for (int i = 0; i < X::kMaxSlots; i++) { liveParam[i].store(0.f); fineSteps[i].store(false); }
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (int i = 0; i < X::kMaxSlots; i++) {
            configParam<SlotQuantity>(P_PARAM + i, 0.f, 1.f, 0.f,
                                      string::f("Slot %d", i + 1));
            configInput(CV_INPUT + i, string::f("Slot %d CV", i + 1));
        }
        for (int i = X::kMaxSlots; i < kSavedSlots; i++) {
            configParam(P_PARAM + i, 0.f, 1.f, 0.f, string::f("Slot %d (former)", i + 1));
            configInput(CV_INPUT + i, string::f("Slot %d CV (former)", i + 1));
        }
        configParam(X_PARAM, -10.f, 10.f, 0.f, "X (former)");
        // GAIN 0..2 puts unity at noon; LIMIT spans the menu's former choices,
        // 5 V default (parameters, both). The floor of 0.1 V keeps the
        // display's +/-limit scale finite.
        configParam(GAIN_PARAM, 0.f, 2.f, 1.f, "Gain", "x");
        configParam(LIMIT_PARAM, 0.1f, 10.f, 5.f, "Limit", " V");
        // DOMAIN has no control on the panel; it stays in the params array
        // so patches saved with it convert in dataFromJson.
        configParam(DOMAIN_PARAM, 0.f, float(kLegacyDomainCount - 1), 0.f, "Domain (former)");
        configSwitch(LOOP_PARAM, 0.f, float(X::kLoopCount - 1), kLoopWhole,
                     "Loop (the share of the domain one lap plays)", kLoopLabels);
        configParam<PosQuantity>(POS_PARAM, 0.f, 1.f, 0.f, "Position (which slice the lap plays)");
        configInput(PHASE_INPUT, "Phase (0-10V ramp, mapped across the looped slice)");
        configInput(X_INPUT, "X (former)");
        configInput(TRIG_INPUT, "Trigger (former)");
        configInput(RST_INPUT, "Reset (program state and smoothing)");
        configInput(GAIN_CV_INPUT, "Gain CV (0.2x per volt, added to the knob)");
        configInput(LIMIT_CV_INPUT, "Limit CV (1 V per volt, added to the knob)");
        configInput(LOOP_CV_INPUT, "Loop CV (10 V sweeps all eight, added to the knob)");
        configInput(POS_CV_INPUT, "Position CV (10 V sweeps every slice, added to the knob)");
        configOutput(Y_OUTPUT, "Y");
        configOutput(X_OUTPUT, "X (the curve's x coordinate, conditioned like Y)");
        onSampleRateChange();
        apply(kDefaultSource);
    }

    ~Expr() override {
        // The engine has removed this module before it is destroyed, so
        // every pointer is ours again.
        delete pending.exchange(nullptr);
        delete active;
        delete outgoing;
        drainRetired();
    }

    void onSampleRateChange() override {
        history.setSampleRate(APP->engine->getSampleRate());
    }

    // --- compile and publish (UI thread) ------------------------------------------

    /// Free what the audio thread has retired. UI thread.
    void drainRetired() {
        int tail = retireTail.load(std::memory_order_relaxed);
        const int head = retireHead.load(std::memory_order_acquire);
        while (tail != head) {
            delete retired[tail].exchange(nullptr, std::memory_order_relaxed);
            tail = (tail + 1) % kRetireSlots;
        }
        retireTail.store(tail, std::memory_order_release);
    }

    /// Compile `src` and, if it is valid, make it the live program. Returns
    /// false with compileMessage/Line/Col set when it is not; the previous
    /// program and slot metadata are untouched in that case.
    bool apply(const std::string& src) {
        X::Program prog;
        X::CompileError err;
        if (!X::compile(src, prog, err)) {
            compileOk = false;
            compileMessage = err.message;
            compileLine = err.line;
            compileCol = err.col;
            return false;
        }
        compileOk = true;
        compileMessage.clear();
        compileLine = compileCol = 0;
        compileWarnings = prog.warnings;

        std::shared_ptr<const X::Program> shared =
            std::make_shared<const X::Program>(prog);
        Prepared* p = new Prepared;
        p->program = shared;
        p->revision = ++revision;
        p->instance.prepare(shared.get(), seed);

        // Knobs keep their positions for a slot that keeps its name; a slot
        // that is new, or renamed, starts at the declared default.
        for (int s = 0; s < X::kMaxSlots; s++) {
            const X::ParamDecl* d = shared->slot(s);
            SlotMeta& m = slots[s];
            const bool wasUsed = m.used;
            const std::string oldName = m.name;
            m.used = d != nullptr;
            if (d) {
                m.name = d->name; m.def = d->def; m.min = d->min; m.max = d->max;
                m.cvScale = d->cvScale; m.generic = d->generic;
                if (!wasUsed || oldName != d->name) {
                    params[P_PARAM + s].setValue(float((d->def - d->min) / (d->max - d->min)));
                    fineSteps[s].store(false);
                }
            } else {
                m.name.clear();
            }
        }
        uiProgram = shared;
        source = src;
        programIsPure.store(shared->pure ? 1 : 0);

        drainRetired();
        delete pending.exchange(p);   // an unseen pending program is simply replaced
        return true;
    }

    /// Program RESET from the UI (menu or editor): the audio thread does it.
    std::atomic<bool> resetRequest{false};

    // --- audio ----------------------------------------------------------------------

    /// Parameter value for slot s: knob mapped into the range, plus CV in
    /// units per volt, then knobValue's rounding and clamp.
    double paramValue(const X::ParamDecl& d, int s) {
        const double cv = inputs[CV_INPUT + s].isConnected()
                        ? d.cvScale * inputs[CV_INPUT + s].getVoltage() : 0.0;
        return knobValue(d.min, d.max, params[P_PARAM + s].getValue(), cv,
                         fineSteps[s].load(std::memory_order_relaxed));
    }

    bool retireRoom(int n) const {
        const int head = retireHead.load(std::memory_order_relaxed);
        const int tail = retireTail.load(std::memory_order_acquire);
        const int used = (head - tail + kRetireSlots) % kRetireSlots;
        return kRetireSlots - 1 - used >= n;
    }

    void retire(Prepared* p) {
        const int head = retireHead.load(std::memory_order_relaxed);
        retired[head].store(p, std::memory_order_relaxed);
        retireHead.store((head + 1) % kRetireSlots, std::memory_order_release);
    }

    /// GAIN and LIMIT with their CV added, clamped to the knob ranges. The CV
    /// scale is the knob's range per 10 V (parameter): 0..10 V sweeps the
    /// whole knob from its bottom, so an envelope or LFO can take either one
    /// from off to full without the knob set first. LIMIT uses 1 V per volt,
    /// the nearest round number to its 9.9 V span.
    static constexpr float kGainCvPerVolt = 0.2f;
    static constexpr float kLimitCvPerVolt = 1.f;
    void updateGainLimit() {
        float g = params[GAIN_PARAM].getValue(), L = params[LIMIT_PARAM].getValue();
        if (inputs[GAIN_CV_INPUT].isConnected())
            g = clamp(g + kGainCvPerVolt * inputs[GAIN_CV_INPUT].getVoltage(), 0.f, 2.f);
        if (inputs[LIMIT_CV_INPUT].isConnected())
            L = clamp(L + kLimitCvPerVolt * inputs[LIMIT_CV_INPUT].getVoltage(), 0.1f, 10.f);
        gainNow.store(g, std::memory_order_relaxed);
        limitNow.store(L, std::memory_order_relaxed);
    }

    void loadConditioner(X::Conditioner& c) {
        c.gain = gainNow.load(std::memory_order_relaxed);
        c.offset = offset.load(std::memory_order_relaxed);
        c.limit = limitNow.load(std::memory_order_relaxed);
        c.knee = knee.load(std::memory_order_relaxed);
        c.smooth = smooth.load(std::memory_order_relaxed);
        c.tauSec = tauSec.load(std::memory_order_relaxed);
        c.riseVps = riseVps.load(std::memory_order_relaxed);
        c.fallVps = fallVps.load(std::memory_order_relaxed);
        c.faultPolicy = faultPolicy.load(std::memory_order_relaxed);
    }

    void process(const ProcessArgs& args) override {
        uint32_t flags = 0;

        // 1. Accept a prepared program at this sample boundary. Needs room
        // to retire up to two (a fade in progress and the active one).
        if (pending.load(std::memory_order_relaxed) && retireRoom(2)) {
            Prepared* p = pending.exchange(nullptr, std::memory_order_acq_rel);
            if (p) {
                if (outgoing) { retire(outgoing); outgoing = nullptr; }   // never stack fades
                const float ms = crossfadeMs.load(std::memory_order_relaxed);
                fadeLength = int(ms * 0.001f * args.sampleRate);
                if (active && fadeLength > 0) {
                    outgoing = active;
                    condOut = cond;       // the outgoing path keeps its smoothing state
                    condOutX = condX;
                    fadeRemaining = fadeLength;
                } else if (active) {
                    retire(active);
                }
                active = p;
                activeRevision.store(p->revision, std::memory_order_relaxed);
                flags |= X::HF_PROGRAM;
            }
        }

        // 2. Inputs. The wire first (and republished, so the chain extends
        // through this module whatever source it ends up using).
        bool wireIsLive = false;
        const float wireV = Lives::resolveWire(this, inputs[PHASE_INPUT], args.frame, &wireIsLive);
        const X::Domain dom = X::domainOf(active ? active->program.get() : nullptr);
        const Slice sl = slice();
        double x, u = 0.0;
        int src;
        if (wireIsLive) {
            u = clamp(wireV / 10.f, 0.f, 1.f);
            x = X::sliceX(dom, sl.k, sl.n,
                          loopRepeats.load(std::memory_order_relaxed) ? X::repeatU(u, sl.n) : u);
            src = wireSource == WIRE_CABLE ? SRC_CABLE : SRC_ADJACENT;
            if (prevSource == src && std::fabs(u - prevU) > 0.1f) flags |= X::HF_PHASE_JUMP;
        } else {
            // No wire: the program holds at the start of the slice.
            x = X::sliceX(dom, sl.k, sl.n, 0.0);
            src = SRC_NONE;
        }
        if (src != prevSource) flags |= X::HF_SOURCE;
        prevSource = src;
        prevU = float(u);

        // 3. RESET.
        bool doReset = rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f);
        if (resetRequest.exchange(false, std::memory_order_relaxed)) doReset = true;
        if (doReset) {
            if (active) active->instance.reset();
            cond.reset();
            condX.reset();
            tSec = 0.0;
            flags |= X::HF_RESET;
        }

        X::Inputs in;
        in.v[X::IN_X] = x;
        in.v[X::IN_U] = u;
        in.v[X::IN_T] = tSec;
        in.v[X::IN_DT] = args.sampleTime;
        in.v[X::IN_SR] = args.sampleRate;
        in.v[X::IN_TRIG] = 0.0;   // the language reads it; the panel has no TRIG

        // 4. Run, 5. condition. 0 V until a program is ready.
        updateGainLimit();
        loadConditioner(cond);
        loadConditioner(condX);
        float y = 0.f, xo = 0.f, target = 0.f;
        if (active) {
            const X::Program& prog = *active->program;
            for (size_t i = 0; i < prog.params.size(); i++) {
                const X::ParamDecl& d = prog.params[i];
                in.param[d.slot] = paramValue(d, d.slot);
                liveParam[d.slot].store(float(in.param[d.slot]), std::memory_order_relaxed);
            }
            const X::Result r = active->instance.run(in);
            if (r.fault) {
                flags |= X::HF_FAULT;
                faultCount.store(active->instance.faultCount, std::memory_order_relaxed);
                faultWhat.store(r.faultWhat, std::memory_order_relaxed);
                faultLine.store(r.faultLine, std::memory_order_relaxed);
            }
            y = cond.process(r.value, r.fault, args.sampleTime, &target);
            if (cond.lastClipped) flags |= X::HF_CLIP;
            float xTarget = 0.f;
            xo = condX.process(r.x, r.fault, args.sampleTime, &xTarget);
        }

        // Crossfade: the outgoing program gets the same inputs, its own
        // conditioning, and a linear fade; both are guarded again at the mix.
        if (outgoing) {
            X::Inputs inOld = in;
            const X::Program& prog = *outgoing->program;
            for (size_t i = 0; i < prog.params.size(); i++) {
                const X::ParamDecl& d = prog.params[i];
                inOld.param[d.slot] = paramValue(d, d.slot);
            }
            const X::Result r = outgoing->instance.run(inOld);
            loadConditioner(condOut);
            float tOld = 0.f;
            const float yOld = condOut.process(r.value, r.fault, args.sampleTime, &tOld);
            loadConditioner(condOutX);
            const float xOld = condOutX.process(r.x, r.fault, args.sampleTime, &tOld);
            const float f = float(fadeRemaining) / float(fadeLength);   // 1 -> 0
            y = y * (1.f - f) + yOld * f;
            xo = xo * (1.f - f) + xOld * f;
            if (--fadeRemaining <= 0) {
                retire(outgoing);
                outgoing = nullptr;
            }
        }
        y = clamp(y, -X::Conditioner::kCeiling, X::Conditioner::kCeiling);
        if (!std::isfinite(y)) y = 0.f;
        xo = clamp(xo, -X::Conditioner::kCeiling, X::Conditioner::kCeiling);
        if (!std::isfinite(xo)) xo = 0.f;

        // 7. History and display.
        history.record(target, y, float(x), flags,
                       activeRevision.load(std::memory_order_relaxed));
        dispX.store(float(x), std::memory_order_relaxed);
        dispU.store(float(u), std::memory_order_relaxed);
        dispY.store(y, std::memory_order_relaxed);
        dispTarget.store(target, std::memory_order_relaxed);
        dispSource.store(src, std::memory_order_relaxed);

        outputs[Y_OUTPUT].setVoltage(y);
        outputs[X_OUTPUT].setVoltage(xo);
        lights[FAULT_LIGHT].setBrightnessSmooth((flags & X::HF_FAULT) ? 1.f : 0.f,
                                                args.sampleTime);
        tSec += args.sampleTime;
    }

    // --- persistence ---------------------------------------------------------------

    void onReset() override {
        offset.store(0.f); knee.store(0.f);
        smooth.store(X::Conditioner::SMOOTH_NONE);
        tauSec.store(0.010f); riseVps.store(100.f); fallVps.store(100.f);
        faultPolicy.store(X::Conditioner::FAULT_ZERO);
        crossfadeMs.store(10.f);
        seed = 1978;
        historyWindowSec = 4;
        showTarget = true;
        loopRepeats.store(true);
        eqPanel = EquationPanelState();
        wideLayout = false;
        draft.clear();
        for (int s = 0; s < X::kMaxSlots; s++) { slots[s] = SlotMeta(); fineSteps[s].store(false); }
        apply(kDefaultSource);
        resetRequest.store(true);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "version", json_integer(X::kLanguageVersion));
        json_object_set_new(root, "source", json_string(source.c_str()));
        if (!draft.empty() && draft != source)
            json_object_set_new(root, "draft", json_string(draft.c_str()));
        json_object_set_new(root, "offset", json_real(offset.load()));
        json_object_set_new(root, "knee", json_real(knee.load()));
        json_object_set_new(root, "smooth", json_integer(smooth.load()));
        json_object_set_new(root, "tau", json_real(tauSec.load()));
        json_object_set_new(root, "rise", json_real(riseVps.load()));
        json_object_set_new(root, "fall", json_real(fallVps.load()));
        json_object_set_new(root, "fault", json_integer(faultPolicy.load()));
        json_object_set_new(root, "seed", json_integer(seed));
        json_object_set_new(root, "crossfadeMs", json_real(crossfadeMs.load()));
        json_object_set_new(root, "window", json_integer(historyWindowSec));
        json_object_set_new(root, "showTarget", json_boolean(showTarget));
        json_object_set_new(root, "loopRepeats", json_boolean(loopRepeats.load()));
        json_t* ep = json_object();
        json_object_set_new(ep, "open", json_boolean(eqPanel.open));
        json_object_set_new(ep, "x", json_real(eqPanel.x));
        json_object_set_new(ep, "y", json_real(eqPanel.y));
        json_object_set_new(ep, "w", json_real(eqPanel.w));
        json_object_set_new(ep, "h", json_real(eqPanel.h));
        json_object_set_new(root, "equationPanel", ep);
        json_object_set_new(root, "wideLayout", json_boolean(wideLayout));
        json_t* fine = json_array();
        for (int i = 0; i < X::kMaxSlots; i++) json_array_append_new(fine, json_boolean(fineSteps[i].load()));
        json_object_set_new(root, "fine", fine);
        // Marks a patch whose domain is in its source, so dataFromJson does
        // not convert it from DOMAIN. 2: LOOP counts up to the whole
        // domain; 1 (unreleased) counted down from it.
        json_object_set_new(root, "loop", json_integer(2));
        return root;
    }

    static float realOr(json_t* root, const char* key, float def) {
        json_t* j = json_object_get(root, key);
        return j ? float(json_number_value(j)) : def;
    }
    static int intOr(json_t* root, const char* key, int def) {
        json_t* j = json_object_get(root, key);
        return j ? int(json_integer_value(j)) : def;
    }

    void dataFromJson(json_t* root) override {
        // Patches saved before the domain moved into the equation carry it
        // one of two ways: a [start, end] pair from when it was a menu
        // setting, or the DOMAIN knob's position. Either becomes a
        // restriction appended to the source below, unless it is the
        // default 0..2π. LOOP and POS did not exist then.
        bool legacy = false;
        double legacyLo = X::kDefaultDomainLo, legacyHi = X::kDefaultDomainHi;
        const int loopMark = intOr(root, "loop", 0);
        if (loopMark == 1)
            params[LOOP_PARAM].setValue(kLoopWhole - std::round(params[LOOP_PARAM].getValue()));
        if (!loopMark) {
            params[LOOP_PARAM].setValue(kLoopWhole);
            params[POS_PARAM].setValue(0.f);
            json_t* d = json_object_get(root, "domain");
            if (d && json_array_size(d) == 2) {
                legacyLo = json_number_value(json_array_get(d, 0));
                legacyHi = json_number_value(json_array_get(d, 1));
            } else if (!json_object_get(root, "range")) {
                const int i = clamp((int) std::lround(params[DOMAIN_PARAM].getValue()), 0, kLegacyDomainCount - 1);
                legacyLo = kLegacyDomains[i].lo;
                legacyHi = kLegacyDomains[i].hi;
            }
            legacy = std::fabs(legacyLo - X::kDefaultDomainLo) > 1e-9
                  || std::fabs(legacyHi - X::kDefaultDomainHi) > 1e-9;
            if (legacy && !(legacyHi > legacyLo)) {
                WARN("EXPR: saved domain [%g, %g] runs backwards; using 0 to 2π", legacyLo, legacyHi);
                legacy = false;
            }
        }
        // Patches saved while gain and limit were menu settings carry them here.
        if (json_object_get(root, "gain")) params[GAIN_PARAM].setValue(clamp(realOr(root, "gain", 1.f), 0.f, 2.f));
        if (json_object_get(root, "limit")) params[LIMIT_PARAM].setValue(clamp(realOr(root, "limit", 5.f), 0.1f, 10.f));
        offset.store(realOr(root, "offset", 0.f));
        knee.store(clamp(realOr(root, "knee", 0.f), 0.f, 20.f));
        smooth.store(clamp(intOr(root, "smooth", 0), 0, 2));
        tauSec.store(std::fmax(0.f, realOr(root, "tau", 0.010f)));
        riseVps.store(std::fmax(0.f, realOr(root, "rise", 100.f)));
        fallVps.store(std::fmax(0.f, realOr(root, "fall", 100.f)));
        faultPolicy.store(clamp(intOr(root, "fault", 0), 0, 1));
        seed = uint32_t(intOr(root, "seed", 1978));
        crossfadeMs.store(clamp(realOr(root, "crossfadeMs", 10.f), 0.f, 1000.f));
        historyWindowSec = clamp(intOr(root, "window", 4), 1, 64);
        if (json_t* j = json_object_get(root, "showTarget")) showTarget = json_boolean_value(j);
        // A patch saved before the option existed has no key and was made
        // with the slice playing slower; it loads that way.
        json_t* repeats = json_object_get(root, "loopRepeats");
        loopRepeats.store(repeats ? json_boolean_value(repeats) : false);
        if (json_t* j = json_object_get(root, "wideLayout")) wideLayout = json_boolean_value(j);
        if (json_t* ep = json_object_get(root, "equationPanel")) {
            if (json_t* j = json_object_get(ep, "open")) eqPanel.open = json_boolean_value(j);
            eqPanel.x = realOr(ep, "x", eqPanel.x);
            eqPanel.y = realOr(ep, "y", eqPanel.y);
            eqPanel.w = realOr(ep, "w", eqPanel.w);
            eqPanel.h = realOr(ep, "h", eqPanel.h);
        }
        json_t* jd = json_object_get(root, "draft");
        if (jd && json_is_string(jd)) draft = json_string_value(jd);
        json_t* js = json_object_get(root, "source");
        if (js && json_is_string(js)) {
            // The same validator and compiler as the editor. A saved source
            // that no longer compiles keeps the default program and shows
            // the error; the text is kept as the draft so nothing is lost.
            // Rack restores params before it calls dataFromJson, so the knobs
            // already hold their saved positions; apply() would reset every
            // slot to its default because none was declared before. Keep
            // the saved positions.
            std::string s = json_string_value(js);
            float saved[X::kMaxSlots];
            for (int i = 0; i < X::kMaxSlots; i++) saved[i] = params[P_PARAM + i].getValue();
            bool applied = apply(s);
            if (applied && legacy && !uiProgram->hasDomain) {
                s += X::restrictionText(uiProgram->mode, legacyLo, legacyHi);
                applied = apply(s);
            }
            if (applied) {
                for (int i = 0; i < X::kMaxSlots; i++) params[P_PARAM + i].setValue(saved[i]);
            } else {
                draft = s;
            }
        }
        // After apply(), which clears the flag on every newly used slot.
        if (json_t* f = json_object_get(root, "fine"))
            for (int i = 0; i < X::kMaxSlots && i < int(json_array_size(f)); i++)
                fineSteps[i].store(json_boolean_value(json_array_get(f, i)));
    }
};

// --- SlotQuantity -------------------------------------------------------------

Expr* SlotQuantity::expr() const { return static_cast<Expr*>(module); }
int SlotQuantity::slotIndex() const { return paramId - Expr::P_PARAM; }
const SlotMeta& SlotQuantity::meta() const { return expr()->slots[slotIndex()]; }

std::string SlotQuantity::getLabel() {
    if (!module) return name;
    const SlotMeta& m = meta();
    if (!m.used) return string::f("Slot %d (unused)", slotIndex() + 1);
    return string::f("%s (slot %d)", m.name.c_str(), slotIndex() + 1);
}

std::string SlotQuantity::getDescription() {
    if (!module || !meta().used) return "";
    const SlotMeta& m = meta();
    std::string s = string::f("Range %g to %g, CV %g per volt%s. Live value %g",
                              m.min, m.max, m.cvScale,
                              m.generic ? " (generic range: declare one)" : "",
                              expr()->liveParam[slotIndex()].load());
    return s;
}

float SlotQuantity::getDisplayValue() {
    const float v = getValue();
    if (!module || !meta().used) return v;
    const SlotMeta& m = meta();
    return float(knobValue(m.min, m.max, v, 0.0, expr()->fineSteps[slotIndex()].load()));
}

void SlotQuantity::setDisplayValue(float v) {
    if (!module || !meta().used) { setValue(v); return; }
    const SlotMeta& m = meta();
    setValue(float((v - m.min) / (m.max - m.min)));
}

std::string SlotQuantity::getDisplayValueString() {
    if (!module || !meta().used) return string::f("%.2f", getValue());
    return string::f("%.4g", getDisplayValue());
}

// --- undo ---------------------------------------------------------------------------

/// A committed edit, for Rack's history: undo and redo go through apply(),
/// the same path as the editor, so the activation mechanism is the same.
struct ProgramEditAction : history::ModuleAction {
    std::string before, after;
    void undo() override { if (auto* m = dynamic_cast<Expr*>(APP->engine->getModule(moduleId))) m->apply(before); }
    void redo() override { if (auto* m = dynamic_cast<Expr*>(APP->engine->getModule(moduleId))) m->apply(after); }
};

/// Commit from the editor or an example: apply, and record for undo.
static bool commitProgram(Expr* module, const std::string& src) {
    const std::string before = module->source;
    if (!module->apply(src)) return false;
    if (before != src) {
        ProgramEditAction* a = new ProgramEditAction;
        a->moduleId = module->id;
        a->name = "edit EXPR program";
        a->before = before;
        a->after = src;
        APP->history->push(a);
    }
    return true;
}

// --- the editor overlay ---------------------------------------------------------------

/// Multiline program text: Enter inserts a line, Cmd/Ctrl+Enter applies.
struct ProgramField : ui::TextField {
    std::function<void()> onApply;
    ProgramField() { multiline = true; }
    void onSelectKey(const SelectKeyEvent& e) override {
        if ((e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)
            && (e.action == GLFW_PRESS || e.action == GLFW_REPEAT)) {
            if ((e.mods & RACK_MOD_MASK) == RACK_MOD_CTRL) { if (onApply) onApply(); }
            else insertText("\n");
            e.consume(this);
            return;
        }
        ui::TextField::onSelectKey(e);
    }
};

struct EditorButton : ui::Button {
    std::function<void()> fn;
    void onAction(const ActionEvent& e) override { (void) e; if (fn) fn(); }
};

/// The editor: the program text, the compile status, Apply / Reset / Close.
/// Lives in a MenuOverlay so a click outside or Esc closes it.
struct ExprEditor : OpaqueWidget {
    // The text box holds five lines (heuristic): room for an equation split
    // at its pieces, small enough to keep programs short. One line is the
    // 13 px label font with leading, 15 px (observed); the padding is
    // blendish's text inset above and below.
    static constexpr int   kLines      = 5;
    static constexpr float kLineHeight = 15.f;
    static constexpr float kFieldPad   = 14.f;

    Expr* module = nullptr;
    ProgramField* field = nullptr;
    ui::Label* status = nullptr;

    ExprEditor(Expr* m) : module(m) {
        const float fieldH = kLines * kLineHeight + kFieldPad;
        box.size = Vec(560, 10 + fieldH + 82);
        field = new ProgramField;
        field->box.pos = Vec(10, 10);
        field->box.size = Vec(box.size.x - 20, fieldH);
        field->placeholder = "out = sin(x)";
        field->setText(module->draft.empty() ? module->source : module->draft);
        field->onApply = [this] { apply(); };
        addChild(field);

        status = new ui::Label;
        status->box.pos = Vec(10, box.size.y - 72);
        status->box.size = Vec(box.size.x - 20, 20);
        status->fontSize = 12;
        addChild(status);
        refreshStatus();

        float bx = 10;
        auto button = [&](const char* text, std::function<void()> fn) {
            EditorButton* b = new EditorButton;
            b->text = text;
            b->fn = fn;
            b->box.pos = Vec(bx, box.size.y - 44);
            b->box.size = Vec(110, 30);
            addChild(b);
            bx += 120;
        };
        button("Apply (" RACK_MOD_CTRL_NAME "+Enter)", [this] { apply(); });
        button("Reset state", [this] { module->resetRequest.store(true); });
        button("Revert", [this] { field->setText(module->source); module->draft.clear(); refreshStatus(); });
        button("Close", [this] { close(); });
    }

    void apply() {
        const std::string src = field->getText();
        if (commitProgram(module, src)) module->draft.clear();
        else module->draft = src;
        refreshStatus();
    }

    void refreshStatus() {
        if (module->compileOk) {
            std::string s = string::f("ready: %s", module->uiProgram ? module->uiProgram->name.c_str() : "");
            for (size_t i = 0; i < module->compileWarnings.size(); i++)
                s += "  |  warning: " + module->compileWarnings[i];
            status->text = s;
            status->color = nvgRGB(140, 220, 150);
        } else {
            status->text = string::f("error at %d:%d: %s", module->compileLine,
                                     module->compileCol, module->compileMessage.c_str());
            status->color = nvgRGB(255, 140, 110);
        }
    }

    void close() {
        if (parent) parent->requestDelete();
    }

    void step() override {
        // Keep the editor's uncommitted text as the draft, and stay centred.
        if (field->getText() != module->source) module->draft = field->getText();
        if (parent) box.pos = parent->box.size.minus(box.size).div(2);
        OpaqueWidget::step();
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 6.f);
        nvgFillColor(args.vg, nvgRGB(22, 20, 19));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(163, 69, 38));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);
        OpaqueWidget::draw(args);
    }
};

static void openEditor(Expr* module) {
    if (!module) return;
    ui::MenuOverlay* overlay = new ui::MenuOverlay;
    ExprEditor* ed = new ExprEditor(module);
    overlay->addChild(ed);
    APP->scene->addChild(overlay);
    APP->event->setSelectedWidget(ed->field);
}

// --- the display -------------------------------------------------------------------------

/// Curve of a pure program, or output history of a stateful one, with a
/// status strip. Click to open the editor.
std::string PosQuantity::getDisplayValueString() {
    Expr* m = dynamic_cast<Expr*>(module);
    if (!m) return string::f("%.2f", getValue());
    const Expr::Slice sl = m->slice();
    return string::f("%d of %d", sl.k + 1, sl.n);
}

struct ExprDisplay : Widget {
    Expr* module = nullptr;
    // The plot fills the display; the slot legend and the status line sit
    // over its bottom edge. Heights in px (layout parameters): kStripH fits
    // the 10 pt status text, kRowH one 9 pt legend row.
    static constexpr float kStripH = 14.f;
    static constexpr float kRowH = 11.f;
    // Knob centres relative to the display, set by the widget: each slot's
    // name and value, and the LOOP and POS readings, sit straight above
    // their knobs.
    float colX[X::kMaxSlots] = {};
    float loopX = 0.f, posX = 0.f;

    // Curve cache: rebuilt when the program revision or a parameter moves.
    X::Preview preview;
    uint32_t previewRevision = 0;
    double previewParams[X::kMaxSlots];
    double previewD0 = 0, previewD1 = 0;
    float previewGain = 1, previewOffset = 0, previewLimit = 5, previewKnee = 0;
    // The PHASE window (START and LEN, fractions of the lap) set by the
    // widget each frame; a negative start means none is known.
    float winStart = -1.f, winLen = 1.f;
    std::vector<X::HistoryBin> bins;

    ExprDisplay() {
        bins.resize(8192 / 2);
        for (int i = 0; i < X::kMaxSlots; i++) previewParams[i] = 0;
    }

    std::shared_ptr<Font> font() {
        return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    }

    void onButton(const ButtonEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            openEditor(module);
            e.consume(this);
            return;
        }
        Widget::onButton(e);
    }

    void drawBezel(NVGcontext* vg, const char* msg) {
        std::shared_ptr<Font> f = font();
        if (!f) return;
        nvgFontFaceId(vg, f->handle);
        nvgFontSize(vg, 13.f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(90, 110, 95));
        nvgText(vg, box.size.x / 2, box.size.y / 2, msg, nullptr);
    }

    /// The bottom of the display: each slot's name and value above its
    /// knob, then the status line, over a shade that lets the curve show
    /// through.
    void drawOverlay(NVGcontext* vg, const std::string& left, const std::string& right) {
        std::shared_ptr<Font> f = font();
        if (!f) return;
        const float w = box.size.x, h = box.size.y;
        const float top = h - kStripH - kRowH;
        nvgBeginPath(vg);
        nvgRect(vg, 0, top, w, h - top);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, top, 0, top + kRowH,
                                           nvgRGBA(14, 13, 12, 0), nvgRGBA(14, 13, 12, 170)));
        nvgFill(vg);

        nvgFontFaceId(vg, f->handle);
        nvgFontSize(vg, 9.f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        const NVGcolor live = nvgRGB(200, 190, 170), idle = nvgRGBA(160, 150, 130, 90);
        const float y = top + 0.5f * kRowH;
        for (int s = 0; s < X::kMaxSlots; s++) {
            const SlotMeta& m = module->slots[s];
            if (!m.used) {
                nvgFillColor(vg, idle);
                nvgText(vg, colX[s], y, "-", nullptr);
                continue;
            }
            std::string name = m.name;
            if (name.size() > 6) name = name.substr(0, 5) + "~";
            nvgFillColor(vg, live);
            nvgText(vg, colX[s], y, string::f("%s %.3g", name.c_str(),
                                              module->liveParam[s].load()).c_str(), nullptr);
        }
        // LOOP as the share of the domain, POS as which slice of how many.
        const Expr::Slice sl = module->slice();
        nvgFillColor(vg, live);
        nvgText(vg, loopX, y, sl.n == 1 ? "all" : string::f("1/%d", sl.n).c_str(), nullptr);
        nvgFillColor(vg, sl.n == 1 ? idle : live);
        nvgText(vg, posX, y, sl.n == 1 ? "-" : string::f("%d/%d", sl.k + 1, sl.n).c_str(), nullptr);
        drawStrip(vg, left, right);
    }

    void drawStrip(NVGcontext* vg, const std::string& left, const std::string& right) {
        std::shared_ptr<Font> f = font();
        if (!f) return;
        const float y = box.size.y - kStripH / 2;
        nvgFontFaceId(vg, f->handle);
        nvgFontSize(vg, 10.f);
        nvgFillColor(vg, nvgRGB(120, 150, 110));
        // One centred line: the left text (the equation's first line, often
        // long), a gap, then the right text. The left text is cut to fit
        // and ends in "~". kStripSep: two characters of space between them.
        static const std::string kStripSep = "  ";
        const std::string tail = right.empty() ? "" : kStripSep + right;
        const float room = box.size.x - 8.f
                         - (tail.empty() ? 0.f : nvgTextBounds(vg, 0, 0, tail.c_str(), nullptr, nullptr));
        std::string shown = left;
        if (nvgTextBounds(vg, 0, 0, shown.c_str(), nullptr, nullptr) > room) {
            while (!shown.empty()) {
                // Drop whole UTF-8 characters (π, θ, ≤) so none is split.
                while (!shown.empty() && (shown.back() & 0xC0) == 0x80) shown.pop_back();
                if (!shown.empty()) shown.pop_back();
                if (nvgTextBounds(vg, 0, 0, (shown + "~").c_str(), nullptr, nullptr) <= room) break;
            }
            shown += "~";
        }
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, box.size.x / 2, y, (shown + tail).c_str(), nullptr);
    }

    /// The domain's ends at the top corners of the plot, clear of the
    /// legend, in π when both are multiples of π/12.
    void drawRangeLabels(NVGcontext* vg, float w, float plotH, const X::Domain& dom) {
        std::shared_ptr<Font> f = font();
        if (!f) return;
        const bool inPi = X::domainInPi(dom);
        nvgFontFaceId(vg, f->handle);
        nvgFontSize(vg, 9.f);
        nvgFillColor(vg, nvgRGBA(160, 150, 130, 140));
        (void) plotH;
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgText(vg, 3.f, 2.f, X::domainText(dom.lo, inPi).c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        nvgText(vg, w - 3.f, 2.f, X::domainText(dom.hi, inPi).c_str(), nullptr);
    }

    /// Volts to a y pixel in the plot area, ±limit filling it.
    float vToY(float v, float plotH, float L) const {
        return plotH / 2 - (v / L) * (plotH / 2) * 0.92f;
    }

    void refreshPreview() {
        const std::shared_ptr<const X::Program> prog = module->uiProgram;
        if (!prog || !prog->pure) return;
        const uint32_t rev = module->revision;
        const X::Domain dom = X::domainOf(prog.get());
        const double d0 = dom.lo, d1 = dom.hi;
        const float g = module->gainNow.load(), o = module->offset.load();
        const float L = module->limitNow.load(), K = module->knee.load();
        bool dirty = rev != previewRevision || d0 != previewD0 || d1 != previewD1
                  || g != previewGain || o != previewOffset || L != previewLimit || K != previewKnee;
        X::Inputs in;
        in.v[X::IN_DT] = 1.0 / 48000.0;
        in.v[X::IN_SR] = 48000.0;
        for (size_t i = 0; i < prog->params.size(); i++) {
            const X::ParamDecl& d = prog->params[i];
            const double knob = module->params[Expr::P_PARAM + d.slot].getValue();
            // The curve is at the knob's value; live CV is on the history.
            const double v = knobValue(d.min, d.max, knob, 0.0, module->fineSteps[d.slot].load());
            in.param[d.slot] = v;
            if (v != previewParams[d.slot]) { previewParams[d.slot] = v; dirty = true; }
        }
        if (!dirty) return;
        // kPreviewCoarse (parameter): the first grid, 510 intervals, finer than
        // the display's ~270 px and not a power of two, so a period that
        // divides the domain evenly (port 03's 0.25 on -8..8) does not land
        // every sample on the same phase and hide its gaps. kPreviewBudget
        // (parameter): total evaluations;
        // 2048 resolves all 64 dashes in under 4 ms a rebuild on an M3
        // (measured with process start included).
        constexpr int kPreviewCoarse = 511, kPreviewBudget = 2048;
        preview.build(*prog, in, d0, d1, kPreviewCoarse, kPreviewBudget);
        previewRevision = rev; previewD0 = d0; previewD1 = d1;
        previewGain = g; previewOffset = o; previewLimit = L; previewKnee = K;
    }

    void drawCurve(NVGcontext* vg, float w, float plotH) {
        refreshPreview();
        const float L = std::fmax(module->limitNow.load(), 0.01f);
        X::Conditioner c;
        module->loadConditioner(c);
        const X::Domain dom = X::domainOf(module->uiProgram.get());
        const double d0 = dom.lo, d1 = dom.hi;
        const Expr::Slice sl = module->slice();
        const float s0 = float(sl.k) / sl.n * w, s1 = float(sl.k + 1) / sl.n * w;
        drawRangeLabels(vg, w, plotH, dom);

        // The looped slice: the rest of the domain sits under a shade.
        if (sl.n > 1) {
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, s0, plotH);
            nvgRect(vg, s1, 0, w - s1, plotH);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 90));
            nvgFill(vg);
        }

        // Axes: zero volts, and the limit lines.
        nvgBeginPath(vg);
        nvgMoveTo(vg, 0, vToY(0, plotH, L)); nvgLineTo(vg, w, vToY(0, plotH, L));
        nvgStrokeColor(vg, nvgRGBA(160, 150, 130, 50));
        nvgStrokeWidth(vg, 0.7f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, 0, vToY(L, plotH, L)); nvgLineTo(vg, w, vToY(L, plotH, L));
        nvgMoveTo(vg, 0, vToY(-L, plotH, L)); nvgLineTo(vg, w, vToY(-L, plotH, L));
        nvgStrokeColor(vg, nvgRGBA(255, 140, 90, 60));
        nvgStroke(vg);

        // The PHASE window: a dark grey bar at START and one at START + LEN,
        // always shown while the wire comes from a PHASE, so moving either
        // knob shows where the lap now runs. The end wraps with the wire,
        // inside the looped slice.
        if (winStart >= 0.f) {
            const float e = winStart + winLen;
            const float ends[2] = {winStart, e - std::floor(e)};
            nvgBeginPath(vg);
            for (float f : ends) {
                const float px = s0 + f * (s1 - s0);
                nvgMoveTo(vg, px, 0); nvgLineTo(vg, px, plotH);
            }
            nvgStrokeColor(vg, nvgRGB(85, 85, 85));
            nvgStrokeWidth(vg, 2.f);
            nvgStroke(vg);
        }

        // The conditioned target curve: gain, offset and the limiter, no
        // smoothing (that has no x). Breaks at faults and at jumps.
        nvgBeginPath(vg);
        bool pen = false;
        for (size_t i = 0; i < preview.points.size(); i++) {
            const X::PreviewPoint& p = preview.points[i];
            if (!p.valid) { pen = false; continue; }
            const float px = float((p.x - d0) / (d1 - d0)) * w;
            const float v = c.limitOnly(p.y * c.gain + c.offset);
            const float py = vToY(v, plotH, L);
            if (!pen || p.breakBefore) { nvgMoveTo(vg, px, py); pen = true; }
            else nvgLineTo(vg, px, py);
        }
        nvgStrokeColor(vg, nvgRGB(150, 145, 132));
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);

        // The operating point: where the wire has the program right now.
        const double x = module->dispX.load();
        if (x >= d0 && x <= d1) {
            const float px = float((x - d0) / (d1 - d0)) * w;
            nvgBeginPath(vg);
            nvgMoveTo(vg, px, 0); nvgLineTo(vg, px, plotH);
            nvgStrokeColor(vg, nvgRGB(120, 200, 180));
            nvgStrokeWidth(vg, 1.f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, px, vToY(module->dispY.load(), plotH, L), 2.5f);
            nvgFillColor(vg, nvgRGB(255, 140, 90));
            nvgFill(vg);
        }
    }

    void drawHistory(NVGcontext* vg, float w, float plotH) {
        const float L = std::fmax(module->limitNow.load(), 0.01f);
        int n = module->historyWindowSec * int(X::HistoryRing<8192>::kBinsPerSecond);
        if (n > int(bins.size())) n = int(bins.size());
        const int got = module->history.snapshot(bins.data(), n);

        nvgBeginPath(vg);
        nvgMoveTo(vg, 0, vToY(0, plotH, L)); nvgLineTo(vg, w, vToY(0, plotH, L));
        nvgStrokeColor(vg, nvgRGBA(160, 150, 130, 50));
        nvgStrokeWidth(vg, 0.7f);
        nvgStroke(vg);
        if (got <= 0) return;

        // Newest bin at the right edge; a window longer than the history
        // shows its limit as empty space on the left, never invented bins.
        const float binW = w / float(n);
        const float x0 = w - got * binW;

        // Target as a dim min/max band, actual as the bright one.
        if (module->showTarget) {
            nvgBeginPath(vg);
            for (int i = 0; i < got; i++) {
                const float x = x0 + (i + 0.5f) * binW;
                nvgMoveTo(vg, x, vToY(bins[i].tMax, plotH, L));
                nvgLineTo(vg, x, vToY(bins[i].tMin, plotH, L) + 0.5f);
            }
            nvgStrokeColor(vg, nvgRGBA(150, 145, 132, 90));
            nvgStrokeWidth(vg, std::fmax(binW * 0.9f, 0.8f));
            nvgStroke(vg);
        }
        nvgBeginPath(vg);
        for (int i = 0; i < got; i++) {
            const float x = x0 + (i + 0.5f) * binW;
            nvgMoveTo(vg, x, vToY(bins[i].aMax, plotH, L));
            nvgLineTo(vg, x, vToY(bins[i].aMin, plotH, L) + 0.5f);
        }
        nvgStrokeColor(vg, nvgRGB(255, 140, 90));
        nvgStrokeWidth(vg, std::fmax(binW * 0.9f, 0.8f));
        nvgStroke(vg);

        // Event markers along the top: reset, trigger, program, fault.
        for (int i = 0; i < got; i++) {
            const uint32_t f = bins[i].flags;
            if (!(f & (X::HF_RESET | X::HF_TRIG | X::HF_FAULT | X::HF_PROGRAM))) continue;
            const float x = x0 + (i + 0.5f) * binW;
            NVGcolor col = (f & X::HF_FAULT)   ? nvgRGB(255, 70, 60)
                         : (f & X::HF_RESET)   ? nvgRGB(120, 200, 180)
                         : (f & X::HF_PROGRAM) ? nvgRGB(220, 200, 120)
                                               : nvgRGB(120, 230, 130);
            nvgBeginPath(vg);
            nvgRect(vg, x - 0.5f, 0, 1.5f, (f & X::HF_FAULT) ? 6.f : 4.f);
            nvgFillColor(vg, col);
            nvgFill(vg);
        }
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        const float w = box.size.x, h = box.size.y;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, w, h, 3.f);
        nvgFillColor(vg, nvgRGB(14, 13, 12));
        nvgFill(vg);
        if (!module) { drawBezel(vg, "PHASES EXPR"); return; }

        const float plotH = h;
        nvgSave(vg);
        nvgScissor(vg, 0, 0, w, plotH);
        if (module->uiProgram && module->uiProgram->pure) drawCurve(vg, w, plotH);
        else drawHistory(vg, w, plotH);
        nvgRestore(vg);

        std::string left = module->uiProgram ? module->uiProgram->name : "no program";
        if (!module->compileOk) left += string::f("  err %d:%d", module->compileLine, module->compileCol);
        else if (module->uiProgram && module->uiProgram->pure && !preview.complete) left += "  partial";
        const char* src;
        switch (module->dispSource.load()) {
        case Expr::SRC_CABLE:    src = "phase"; break;
        case Expr::SRC_ADJACENT: src = "adj"; break;
        default:                 src = "no wire"; break;
        }
        // The source only. x and y change every frame while the wire runs,
        // too fast to read; the playhead and the dot on the curve show them.
        std::string right = src;
        drawOverlay(vg, left, right);
    }
};

// --- the equation panel ------------------------------------------------------------------

/// The program's equation typeset as LaTeX sets it (src/expr/ExprMath.hpp),
/// drawn in Latin Modern Math. Laid out once per source at kLayoutPx and
/// scaled when drawn.
struct EquationArt {
    // kLayoutPx (parameter): the size the layout is computed at; any size
    // works, since drawing scales it.
    static constexpr float kLayoutPx = 20.f;
    std::string source;
    bool ok = false;
    X::Math::Box box;

    static std::shared_ptr<Font> font() {
        return APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/latinmodern-math.otf"));
    }

    void refresh(NVGcontext* vg, const std::string& src) {
        if (src == source) return;
        source = src;
        ok = false;
        std::shared_ptr<Font> f = font();
        if (!f || src.empty()) return;
        nvgSave(vg);
        nvgFontFaceId(vg, f->handle);
        X::Math::Measure measure = [vg](const std::string& t, float size) {
            nvgFontSize(vg, size);
            return nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr);
        };
        ok = X::Math::layout(src, kLayoutPx, measure, box);
        nvgRestore(vg);
    }

    static void shape(NVGcontext* vg, const X::Math::Shape& sh) {
        const float x = sh.x, t = sh.top, b = sh.bottom, w = sh.w, em = sh.em;
        const float mid = (t + b) / 2;
        const float stroke = 0.045f * em;
        nvgBeginPath(vg);
        switch (sh.kind) {
        case X::Math::S_PAREN_L:
        case X::Math::S_PAREN_R: {
            // A crescent: thin at the ends, thickest at the middle.
            const float dir = sh.kind == X::Math::S_PAREN_L ? 1.f : -1.f;
            const float x0 = sh.kind == X::Math::S_PAREN_L ? x : x + w;
            const float end = x0 + dir * 0.78f * w, bulge = x0 + dir * 0.18f * w;
            const float thick = 0.07f * em + 0.02f * (b - t);
            const float ctrl = 2 * bulge - end;
            nvgMoveTo(vg, end, t);
            nvgQuadTo(vg, ctrl, mid, end, b);
            nvgLineTo(vg, end + dir * 0.012f * em, b);
            nvgQuadTo(vg, ctrl + dir * 2 * thick, mid, end + dir * 0.012f * em, t);
            nvgClosePath(vg);
            nvgFill(vg);
            return;
        }
        case X::Math::S_FLOOR_L: case X::Math::S_CEIL_L: case X::Math::S_BRACKET_L: {
            const float sx = x + 0.38f * w, fx = x + 0.88f * w;
            nvgMoveTo(vg, sx, t); nvgLineTo(vg, sx, b);
            if (sh.kind != X::Math::S_CEIL_L) { nvgMoveTo(vg, sx, b); nvgLineTo(vg, fx, b); }
            if (sh.kind != X::Math::S_FLOOR_L) { nvgMoveTo(vg, sx, t); nvgLineTo(vg, fx, t); }
            break;
        }
        case X::Math::S_FLOOR_R: case X::Math::S_CEIL_R: case X::Math::S_BRACKET_R: {
            const float sx = x + 0.62f * w, fx = x + 0.12f * w;
            nvgMoveTo(vg, sx, t); nvgLineTo(vg, sx, b);
            if (sh.kind != X::Math::S_CEIL_R) { nvgMoveTo(vg, sx, b); nvgLineTo(vg, fx, b); }
            if (sh.kind != X::Math::S_FLOOR_R) { nvgMoveTo(vg, sx, t); nvgLineTo(vg, fx, t); }
            break;
        }
        case X::Math::S_BAR:
            nvgMoveTo(vg, x + w / 2, t); nvgLineTo(vg, x + w / 2, b);
            break;
        case X::Math::S_BRACE_L:
        case X::Math::S_BRACE_R: {
            const float dir = sh.kind == X::Math::S_BRACE_L ? 1.f : -1.f;
            const float x0 = sh.kind == X::Math::S_BRACE_L ? x : x + w;
            const float tip = x0 + dir * 0.12f * w, stem = x0 + dir * 0.5f * w, hook = x0 + dir * 0.9f * w;
            const float r = std::fmin(0.22f * em, (b - t) / 4);
            nvgMoveTo(vg, hook, t);
            nvgQuadTo(vg, stem, t, stem, t + r);
            nvgLineTo(vg, stem, mid - r);
            nvgQuadTo(vg, stem, mid, tip, mid);
            nvgQuadTo(vg, stem, mid, stem, mid + r);
            nvgLineTo(vg, stem, b - r);
            nvgQuadTo(vg, stem, b, hook, b);
            nvgStrokeWidth(vg, 0.055f * em);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
            return;
        }
        case X::Math::S_RADICAL: {
            const float hgt = b - t;
            const float tickY = b - std::fmin(0.45f * hgt, 0.5f * em);
            nvgMoveTo(vg, x + 0.08f * w, tickY + 0.06f * em);
            nvgLineTo(vg, x + 0.25f * w, tickY);
            nvgLineTo(vg, x + 0.5f * w, b);
            nvgLineTo(vg, x + w, t + 0.02f * em);
            nvgStrokeWidth(vg, stroke);
            nvgLineJoin(vg, NVG_MITER);
            nvgStroke(vg);
            // The down stroke is the heavy one.
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 0.25f * w, tickY);
            nvgLineTo(vg, x + 0.5f * w, b);
            nvgStrokeWidth(vg, 2.2f * stroke);
            nvgStroke(vg);
            return;
        }
        }
        nvgStrokeWidth(vg, stroke);
        nvgLineCap(vg, NVG_SQUARE);
        nvgStroke(vg);
    }

    /// Draw the equation as large as fits the rectangle, centred in it.
    void draw(NVGcontext* vg, Rect r, NVGcolor ink) {
        std::shared_ptr<Font> f = font();
        if (!ok || !f) return;
        const float s = std::fmin(r.size.x / std::fmax(box.w, 1.f),
                                  r.size.y / std::fmax(box.h + box.d, 1.f));
        const float x0 = r.pos.x + (r.size.x - box.w * s) / 2;
        const float y0 = r.pos.y + (r.size.y - (box.h + box.d) * s) / 2 + box.h * s;
        nvgSave(vg);
        nvgTranslate(vg, x0, y0);
        nvgScale(vg, s, s);
        nvgFontFaceId(vg, f->handle);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFillColor(vg, ink);
        for (const X::Math::Run& run : box.runs) {
            nvgFontSize(vg, run.size);
            nvgText(vg, run.x, run.y, run.text.c_str(), nullptr);
        }
        nvgStrokeColor(vg, ink);
        for (const X::Math::Rule& rule : box.rules) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, rule.x0, rule.y0); nvgLineTo(vg, rule.x1, rule.y1);
            nvgStrokeWidth(vg, rule.width);
            nvgStroke(vg);
        }
        for (const X::Math::Shape& sh : box.shapes) shape(vg, sh);
        nvgRestore(vg);
    }
};

/// A floating window over the rack showing one EXPR's equation, typeset,
/// as large as the window. Drag it to move it, drag the corner to resize,
/// × to close. Its place and size live on the module (eqPanel) and are
/// saved with the patch; the ExprWidget opens and closes it from there.
struct EquationPanel : OpaqueWidget {
    // Layout parameters, px: kPad around the equation; kGrip the resize
    // corner and the close box; kMinW/kMinH the smallest the window gets
    // and kKeepVisible how much of it stays on screen when dragged off.
    static constexpr float kPad = 18.f, kGrip = 16.f;
    static constexpr float kMinW = 160.f, kMinH = 60.f, kKeepVisible = 40.f;
    Expr* module = nullptr;
    EquationArt art;
    bool resizing = false;

    void step() override {
        Expr::EquationPanelState& st = module->eqPanel;
        if (parent) {
            const Vec scene = parent->box.size;
            st.x = clamp(st.x, kKeepVisible - st.w, scene.x - kKeepVisible);
            st.y = clamp(st.y, 0.f, scene.y - kKeepVisible);
        }
        box.pos = Vec(st.x, st.y);
        box.size = Vec(st.w, st.h);
        OpaqueWidget::step();
    }

    bool inGrip(Vec p) const { return p.x > box.size.x - kGrip && p.y > box.size.y - kGrip; }
    bool inClose(Vec p) const { return p.x > box.size.x - kGrip - 4 && p.y < kGrip + 4; }

    void onButton(const ButtonEvent& e) override {
        OpaqueWidget::onButton(e);
        if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) return;
        if (inClose(e.pos)) { module->eqPanel.open = false; return; }
        resizing = inGrip(e.pos);
    }

    void onDragMove(const DragMoveEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        Expr::EquationPanelState& st = module->eqPanel;
        const Vec d = e.mouseDelta;
        if (resizing) {
            st.w = std::fmax(kMinW, st.w + d.x);
            st.h = std::fmax(kMinH, st.h + d.y);
        } else {
            st.x += d.x;
            st.y += d.y;
        }
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        const float w = box.size.x, h = box.size.y;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, w, h, 6.f);
        nvgFillColor(vg, nvgRGB(22, 20, 19));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgRGB(163, 69, 38));
        nvgStrokeWidth(vg, 1.f);
        nvgStroke(vg);

        art.refresh(vg, module->uiProgram ? module->uiProgram->source : std::string());
        art.draw(vg, Rect(kPad, kPad, w - 2 * kPad, h - 2 * kPad), nvgRGB(232, 224, 208));

        // × in the top right corner; grip lines in the bottom right.
        const NVGcolor dim = nvgRGBA(200, 190, 170, 110);
        nvgBeginPath(vg);
        const float cx = w - kGrip / 2 - 3, cy = kGrip / 2 + 3, r = 4.f;
        nvgMoveTo(vg, cx - r, cy - r); nvgLineTo(vg, cx + r, cy + r);
        nvgMoveTo(vg, cx + r, cy - r); nvgLineTo(vg, cx - r, cy + r);
        for (int i = 1; i <= 3; i++) {
            const float o = 4.f * i;
            nvgMoveTo(vg, w - o, h - 3); nvgLineTo(vg, w - 3, h - o);
        }
        nvgStrokeColor(vg, dim);
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);
    }
};

/// A ring around a slot knob whose letter the equation uses. Dark means
/// turning it does nothing.
struct ActiveRing : Widget {
    Expr* module = nullptr;
    int slot = 0;
    bool active() const { return module && module->slots[slot].used; }
    void draw(const DrawArgs& args) override {
        if (!active()) return;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x / 2, box.size.y / 2, box.size.x / 2 - 1.f);
        nvgStrokeColor(args.vg, nvgRGB(255, 140, 90));   // the display's curve accent
        nvgStrokeWidth(args.vg, 1.6f);
        nvgStroke(args.vg);
    }
};

// --- the widget --------------------------------------------------------------------------

/// The panel of the wide layout, drawn rather than loaded: the rack
/// panel's orange, the name, and a label for each row of the control
/// column. Row labels and centres are set by the widget.
struct WideBackground : Widget {
    Expr* module = nullptr;
    struct Label { std::string text; Vec at; float size; };
    std::vector<Label> labels;

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, box.size.x, box.size.y);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, box.size.y, nvgRGB(0xe5, 0x61, 0x3e), nvgRGB(0xd0, 0x51, 0x2f)));
        nvgFill(vg);
        std::shared_ptr<Font> f = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/SairaCondensed-SemiBold.ttf"));
        if (!f) return;
        nvgFontFaceId(vg, f->handle);
        nvgFillColor(vg, nvgRGB(0x24, 0x13, 0x10));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (const Label& l : labels) {
            std::string t = l.text;
            // A knob row is named by its letter once the equation uses it.
            if (module && t.size() == 1 && (t == "1" || t == "2")) {
                const SlotMeta& m = module->slots[t[0] - '1'];
                if (m.used) t = m.name;
            }
            nvgFontSize(vg, l.size);
            nvgText(vg, l.at.x, l.at.y, t.c_str(), nullptr);
        }
    }
};

struct ExprWidget : ModuleWidget, SvgHelper<ExprWidget> {
    ExprDisplay* display = nullptr;
    EquationPanel* panel = nullptr;
    WideBackground* wideBg = nullptr;
    // Every child's place in the rack layout, recorded once built, so the
    // wide layout can be undone exactly.
    std::vector<std::pair<Widget*, Rect>> rackPlaces;
    Vec rackSize;
    float rackColX[X::kMaxSlots] = {}, rackLoopX = 0, rackPosX = 0;
    std::vector<ActiveRing*> rings;
    bool wideShown = false;

    ~ExprWidget() override { closePanel(); }

    void closePanel() {
        if (!panel) return;
        APP->scene->removeChild(panel);
        delete panel;
        panel = nullptr;
    }

    /// Open or close the equation panel to match the module's saved state.
    void syncPanel(Expr* m) {
        if (m->eqPanel.open && !panel) {
            panel = new EquationPanel;
            panel->module = m;
            APP->scene->addChild(panel);
        } else if (!m->eqPanel.open && panel) {
            closePanel();
        }
    }

    explicit ExprWidget(Expr* module) {
        setModule(module);
        loadPanel(asset::plugin(pluginInstance, "res/Expr.svg"));

        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ThemedScrew>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        Vec tl = anchorCentre("display.main.tl");
        Vec br = anchorCentre("display.main.br");
        ExprDisplay* disp = new ExprDisplay;
        disp->module = module;
        disp->box.pos = tl;
        disp->box.size = br.minus(tl);
        addChild(disp);
        display = disp;

        for (int i = 0; i < X::kMaxSlots; i++) {
            const std::string n = std::to_string(i + 1);
            addRing(module, i, "param.main.p" + n);
            bindParam<RoundBlackKnob>("param.main.p" + n, Expr::P_PARAM + i);
            bindInput<PJ301MPort>("cv.main.p" + n, Expr::CV_INPUT + i);
        }
        bindParam<RoundBlackKnob>("param.main.gain", Expr::GAIN_PARAM);
        bindParam<RoundBlackKnob>("param.main.limit", Expr::LIMIT_PARAM);
        bindInput<PJ301MPort>("in.main.phase", Expr::PHASE_INPUT);
        bindInput<PJ301MPort>("trig.main.reset", Expr::RST_INPUT);
        bindInput<PJ301MPort>("cv.main.gain", Expr::GAIN_CV_INPUT);
        bindInput<PJ301MPort>("cv.main.limit", Expr::LIMIT_CV_INPUT);
        bindParam<RoundBlackKnob>("param.main.loop", Expr::LOOP_PARAM);
        bindInput<PJ301MPort>("cv.main.loop", Expr::LOOP_CV_INPUT);
        bindParam<RoundBlackKnob>("param.main.pos", Expr::POS_PARAM);
        bindInput<PJ301MPort>("cv.main.pos", Expr::POS_CV_INPUT);
        bindOutput<PJ301MPort>("out.main.y", Expr::Y_OUTPUT);
        bindOutput<PJ301MPort>("out.main.x", Expr::X_OUTPUT);
        bindLight<MediumLight<RedLight>>("light.main.fault", Expr::FAULT_LIGHT);

        for (int i = 0; i < X::kMaxSlots; i++)
            disp->colX[i] = anchorCentre("param.main.p" + std::to_string(i + 1)).x - tl.x;
        disp->loopX = anchorCentre("param.main.loop").x - tl.x;
        disp->posX = anchorCentre("param.main.pos").x - tl.x;

        wideBg = new WideBackground;
        wideBg->module = module;
        wideBg->hide();
        addChildBottom(wideBg);
        rackSize = box.size;
        for (Widget* w : children)
            if (w != wideBg) rackPlaces.push_back({w, w->box});
        for (int i = 0; i < X::kMaxSlots; i++) rackColX[i] = disp->colX[i];
        rackLoopX = disp->loopX;
        rackPosX = disp->posX;
    }

    // The wide layout, px. kWideHp (parameter): three times the rack's
    // height across, the width asked for, so the display reads as a wide
    // screen. kColumnW: the control column, 8 HP, a label, a knob and its
    // CV jack side by side. kRowTop/kRowStep space the eight rows of the
    // column between the screws; kLabelX, kKnobX, kJackX are its three
    // centres. kGap separates the column from the display, kEdge keeps
    // the display clear of the screws at the top and bottom.
    static constexpr int kWideHp = 76;
    static constexpr float kColumnW = 8 * 15.f;   // RACK_GRID_WIDTH is 15 px
    static constexpr float kRowTop = 38.f, kRowStep = 43.f;
    static constexpr float kLabelX = 22.f, kKnobX = 62.f, kJackX = 98.f;
    static constexpr float kGap = 4.f, kEdge = 18.f;

    static void centre(Widget* w, Vec c) { w->box.pos = c.minus(w->box.size.div(2)); }

    /// Lay the module out wide or as the rack panel, and push the
    /// neighbours aside if it grew.
    void setLayout(bool wide) {
        wideShown = wide;
        for (const auto& p : rackPlaces) p.first->box = p.second;
        box.size = rackSize;
        for (int i = 0; i < X::kMaxSlots; i++) display->colX[i] = rackColX[i];
        display->loopX = rackLoopX;
        display->posX = rackPosX;
        if (Widget* pn = getPanel()) pn->setVisible(!wide);
        wideBg->setVisible(wide);
        if (wide) {
            box.size.x = kWideHp * RACK_GRID_WIDTH;
            wideBg->box = Rect(Vec(0, 0), box.size);
            wideBg->labels.clear();
            auto row = [&](int k) { return kRowTop + k * kRowStep; };
            // Knob rows: label, knob, CV jack.
            struct KnobRow { const char* label; int param; int cv; };
            const KnobRow rows[] = {
                {"1", Expr::P_PARAM + 0, Expr::CV_INPUT + 0}, {"2", Expr::P_PARAM + 1, Expr::CV_INPUT + 1},
                {"LOOP", Expr::LOOP_PARAM, Expr::LOOP_CV_INPUT}, {"POS", Expr::POS_PARAM, Expr::POS_CV_INPUT},
                {"GAIN", Expr::GAIN_PARAM, Expr::GAIN_CV_INPUT}, {"LIMIT", Expr::LIMIT_PARAM, Expr::LIMIT_CV_INPUT}};
            for (int k = 0; k < 6; k++) {
                centre(getParam(rows[k].param), Vec(kKnobX, row(k)));
                centre(getInput(rows[k].cv), Vec(kJackX, row(k)));
                wideBg->labels.push_back({rows[k].label, Vec(kLabelX, row(k)), 11.f});
            }
            for (int i = 0; i < (int) rings.size(); i++) centre(rings[i], Vec(kKnobX, row(i)));
            // Jack rows: the wire in and reset, then the two outputs, each
            // jack named underneath.
            const float jackLabelDy = 15.f;
            centre(getInput(Expr::PHASE_INPUT), Vec(kKnobX, row(6)));
            centre(getInput(Expr::RST_INPUT), Vec(kJackX, row(6)));
            centre(getOutput(Expr::Y_OUTPUT), Vec(kKnobX, row(7)));
            centre(getOutput(Expr::X_OUTPUT), Vec(kJackX, row(7)));
            wideBg->labels.push_back({"IN", Vec(kLabelX, row(6)), 11.f});
            wideBg->labels.push_back({"OUT", Vec(kLabelX, row(7)), 11.f});
            wideBg->labels.push_back({"PHASE", Vec(kKnobX, row(6) + jackLabelDy), 7.f});
            wideBg->labels.push_back({"RST", Vec(kJackX, row(6) + jackLabelDy), 7.f});
            wideBg->labels.push_back({"Y", Vec(kKnobX, row(7) + jackLabelDy), 7.f});
            wideBg->labels.push_back({"X", Vec(kJackX, row(7) + jackLabelDy), 7.f});
            for (Widget* w : children) {
                if (auto* l = dynamic_cast<ModuleLightWidget*>(w)) centre(l, Vec(kLabelX, row(7) - 12.f));
                // The right-hand screws go to the new right edge.
                if (dynamic_cast<ThemedScrew*>(w) && w->box.pos.x > rackSize.x / 2)
                    w->box.pos.x = box.size.x - 2 * RACK_GRID_WIDTH;
            }
            display->box.pos = Vec(kColumnW + kGap, kEdge);
            display->box.size = Vec(box.size.x - kColumnW - kGap - 2 * RACK_GRID_WIDTH, box.size.y - 2 * kEdge);
            // The legend's four readings spread evenly under the plot.
            const float dw = display->box.size.x;
            for (int i = 0; i < X::kMaxSlots; i++) display->colX[i] = dw * (2 * i + 1) / 8.f;
            display->loopX = dw * 5 / 8.f;
            display->posX = dw * 7 / 8.f;
        }
        if (APP->scene && APP->scene->rack && parent) APP->scene->rack->setModulePosForce(this, box.pos);
    }

    /// kRingMm (layout parameter): RoundBlackKnob is 9.6 mm across; 12 mm
    /// leaves a visible ring that stays clear of the slot's CV jack.
    void addRing(Expr* module, int slot, const std::string& anchor) {
        constexpr float kRingMm = 12.f;
        ActiveRing* r = new ActiveRing;
        r->module = module;
        r->slot = slot;
        r->box.size = mm2px(Vec(kRingMm, kRingMm));
        r->box.pos = anchorCentre(anchor).minus(r->box.size.div(2));
        addChild(r);
        rings.push_back(r);
    }

    Vec anchorCentre(const std::string& name) {
        Vec found = Vec(0, 0);
        forEachShape([&](NSVGshape* shape) {
            if (name == shape->id) found = calculateCenter(shape);
        });
        return found;
    }

    /// Free retired programs on the UI thread, and find the window of the
    /// PHASE feeding the wire for the display.
    void step() override {
        if (auto* m = dynamic_cast<Expr*>(module)) {
            m->drainRetired();
            syncPanel(m);
            if (m->wideLayout != wideShown) setLayout(m->wideLayout);
            display->winStart = -1.f;
            const int src = m->dispSource.load();
            if (src == Expr::SRC_ADJACENT) {
                display->winStart = m->windowStart;
                display->winLen = m->windowLen;
            } else if (src == Expr::SRC_CABLE) {
                // A cable carries only volts; read the window off the module
                // at the other end, when it is one of the family.
                for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(getInput(Expr::PHASE_INPUT))) {
                    if (auto* a = dynamic_cast<AgentModule*>(cw->cable->outputModule)) {
                        display->winStart = a->windowStart;
                        display->winLen = a->windowLen;
                    }
                }
            }
        }
        ModuleWidget::step();
    }

    void appendContextMenu(Menu* menu) override {
        Expr* module = getModule<Expr>();
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Edit program...", "", [=] { openEditor(module); }));
        menu->addChild(createBoolPtrMenuItem("Show equation", "", &module->eqPanel.open));
        menu->addChild(createIndexPtrSubmenuItem("Layout", {"Rack panel", "Wide display"}, &module->wideLayout));
        menu->addChild(createSubmenuItem("Examples", "", [=](Menu* sub) {
            sub->addChild(createMenuLabel("Patterns"));
            for (int i = 0; i < X::kPatternExampleCount; i++) {
                const X::Example& ex = X::kPatternExamples[i];
                sub->addChild(createMenuItem(ex.name, "", [=] { commitProgram(module, ex.source); }));
            }
            sub->addChild(new MenuSeparator);
            sub->addChild(createMenuLabel("Basics"));
            for (int i = 0; i < X::kBasicExampleCount; i++) {
                const X::Example& ex = X::kBasicExamples[i];
                sub->addChild(createMenuItem(ex.name, "", [=] { commitProgram(module, ex.source); }));
            }
        }));
        menu->addChild(createSubmenuItem("Knob steps", "", [=](Menu* sub) {
            bool any = false;
            for (int i = 0; i < X::kMaxSlots; i++) {
                if (!module->slots[i].used) continue;
                any = true;
                sub->addChild(createCheckMenuItem(
                    string::f("%s: fine (unchecked: whole numbers)", module->slots[i].name.c_str()), "",
                    [=] { return module->fineSteps[i].load(); },
                    [=] { module->fineSteps[i].store(!module->fineSteps[i].load()); }));
            }
            if (!any) sub->addChild(createMenuLabel("No knobs: the equation has no letters"));
        }));
        menu->addChild(createMenuItem("Reset program state", "", [=] { module->resetRequest.store(true); }));

        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Offset", "", [=](Menu* sub) {
            static const float opts[] = {-5.f, -2.f, -1.f, 0.f, 1.f, 2.f, 5.f};
            for (float o : opts)
                sub->addChild(createCheckMenuItem(string::f("%+g V", o), "",
                    [=] { return module->offset.load() == o; }, [=] { module->offset.store(o); }));
        }));
        menu->addChild(createSubmenuItem("Limiter knee", "", [=](Menu* sub) {
            static const float knees[] = {0.f, 0.5f, 1.f, 2.f, 4.f};
            for (float K : knees)
                sub->addChild(createCheckMenuItem(K == 0.f ? "Hard knee" : string::f("Soft knee %g V", K), "",
                    [=] { return module->knee.load() == K; }, [=] { module->knee.store(K); }));
        }));
        menu->addChild(createSubmenuItem("Smoothing", "", [=](Menu* sub) {
            sub->addChild(createCheckMenuItem("Off", "",
                [=] { return module->smooth.load() == X::Conditioner::SMOOTH_NONE; },
                [=] { module->smooth.store(X::Conditioner::SMOOTH_NONE); }));
            static const float taus[] = {0.001f, 0.010f, 0.100f, 1.f};
            for (float t : taus)
                sub->addChild(createCheckMenuItem(string::f("Lag %g ms", t * 1000.f), "",
                    [=] { return module->smooth.load() == X::Conditioner::SMOOTH_LAG && module->tauSec.load() == t; },
                    [=] { module->smooth.store(X::Conditioner::SMOOTH_LAG); module->tauSec.store(t); }));
            static const float rates[] = {10.f, 100.f, 1000.f, 10000.f};
            for (float r : rates)
                sub->addChild(createCheckMenuItem(string::f("Slew %g V/s", r), "",
                    [=] { return module->smooth.load() == X::Conditioner::SMOOTH_SLEW && module->riseVps.load() == r; },
                    [=] { module->smooth.store(X::Conditioner::SMOOTH_SLEW); module->riseVps.store(r); module->fallVps.store(r); }));
        }));
        menu->addChild(createCheckMenuItem("LOOP repeats the slice at full speed", "",
            [=] { return module->loopRepeats.load(); },
            [=] { module->loopRepeats.store(!module->loopRepeats.load()); }));
        menu->addChild(createSubmenuItem("On fault", "", [=](Menu* sub) {
            sub->addChild(createCheckMenuItem("Output 0 V", "",
                [=] { return module->faultPolicy.load() == X::Conditioner::FAULT_ZERO; },
                [=] { module->faultPolicy.store(X::Conditioner::FAULT_ZERO); }));
            sub->addChild(createCheckMenuItem("Hold the last valid value", "",
                [=] { return module->faultPolicy.load() == X::Conditioner::FAULT_HOLD; },
                [=] { module->faultPolicy.store(X::Conditioner::FAULT_HOLD); }));
        }));
        menu->addChild(createSubmenuItem("Edit crossfade", "", [=](Menu* sub) {
            static const float opts[] = {0.f, 10.f, 50.f};
            for (float ms : opts)
                sub->addChild(createCheckMenuItem(ms == 0.f ? "Immediate" : string::f("%g ms", ms), "",
                    [=] { return module->crossfadeMs.load() == ms; }, [=] { module->crossfadeMs.store(ms); }));
        }));
        menu->addChild(createSubmenuItem("History window", "", [=](Menu* sub) {
            static const int opts[] = {1, 4, 16, 32};
            for (int s : opts)
                sub->addChild(createCheckMenuItem(string::f("%d s", s), "",
                    [=] { return module->historyWindowSec == s; }, [=] { module->historyWindowSec = s; }));
            sub->addChild(new MenuSeparator);
            sub->addChild(createBoolPtrMenuItem("Show target under output", "", &module->showTarget));
        }));
    }
};

Model* modelExpr = createModel<Expr, ExprWidget>("Expr");
