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
    Table globals;
    Obj* objects{ nullptr };
    
    template<typename... Args>
    [[noreturn]] void RuntimeError(const char* format, Args... args);

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

template<typename... Args>
void VMCore::RuntimeError(const char* message, Args... args) {
    // Compute required length for the std::string
    int length = std::snprintf(nullptr, 0, message, args...);
    ASSERT(length, "snprintf formatting error");

    // Allocate exactly enough space
    std::string formatted(length, '\0');

    // Format the error message with the args
    std::snprintf(formatted.data(), static_cast<std::size_t>(length) + 1, message, args...);

    // Write the full error message and create LugError
    std::size_t byteOffset = static_cast<std::size_t>(ip - chunk->bytecode);
    std::size_t line = chunk->GetLine(byteOffset);

    std::string errorMessage;
    errorMessage += "[line ";
    errorMessage += std::to_string(line);
    errorMessage += "]\n";
    errorMessage += formatted;

    LugError error;
    error.type = ErrorType::RUNTIME;
    error.message = errorMessage;

    throw error;
}


}