#pragma once

#include "lug_common.h"
#include "lug_value.h"
#include "lug_vector.h"
#include "lug_lexer.h"

// Forward declaration for VMCore since
// we need it for memory operations

namespace Luggage {
    
class VMCore;

struct SourcePosition {
    std::size_t line;       // The line number.
    std::size_t column;     // The column number.
    std::size_t offset;     // The index of the first instruction of that line.
};

struct Chunk {
public: 
    using ConstantIndex = std::uint16_t;

    explicit Chunk(VMCore& vm) 
        : vm(vm), constants(vm), bytecode(vm), positions(vm) 
    {};
    ~Chunk();

    void Write(std::uint8_t byte, const Token& where);
    void WriteShort(std::uint16_t byte, const Token& where);
    void WriteConstant(Value value, const Token& where);
    ConstantIndex AddConstant(Value value);
    SourcePosition GetSourcePosition(std::size_t byteOffset);

    Vector<std::uint8_t> bytecode;
    Vector<SourcePosition> positions;
    Vector<Value> constants;

private:
    VMCore& vm;
};


}