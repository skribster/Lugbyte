#pragma once

#include "lug_common.h"

#define OPCODE_LIST         \
    OPCODE(CONSTANT)        \
                            \
    OPCODE(ADD)             \
    OPCODE(SUBTRACT)        \
    OPCODE(MULTIPLY)        \
    OPCODE(DIVIDE)          \
    OPCODE(INCREMENT)       \
                            \
    OPCODE(PRINT)           \
                            \
    OPCODE(RETURN)

namespace Luggage {

namespace OpCode {
    enum Type : std::uint8_t {
        #define OPCODE(name) name,
        OPCODE_LIST
        #undef OPCODE
    };

    // A string array of all the opcodes
    extern const char* Strings[];
    inline const char* ToString(OpCode::Type op) {
        return Strings[static_cast<int>(op)];   
    }
}


}