#pragma once

// ExprHistory.hpp -- what the module actually put out, kept for the display.
//
// The audio thread aggregates every sample into the current bin (min, max,
// first and last of both the conditioned target and the actual output, the
// x endpoints, and event flags) and publishes a completed bin roughly 128
// times a second into a ring of 8192: about 64 seconds retained. Spikes
// survive aggregation because a bin carries its extremes, not a decimated
// point.
//
// Transfer is a single-producer single-consumer ring: the writer stores a
// bin's fields, then publishes its index with a release store; the reader
// takes the index with an acquire load and copies back from it. The bin
// fields are relaxed atomics so that even a reader lapped by the writer --
// which the reader detects after copying, and discards -- is not a data
// race in the C++ sense. The reader may lag or skip; it never blocks the
// writer, and the writer never waits.
//
// History is not a delay line and not a full-rate recording. Scrubbing or
// reversing the wire changes x; it does not rewind this.

#include <atomic>
#include <cstdint>

namespace Lives {
namespace Expr {

enum HistoryFlag : uint32_t {
    HF_RESET      = 1u << 0,
    HF_TRIG       = 1u << 1,
    HF_FAULT      = 1u << 2,
    HF_CLIP       = 1u << 3,
    HF_SOURCE     = 1u << 4,   // input source changed (cable, adjacency, X, manual)
    HF_PROGRAM    = 1u << 5,   // a new program activated
    HF_PHASE_JUMP = 1u << 6,   // the wire stepped by more than a tenth of a lap
};

/// A completed bin, as the display reads it.
struct HistoryBin {
    float tMin, tMax, tFirst, tLast;   // conditioned target, volts
    float aMin, aMax, aFirst, aLast;   // actual output, volts
    float xFirst, xLast;               // function argument
    uint32_t flags;
    uint32_t revision;                 // program revision active during the bin
};

namespace detail {
/// The ring's storage form of a bin: every field an atomic, relaxed.
struct AtomicBin {
    std::atomic<float> f[10];
    std::atomic<uint32_t> flags;
    std::atomic<uint32_t> revision;
    AtomicBin() : flags(0), revision(0) { for (int i = 0; i < 10; i++) f[i].store(0.f); }
};
}  // namespace detail

template <int CAPACITY>
struct HistoryRing {
    static constexpr float kBinsPerSecond = 128.f;

    detail::AtomicBin bins[CAPACITY];
    std::atomic<uint64_t> written;   // bins published so far

    // Writer-private.
    HistoryBin cur;
    int curCount = 0;
    int binSamples = 375;            // 48 kHz / 128

    HistoryRing() : written(0) { clearCurrent(); }

    void setSampleRate(float sr) {
        binSamples = int(sr / kBinsPerSecond);
        if (binSamples < 1) binSamples = 1;
    }

    void clearCurrent() {
        cur.tMin = cur.tMax = cur.tFirst = cur.tLast = 0.f;
        cur.aMin = cur.aMax = cur.aFirst = cur.aLast = 0.f;
        cur.xFirst = cur.xLast = 0.f;
        cur.flags = 0;
        cur.revision = 0;
        curCount = 0;
    }

    /// Audio thread: one sample. flags accumulate over the bin.
    void record(float target, float actual, float x, uint32_t flags, uint32_t revision) {
        if (curCount == 0) {
            cur.tMin = cur.tMax = cur.tFirst = target;
            cur.aMin = cur.aMax = cur.aFirst = actual;
            cur.xFirst = x;
            cur.flags = 0;
            cur.revision = revision;
        } else {
            if (target < cur.tMin) cur.tMin = target;
            if (target > cur.tMax) cur.tMax = target;
            if (actual < cur.aMin) cur.aMin = actual;
            if (actual > cur.aMax) cur.aMax = actual;
        }
        cur.tLast = target;
        cur.aLast = actual;
        cur.xLast = x;
        cur.flags |= flags;
        if (++curCount >= binSamples) {
            publish();
            curCount = 0;
        }
    }

    void publish() {
        const uint64_t w = written.load(std::memory_order_relaxed);
        detail::AtomicBin& b = bins[w % CAPACITY];
        const float src[10] = {cur.tMin, cur.tMax, cur.tFirst, cur.tLast,
                               cur.aMin, cur.aMax, cur.aFirst, cur.aLast,
                               cur.xFirst, cur.xLast};
        for (int i = 0; i < 10; i++) b.f[i].store(src[i], std::memory_order_relaxed);
        b.flags.store(cur.flags, std::memory_order_relaxed);
        b.revision.store(cur.revision, std::memory_order_relaxed);
        written.store(w + 1, std::memory_order_release);
    }

    /// Reader: copy the most recent `n` completed bins into `out`, oldest
    /// first. Returns how many were copied (fewer if the ring holds fewer).
    /// A reader lapped by the writer mid-copy gets a second try; after
    /// that it reports what it has, which the display may show as a gap.
    int snapshot(HistoryBin* out, int n) const {
        if (n > CAPACITY / 2) n = CAPACITY / 2;
        for (int attempt = 0; attempt < 2; attempt++) {
            const uint64_t w = written.load(std::memory_order_acquire);
            const uint64_t count = w < uint64_t(n) ? w : uint64_t(n);
            const uint64_t start = w - count;
            for (uint64_t i = 0; i < count; i++) {
                const detail::AtomicBin& b = bins[(start + i) % CAPACITY];
                float v[10];
                for (int k = 0; k < 10; k++) v[k] = b.f[k].load(std::memory_order_relaxed);
                HistoryBin& o = out[i];
                o.tMin = v[0]; o.tMax = v[1]; o.tFirst = v[2]; o.tLast = v[3];
                o.aMin = v[4]; o.aMax = v[5]; o.aFirst = v[6]; o.aLast = v[7];
                o.xFirst = v[8]; o.xLast = v[9];
                out[i].flags = b.flags.load(std::memory_order_relaxed);
                out[i].revision = b.revision.load(std::memory_order_relaxed);
            }
            const uint64_t w2 = written.load(std::memory_order_acquire);
            if (w2 - start <= uint64_t(CAPACITY)) return int(count);
        }
        return 0;
    }
};

}  // namespace Expr
}  // namespace Lives
