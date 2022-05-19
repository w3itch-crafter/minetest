#include <ostream>
#include "network/binreader.h"
#include "network/networkprotocol.h"
#include "network/packet.h"
#include "util/uuid.h"

namespace con {

void ReceivedPacket::parse() {
	// BinReader will throw if data runs out early.
	BinReader r(data, data_size);

	// Generate a UUID for tracking this packet in the logs
	genHexUUID(uuid, sizeof(uuid));

	r >> protocol_id;
	if (protocol_id != PROTOCOL_ID)
		throw ParseError("protocol_id=%08x != %08x", protocol_id, PROTOCOL_ID);
	r >> peer_id;
	r >> channelnum;
	if (channelnum >= CHANNEL_COUNT)
		throw ParseError("Invalid channel %u", channelnum);

	u8 raw_type;
	r >> raw_type;
        if (raw_type >= PACKET_TYPE_MAX)
                throw ParseError("Got invalid raw type: %u", raw_type);

	if (raw_type == PACKET_TYPE_RELIABLE) {
		is_reliable = true;
		r >> reliable.seqnum;
		// After the reliable header, comes another packet of a different type.
		r >> raw_type;
	}

	type = RPT_INVALID;
	bool cannot_be_empty = false;
	switch (raw_type) {
	case PACKET_TYPE_CONTROL:
	{
		u8 control_type = r.readU8();
		switch (control_type) {
		case CONTROLTYPE_ACK:
		{
			type = RPT_ACK;
			r >> ack.seqnum;
			break;
		}
		case CONTROLTYPE_SET_PEER_ID:
		{
			type = RPT_SET_PEER_ID;
			r >> set_peer_id.new_peer_id;
			break;
		}
		case CONTROLTYPE_PING:
		{
			type = RPT_PING;
			break;
		}
		case CONTROLTYPE_DISCO:
		{
			type = RPT_DISCO;
			break;
		}
		default:
			throw ParseError("Invalid control_type = %u", control_type);
		}
		break;
	}
	case PACKET_TYPE_ORIGINAL:
	{
		type = RPT_ORIGINAL;
		cannot_be_empty = true;
		break;
	}
	case PACKET_TYPE_SPLIT:
	{
		type = RPT_SPLIT;
		r >> split.seqnum;
		r >> split.chunk_count;
		r >> split.chunk_num;
		if (split.chunk_num >= split.chunk_count)
			throw ParseError("chunk_num >= chunksize: %u >= %u", split.chunk_num, split.chunk_count);
		cannot_be_empty = true;
		break;
	}
	case PACKET_TYPE_RELIABLE:
	{
		throw ParseError("Found nested reliable packets");
	}
	default:
		throw ParseError("Invalid packet type: %u", raw_type);
	}
	assert(type != RPT_INVALID);
	contents = data + r.getPosition();
	contents_size = data_size - r.getPosition();
	if (cannot_be_empty && contents_size == 0)
		throw ParseError("Empty contents");
}

static const char* getTypeLabel(ReceivedPacketType type) {
	switch (type) {
	case RPT_INVALID:
		return "RPT_INVALID";
	case RPT_ORIGINAL:
		return "RPT_ORIGINAL";
	case RPT_ACK:
		return "RPT_ACK";
	case RPT_SET_PEER_ID:
		return "RPT_SET_PEER_ID";
	case RPT_PING:
		return "RPT_PING";
	case RPT_DISCO:
		return "RPT_DISCO";
	case RPT_SPLIT:
		return "RPT_SPLIT";
	}
	return "RPT_UNKNOWN";
}

void ReceivedPacket::dump(std::ostream &os) const {
	os << "ReceivedPacket";
	os << "[" << uuid << "]:";
	os << "protocol_id" << protocol_id;
	os << ",peer_id=" << peer_id;
	os << ",channel=" << channelnum;
	os << ",type=" << getTypeLabel(type);
	switch (type) {
	case RPT_ACK:
		os << ",ack_seqnum=" << ack.seqnum;
		break;
	case RPT_SET_PEER_ID:
		os << ",new_peer_id=" << set_peer_id.new_peer_id;
		break;
	case RPT_SPLIT:
		os << ",split_seqnum=" << split.seqnum;
		os << ",split_chunk_count=" << split.chunk_count;
		os << ",split_chunk_num=" << split.chunk_num;
		break;
	default:
		break;
	}
	os << ",contents_size=" << contents_size;
	os << std::endl;
}

std::ostream& operator<<(std::ostream &os, const ReceivedPacket &rpkt) {
	os << "ReceivedPacket[" << rpkt.uuid << "]";
	return os;
}

std::ostream& operator<<(std::ostream &os, const ReceivedPacketPtr &rpkt) {
	os << "ReceivedPacket[" << rpkt->uuid << "]";
	return os;
}

} // namespace con
