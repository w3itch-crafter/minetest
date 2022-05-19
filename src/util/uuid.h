#pragma once
#include "irrlichttypes.h"

//
// These generate UUIDs very quickly using low-quality random data.
//
// This is good to use a logging tag, to track the movement of data
// between components.
//

// Writes a null-terminated human-readable hex UUID to dest.
void genHexUUID(char *dest, size_t dest_size);

// Writes a UUID (raw binary) to dest, up to dest_size.
void genRawUUID(char *dest, size_t dest_size);
