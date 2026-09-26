#pragma once

#include "lug_vm.h"
#include "lug_chunk.h"
#include "lug_lexer.h"

namespace Luggage {
    
class Compiler {
public:
    explicit Compiler(VMCore& vm) : vm(vm) {}

    bool Compile(Chunk& chunk, const char* source);

private:
    VMCore& vm;

};


}