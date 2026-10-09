#pragma once

#include "lug_common.h"

#define OPCODE_LIST                 \
    OPCODE(CONSTANT)                \
                                    \
    OPCODE(NEGATE)                  \
    OPCODE(NOT)                     \
                                    \
    OPCODE(TRUE)                    \
    OPCODE(FALSE)                   \
    OPCODE(NIL)                     \
                                    \
    OPCODE(ADD)                     \
    OPCODE(SUBTRACT)                \
    OPCODE(MULTIPLY)                \
    OPCODE(DIVIDE)                  \
                                    \
    OPCODE(EQUAL)                   \
    OPCODE(GREATER)                 \
    OPCODE(GREATER_EQUAL)           \
    OPCODE(LESS)                    \
    OPCODE(LESS_EQUAL)              \
                                    \
    OPCODE(JUMP_IF_FALSE)           \
    OPCODE(JUMP_IF_TRUE)            \
    OPCODE(JUMP_IF_FALSE_OR_POP)    \
    OPCODE(JUMP_IF_TRUE_OR_POP)     \
    OPCODE(POP_JUMP_IF_FALSE)       \
    OPCODE(POP_JUMP_IF_TRUE)        \
    OPCODE(JUMP)                    \
    OPCODE(LOOP)                    \
    OPCODE(COUNT_PREP)              \
    OPCODE(COUNT_LOOP)              \
    OPCODE(PLACEHOLDER)             \
                                    \
    OPCODE(DEFINE_GLOBAL)           \
    OPCODE(GET_GLOBAL)              \
    OPCODE(SET_GLOBAL)              \
    OPCODE(GET_LOCAL)               \
    OPCODE(SET_LOCAL)               \
                                    \
    OPCODE(POP)                     \
    OPCODE(DUPLICATE)               \
                                    \
    OPCODE(PRINT)                   \
    OPCODE(CALL)                    \
                                    \
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