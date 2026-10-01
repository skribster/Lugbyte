#include "lug_chunk.h"
#include "lug_memory.h"
#include "lug_opcodes.h"


namespace Luggage {


Chunk::~Chunk() {
    Memory::FreeArray<std::uint8_t>(vm, bytecode, byteCapacity);
}


/**
 * Adds a single byte into the chunk's
 * bytecode array.
 */
void Chunk::Write(std::uint8_t byte, std::size_t line) {
    // If we're about to exceed the maximum capacity
    // for this chunk...
    if (byteCount >= byteCapacity) {
        std::size_t oldCapacity = byteCapacity;

        // Calculate the new capacity of the array and update it.
        byteCapacity = Memory::GrowCapacity(oldCapacity);

        // Grow the array in heap memory.
        bytecode = Memory::GrowArray<std::uint8_t>(vm, bytecode, oldCapacity, byteCapacity);
    }

    bytecode[byteCount] = byte;
    byteCount++;


    // Check if we're still on the same line as the last instruction.
    if (lineCount > 0 && lines[lineCount - 1].line == line){
        return;
    }

    // If we reached a new line, we will add a new
    // LineStart to the line array.

    // If we're about to exceed the maximum line capacity
    // for this chunk...
    // Grow the line array if we're full.
    if (lineCount >= lineCapacity) {
        std::size_t oldCapacity = lineCapacity;

        lineCapacity = Memory::GrowCapacity(oldCapacity);
        lines = Memory::GrowArray<LineStart>(vm, lines, oldCapacity, byteCapacity);
    }

    // Write a new line start.
    LineStart& lineStart = lines[lineCount];
    lineStart.offset = byteCount - 1;
    lineStart.line = line;
    lineCount++;
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

    Write(OpCode::CONSTANT, line);
    Write(static_cast<std::uint8_t>(constantIndex), line);
}



/**
 * Adds a constant into the chunk's constant pool.
 */
std::size_t Chunk::AddConstant(Value value) {
    constants.Write(value);
    return constants.valueCount - 1;
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

    std::size_t end = lineCount - 1;
    for (std::size_t i = 0; i <= end; i++) {
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