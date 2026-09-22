#include <cstdio>

#include "lug_debug.h"
#include "lug_opcodes.h"


namespace Luggage::Debug {

// Instruction printing helper functions -------------------------

static std::size_t simpleInstruction(const char* name, std::size_t offset) {
    std::printf("%s\n", name);
    return offset + 1;
}


static std::size_t constantInstruction(const char* name, Chunk& chunk, std::size_t offset) {
    std::uint8_t constantIndex = chunk.bytecode[offset + 1];
    std::printf("%-16s %4d '", name, constantIndex);

    Value::Print(chunk.constants.values[constantIndex]);

    std::printf("'\n");

    return offset + 2;
}

// Debug methods -------------------------

std::size_t DisassembleInstruction(Chunk& chunk, std::size_t offset) {
    // Print out the bytecode offset on the left.
    std::printf("%04zu ", offset);

    // Display line information
    // If we have two instructions that are the same line,
    // we simply display a | to symbolize that the line
    // continues on for these instructions. Only when we
    // encounter a new line do we display the line number.
    if (offset > 0 && chunk.GetLine(offset) == chunk.GetLine(offset - 1)) {
        std::printf("   | ");
    } else {
        std::printf("%4zu ", chunk.GetLine(offset));
    }

    std::uint8_t instruction = chunk.bytecode[offset];
    switch (static_cast<OpCode::Type>(instruction)) {
        case OpCode::CONSTANT: {
            return constantInstruction("OP_CONSTANT", chunk, offset);
        }

        case OpCode::ADD: {
            return simpleInstruction("OP_ADD", offset);
        }

        case OpCode::RETURN: {
            return simpleInstruction("OP_RETURN", offset);
        }

        default: {
            std::printf("UNKNOWN OPCODE '%d'\n", instruction);
            return offset + 1;
        }
    }
}



void DisassembleChunk(Chunk& chunk, const char* name) {
    std::printf("== %s ==\n", name);

    /**
     * We're going to loop through the bytecode stream and print
     * each instruction.
     */
    for (std::size_t offset = 0; offset < chunk.byteCount;) {
        offset = DisassembleInstruction(chunk, offset);
    }
}


}