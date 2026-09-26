#pragma once

#include "luggage.hpp"
#include "lug_common.h"
#include "lug_value.h"
#include "lug_chunk.h"

namespace Luggage {

constexpr std::size_t STACK_MAX = 256;

class VMCore {
public:
    VMCore();
    std::size_t bytesAllocated{ 0 };

    Chunk* chunk;
    Value stack[STACK_MAX];
    Value* stackTop{ nullptr };
    std::uint8_t* ip{ nullptr };

    template<typename... Args>
    void RuntimeError(const char* format, Args... args);

private:
    void ResetStack();
    void Push(Value value);
    Value Pop();

    InterpretResult Run();

    friend class VM;
};


template<typename...Args>
void VMCore::RuntimeError(const char* format, Args... args) {
    // Get current byte
    std::size_t byteOffset = static_cast<std::size_t>(ip - chunk->bytecode);
    std::size_t line = chunk->GetLine(byteOffset);

    std::fprintf(stderr, "[line %zu]\n", line);
    std::fprintf(stderr, format, args...);
}

}