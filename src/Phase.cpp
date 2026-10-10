// PHASES PHASOR -- the time source: a clock-to-phase converter.
//
// Position is a wire. PHASE carries one 0-10 V ramp lap per CLK pulse: each
// pulse restarts the ramp, the previous pulse-to-pulse interval sets the
// slope, and the ramp clamps just short of the top until the next pulse
// lands. When pulses stop the transport freezes. RST snaps the lap to zero,
// and the span it interrupts is not measured as tempo. DIRECTION reverses
// the ramp.
//
// A stopped transport is not necessarily a still wire. The window (below)
// is applied after the freeze, so with no pulses arriving the output is
// START plus a constant: move START and the wire moves, and every follower
// plays. That is scrubbing a stopped tape by hand, and it is deliberate --
// the invariant is that a motionless WIRE makes no sound, which is a
// stricter and more useful statement than one about the clock.
//
// Lap length is a knob, not patch math. LAP (BEATS_PARAM) says how many
// beats a lap spans, and RATE how many CLK pulses count as a beat: 24 is the
// hardware sync rate a drum machine wants, so one cable feeds the drums and
// the wire. Fewer pulses per beat on the same clock is a faster lap, which
// is how RATE pushes the wire towards audio rate. RATE 1 with LAP 1 is the
// older contract where each pulse is a whole lap. Followers read the wire as pure position -- 0 V is the top of
// their material, 10 V the end -- so every follower of one wire agrees by
// construction.
//
// Between pulses the ramp interpolates on the last measured interval, so a
// coarse clock still yields a smooth wire.

#include <rack.hpp>
#include "AgentModule.hpp"
#include "SvgHelper.hpp"
#include "lives/LapCounter.hpp"
#include "lives/PhaseWire.hpp"

#include <cmath>

using namespace rack;

extern Plugin* pluginInstance;

// CLK rates, as a panel selector rather than a menu item. 24 PPQN is what a
// drum machine takes off a sync cable and what the Ghost drums want, so it
// stays the default -- but most Rack clocks emit roughly one pulse per beat,
// and at the default those land a 96-pulse lap: twenty-four bars, which reads
// as a dead module rather than a wrong setting. Putting the rate on the panel
// is the difference between a visible knob and a right-click nobody opens.
// Clockwise means fewer pulses per lap, so turning RATE up speeds the lap up.
// Patches store the PPQN itself ("ppqn"), not the knob position.
static const int kRates[] = {48, 24, 16, 12, 8, 4, 2, 1};
static const int kRateCount = 8;
static const int kRateDefault = 1;   // 24 PPQN
static const int kRateOne = 7;       // 1 pulse per beat

// RATE reads as a speed: how much faster than LAP the ramp runs on a 24 PPQN
// clock, the default and what Clocked's sync output sends. The PPQN it
// counts is in brackets for any other clock. Turned all the way up, a fast
// clock pushes the lap into audio rate.
struct RateQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int i = clamp((int) std::lround(getValue()), 0, kRateCount - 1);
        return string::f("×%g on a 24 PPQN clock (counts %d PPQN)", 24.0 / kRates[i], kRates[i]);
    }
};

struct BeatsQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        int n = std::max(1, (int) std::lround(getValue()));
        int bars = n / 4, rem = n % 4;
        if (n < 4)
            return string::f("%d beat%s", n, n == 1 ? "" : "s");
        if (rem == 0)
            return string::f("%d bar%s (%d beats)", bars, bars == 1 ? "" : "s", n);
        return string::f("%d bar%s + %d (%d beats)", bars, bars == 1 ? "" : "s", rem, n);
    }
};

struct Phase : AgentModule {
    enum ParamId { DIR_PARAM, START_PARAM, LEN_PARAM, BEATS_PARAM,
                   RATE_PARAM, NUM_PARAMS };
    enum InputId { CLK_INPUT, RST_INPUT, START_CV_INPUT, LEN_CV_INPUT,
                   DIR_CV_INPUT, NUM_INPUTS };
    enum OutputId { PHASE_OUTPUT, NUM_OUTPUTS };
    enum LightId { CLK_LIGHT, NUM_LIGHTS };

    dsp::SchmittTrigger clkTrig, rstTrig, dirTrig;
    dsp::PulseGenerator lapLight;   // CLK jack's light: one flash per lap
    double phase     = 0.0;
    bool   haveEdge  = false;  // seen a pulse this cable session
    bool   cleanSpan = false;  // no RST since the last pulse
    float  sinceEdge = 0.f;    // samples since the last pulse (or RST)
    float  interval  = 0.f;    // last full pulse-to-pulse span, samples
    Lives::LapCounter lap;     // where a lap starts, and when it may resize
    double heldPhase = 0.0;    // the wire while the clock is late (see below)

    /// The lap length the controls are currently asking for. Read only on a
    /// clock pulse -- see process().
    int wantLapPulses() {
        return std::max(1,
            (int) std::lround(params[BEATS_PARAM].getValue()) * ppqn());
    }

    /// CLK pulses per beat, from the panel selector. Non-const because
    /// Rack does not const-qualify Param::getValue().
    int ppqn() {
        int i = clamp((int) std::lround(params[RATE_PARAM].getValue()),
                      0, kRateCount - 1);
        return kRates[i];
    }

    Phase() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(DIR_PARAM, 0.f, 1.f, 0.f, "Direction",
                     {"Forward", "Reverse"});
        configParam(START_PARAM, 0.f, 1.f, 0.f,
                    "Window start", "%", 0.f, 100.f);
        configParam(LEN_PARAM, 0.f, 1.f, 1.f,
                    "Window length", "%", 0.f, 100.f);
        configParam<BeatsQuantity>(BEATS_PARAM, 1.f, 16.f, 4.f, "Lap length");
        paramQuantities[BEATS_PARAM]->snapEnabled = true;
        configParam<RateQuantity>(RATE_PARAM, 0.f, float(kRateCount - 1),
                                  float(kRateDefault), "CLK rate");
        paramQuantities[RATE_PARAM]->snapEnabled = true;
        configInput(CLK_INPUT, "Clock (RATE counts its pulses per beat)");
        configInput(RST_INPUT, "Reset");
        configInput(START_CV_INPUT, "Window start CV (10V = full range)");
        configInput(LEN_CV_INPUT, "Window length CV (10V = full range)");
        configInput(DIR_CV_INPUT, "Direction gate (inverts the switch)");
        configOutput(PHASE_OUTPUT, "Phase (0-10V ramp)");
    }

    void process(const ProcessArgs& args) override {
        if (rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f)) {
            sinceEdge = 0.f;
            cleanSpan = false;
            lap.arm();          // the pulse that lands with RST opens the lap
        }

        if (inputs[CLK_INPUT].isConnected()) {
            sinceEdge += 1.f;
            if (clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f)) {
                // Only an uninterrupted pulse-to-pulse span is an interval;
                // the wait before the first pulse, and any span RST cut
                // short, are dead time, not tempo.
                if (haveEdge && cleanSpan)
                    interval = sinceEdge;
                haveEdge  = true;
                cleanSpan = true;
                sinceEdge = 0.f;
                // The lap length is read HERE and nowhere else: it has one
                // consumer, and reading it per pulse rather than per sample
                // is both cheaper and the semantics we want -- a knob turn or
                // a CV is sampled at the boundary that will use it.
                if (lap.pulse(wantLapPulses()))
                    lapLight.trigger(0.06f);
            }
            if (haveEdge) {
                // Interpolate across the pulse so a coarse clock still gives
                // a smooth ramp; the pulse count carries the lap position.
                const double frac = (interval > 0.f)
                    ? std::fmin(sinceEdge / interval, 1.0) : 0.0;
                // A clock that is merely late pins frac at 1 and the wire
                // holds. Emit the held value rather than recomputing, so the
                // lap length drops out of the stopped-state expression: with
                // it in, adopting a new length while stopped would move the
                // wire, and the followers' motion detector reads any move as
                // the transport running -- 20-odd ms of audio out of a
                // machine the user believes is stopped.
                if (frac < 1.0) {
                    phase = lap.phase(frac);
                    heldPhase = phase;
                }
                else {
                    phase = heldPhase;
                    // Genuinely stopped rather than jittery: no lap is in
                    // flight, so there is no boundary to wait for and the
                    // knob takes effect at once. The 2x test keeps a pulse
                    // that is a sample late from counting as a stop.
                    if (interval > 0.f && sinceEdge > 2.f * interval)
                        lap.adoptNow(wantLapPulses());
                }
            }
        } else {
            // Unpatched: hold position, and forget the follower so a
            // re-patch starts clean.
            haveEdge = false; cleanSpan = false;
            sinceEdge = 0.f; interval = 0.f;
            lap.arm();
        }

        // The window: START and LEN select which part of the material a lap
        // plays -- on the generator, so every follower of the wire windows
        // together. frac() lets a window cross the splice. Followers see
        // per-lap jumps of LEN, which is why their wrap threshold is 10%.
        // DIRECTION: the gate inverts the switch rather than overriding it, so
        // the switch still picks the resting direction with something patched
        // and a momentary gate is a reverse stab. The flip lands immediately,
        // mid-lap, which is what the panel switch has always done -- followers
        // read the jump as a wrap, and that discontinuity is the gesture.
        bool rev = params[DIR_PARAM].getValue() > 0.5f;
        dirTrig.process(inputs[DIR_CV_INPUT].getVoltage(), 0.1f, 1.f);
        if (dirTrig.isHigh())
            rev = !rev;
        double p = rev ? 1.0 - phase : phase;
        // START is a position on the loop, so knob + CV wraps rather than
        // clamps: a bipolar LFO on START CV keeps the window travelling
        // instead of pinning it at an edge for part of every cycle.
        float startV = params[START_PARAM].getValue()
            + (inputs[START_CV_INPUT].isConnected()
                   ? inputs[START_CV_INPUT].getVoltage() / 10.f : 0.f);
        startV -= std::floor(startV);
        float lenV = clamp(params[LEN_PARAM].getValue()
            + (inputs[LEN_CV_INPUT].isConnected()
                   ? inputs[LEN_CV_INPUT].getVoltage() / 10.f : 0.f),
            0.05f, 1.f);
        double w = startV + p * lenV;
        w -= std::floor(w);
        float out = 10.f * float(w);
        outputs[PHASE_OUTPUT].setVoltage(out);
        // Publish the wire for an adjacent follower to pick up; that read is
        // what starts the chain (Lives::resolveWire). Live only once a pulse
        // has been seen: an unclocked PHASE has no transport to offer, and
        // claiming one makes its neighbours report a healthy wire while
        // sitting silent -- the wrong answer in exactly the case where the
        // user is already stuck. A clock that is patched but stopped keeps
        // haveEdge, so the wire stays live and START still scrubs.
        // The CLK light answers the question the panel could not: is a clock
        // arriving, and how long is a lap. A dim glow while pulses land, a
        // flash on each lap. Dark means no clock -- which used to be
        // indistinguishable from every other reason for silence.
        const bool flash = lapLight.process(args.sampleTime);
        lights[CLK_LIGHT].setBrightnessSmooth(
            flash ? 1.f : (haveEdge ? 0.12f : 0.f), args.sampleTime);

        wireVolts = out;
        wireLive  = haveEdge;
        windowStart = startV;
        windowLen   = lenV;
        wireFrame    = args.frame;   // the leader's stamp; followers propagate it
        lastRunFrame = args.frame;
    }

    // Initialize snaps the params back, but PPQN is menu state and the lap is
    // engine state, so without this a reset PHASE keeps whatever rate the menu
    // was left on and resumes mid-lap. Default means default.
    void onReset() override {
        phase = 0.0;
        heldPhase = 0.0;
        haveEdge = cleanSpan = false;
        sinceEdge = interval = 0.f;
        lap.reset();
        wireVolts = 0.f;
        wireLive = false;
        wireFrame = lastRunFrame = -1;
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "ppqn", json_integer(ppqn()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        // RATE is a param now, so it rides the params array. Patches saved
        // while PPQN was menu state still carry the key; map it back.
        if (json_t* j = json_object_get(root, "ppqn")) {
            const int want = std::max(1, (int) json_integer_value(j));
            for (int i = 0; i < kRateCount; i++)
                if (kRates[i] == want) {
                    params[RATE_PARAM].setValue(float(i));
                    break;
                }
        }
    }

    // Legacy load. A patch written before BEATS and PPQN existed has no
    // "data" object at all, and Rack does not call dataFromJson when it is
    // absent -- Module.hpp: "Not called if 'data' property is not present."
    // So the old contract cannot be restored there; it has to be restored
    // here, which is what fromJson is documented as being virtual for.
    //
    // Without this such a patch keeps the new defaults, ppqn 24 and BEATS 4,
    // giving a 96-pulse lap against an old contract of one pulse per lap:
    // the whole patch plays 96x slow with nothing on screen to explain it.
    // Module::fromJson loads params and calls dataFromJson first, so the
    // override lands after both and wins.
    void fromJson(json_t* rootJ) override {
        Module::fromJson(rootJ);
        if (!json_object_get(rootJ, "data")) {
            params[RATE_PARAM].setValue(float(kRateOne));
            params[BEATS_PARAM].setValue(1.f);
        }
    }
};

struct PhaseWidget : ModuleWidget, SvgHelper<PhaseWidget> {
    explicit PhaseWidget(Phase* module) {
        setModule(module);
        loadPanel(asset::plugin(pluginInstance, "res/Phase.svg"));

        addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ThemedScrew>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        bindParam<CKSS>("param.main.dir", Phase::DIR_PARAM);
        bindParam<RoundBlackKnob>("param.main.start", Phase::START_PARAM);
        bindParam<RoundBlackKnob>("param.main.len",   Phase::LEN_PARAM);
        bindParam<RoundBlackKnob>("param.main.beats", Phase::BEATS_PARAM);
        bindParam<RoundBlackKnob>("param.main.rate",  Phase::RATE_PARAM);
        bindInput<PJ301MPort>("clock.main.in", Phase::CLK_INPUT);
        bindInput<PJ301MPort>("trig.main.reset", Phase::RST_INPUT);
        bindInput<PJ301MPort>("cv.main.start", Phase::START_CV_INPUT);
        bindInput<PJ301MPort>("cv.main.len",   Phase::LEN_CV_INPUT);
        bindInput<PJ301MPort>("cv.main.dir",   Phase::DIR_CV_INPUT);
        bindOutput<PJ301MPort>("out.main.phase", Phase::PHASE_OUTPUT);
        bindLight<MediumLight<GreenLight>>("light.main.clock",
                                          Phase::CLK_LIGHT);
    }

};

Model* modelPhase = createModel<Phase, PhaseWidget>("Phase");
