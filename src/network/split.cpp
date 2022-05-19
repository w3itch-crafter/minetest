#include "network/split.h"

namespace con {

/*
	IncomingSplitPacket
*/

struct IncomingSplitPacket
{
	IncomingSplitPacket() = delete;

	IncomingSplitPacket(
			IncomingSplitBuffer *parent,
			u16 seqnum,
			u32 chunk_count,
			bool reliable,
			TimeoutQueue &timeout_queue) :
		m_parent(parent),
		m_seqnum(seqnum),
		m_chunk_count(chunk_count),
		m_reliable(reliable),
		m_timeout_handle(timeout_queue)
	{
		resetTimeout();
	}

	void resetTimeout()
	{
		if (!m_reliable) {
			m_timeout_handle.setTimeout(SPLIT_TIMEOUT_MS, [this]() {
				m_parent->handleTimeout(this);
			});
		}
	}

	bool allReceived() const
	{
		return m_chunks.size() == m_chunk_count;
	}

	void insert(ReceivedPacketPtr&& rpkt)
	{
		auto chunk_num = rpkt->split.chunk_num;
		auto chunk_count = rpkt->split.chunk_count;
		sanity_check(chunk_num < m_chunk_count);
		if (m_chunk_count != chunk_count) {
			errorstream << rpkt << " IncomingSplitPacket::insert(): m_chunk_count="
					<< m_chunk_count << " != chunk_count=" << chunk_count
					<< std::endl;
			return;
		}
		if (m_reliable != rpkt->is_reliable)
			derr_con << rpkt << " Connection: WARNING: m_reliable=" << m_reliable
					<< " != rpkt->is_reliable=" << rpkt->is_reliable
					<< std::endl;

		// If chunk already exists, ignore it.
		// Sometimes two identical packets may arrive when there is network
		// lag and the server re-sends stuff.
		if (m_chunks.find(chunk_num) != m_chunks.end())
			return;

		// Set chunk data in buffer
		m_chunks.emplace(chunk_num, std::move(rpkt));
		resetTimeout();
	}

	Buffer<u8> reassemble() {
		sanity_check(allReceived());

		// Calculate total size
		size_t totalsize = 0;
		for (const auto &chunk : m_chunks)
			totalsize += chunk.second->contents_size;

		// Copy chunks to data buffer
		Buffer<u8> fulldata(totalsize);
		size_t offset = 0;
		for (const auto &chunk : m_chunks) {
			const auto &rpkt = chunk.second;
			memcpy(&fulldata[offset], rpkt->contents, rpkt->contents_size);
			offset += rpkt->contents_size;
		}
		sanity_check(offset == totalsize);
		return fulldata;
	}

	IncomingSplitBuffer *m_parent;

	u16 m_seqnum;
	// Key is chunk number
	std::map<u16, ReceivedPacketPtr> m_chunks;
	size_t m_chunk_count;
	bool m_reliable; // If true, isn't deleted on timeout
	TimeoutHandle m_timeout_handle;
};

/*
	IncomingSplitBuffer
*/

void IncomingSplitBuffer::insert(ReceivedPacketPtr&& rpkt)
{
	sanity_check(rpkt->type == RPT_SPLIT);
	u16 seqnum = rpkt->split.seqnum;
	u16 chunk_count = rpkt->split.chunk_count;

	// Add if doesn't exist
	IncomingSplitPacket *sp;
	auto it = m_buf.find(seqnum);
	if (it == m_buf.end()) {
		sp = new IncomingSplitPacket(
			this, seqnum, chunk_count, rpkt->is_reliable, m_timeout_queue);
		m_buf[seqnum] = sp;
	} else {
		sp = it->second;
	}
	sp->insert(std::move(rpkt));

	if (sp->allReceived()) {
		m_buf.erase(seqnum);
		Buffer<u8> full_contents = sp->reassemble();
		delete sp;
		m_data_received(std::move(full_contents));
	}
}

void IncomingSplitBuffer::handleTimeout(IncomingSplitPacket *sp)
{
	dout_con << "NOTE: Removing timed out unreliable split packet" << std::endl;
	auto it = m_buf.find(sp->m_seqnum);
	sanity_check(it != m_buf.end());
	sanity_check(it->second == sp);
	m_buf.erase(it);
	delete sp;
}

} // namespace con
