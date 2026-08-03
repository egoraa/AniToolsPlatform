#ifndef ATP_RUNTIME_COMMAND_QUEUE_HPP
#define ATP_RUNTIME_COMMAND_QUEUE_HPP

#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace atp::runtime {

/// Marshals a call onto the thread that owns the pipeline.
///
/// All pipeline_runner control is owner-thread-only, and the studio honours that by making the GUI
/// thread the owner. A host with a transport of its own has no such luxury: requests arrive on the
/// transport thread and have to be handed over. The transport calls call(), the owner loop calls
/// run_pending(), and the result — or the exception — travels back.
///
/// Deliberately not usable from the owner thread itself: call() from inside run_pending() would
/// deadlock, and no host has a reason to do it.
class command_queue {
   public:
    /// Runs fn on the owner thread and waits for it.
    /// @param fn callable returning a non-void, movable result
    /// @return whatever fn returned
    /// @throws std::runtime_error if the queue is or becomes closed; anything fn throws is rethrown
    ///         here, on the calling thread, which is what turns a tool's config_error into an error
    ///         reply without a line of extra handling
    template <typename TFn>
    std::invoke_result_t<TFn&> call(TFn&& fn) {
        using result_t = std::invoke_result_t<TFn&>;
        std::optional<result_t> result;
        entry e;
        e.work = [&] { result.emplace(std::forward<TFn>(fn)()); };
        {
            std::unique_lock lock(mutex_);
            if (closed_) {
                throw std::runtime_error("the control queue is closed");
            }
            pending_.push_back(&e);
            arrived_.notify_one();
            finished_.wait(lock, [&e] { return e.done; });
        }
        if (e.failure) {
            std::rethrow_exception(e.failure);
        }
        if (!result) {
            throw std::runtime_error("the control queue was closed before the call ran");
        }
        return std::move(*result);
    }

    /// Owner thread: waits up to the timeout for work, then runs everything queued. The wait is what
    /// replaces the host's idle sleep, so a control request wakes it at once instead of at the next
    /// tick.
    /// @param timeout how long to wait when the queue is empty
    void run_pending(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        arrived_.wait_for(lock, timeout, [this] { return !pending_.empty() || closed_; });
        while (!pending_.empty()) {
            entry* e = pending_.front();
            pending_.pop_front();
            lock.unlock();
            try {
                e->work();
            } catch (...) {
                e->failure = std::current_exception();
            }
            lock.lock();
            e->done = true;
            finished_.notify_all();
        }
    }

    /// Refuses further calls and releases whoever is waiting. The owner loop calls it on the way out,
    /// so a transport thread blocked in call() fails instead of waiting for a loop that is gone.
    void close() {
        const std::unique_lock lock(mutex_);
        closed_ = true;
        for (entry* e : pending_) {
            e->done = true;
        }
        pending_.clear();
        arrived_.notify_all();
        finished_.notify_all();
    }

   private:
    /// One queued call. It lives on the caller's stack, which is safe because the caller waits for
    /// done inside call() and cannot leave the frame before the owner has set it.
    struct entry {
        std::function<void()> work;
        std::exception_ptr failure;
        bool done = false;
    };

    std::mutex mutex_;
    std::condition_variable arrived_;
    std::condition_variable finished_;
    std::deque<entry*> pending_;
    bool closed_ = false;
};

}  // namespace atp::runtime

#endif
