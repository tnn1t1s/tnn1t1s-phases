// wire_test.cpp -- coverage for the adjacency freshness rule.
//
// Drives the shipped Lives::wireFresh over a simulated [PHASOR][EXPR][EXPR]
// row, in every process order, with and without a leader. No Rack: the
// predicate is a pure function of four values, which is the whole point of
// having split it out.
//
// Why this test exists: the first version of the frame-stamp rule compiled
// clean, read correctly, and silently killed a three-module chain depending
// on the order the modules were dropped into the rack. Nothing caught it
// because nothing tested it.

#include "lives/WireFresh.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Node {
    std::string name;
    bool    leader       = false;
    bool    wireLive     = false;
    int64_t wireFrame    = -1;
    int64_t lastRunFrame = -1;
    float   volts        = 0.f;
    bool    present      = true;
};

// One module's turn, mirroring resolveWire's cable-less path.
void step(Node& m, int64_t frame, Node* left, Node* right) {
    if (m.leader) {
        m.volts = 1.f;
        m.wireLive = true;
        m.wireFrame = frame;
        m.lastRunFrame = frame;
        return;
    }
    float v = 0.f; bool live = false; int64_t stamp = -1;
    Node* nbrs[2] = {left, right};
    for (Node* n : nbrs) {
        if (!n || !n->present) continue;
        if (Lives::wireFresh(n->wireLive, n->wireFrame,
                             n->lastRunFrame, frame)) {
            v = n->volts; live = true; stamp = n->wireFrame;
            break;
        }
    }
    m.lastRunFrame = frame;
    m.volts = v; m.wireLive = live; m.wireFrame = stamp;
}

int failures = 0;

void check(const std::string& what, bool got, bool want) {
    const bool ok = (got == want);
    if (!ok) failures++;
    std::printf("  %-46s %-5s  %s\n", what.c_str(),
                got ? "live" : "dead", ok ? "ok" : "*** FAIL ***");
}

// Run `frames` steps over a 3-module row in the given process order.
// dropLeaderAt >= 0 removes PHASE at that frame.
void run(const std::vector<int>& order, int frames, int dropLeaderAt,
         bool& firstLive, bool& secondLive) {
    std::vector<Node> row(3);
    row[0].name = "PHASE"; row[0].leader = true;
    row[1].name = "EXPR1";
    row[2].name = "EXPR2";

    for (int f = 0; f < frames; f++) {
        if (dropLeaderAt >= 0 && f == dropLeaderAt)
            row[0].present = false;
        for (int idx : order) {
            if (!row[idx].present) continue;
            Node* l = nullptr;
            Node* r = nullptr;
            for (int j = idx - 1; j >= 0; j--)
                if (row[j].present) { l = &row[j]; break; }
            for (int j = idx + 1; j < 3; j++)
                if (row[j].present) { r = &row[j]; break; }
            step(row[idx], f, l, r);
        }
    }
    firstLive = row[1].wireLive;
    secondLive = row[2].wireLive;
}

const char* kNames[3] = {"PHASE", "EXPR1", "EXPR2"};

std::string label(const std::vector<int>& o) {
    std::string s;
    for (size_t i = 0; i < o.size(); i++) {
        if (i) s += ",";
        s += kNames[o[i]];
    }
    return s;
}

}  // namespace

int main() {
    // All six process orders. Rack orders modules by insertion, not by rack
    // position, so every one of these is a patch a user can actually build.
    const std::vector<std::vector<int>> orders = {
        {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0},
    };

    std::printf("leader present -- the chain must stay live at depth 2\n");
    for (const auto& o : orders) {
        bool c, t;
        run(o, 64, -1, c, t);
        check(label(o) + "  EXPR1", c, true);
        check(label(o) + "  EXPR2", t, true);
    }

    std::printf("leader removed -- no mutual sustain between followers\n");
    for (const auto& o : orders) {
        bool c, t;
        run(o, 64, 32, c, t);
        check(label(o) + "  EXPR1", c, false);
        check(label(o) + "  EXPR2", t, false);
    }

    std::printf("unit cases\n");
    // A dead neighbour is never fresh, whatever its stamps say.
    check("dead neighbour", Lives::wireFresh(false, 100, 100, 100), false);
    // A neighbour that has stopped running goes stale.
    check("neighbour stopped running",
          Lives::wireFresh(true, 100, 100, 103), false);
    // Running, and it saw the leader when it ran: fresh even though its
    // stamp is older than our frame -- this is the depth case.
    check("stale stamp, but fresh when it ran",
          Lives::wireFresh(true, 98, 99, 100), true);
    // Running, but its leader stamp has fallen behind its own last run.
    check("running with a stale leader",
          Lives::wireFresh(true, 90, 100, 100), false);

    std::printf("\n%s\n", failures ? "FAILURES" : "all wire checks passed");
    return failures ? 1 : 0;
}
