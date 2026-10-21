#pragma once

#include "lug_common.h"
#include "lug_value.h"
#include "lug_vector.h"

// Forward declaration for VMCore since
// we need it for memory operations

namespace Luggage {
    
class VMCore;

struct LineStart {
    std::size_t line;       // The line number.
    std::size_t offset;     // The index of the first instruction of that line.
};

struct Chunk {
public: 
    explicit Chunk(VMCore& vm) 
        : vm(vm), constants(vm), bytecode(vm), lines(vm) 
    {};
    ~Chunk();

    void Write(std::uint8_t byte, std::size_t line);
    void WriteShort(std::uint16_t byte, std::size_t line);
    void WriteConstant(Value value, std::size_t line);
    std::size_t AddConstant(Value value);
    std::size_t GetLine(std::size_t byteOffset);

    Vector<std::uint8_t> bytecode;
    Vector<LineStart> lines;

    Vector<Value> constants;

private:
    VMCore& vm;
};


}