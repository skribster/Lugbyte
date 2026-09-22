#pragma once

#include "lug_common.h"
#include "lug_chunk.h"

namespace Luggage::Debug {

void DisassembleChunk(Chunk& chunk, const char* name);
std::size_t DisassembleInstruction(Chunk& chunk, std::size_t offset);

}