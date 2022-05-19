/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include "irrlichttypes.h"
#include "peerhandler.h"
#include "socket.h"
#include "constants.h"
#include "util/pointer.h"
#include "util/container.h"
#include "util/thread.h"
#include "util/numeric.h"
#include "networkprotocol.h"
#include "network/reliable.h"
#include "network/split.h"
#include <iostream>
#include <vector>
#include <map>

/* See packet.h for a description of the packet protocol */

class NetworkPacket;

namespace con
{

class ConnectionReceiveThread;
class ConnectionSendThread;

typedef enum MTProtocols {
	MTP_PRIMARY,
	MTP_UDP,
	MTP_MINETEST_RELIABLE_UDP
} MTProtocols;

inline bool seqnum_in_window(u16 seqnum, u16 next,u16 window_size)
{
	u16 window_start = next;
	u16 window_end   = ( next + window_size ) % (SEQNUM_MAX+1);

	if (window_start < window_end) {
		return ((seqnum >= window_start) && (seqnum < window_end));
	}


	return ((seqnum < window_end) || (seqnum >= window_start));
}

static inline float CALC_DTIME(u64 lasttime, u64 curtime)
{
	float value = ( curtime - lasttime) / 1000.0;
	return MYMAX(MYMIN(value,0.1),0.0);
}

/*
	Struct for all kinds of packets. Includes following data:
		BASE_HEADER
		u8[] packet data (usually copied from SharedBuffer<u8>)
*/
struct BufferedPacket {
	BufferedPacket(u32 a_size)
	{
		m_data.resize(a_size);
		data = &m_data[0];
	}

	DISABLE_CLASS_COPY(BufferedPacket)

	u16 getSeqnum() const;

	inline size_t size() const { return m_data.size(); }

	u8 *data; // Direct memory access
	float time = 0.0f; // Seconds from buffering the packet or re-sending
	float totaltime = 0.0f; // Seconds from buffering the packet
	u64 absolute_send_time = -1;
	Address address; // Sender or destination
	unsigned int resend_count = 0;

private:
	std::vector<u8> m_data; // Data of the packet, including headers
};

typedef std::shared_ptr<BufferedPacket> BufferedPacketPtr;


// This adds the base headers to the data and makes a packet out of it
BufferedPacketPtr makePacket(Address &address, const SharedBuffer<u8> &data,
		session_t sender_peer_id, u8 channel);

// Depending on size, make a TYPE_ORIGINAL or TYPE_SPLIT packet
// Increments split_seqnum if a split packet is made
void makeAutoSplitPacket(const SharedBuffer<u8> &data, u32 chunksize_max,
		u16 &split_seqnum, std::list<SharedBuffer<u8>> *list);

// Add the TYPE_RELIABLE header to the data
SharedBuffer<u8> makeReliablePacket(const SharedBuffer<u8> &data, u16 seqnum);

/*
	A buffer which stores reliable packets and sorts them internally
	for fast access to the smallest one.
*/

typedef std::list<BufferedPacketPtr>::iterator RPBSearchResult;

class ReliablePacketBuffer
{
public:
	ReliablePacketBuffer() = default;

	bool getFirstSeqnum(u16& result);

	BufferedPacketPtr popFirst();
	BufferedPacketPtr popSeqnum(u16 seqnum);
	void insert(BufferedPacketPtr &p_ptr, u16 next_expected);

	void incrementTimeouts(float dtime);
	std::list<ConstSharedPtr<BufferedPacket>> getTimedOuts(float timeout, u32 max_packets);

	void print();
	bool empty();
	u32 size();


private:
	RPBSearchResult findPacketNoLock(u16 seqnum);
	std::list<BufferedPacketPtr> m_list;

	u16 m_oldest_non_answered_ack;

	std::mutex m_list_mutex;
};

enum ConnectionCommandType{
	CONNCMD_NONE,
	CONNCMD_SERVE,
	CONNCMD_CONNECT,
	CONNCMD_DISCONNECT,
	CONNCMD_DISCONNECT_PEER,
	CONNCMD_SEND,
	CONNCMD_SEND_TO_ALL,
	CONCMD_ACK,
	CONCMD_CREATE_PEER
};

struct ConnectionCommand;
typedef std::shared_ptr<ConnectionCommand> ConnectionCommandPtr;

// This is very similar to ConnectionEvent
struct ConnectionCommand
{
	const ConnectionCommandType type;
	Address address;
	session_t peer_id = PEER_ID_INEXISTENT;
	u8 channelnum = 0;
	Buffer<u8> data;
	bool reliable = false;
	bool raw = false;

	DISABLE_CLASS_COPY(ConnectionCommand);

	static ConnectionCommandPtr serve(Address address);
	static ConnectionCommandPtr connect(Address address);
	static ConnectionCommandPtr disconnect();
	static ConnectionCommandPtr disconnect_peer(session_t peer_id);
	static ConnectionCommandPtr send(session_t peer_id, u8 channelnum, NetworkPacket *pkt, bool reliable);
	static ConnectionCommandPtr ack(session_t peer_id, u8 channelnum, const Buffer<u8> &data);
	static ConnectionCommandPtr createPeer(session_t peer_id, const Buffer<u8> &data);

private:
	ConnectionCommand(ConnectionCommandType type_) :
		type(type_) {}

	static ConnectionCommandPtr create(ConnectionCommandType type);
};

class UDPPeer;

class Channel
{

public:
	u16 getOutgoingSequenceNumber(bool& successfull);
	u16 readOutgoingSequenceNumber();
	bool putBackSequenceNumber(u16);

	u16 readNextSplitSeqNum();
	void setNextSplitSeqNum(u16 seqnum);

	// TODO: Make all of this private
	UDPPeer *m_peer;

	// This is for buffering the incoming packets that are coming in
	// the wrong order
	ReliableReceivedPacketBuffer incoming_reliables;

	// This is for buffering the sent packets so that the sender can
	// re-send them if no ACK is received
	ReliablePacketBuffer outgoing_reliables_sent;

	//queued reliable packets
	std::queue<BufferedPacketPtr> queued_reliables;

	//queue commands prior splitting to packets
	std::deque<ConnectionCommandPtr> queued_commands;

	IncomingSplitBuffer incoming_splits;

	Channel() = delete;
	Channel(UDPPeer *peer);
	~Channel() = default;

	void UpdatePacketLossCounter(unsigned int count);
	void UpdatePacketTooLateCounter();
	void UpdateBytesSent(unsigned int bytes,unsigned int packages=1);
	void UpdateBytesLost(unsigned int bytes);
	void UpdateBytesReceived(unsigned int bytes);

	void UpdateTimers(float dtime);

	float getCurrentDownloadRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return cur_kbps; };
	float getMaxDownloadRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return max_kbps; };

	float getCurrentLossRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return cur_kbps_lost; };
	float getMaxLossRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return max_kbps_lost; };

	float getCurrentIncomingRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return cur_incoming_kbps; };
	float getMaxIncomingRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return max_incoming_kbps; };

	float getAvgDownloadRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return avg_kbps; };
	float getAvgLossRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return avg_kbps_lost; };
	float getAvgIncomingRateKB()
		{ MutexAutoLock lock(m_internal_mutex); return avg_incoming_kbps; };

	u16 getWindowSize() const { return m_window_size; };

	void setWindowSize(long size)
	{
		m_window_size = (u16)rangelim(size, MIN_RELIABLE_WINDOW_SIZE, MAX_RELIABLE_WINDOW_SIZE);
	}

	void setPeer(UDPPeer *peer) {
		m_peer = peer;
	}

private:
	std::mutex m_internal_mutex;
	u16 m_window_size = MIN_RELIABLE_WINDOW_SIZE;

	u16 next_outgoing_seqnum = SEQNUM_INITIAL;
	u16 next_outgoing_split_seqnum = SEQNUM_INITIAL;

	unsigned int current_packet_loss = 0;
	unsigned int current_packet_too_late = 0;
	unsigned int current_packet_successful = 0;
	float packet_loss_counter = 0.0f;

	unsigned int current_bytes_transfered = 0;
	unsigned int current_bytes_received = 0;
	unsigned int current_bytes_lost = 0;
	float max_kbps = 0.0f;
	float cur_kbps = 0.0f;
	float avg_kbps = 0.0f;
	float max_incoming_kbps = 0.0f;
	float cur_incoming_kbps = 0.0f;
	float avg_incoming_kbps = 0.0f;
	float max_kbps_lost = 0.0f;
	float cur_kbps_lost = 0.0f;
	float avg_kbps_lost = 0.0f;
	float bpm_counter = 0.0f;

	unsigned int rate_samples = 0;
};

class Peer;

class PeerHelper
{
public:
	PeerHelper() = default;
	PeerHelper(Peer* peer);
	~PeerHelper();

	PeerHelper&   operator=(Peer* peer);
	Peer*         operator->() const;
	bool          operator!();
	Peer*         operator&() const;
	bool          operator!=(void* ptr);

private:
	Peer *m_peer = nullptr;
};

class Connection;

typedef enum {
	CUR_DL_RATE,
	AVG_DL_RATE,
	CUR_INC_RATE,
	AVG_INC_RATE,
	CUR_LOSS_RATE,
	AVG_LOSS_RATE,
} rate_stat_type;

class Peer {
	public:
		friend class PeerHelper;

		Peer(Address address_,session_t id_,Connection* connection) :
			id(id_),
			m_connection(connection),
			address(address_),
			m_last_timeout_check(porting::getTimeMs())
		{
		};

		virtual ~Peer() {
			MutexAutoLock usage_lock(m_exclusive_access_mutex);
			FATAL_ERROR_IF(m_usage != 0, "Reference counting failure");
		};

		// Unique id of the peer
		const session_t id;

		void Drop();

		virtual void PutReliableSendCommand(ConnectionCommandPtr &c,
						unsigned int max_packet_size) {};

		virtual bool getAddress(MTProtocols type, Address& toset) = 0;

		bool isPendingDeletion()
		{ return m_pending_deletion; };

		void ResetTimeout()
			{MutexAutoLock lock(m_exclusive_access_mutex); m_timeout_counter = 0.0; };

		bool isTimedOut(float timeout);

		unsigned int m_increment_packets_remaining = 0;

		virtual u16 getNextSplitSequenceNumber(u8 channel) { return 0; };
		virtual void setNextSplitSequenceNumber(u8 channel, u16 seqnum) {};

		virtual bool Ping(float dtime, SharedBuffer<u8>& data) { return false; };

		virtual float getStat(rtt_stat_type type) const {
			switch (type) {
				case MIN_RTT:
					return m_rtt.min_rtt;
				case MAX_RTT:
					return m_rtt.max_rtt;
				case AVG_RTT:
					return m_rtt.avg_rtt;
				case MIN_JITTER:
					return m_rtt.jitter_min;
				case MAX_JITTER:
					return m_rtt.jitter_max;
				case AVG_JITTER:
					return m_rtt.jitter_avg;
			}
			return -1;
		}

		Connection* getConnection()
		{
			return m_connection;
		}

		// May only be called on the ConnectionReceiveThread
		virtual void ProcessPacket(ReceivedPacketPtr&& rpkt, bool fromReliableBuffer = false) = 0;

	protected:
		virtual void reportRTT(float rtt) {};

		void RTTStatistics(float rtt,
							const std::string &profiler_id = "",
							unsigned int num_samples = 1000);

		bool IncUseCount();
		void DecUseCount();

		mutable std::mutex m_exclusive_access_mutex;

		std::atomic<bool> m_pending_deletion{false};

		Connection* m_connection;

		// Address of the peer
		Address address;

		// Ping timer
		float m_ping_timer = 0.0f;
	private:

		struct rttstats {
			float jitter_min = FLT_MAX;
			float jitter_max = 0.0f;
			float jitter_avg = -1.0f;
			float min_rtt = FLT_MAX;
			float max_rtt = 0.0f;
			float avg_rtt = -1.0f;

			rttstats() = default;
		};

		rttstats m_rtt;
		float m_last_rtt = -1.0f;

		// current usage count
		unsigned int m_usage = 0;

		// Seconds from last receive
		float m_timeout_counter = 0.0f;

		u64 m_last_timeout_check;
};

class UDPPeer : public Peer
{
public:

	friend class PeerHelper;
	friend class ConnectionReceiveThread;
	friend class ConnectionSendThread;
	friend class Connection;

	UDPPeer(u16 a_id, Address a_address, Connection* connection);
	virtual ~UDPPeer() = default;

	void PutReliableSendCommand(ConnectionCommandPtr &c,
							unsigned int max_packet_size);

	bool getAddress(MTProtocols type, Address& toset);

	u16 getNextSplitSequenceNumber(u8 channel);
	void setNextSplitSequenceNumber(u8 channel, u16 seqnum);

	// May only be called on the ConnectionReceiveThread
	virtual void ProcessPacket(ReceivedPacketPtr&& rpkt, bool fromReliableBuffer = false);
protected:


	/*
		Calculates avg_rtt and resend_timeout.
		rtt=-1 only recalculates resend_timeout
	*/
	void reportRTT(float rtt);

	void RunCommandQueues(
					unsigned int max_packet_size,
					unsigned int maxcommands,
					unsigned int maxtransfer);

	float getResendTimeout()
		{ return resend_timeout; }

	void setResendTimeout(float timeout)
		{ resend_timeout = timeout; }
	bool Ping(float dtime,SharedBuffer<u8>& data);

	Channel channels[CHANNEL_COUNT];
	bool m_pending_disconnect = false;
private:
	// This is changed dynamically
	std::atomic<float> resend_timeout{0.5};

	bool processReliableSendCommand(
					ConnectionCommandPtr &c_ptr,
					unsigned int max_packet_size);
};

/*
	Connection
*/

enum ConnectionEventType {
	CONNEVENT_NONE,
	CONNEVENT_DATA_RECEIVED,
	CONNEVENT_PEER_ADDED,
	CONNEVENT_PEER_REMOVED,
	CONNEVENT_BIND_FAILED,
};

struct ConnectionEvent;
typedef std::shared_ptr<ConnectionEvent> ConnectionEventPtr;

// This is very similar to ConnectionCommand
struct ConnectionEvent
{
	const ConnectionEventType type;
	session_t peer_id = 0;
	Buffer<u8> data;
	bool timeout = false;
	Address address;

	// We don't want to copy "data"
	DISABLE_CLASS_COPY(ConnectionEvent);

	static ConnectionEventPtr create(ConnectionEventType type);
	static ConnectionEventPtr dataReceived(session_t peer_id, Buffer<u8>&& data);
	static ConnectionEventPtr peerAdded(session_t peer_id, Address address);
	static ConnectionEventPtr peerRemoved(session_t peer_id, bool is_timeout, Address address);
	static ConnectionEventPtr bindFailed();

	const char *describe() const;

private:
	ConnectionEvent(ConnectionEventType type_) :
		type(type_) {}
};

class PeerHandler;

class Connection
{
public:
	friend class ConnectionSendThread;
	friend class ConnectionReceiveThread;
	friend class UDPPeer;
	friend class Channel;

	Connection(u32 max_packet_size, float timeout, bool ipv6,
			PeerHandler *peerhandler);
	~Connection();

	/* Interface */
	ConnectionEventPtr waitEvent(u32 timeout_ms);

	void putCommand(ConnectionCommandPtr c);

	void SetTimeoutMs(u32 timeout) { m_bc_receive_timeout = timeout; }
	void Serve(Address bind_addr);
	void Connect(Address address);
	bool Connected();
	void Disconnect();
	void Receive(NetworkPacket* pkt);
	bool TryReceive(NetworkPacket *pkt);
	void Send(session_t peer_id, u8 channelnum, NetworkPacket *pkt, bool reliable);
	session_t GetPeerID() const { return m_peer_id; }
	Address GetPeerAddress(session_t peer_id);
	float getPeerStat(session_t peer_id, rtt_stat_type type);
	float getLocalStat(rate_stat_type type);
	const std::string getDesc();
	void DisconnectPeer(session_t peer_id);

protected:
	PeerHelper getPeerNoEx(session_t peer_id);
	u16   lookupPeer(Address& sender);

	u16 createPeer(Address& sender, MTProtocols protocol, int fd);
	UDPPeer*  createServerPeer(Address& sender);
	bool deletePeer(session_t peer_id, bool timeout);

	void SetPeerID(session_t id) { m_peer_id = id; }

	std::vector<session_t> getPeerIDs()
	{
		MutexAutoLock peerlock(m_peers_mutex);
		return m_peer_ids;
	}

	UDPSocket m_udpSocket;
	// Command queue: user -> SendThread
	MutexedQueue<ConnectionCommandPtr> m_command_queue;

	bool Receive(NetworkPacket *pkt, u32 timeout);

	void putEvent(ConnectionEventPtr e);

	void TriggerSend();

	bool ConnectedToServer()
	{
		return getPeerNoEx(PEER_ID_SERVER) != nullptr;
	}

	void sendAck(const ReceivedPacketPtr &rpkt);

	TimeoutQueue& getTimeoutQueue() {
		return m_timeout_queue;
	}

private:
	// Event queue: ReceiveThread -> user
	MutexedQueue<ConnectionEventPtr> m_event_queue;

	session_t m_peer_id = 0;

	std::map<session_t, Peer *> m_peers;
	std::vector<session_t> m_peer_ids;
	std::mutex m_peers_mutex;

	std::unique_ptr<ConnectionSendThread> m_sendThread;
	std::unique_ptr<ConnectionReceiveThread> m_receiveThread;

	// For now this is only for ConnectionReceiveThread.
	TimeoutQueue m_timeout_queue;

	mutable std::mutex m_info_mutex;

	// Backwards compatibility
	PeerHandler *m_bc_peerhandler;
	u32 m_bc_receive_timeout = 0;

	bool m_shutting_down = false;

	session_t m_next_remote_peer_id = 2;
};

} // namespace
