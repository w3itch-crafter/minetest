#include "network/reliable.h"

namespace con {

inline u64 compute_full_seqnum(u64 base, u16 seqnum) {
	u16 base_mod = base & 0xFFFF;
	u16 forward_diff = seqnum - base_mod;
	u16 backward_diff = base_mod - seqnum;
	if (forward_diff <= 32768 || backward_diff > base) {
		return base + forward_diff;
	}
	return base - backward_diff;
}

/*
	ReliableReceivedPacketBuffer
*/

ReliableReceivedPacketBuffer::~ReliableReceivedPacketBuffer() {
	// The priority queue contains bare pointers instead of unique_ptr
	// because it doesn't work with move semantics. Delete manually.
	while (!m_queue.empty()) {
		ReceivedPacket* rpkt = m_queue.top();
		delete rpkt;
		m_queue.pop();
	}
}

// May only be called from ConnectionReceiveThread.
void ReliableReceivedPacketBuffer::insert(ReceivedPacketPtr &&rpkt) {
	assert(rpkt->is_reliable);
	u64 full_seqnum = compute_full_seqnum(next_incoming_seqnum, rpkt->reliable.seqnum);
	rpkt->reliable.full_seqnum = full_seqnum;

	// TODO: Will this ever actually be true?
	if (full_seqnum > next_incoming_seqnum + MAX_RELIABLE_WINDOW_SIZE) {
		// Too far in the future, discard without sending an ack.
		// If this is a valid packet, it'll be retransmitted.
		derr_con << rpkt << "Reliable packet too far in the future. Ignoring." << std::endl;
		return;
	}

	// Send ACK
	sendAck(rpkt);

	if (full_seqnum < next_incoming_seqnum) {
		// Old packet. Don't process again. We sent another ACK above.
		return;
	}

	if (next_incoming_seqnum == rpkt->reliable.full_seqnum) {
		// Process it right away
		next_incoming_seqnum++;
		if (!processPacket(std::move(rpkt))) {
			// Connection closed
			return;
		}
		// Process any other packets that are now ready
		flush();
		return;
	}
	m_queue.push(rpkt.release());
}

void ReliableReceivedPacketBuffer::flush() {
	while (!m_queue.empty() &&
		m_queue.top()->reliable.full_seqnum <= next_incoming_seqnum)
	{
		ReceivedPacketPtr rpkt(m_queue.top());
		m_queue.pop();
		if (rpkt->reliable.full_seqnum < next_incoming_seqnum) {
			dout_con << rpkt << "Discarding duplicate packet" << std::endl;
			continue;
		}
		assert(rpkt->reliable.full_seqnum == next_incoming_seqnum);
		next_incoming_seqnum++;
		if (!processPacket(std::move(rpkt))) {
			// Connection closed
			return;
		}
	}
}

} // namespace con
