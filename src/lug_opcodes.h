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
    OPCODE(MODULO)                  \
                                    \
    OPCODE(EQUAL)                   \
    OPCODE(GREATER)                 \
    OPCODE(GREATER_EQUAL)           \
    OPCODE(LESS)                    \
    OPCODE(LESS_EQUAL)              \
                                    \
    OPCODE(JUMP)                    \
    OPCODE(JUMP_IF_FALSE)           \
    OPCODE(JUMP_IF_TRUE)            \
    OPCODE(JUMP_IF_FALSE_OR_POP)    \
    OPCODE(JUMP_IF_TRUE_OR_POP)     \
    OPCODE(POP_JUMP_IF_FALSE)       \
    OPCODE(POP_JUMP_IF_TRUE)        \
    OPCODE(LOOP)                    \
    OPCODE(COUNT_PREP)              \
    OPCODE(COUNT_LOOP)              \
    OPCODE(PLACEHOLDER)             \
                                    \
    OPCODE(POP)                     \
    OPCODE(DUPLICATE)               \
                                    \
    OPCODE(DEFINE_GLOBAL)           \
    OPCODE(GET_GLOBAL)              \
    OPCODE(SET_GLOBAL)              \
    OPCODE(GET_LOCAL)               \
    OPCODE(SET_LOCAL)               \
    OPCODE(GET_UPVALUE)             \
    OPCODE(SET_UPVALUE)             \
    OPCODE(CLOSE_UPVALUE)           \
                                    \
    OPCODE(CALL)                    \
    OPCODE(SELF)                    \
    OPCODE(CLOSURE)                 \
                                    \
    OPCODE(LIST)                    \
    OPCODE(DICTIONARY)              \
    OPCODE(APPEND)                  \
    OPCODE(DEFINE_ENTRY)            \
    OPCODE(GET_INDEX)               \
    OPCODE(SET_INDEX)               \
    OPCODE(GET_ATTRIBUTE)           \
    OPCODE(SET_ATTRIBUTE)           \
                                    \
    OPCODE(PRINT)                   \
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