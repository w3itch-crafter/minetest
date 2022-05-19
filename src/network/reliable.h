#pragma once

#include <network/packet.h>
#include <queue>
#include <functional>

namespace con {

/* maximum window size to use, 0xFFFF is theoretical maximum. don't think about
 * touching it, the less you're away from it the more likely data corruption
 * will occur
 */
#define MAX_RELIABLE_WINDOW_SIZE 0x8000
/* starting value for window size */
#define START_RELIABLE_WINDOW_SIZE 0x400
/* minimum value for window size */
#define MIN_RELIABLE_WINDOW_SIZE 0x40

#define SEQNUM_INITIAL 65500
#define SEQNUM_MAX 65535

struct OrderByFullSeqNum {
	bool operator()(ReceivedPacket *a, ReceivedPacket *b) const {
		return a->reliable.full_seqnum > b->reliable.full_seqnum;
	}
};

/*
 * Implements the receiving side of reliable packet delivery.
 *
 * Takes two callbacks, sendAck and processPacket.
 * sendAck() will be called when an ACK should be sent.
 * processPacket() will be called when a reliable packet is ready to be processed.
 *
 * processPacket() should return false if during processing of the packet, the
 * connection ended, and further processing should be halted.
 *
 * When a reliable packet is received, use insert() to add it to the buffer.
 * The callbacks may be called multiple times during insert.
 *
 */
class ReliableReceivedPacketBuffer
{
public:

	using SendAckFunc = std::function<void(const ReceivedPacketPtr &)>;
	using ProcessPacketFunc = std::function<bool(ReceivedPacketPtr &&)>;

	ReliableReceivedPacketBuffer() = delete;

	ReliableReceivedPacketBuffer(SendAckFunc sendAck, ProcessPacketFunc processPacket) :
		sendAck(sendAck),
		processPacket(processPacket)
	{ }

	~ReliableReceivedPacketBuffer();

	// May only be called from ConnectionReceiveThread.
	void insert(ReceivedPacketPtr&& rpkt);

private:
	// Process all packets that are ready
	void flush();

	u64 next_incoming_seqnum = SEQNUM_INITIAL;
	std::priority_queue<
		ReceivedPacket*,
		std::vector<ReceivedPacket*>,
		OrderByFullSeqNum> m_queue;
	SendAckFunc sendAck;
	ProcessPacketFunc processPacket;
};

} // namespace con
