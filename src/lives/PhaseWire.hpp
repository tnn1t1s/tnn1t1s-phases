#pragma once

// PhaseWire.hpp -- how a follower finds its transport.
//
// Two ways, tried in order:
//
//   1. a cable on the PHASE input -- explicit, and always wins
//   2. the family module touching it, left or right
//
// and if neither answers, the wire is dead, which every follower already
// renders as silence. Each follower republishes what it resolved
// (AgentModule::wireVolts), so the chain extends through it: PHASE, CLIPS,
// TAPES standing in a row share one transport with no cables at all.
//
// Adjacency is the whole wireless mechanism. There is deliberately no
// rack-wide fallback: two LIVES PHASE modules are two transports, and which
// one a follower belongs to is answered by where it sits, the same way the
// Ghost drums answer it (Lives::resolveBus). A follower standing alone makes
// no sound, which is a legible failure -- you can see the gap on the panel.
//
// Rack may process modules on parallel engine threads, so a follower can read
// its neighbour's value one sample stale. For absolute position on a
// bar-length lap that is a constant sub-sample offset that cannot accumulate,
// because nothing downstream integrates it. The same channel carrying events
// would be unsound; see doc/dev/phase-wire.md.

#include "AgentModule.hpp"
#include "lives/WireFresh.hpp"

namespace Lives {

/// Resolve a follower's transport: cable, then adjacency, then nothing.
/// Republishes the result on `self` so the chain extends to its far
/// neighbour. `in` is non-const because Rack does not const-qualify
/// Port::isConnected().
inline float resolveWire(AgentModule* self, rack::engine::Input& in,
                         int64_t frame, bool* liveOut) {
    float v = 0.f;
    bool live = false;
    int64_t stamp = -1;
    AgentModule::WireSource src = AgentModule::WIRE_NONE;

    // A neighbour counts if it is still running, and if it saw a live leader
    // the last time IT ran. The second test is against the neighbour's own
    // lastRunFrame rather than our frame, which is what makes this hold at
    // any chain depth: each hop only has to have been fresh when it ran, so
    // the tolerance does not tighten as the stamp travels. A leaderless pair
    // still dies -- pull PHASE and the pair keep running, so lastRunFrame
    // advances while wireFrame does not, and the second test fails.
    auto fresh = [frame](const AgentModule* n) {
        return n && wireFresh(n->wireLive, n->wireFrame,
                              n->lastRunFrame, frame);
    };

    if (in.isConnected()) {
        v = in.getVoltage();
        live = true;
        stamp = frame;                  // a cable is always current
        src = AgentModule::WIRE_CABLE;
    }
    float winStart = -1.f, winLen = 1.f;
    if (!live) {
        AgentModule* n =
            dynamic_cast<AgentModule*>(self->leftExpander.module);
        if (fresh(n)) {
            v = n->wireVolts; live = true; stamp = n->wireFrame;
            src = AgentModule::WIRE_ADJACENT;
            winStart = n->windowStart; winLen = n->windowLen;
        }
    }
    if (!live) {
        AgentModule* n =
            dynamic_cast<AgentModule*>(self->rightExpander.module);
        if (fresh(n)) {
            v = n->wireVolts; live = true; stamp = n->wireFrame;
            src = AgentModule::WIRE_ADJACENT;
            winStart = n->windowStart; winLen = n->windowLen;
        }
    }

    self->lastRunFrame = frame;   // stamped whether or not a wire was found
    self->wireVolts  = v;
    self->wireLive   = live;
    self->wireFrame  = stamp;           // the leader's stamp, not ours
    self->wireSource = src;
    self->windowStart = winStart;
    self->windowLen   = winLen;
    if (liveOut) *liveOut = live;
    return v;
}

}  // namespace Lives
