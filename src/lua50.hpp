#pragma once

// The 2006 release's scripts are Lua 5.0 bytecode. We run them on our Lua 5.1
// by translating each chunk to 5.1 bytecode as it is loaded (lua50.cpp).

#include <string>

namespace lua50 {

// True for a precompiled Lua 5.0 chunk.
bool isChunk(const std::string& bytes);

// The same program as a Lua 5.1 chunk (32-bit size_t, as the original scripts).
// Throws std::runtime_error for constructs that have no 5.1 equivalent here.
std::string translate(const std::string& bytes);

}  // namespace lua50
