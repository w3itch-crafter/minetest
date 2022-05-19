#include <ostream>
#include <functional>
#include "util/binheap.h"
#include "porting.h"

//
// The main exports of this file are TimeoutQueue and TimeoutHandle
//

using TimeoutCallback = std::function<void()>;

struct TimeoutRecord {
        u64 expiration;
        TimeoutCallback callback;

	bool operator<(const TimeoutRecord &other) const {
		return expiration < other.expiration;
	}
};

using TimeoutHeap = BinHeap<TimeoutRecord>;
using TimeoutHeapNode = TimeoutHeap::node_type;

class TimeoutQueue {
public:
	DISABLE_CLASS_COPY(TimeoutQueue);
	TimeoutQueue() = default;

	bool empty() const {
		return m_heap.empty();
	}

	// Milliseconds until the next timeout expires.
	// Returns 0 if there are already expired timeouts.
	// Only call this after checking that the queue is not empty.
	u64 nextTimeoutMs()
	{
		sanity_check(!m_heap.empty());
		u64 now = porting::getTimeMs();
		u64 expiration = m_heap.top()->value.expiration;
		if (expiration <= now)
			return 0;
		return expiration - now;
	}

	// Process all timeouts, calling the callbacks.
	void processTimeouts()
	{
		if (m_heap.empty())
			return;
		u64 now = porting::getTimeMs();
		while (!m_heap.empty() && m_heap.top()->value.expiration < now) {
			auto node = m_heap.top();
			TimeoutCallback callback = std::move(node->value.callback);
			m_heap.remove(node);
			callback();
		}
	}

private:
	friend class TimeoutHandle;
	TimeoutHeap m_heap;
};

/*
 * A handle which tracks an active (or inactive) pending callback on a timeout queue.
 * If the handle is destroyed or cleared, it is guaranteed the callback won't be called
 * in the future.
 */
class TimeoutHandle {
public:
	TimeoutHandle() = delete;

	TimeoutHandle(TimeoutQueue &tq) : tq(tq) { }

	~TimeoutHandle() {
		clearTimeout();
	}

	// Timeout is active in the queue
	bool isActive() const {
		return node.inHeap();
	}

	// Cancel the timeout
	void clearTimeout() {
		if (node.inHeap()) {
			tq.m_heap.remove(&node);
			node.value.callback = nullptr;
		}
	}

	// Set or update this timeout. Any previous timeouts are cancelled.
	void setTimeout(u64 delayMs, TimeoutCallback &&callback) {
		sanity_check(callback);
		clearTimeout();
		node.value.expiration = porting::getTimeMs() + delayMs;
		node.value.callback = std::move(callback);
		tq.m_heap.insert(&node);
	}

private:
	TimeoutQueue &tq;
	TimeoutHeapNode node;
};
