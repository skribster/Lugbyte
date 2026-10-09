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
    std::printf("%-20s %4d '", name, constantIndex);

    Value::Print(chunk.constants.values[constantIndex]);

    std::printf("'\n");

    return offset + 2;
}


static int jumpInstruction(const char* name, int sign, Chunk& chunk, int offset) {
    std::uint16_t jump = (std::uint16_t)(chunk.bytecode[offset + 1] << 8);
    jump |= chunk.bytecode[offset + 2];
    std::printf("%-20s %4d -> %d\n", name, offset,
            offset + 3 + sign * jump);
    return offset + 3;
}


static int patchInstruction(const char* name, Chunk& chunk, int offset) {
    std::uint16_t jump = (std::uint16_t)(chunk.bytecode[offset + 1] << 8);
    jump |= chunk.bytecode[offset + 2];

    if (jump == UINT16_MAX) {
        std::printf("%-20s %4d -> 'NO_BREAK'\n", name, offset);
        return offset + 3;
    }

    std::printf("%-20s %4d -> %d\n", name, offset,
            offset - jump);

    return offset + 3;
}


/**
 * Helper for displaying a 'byte' instruction
 */
static std::size_t byteInstruction(const char* name, Chunk& chunk, std::size_t offset) {
    std::uint16_t slot = chunk.bytecode[offset + 1];
    std::printf("%-16s %4d\n", name, slot);
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

        case OpCode::NEGATE: {
            return simpleInstruction("OP_NEGATE", offset);
        }

        case OpCode::NOT: {
            return simpleInstruction("OP_NOT", offset);
        }

        case OpCode::TRUE: {
            return simpleInstruction("OP_TRUE", offset);
        }

        case OpCode::FALSE: {
            return simpleInstruction("OP_FALSE", offset);
        }

        case OpCode::NIL: {
            return simpleInstruction("OP_NIL", offset);
        }

        case OpCode::ADD: {
            return simpleInstruction("OP_ADD", offset);
        }

        case OpCode::SUBTRACT: {
            return simpleInstruction("OP_SUBTRACT", offset);
        }

        case OpCode::MULTIPLY: {
            return simpleInstruction("OP_MULTIPLY", offset);
        }

        case OpCode::DIVIDE: {
            return simpleInstruction("OP_DIVIDE", offset);
        }
        
        case OpCode::EQUAL: {
            return simpleInstruction("OP_EQUAL", offset);
        }
        
        case OpCode::GREATER: {
            return simpleInstruction("OP_GREATER", offset);
        }
        
        case OpCode::GREATER_EQUAL: {
            return simpleInstruction("OP_GREATER_EQUAL", offset);
        }
        
        case OpCode::LESS: {
            return simpleInstruction("OP_LESS", offset);
        }
        
        case OpCode::LESS_EQUAL: {
            return simpleInstruction("OP_LESS_EQUAL", offset);
        }

        case OpCode::JUMP_IF_FALSE: {
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        }   
        
        case OpCode::JUMP_IF_TRUE: {
            return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
        }

        case OpCode::JUMP_IF_FALSE_OR_POP: {
            return jumpInstruction("OP_JUMP_IF_FALSE_OR_POP", 1, chunk, offset);
        }

        case OpCode::JUMP_IF_TRUE_OR_POP: {
            return jumpInstruction("OP_JUMP_IF_TRUE_OR_POP", 1, chunk, offset);
        }

        case OpCode::POP_JUMP_IF_FALSE: {
            return jumpInstruction("OP_POP_JUMP_IF_FALSE", 1, chunk, offset);
        }

        case OpCode::POP_JUMP_IF_TRUE: {
            return jumpInstruction("OP_POP_JUMP_IF_TRUE", 1, chunk, offset);
        }

        case OpCode::JUMP: {
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        }

        case OpCode::LOOP: {
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        }

        case OpCode::COUNT_PREP: {
            return jumpInstruction("OP_COUNT_PREP", 1, chunk, offset);
        }

        case OpCode::COUNT_LOOP: {
            return jumpInstruction("OP_COUNT_LOOP", -1, chunk, offset);
        }

        case OpCode::PLACEHOLDER: {
            return patchInstruction("OP_PLACEHOLDER", chunk, offset);
        }

        case OpCode::DEFINE_GLOBAL: {
            return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        }
        
        case OpCode::GET_GLOBAL: {
            return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        }
        
        case OpCode::SET_GLOBAL: {
            return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        }
        
        case OpCode::GET_LOCAL: {
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        }

        case OpCode::SET_LOCAL: {
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        }

        case OpCode::POP: {
            return simpleInstruction("OP_POP", offset);
        }

        case OpCode::DUPLICATE: {
            return simpleInstruction("OP_DUPLICATE", offset);
        }

        case OpCode::PRINT: {
            return simpleInstruction("OP_PRINT", offset);
        }

        case OpCode::CALL: {
            return byteInstruction("OP_CALL", chunk, offset);
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