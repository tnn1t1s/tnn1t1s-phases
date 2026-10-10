#pragma once
#include <rack.hpp>
#include <cstdint>

/**
 * AgentModule -- base class for Phases modules: the adjacency wire state
 * each module republishes so a neighbour can read it without a cable.
 */
struct AgentModule : rack::Module {
    // The family wire as this module resolved it, republished every sample so
    // the adjacency chain extends through it: PHASE publishes, its neighbour
    // reads and republishes, and the neighbour's neighbour reads that. See
    // Lives::resolveWire in lives/PhaseWire.hpp.
    float wireVolts = 0.f;
    bool  wireLive  = false;

    // The engine frame the LEADER stamped this wire on, propagated unchanged
    // down the chain. Without it two adjacent followers sustain each other:
    // pull PHASOR out of [PHASOR][EXPR][EXPR] and each EXPR reads the other,
    // both republishing a frozen voltage forever, panels still reporting a
    // live wire. A stamp that stops advancing when the
    // leader goes away makes the chain die within a sample.
    int64_t wireFrame = -1;

    // The engine frame this module last ran on. Freshness is judged against
    // THIS, not against the reader's own frame: a neighbour counts if it is
    // still running and it saw a live leader the last time it ran. Comparing
    // the leader's stamp to the current frame instead makes the test tighten
    // by one frame per hop, so a three-deep chain dies on an unlucky process
    // order -- which is deterministic per patch, because Rack orders modules
    // by insertion, not by rack position.
    int64_t lastRunFrame = -1;

    // Which tier answered, for display. Nothing downstream branches on it.
    enum WireSource { WIRE_NONE, WIRE_CABLE, WIRE_ADJACENT };
    WireSource wireSource = WIRE_NONE;

    // The window PHASE applied to this wire (START and LEN, fractions of the
    // lap), for display only. A negative start means unknown. It travels down
    // an adjacency chain with the wire; a cable carries only the voltage, so
    // a cabled follower's widget reads it off the module at the far end.
    float windowStart = -1.f;
    float windowLen   = 1.f;
};
