// TantraPerf: where the frame goes. The module's own work per frame, block by block (clbkPreStep / clbkPostStep), timed
// with the performance counter; every 5 s the averages (ms per frame), the frame rate and the time warp go to Orbiter.log
// as one line «Tantra perf». What is not ours (Orbiter, the graphics client drawing the scene and the custom cameras)
// is the rest of the frame: frame time - our total.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "orbitersdk.h"

namespace tantra::perf {

class Prof {
public:
    void Begin() { QueryPerformanceCounter(&t0_); }
    void Mark(const char* name) {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        if (!f_.QuadPart) QueryPerformanceFrequency(&f_);
        const double ms = double(t.QuadPart - t0_.QuadPart) * 1000.0 / double(f_.QuadPart);
        t0_ = t;
        int i = 0;
        while (i < n_ && name_[i] != name) ++i;
        if (i == n_) {
            if (n_ == kN) return;
            name_[n_++] = name;
        }
        acc_[i] += ms;
        if (ms > max_[i]) max_[i] = ms;
    }
    void Frame() {
        ++frames_;
        const double now = oapiGetSysTime();
        if (since_ < 0.0) { since_ = now; return; }
        if (now - since_ < 5.0) return;
        char buf[1400];
        int k = std::snprintf(buf, sizeof buf, "Tantra perf: %.0f fps, warp x%.0f, ms per frame:", frames_ / (now - since_), oapiGetTimeAcceleration());
        double total = 0.0;
        for (int i = 0; i < n_; ++i) total += acc_[i];
        k += std::snprintf(buf + k, sizeof buf - k, " ours %.2f of %.2f |", total / frames_, 1000.0 * (now - since_) / frames_);
        for (int i = 0; i < n_ && k < int(sizeof buf) - 40; ++i)
            if (acc_[i] / frames_ >= 0.05 || max_[i] >= 2.0)
                k += std::snprintf(buf + k, sizeof buf - k, " %s %.2f (max %.1f)", name_[i], acc_[i] / frames_, max_[i]);
        oapiWriteLog(buf);
        for (int i = 0; i < n_; ++i) acc_[i] = max_[i] = 0.0;
        frames_ = 0;
        since_ = now;
    }

private:
    static constexpr int kN = 40;
    const char* name_[kN] = {};
    double acc_[kN] = {}, max_[kN] = {};
    int n_ = 0, frames_ = 0;
    double since_ = -1.0;
    LARGE_INTEGER t0_ = {}, f_ = {};
};

inline Prof& P() { static Prof p; return p; }

}  // namespace tantra::perf

#define TANTRA_PERF(name) tantra::perf::P().Mark(name)
