#pragma once
// Lock-free hand-over of heavyweight objects (amp runtimes, IRs) from a worker/message thread
// to the audio thread. The audio thread never allocates or frees: replaced objects are parked
// in a retire list and deleted later by collectGarbage() on a non-realtime thread.

#include <array>
#include <atomic>
#include <memory>

namespace namknobs
{
template <typename T, int kRetireSlots = 8>
class RealtimeSwap
{
public:
    ~RealtimeSwap()
    {
        delete pending.exchange (nullptr);
        delete current;
        for (auto& r : retired) delete r.exchange (nullptr);
    }

    /** Non-realtime thread. Replaces any not-yet-consumed pending object. */
    void publish (std::unique_ptr<T> obj)
    {
        delete pending.exchange (obj.release(), std::memory_order_acq_rel);
    }

    /** Audio thread: picks up a pending object (if any). Returns the previous object, which stays
        valid until the caller calls retire() on it (lets the caller crossfade old → new). */
    T* acquire (T*& previousOut) noexcept
    {
        previousOut = nullptr;
        if (pending.load (std::memory_order_acquire) == nullptr) return current;
        // need a free retire slot for the old object, otherwise try again next block
        if (current != nullptr && ! hasFreeSlot()) return current;
        T* p = pending.exchange (nullptr, std::memory_order_acq_rel);
        if (p == nullptr) return current;
        previousOut = current;
        current = p;
        return current;
    }

    /** Audio thread: hand an object back for deletion off the audio thread. */
    void retire (T* obj) noexcept
    {
        if (obj == nullptr) return;
        for (auto& r : retired)
        {
            T* expected = nullptr;
            if (r.compare_exchange_strong (expected, obj, std::memory_order_acq_rel)) return;
        }
        // all slots full (cannot happen: acquire() checks for a free slot) – leak rather than free on audio thread
    }

    /** Non-realtime thread. */
    void collectGarbage()
    {
        for (auto& r : retired) delete r.exchange (nullptr, std::memory_order_acq_rel);
    }

    T* get() const noexcept { return current; }

private:
    bool hasFreeSlot() const noexcept
    {
        int free = 0;
        for (auto& r : retired) if (r.load (std::memory_order_acquire) == nullptr) ++free;
        return free >= 2;
    }

    std::atomic<T*> pending { nullptr };
    T* current = nullptr; // audio-thread owned
    std::array<std::atomic<T*>, kRetireSlots> retired {};
};
} // namespace namknobs
