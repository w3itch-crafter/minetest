#pragma once

#include "util/pointer.h"
#include "util/timeout_queue.h"
#include "network/packet.h"

// How long before a non-reliable split packet seqnum without activity is discarded
#define SPLIT_TIMEOUT_MS 30

namespace con {

struct IncomingSplitPacket;

using DataReceivedCallback = std::function<void(Buffer<u8> &&)>;

// IncomingSplitBuffer
//
// A buffer for reconstructing split packets. When a full packet is reconstructed,
// invokes the data_received callback.
//
// Not thread-safe. Should only be used from ConnectionReceiveThread
//
class IncomingSplitBuffer
{
public:
	IncomingSplitBuffer() = delete;
	DISABLE_CLASS_COPY(IncomingSplitBuffer);

	IncomingSplitBuffer(TimeoutQueue &timeout_queue, DataReceivedCallback && data_received) :
		m_timeout_queue(timeout_queue),
		m_data_received(std::move(data_received))
	{ }

	// Insert a split packet into the buffer, possibly calling the DataReceivedCallback if
	// a split packet has been fully reconstructed.
	void insert(ReceivedPacketPtr&& rpkt);

protected:
	friend struct IncomingSplitPacket;
	void handleTimeout(IncomingSplitPacket *sp);

private:
	TimeoutQueue &m_timeout_queue;
	DataReceivedCallback m_data_received;

	// Key is seqnum
	std::unordered_map<u16, IncomingSplitPacket*> m_buf;
};

} // namespace con
