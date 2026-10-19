// TantraWorker: the ship's planning off the frame (2026-10-16). Orbiter runs the whole frame on one thread (Orbiter.cpp:
// UpdateWorld -> ModulePreStep / ModulePostStep, then the rendering), so a heavy plan in clbkPreStep stalls the picture. The
// planners (TantraReentry, TantraGuidance, TantraEntryForecast) are pure C++ - no Orbiter API, no shared mutable statics -
// and run here on their own copies of the inputs; the frame takes the newest result when it is ready.
//
//   Pool       one background thread of a ship (TantraDisplays::pool_); Start() / Stop() - Stop joins it, so the ship's
//              destructor never leaves it running. Not started (the offline tests): every job runs at once, inline.
//   Async<R>   one kind of job (a plan, a forecast): Request(pool, fn) replaces any request not yet begun - the newest wins;
//              Take(&r) on the frame's thread hands over a finished result once. A job never calls Orbiter.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace tantra::work {

class Pool {
public:
    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    ~Pool() { Stop(); }

    void Start() {
        std::lock_guard<std::mutex> lk(m_);
        if (th_.joinable()) return;
        stop_ = false;
        th_ = std::thread([this] { Loop(); });
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!th_.joinable()) return;
            stop_ = true;
            q_.clear();                               // what has not begun is dropped; the running job ends on its own
        }
        cv_.notify_all();
        th_.join();
    }
    bool Running() const { std::lock_guard<std::mutex> lk(m_); return th_.joinable() && !stop_; }
    void Post(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (th_.joinable() && !stop_) { q_.push_back(std::move(f)); cv_.notify_one(); return; }
        }
        f();                                          // no thread: at once (the tests, or after Stop)
    }

private:
    void Loop() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
                if (stop_) return;
                f = std::move(q_.front()); q_.pop_front();
            }
            try { f(); } catch (...) {}               // a planner's failure must not take Orbiter down
        }
    }
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    std::thread th_;
    bool stop_ = false;
};

template <class R>
class Async {
public:
    // the job to run (on its own copies); it replaces a request still waiting
    void Request(Pool* pool, std::function<R()> fn) {
        bool post = false;
        {
            std::lock_guard<std::mutex> lk(s_->m);
            s_->next = std::move(fn); ++s_->asked;
            if (!s_->busy) { s_->busy = true; post = true; }
        }
        if (!post) return;
        std::shared_ptr<State> s = s_;
        auto run = [s] { Drain(s); };
        if (pool) pool->Post(run); else run();
    }
    // the frame's thread: a result finished since the last Take (once)
    bool Take(R* out) {
        std::lock_guard<std::mutex> lk(s_->m);
        if (!s_->fresh) return false;
        *out = std::move(s_->out); s_->fresh = false;
        return true;
    }
    bool Busy() const { std::lock_guard<std::mutex> lk(s_->m); return s_->busy; }   // asked and not finished yet

private:
    struct State {
        std::mutex m;
        std::function<R()> next;
        R out{};
        bool busy = false, fresh = false;
        std::uint64_t asked = 0;
    };
    static void Drain(const std::shared_ptr<State>& s) {
        for (;;) {
            std::function<R()> fn;
            {
                std::lock_guard<std::mutex> lk(s->m);
                if (!s->next) { s->busy = false; return; }
                fn = std::move(s->next); s->next = nullptr;
            }
            R r{};
            bool ok = true;
            try { r = fn(); } catch (...) { ok = false; }
            std::lock_guard<std::mutex> lk(s->m);
            if (ok) { s->out = std::move(r); s->fresh = true; }
        }
    }
    std::shared_ptr<State> s_ = std::make_shared<State>();
};

}  // namespace tantra::work
