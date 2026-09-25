#include <iostream>

#include "luggage.hpp"
#include "lug_chunk.h"
#include "lug_opcodes.h"
#include "lug_debug.h"

int main(int argc, char** argv){
    using namespace Luggage;

    VM vm;

    Chunk chunk;
    
    chunk.WriteConstant(1.2, 123);
    chunk.WriteConstant(3.4, 123);
    chunk.Write(OpCode::ADD, 123);

    chunk.WriteConstant(5.6, 123);
    chunk.Write(OpCode::DIVIDE, 123);
    chunk.Write(OpCode::NEGATE, 123);
    chunk.Write(OpCode::RETURN, 123);

    Debug::DisassembleChunk(chunk, "test chunk");

    vm.Interpret(chunk);

    return 0;
}