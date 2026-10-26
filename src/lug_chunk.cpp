#include "lug_chunk.h"
#include "lug_memory.h"
#include "lug_opcodes.h"
#include "lug_vm.h"

namespace Luggage {


Chunk::~Chunk() {

}


/**
 * Adds a single byte into the chunk's
 * bytecode array.
 */
void Chunk::Write(std::uint8_t byte, std::size_t line) {
    bytecode.PushBack(byte);

    // Check if we're still on the same line as the last instruction.
    if (lines.Count() > 0 && lines[lines.Count() - 1].line == line) {
        return;
    }

    LineStart newLine;
    newLine.offset = bytecode.Count() - 1;
    newLine.line = line;
    lines.PushBack(newLine);
}


/**
 * Writes a 16-bit instruction into the bytecode stream. 
 * This is used for instructions that require 2 bytes of data,
 * such as an instruction prefixed with OpCode::WIDE
 */
void Chunk::WriteShort(std::uint16_t byte, std::size_t line) {
    Write((byte >> 8) & 0xFF, line);
    Write(byte & 0xFF, line);
}


/**
 * Adds the given constant into its constant pool
 * and also writes an OP_CONSTANT instruction
 * in its bytecode stream to load it during
 * runtime.
 */
void Chunk::WriteConstant(Value value, std::size_t line) {
    std::size_t constantIndex = AddConstant(value);

    if (constantIndex > UINT8_MAX) {
        Write(OpCode::SHORT, line);
        Write(OpCode::CONSTANT, line);

        /*
            We will split the index (a std::uint16_t) 
            into two std::uint8_t's, and during runtime
            we'll combine it all again.
        */
        WriteShort(static_cast<std::uint16_t>(constantIndex), line);
    } else {
        Write(OpCode::CONSTANT, line);
        Write(static_cast<std::uint8_t>(constantIndex), line);
    }
}



/**
 * Adds a constant into the chunk's constant pool.
 */
std::size_t Chunk::AddConstant(Value value) {
    // So the GC doesn't collect when we add it to the vector
    vm.stack.Push(value); 

    constants.PushBack(value);
    vm.stack.Pop();

    return constants.Count() - 1;
}   

/**
 * Returns the line of a byte in the bytecode stream,
 * taking its index as the input.
 */
std::size_t Chunk::GetLine(std::size_t byteOffset) {
    /*
        We will loop through the line array from beginning
        to end until we find a LineStart that matches the 
        instruction's byte offset.
    */

    std::size_t end = lines.Count() - 1;
    for (std::size_t i = 0; i < end; i++) {
        /*
            The next line in the list has a larger
            instruction offset, that means the last one
            has to be the LineStart we're looking for
        */
        if (byteOffset < lines[i + 1].offset) return lines[i].line;
    }

    return lines[end].line;
}


}