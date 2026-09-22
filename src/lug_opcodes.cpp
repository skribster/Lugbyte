#include "lug_opcodes.h"


namespace Luggage {

namespace OpCode {
    #define OPCODE(name) "OP_" #name,
    const char* Strings[] = {
        OPCODE_LIST
    };
    #undef OPCODE
}
    
}