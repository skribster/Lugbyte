#include <cstdio>

#include "lug_debug.h"
#include "lug_opcodes.h"
#include "lug_object.h"


namespace Luggage::Debug {

// Instruction printing helper functions -------------------------

static std::size_t simpleInstruction(const char* name, std::size_t offset) {
    std::printf("%s\n", name);
    return offset + 1;
}


static std::size_t constantInstruction(const char* name, Chunk& chunk, std::size_t offset) {
    std::uint8_t constantIndex = chunk.bytecode[offset + 1];
    std::printf("%-20s %4d '", name, constantIndex);

    Value::Print(chunk.constants[constantIndex]);

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
    std::uint8_t slot = chunk.bytecode[offset + 1];
    std::printf("%-20s %4d\n", name, slot);
    return offset + 2;
}


static std::size_t sharedInstruction(const char* name, Chunk& chunk, std::size_t offset) {
    std::uint8_t slot = chunk.bytecode[offset + 1];
    std::uint16_t sharedArgs = chunk.bytecode[offset + 2];
    
    std::printf("%-20s %4d '", name, slot); 

    // Print sharedArgs as binary representation
    for (int i = 7; i >= 0; i--) {
        // Shift bit to the 1st position and mask it
        std::printf("%d", (sharedArgs >> i) & 1);
        
        // Add a space every 4 bits for easy readability
        if (i % 4 == 0 && i != 0) {
            printf(" ");
        }
    }

    std::printf("'\n");

    return offset + 3;
}


static std::size_t shortInstruction(const char* name, Chunk& chunk, std::size_t offset) {
    OpCode::Type real = static_cast<OpCode::Type>(chunk.bytecode[offset + 1]);  
    std::uint16_t constantIndex = (chunk.bytecode[offset + 2] << 8);
    constantIndex |= chunk.bytecode[offset + 3];

    std::printf("%-20s %4d '", OpCode::ToString(real), constantIndex);
    Value::Print(chunk.constants[constantIndex]);
    std::printf("' (short) \n");

    return offset + 4;
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

        case OpCode::SHORT: {
            return shortInstruction("OP_SHORT", chunk, offset);
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
        
        case OpCode::MODULO: {
            return simpleInstruction("OP_MODULO", offset);
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

        case OpCode::JUMP: {
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
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
  
        case OpCode::LOOP: {
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        }

        case OpCode::COUNT_PREP: {
            return jumpInstruction("OP_COUNT_PREP", 1, chunk, offset);
        }

        case OpCode::COUNT_LOOP: {
            return jumpInstruction("OP_COUNT_LOOP", -1, chunk, offset);
        }

        case OpCode::FOREACH_PREP: {
            return jumpInstruction("OP_FOREACH_PREP", 1, chunk, offset);
        }

        case OpCode::FOREACH_LOOP: {
            return jumpInstruction("OP_FOREACH_LOOP", -1, chunk, offset);
        }

        case OpCode::PLACEHOLDER: {
            return patchInstruction("OP_PLACEHOLDER", chunk, offset);
        }

        case OpCode::POP: {
            return simpleInstruction("OP_POP", offset);
        }

        case OpCode::DUPLICATE: {
            return simpleInstruction("OP_DUPLICATE", offset);
        }

        case OpCode::COPY: {
            return simpleInstruction("OP_COPY", offset);
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

        case OpCode::GET_UPVALUE: {
            return byteInstruction("OP_GET_UPVALUE", chunk, offset);
        }

        case OpCode::SET_UPVALUE: {
            return byteInstruction("OP_SET_UPVALUE", chunk, offset);
        }

        case OpCode::CLOSE_UPVALUE: {
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        }

        case OpCode::CALL: {
            return byteInstruction("OP_CALL", chunk, offset);
        }

        case OpCode::CALL_SHARED: {
            return sharedInstruction("OP_CALL_SHARED", chunk, offset);
        }

        case OpCode::SELF: {
            return constantInstruction("OP_SELF", chunk, offset);
        }

        case OpCode::CLOSURE: {
            offset++;
            
            std::uint8_t constant = chunk.bytecode[offset++];
            std::printf("%-20s %4d ", "OP_CLOSURE", constant);
            Value::Print(chunk.constants[constant]);
            std::printf("\n");

            ObjFunction* function = chunk.constants[constant].AsObj<ObjFunction>();
            for (int j = 0; j < function->upvalueCount; j++) {
                int isLocal = chunk.bytecode[offset++];
                int index = chunk.bytecode[offset++];

                std::printf(
                    "%04zu      |                         %s %d\n",
                    offset - 2, 
                    isLocal ? "local" : "upvalue", 
                    index
                );
            }

            return offset;
        }

        case OpCode::LIST: {
            return simpleInstruction("OP_LIST", offset);
        }

        case OpCode::DICT: {
            return simpleInstruction("OP_DICT", offset);
        }

        case OpCode::THING: {
            return simpleInstruction("OP_THING", offset);
        }

        case OpCode::CATEGORY: {
            return simpleInstruction("OP_CATEGORY", offset);
        }

        case OpCode::APPEND: {
            return simpleInstruction("OP_APPEND", offset);
        }

        case OpCode::DEFINE_ENTRY: {
            return simpleInstruction("OP_DEFINE_ENTRY", offset);
        }

        case OpCode::GET_INDEX: {
            return simpleInstruction("OP_GET_INDEX", offset);
        }
        
        case OpCode::SET_INDEX: {
            return simpleInstruction("OP_SET_INDEX", offset);
        }

        case OpCode::GET_ATTRIBUTE: {
            return constantInstruction("OP_GET_ATTRIBUTE", chunk, offset);
        }

        case OpCode::SET_ATTRIBUTE: {
            return constantInstruction("OP_SET_ATTRIBUTE", chunk, offset);
        }

        case OpCode::PRINT: {
            return simpleInstruction("OP_PRINT", offset);
        }

        case OpCode::RETURN: {
            return simpleInstruction("OP_RETURN", offset);
        }

        default: {
            std::printf("unknown opcode '%d'\n", instruction);
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
    for (std::size_t offset = 0; offset < chunk.bytecode.Count();) {
        offset = DisassembleInstruction(chunk, offset);
    }
}


}