#include "exceptions.h"
#include "util/serialize.h"

// Utility for reading binary data (in network order) from a buffer.
//
// This could be combined with NetworkPacket, except for the fact
// that NetworkPacket expects to own the buffer being read.
// TODO: Merge this gracefully with NetworkPacket.
class BinReader {
public:
	BinReader(const u8 *data, size_t size) :
		m_data(data),
		m_size(size),
		m_pos(0)
	{ }

	size_t getPosition() const
	{
		return m_pos;
	}

	size_t size() const
	{
		return m_size;
	}

	u8 readU8()   { return ::readU8(_read(1)); }
	u16 readU16() { return ::readU16(_read(2)); }
	u32 readU32() { return ::readU32(_read(4)); }
	u64 readU64() { return ::readU64(_read(8)); }

	s8 readS8()   { return ::readS8(_read(1)); }
	s16 readS16() { return ::readS16(_read(2)); }
	s32 readS32() { return ::readS32(_read(4)); }
	s64 readS64() { return ::readS64(_read(8)); }

	BinReader& operator>>(u8 &out)  { out = readU8(); return *this; }
	BinReader& operator>>(u16 &out) { out = readU16(); return *this; }
	BinReader& operator>>(u32 &out) { out = readU32(); return *this; }
	BinReader& operator>>(u64 &out) { out = readU64(); return *this; }

	BinReader& operator>>(s8 &out)  { out = readS8(); return *this; }
	BinReader& operator>>(s16 &out) { out = readS16(); return *this; }
	BinReader& operator>>(s32 &out) { out = readS32(); return *this; }
	BinReader& operator>>(s64 &out) { out = readS64(); return *this; }

private:
	const u8 *m_data;
	size_t m_size;
	size_t m_pos;

	const u8 *_read(size_t n)
	{
		if (m_pos + n > m_size) {
			throw ParseError("BinReader: Unexpected EOF m_size=%zu", m_size);
		}
		const u8 *cursor = &m_data[m_pos];
		m_pos += n;
		return cursor;
	}
};
