#pragma once

#include "luggage.hpp"

#include "lug_common.h"
#include "lug_value.h"
#include "lug_chunk.h"
#include "lug_table.h"

namespace Luggage {

constexpr std::size_t STACK_MAX = 256;

class VMCore {
public:
    VMCore();
    ~VMCore();
    
    std::size_t bytesAllocated{ 0 };

    Chunk* chunk;
    Value stack[STACK_MAX];
    Value* stackTop{ nullptr };
    std::uint8_t* ip{ nullptr };
    
    Table strings;
    Obj* objects{ nullptr };
    
    [[noreturn]] void RuntimeError(std::string errorMessage);

private:
    void ResetStack();
    void Concatenate();
    void Push(Value value);
    Value& Peek(std::size_t distance) const;
    Value Pop();

    void Run();

    LugError lastError;

    friend class VM;
};

}