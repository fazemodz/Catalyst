#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace CatalystImport {

// DEFLATE decompression, RFC 1951, with the optional zlib wrapper of RFC 1950.
//
// FBX stores its large arrays - positions, indices, normals, UVs - as zlib
// streams, so reading the format at all needs this first. It is written here
// rather than pulled in so the engine keeps its no-dependency build.
//
// expectedSize is the uncompressed length the caller already knows from the FBX
// array header. Passing it lets the output be allocated exactly once, and lets
// a stream that decodes to the wrong length be rejected rather than trusted.
bool Inflate(const uint8_t* data,
             size_t size,
             size_t expectedSize,
             std::vector<uint8_t>& out,
             std::string* outError);

}
