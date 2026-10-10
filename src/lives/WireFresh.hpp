#pragma once

// WireFresh.hpp -- "is this neighbour's wire still good?", as a pure function.
//
// Split out of PhaseWire.hpp so it can be tested without Rack: resolveWire
// needs a real rack::Module to have expander pointers, and the headless
// harness does not link libRack. This predicate is the whole of the logic
// that was wrong, so it is the part worth covering.
//
// The rule: a neighbour counts if it is still running, and if it saw a live
// leader the last time IT ran.
//
// The second test compares the neighbour's leader stamp to the neighbour's
// OWN last run, not to the reader's current frame. That is what makes the
// chain hold at any depth. Comparing against the reader's frame instead
// tightens the test by one frame per hop, because a follower that runs
// before its leader in a given frame republishes a stamp that is already one
// frame old -- so a three-deep chain fails on an unlucky process order, and
// Rack's process order follows module insertion, not rack position, making
// that failure deterministic per patch rather than intermittent.
//
// A leaderless pair still dies: pull the leader out and both followers keep
// running, so lastRunFrame advances while wireFrame stands still, and the
// second test fails within a frame.

#include <cstdint>

namespace Lives {

inline bool wireFresh(bool neighbourLive, int64_t neighbourWireFrame,
                      int64_t neighbourLastRunFrame, int64_t frame) {
    return neighbourLive
        && neighbourLastRunFrame + 1 >= frame
        && neighbourWireFrame + 1 >= neighbourLastRunFrame;
}

}  // namespace Lives
