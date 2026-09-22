#include <iostream>

#include "luggage.hpp"
#include "lug_chunk.h"
#include "lug_opcodes.h"
#include "lug_debug.h"

int main(int argc, char** argv){
    using namespace Luggage;

    Chunk chunk;

    chunk.WriteConstant(5, 123);
    chunk.WriteConstant(8, 123);
    chunk.Write(OpCode::ADD, 123);

    chunk.Write(OpCode::RETURN, 124);

    Debug::DisassembleChunk(chunk, "test chunk");

    return 0;
}