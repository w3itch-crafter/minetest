#include <cassert>
#include <random>
#include <chrono>
#include <util/hex.h>

struct RandState {
	// 32-bit linear-congruential pseudo-random generator
	std::minstd_rand fast32;

	RandState()
	{
		// Avoid using std::random_device because it could block.
		uint64_t ticks = (uint64_t)std::chrono::system_clock::now().time_since_epoch().count();
		// Combine high and low bits to make a 32-bit seed
		ticks = (ticks ^ (ticks >> 32)) & 0xFFFFFFFF;
		fast32.seed(ticks);
	}

	uint32_t rand()
	{
		return fast32();
	}
};

static thread_local RandState g_state;

void genHexUUID(char *dest, size_t dest_size)
{
	assert(dest_size > 0);
	// cache thread-local lookup
	RandState& state = g_state;
	for (size_t i = 0; i < dest_size - 1; i++) {
		dest[i] = hex_chars[state.rand() & 0xF];
	}
	dest[dest_size - 1] = '\0';
}


void genRawUUID(char *dest, size_t dest_size)
{
	// cache thread-local lookup
	RandState& state = g_state;
	for (size_t i = 0; i < dest_size; i++) {
		dest[i] = state.rand() & 0xFF;
	}
}
