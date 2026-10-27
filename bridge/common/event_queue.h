// The event queue of docs/abi.md, shared by the real bridge and the mock so both
// have exactly the same ordering, bound and overflow behaviour. Standard C++ only.
//
//  * Events get strictly increasing `seq` numbers, assigned under the queue's lock
//    so the delivery order is the seq order.
//  * The queue is bounded. On overflow the oldest events are dropped and replaced
//    by ONE core.overflow marker at the front standing for the lost range
//    (`seq` = first dropped, `last_seq`, `dropped`); the next event has
//    seq = last_seq + 1. Nothing disappears silently.
//  * close() ends the stream: pop() returns false at once once the queue is empty.
#ifndef TINOXKDE_EVENT_QUEUE_H
#define TINOXKDE_EVENT_QUEUE_H

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace tkde
{
inline int64_t monotonicNanos()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

class EventQueue
{
public:
    static constexpr size_t kDefaultCapacity = 65536;

    explicit EventQueue(size_t capacity = kDefaultCapacity)
        : m_capacity(capacity < 2 ? 2 : capacity)
    {
    }

    /// `make(seq)` returns the event's JSON for the assigned sequence number.
    /// `forcedSeq > 0` (replay, injected events with a seq) uses that number instead.
    template<class Make>
    void push(Make &&make, int64_t forcedSeq = 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) {
            return;
        }
        int64_t seq;
        if (forcedSeq > 0) {
            seq = forcedSeq;
            m_nextSeq = std::max(m_nextSeq, seq + 1);
        } else {
            seq = m_nextSeq++;
        }
        std::string json = make(seq);
        if (m_items.size() >= m_capacity) {
            if (!m_items.front().overflow) {
                Item marker;
                marker.overflow = true;
                marker.seq = marker.lastSeq = m_items.front().seq;
                marker.dropped = 1;
                m_items.front() = std::move(marker);
                ++m_droppedTotal;
            }
            if (m_items.size() > 1) {
                const int64_t droppedSeq = m_items[1].seq;
                m_items.erase(m_items.begin() + 1); // invalidates references: re-read the front
                m_items.front().dropped += 1;
                m_items.front().lastSeq = droppedSeq;
                ++m_droppedTotal;
            }
        }
        Item item;
        item.seq = seq;
        item.json = std::move(json);
        m_items.push_back(std::move(item));
        m_cv.notify_all();
    }

    /// Waits up to `timeoutMs` (< 0: until an event arrives or the queue is closed).
    bool pop(int64_t timeoutMs, std::string &out)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto ready = [&] { return !m_items.empty() || m_closed; };
        if (timeoutMs < 0) {
            m_cv.wait(lock, ready);
        } else {
            m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
        }
        if (m_items.empty()) {
            return false;
        }
        Item item = std::move(m_items.front());
        m_items.pop_front();
        out = item.overflow ? overflowJson(item) : std::move(item.json);
        return true;
    }

    /// No further events are accepted; waiting pops return once the queue is drained.
    void close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_cv.notify_all();
    }

    bool closed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    int64_t dropped() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_droppedTotal;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

private:
    struct Item {
        int64_t seq = 0;
        std::string json;
        bool overflow = false;
        int64_t dropped = 0;
        int64_t lastSeq = 0;
    };

    static std::string overflowJson(const Item &i)
    {
        return "{\"type\":\"core.overflow\",\"seq\":" + std::to_string(i.seq) + ",\"t_ns\":" + std::to_string(monotonicNanos())
            + ",\"dropped\":" + std::to_string(i.dropped) + ",\"last_seq\":" + std::to_string(i.lastSeq) + "}";
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Item> m_items;
    size_t m_capacity;
    int64_t m_nextSeq = 1;
    int64_t m_droppedTotal = 0;
    bool m_closed = false;
};
} // namespace tkde

#endif // TINOXKDE_EVENT_QUEUE_H
