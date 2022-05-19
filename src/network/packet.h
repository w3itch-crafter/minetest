#pragma once

#include <ostream>
#include <util/pointer.h>
#include <network/address.h>

namespace con {

#define MAX_UDP_PEERS 65535

/*
=== NOTES ===

A packet is sent through a channel to a peer with a basic header:
	Header (7 bytes):
	[0] u32 protocol_id
	[4] session_t sender_peer_id
	[6] u8 channel
sender_peer_id:
	Unique to each peer.
	value 0 (PEER_ID_INEXISTENT) is reserved for making new connections
	value 1 (PEER_ID_SERVER) is reserved for server
	these constants are defined in constants.h
channel:
	Channel numbers have no intrinsic meaning. Currently only 0, 1, 2 exist.
*/
#define BASE_HEADER_SIZE 7

/*
Packet types:

CONTROL: This is a packet used by the protocol.
- When this is processed, nothing is handed to the user.
	Header (2 byte):
	[0] u8 type
	[1] u8 controltype
controltype and data description:
	CONTROLTYPE_ACK
		[2] u16 seqnum
	CONTROLTYPE_SET_PEER_ID
		[2] session_t peer_id_new
	CONTROLTYPE_PING
	- There is no actual reply, but this can be sent in a reliable
	  packet to get a reply
	CONTROLTYPE_DISCO
*/
enum ControlType : u8 {
	CONTROLTYPE_ACK = 0,
	CONTROLTYPE_SET_PEER_ID = 1,
	CONTROLTYPE_PING = 2,
	CONTROLTYPE_DISCO = 3,
};

/*
ORIGINAL: This is a plain packet with no control and no error
checking at all.
- When this is processed, it is directly handed to the user.
	Header (1 byte):
	[0] u8 type
*/
//#define TYPE_ORIGINAL 1
#define ORIGINAL_HEADER_SIZE 1

/*
SPLIT: These are sequences of packets forming one bigger piece of
data.
- When processed and all the packet_nums 0...packet_count-1 are
  present (this should be buffered), the resulting data shall be
  directly handed to the user.
- If the data fails to come up in a reasonable time, the buffer shall
  be silently discarded.
- These can be sent as-is or atop of a RELIABLE packet stream.
	Header (7 bytes):
	[0] u8 type
	[1] u16 seqnum
	[3] u16 chunk_count
	[5] u16 chunk_num
*/
//#define TYPE_SPLIT 2

/*
RELIABLE: Delivery of all RELIABLE packets shall be forced by ACKs,
and they shall be delivered in the same order as sent. This is done
with a buffer in the receiving and transmitting end.
- When this is processed, the contents of each packet is recursively
  processed as packets.
	Header (3 bytes):
	[0] u8 type
	[1] u16 seqnum

*/
//#define TYPE_RELIABLE 3
#define RELIABLE_HEADER_SIZE 3

#define CHANNEL_COUNT 3

// TODO: Hide this entire in abstraction, move it into packet.cpp
enum PacketType : u8 {
        PACKET_TYPE_CONTROL = 0,
        PACKET_TYPE_ORIGINAL = 1,
        PACKET_TYPE_SPLIT = 2,
        PACKET_TYPE_RELIABLE = 3,
        PACKET_TYPE_MAX
};

// use IPv6 minimum allowed MTU as receive buffer size as this is
// theoretical reliable upper boundary of a udp packet for all IPv6 enabled
// infrastructure
#define PACKET_MAXSIZE 1500

enum ReceivedPacketType {
	RPT_INVALID = 0,
	RPT_ORIGINAL,
	RPT_ACK,
	RPT_SET_PEER_ID,
	RPT_PING,
	RPT_DISCO,
	RPT_SPLIT
};

struct ReceivedPacket;
using ReceivedPacketPtr = std::unique_ptr<ReceivedPacket>;

// This should be a POD. Don't add any constructors.
struct ReceivedPacket {
	u64 received_time_ms;
	Address source_address;
	u8 data[PACKET_MAXSIZE];
	size_t data_size;

	// Call this after filling in the above.
	// Throws an exception if the packet is malformed.
	// Values below are filled in by parse()
	void parse();

	// Generated in parse to identify this packet in logs
	char uuid[16 + 1];

	u32 protocol_id;
	u16 peer_id;
	u8 channelnum;

	ReceivedPacketType type;

	// Reliable header present
	bool is_reliable;
	struct {
		u16 seqnum;
		// The actual sequence number of this packet if the
		// sequence number was a full 64-bits. Computed later.
		u64 full_seqnum;
	} reliable;

	struct {
		u16 seqnum;
	} ack;

	struct {
		u16 new_peer_id;
	} set_peer_id;

	struct {
		u16 seqnum;
		u16 chunk_count;
		u16 chunk_num;
	} split;

	// The start of the contents not parsed.
	u8 *contents;
	size_t contents_size;

	// Make a new zero-initialized ReceivedPacket.
	static ReceivedPacketPtr make()
	{
		return ReceivedPacketPtr(new ReceivedPacket{});
	}

	// Print all packet metadata
	void dump(std::ostream &os) const;
};


// Prints ReceivedPacket[UUID]
std::ostream& operator<<(std::ostream &os, const ReceivedPacket &rpkt);
std::ostream& operator<<(std::ostream &os, const ReceivedPacketPtr &rpkt);

} // namespace
