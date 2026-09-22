#pragma once

#include "lug_vm.h"

namespace Luggage {

// VMCore methods -----------------------------------


// Luggage public header methods -----------------------------------
VM::VM() 
: core(std::make_unique<VMCore>()) 
{

}


VM::~VM() = default;


InterpretResult VM::Interpret(std::string_view source) {
    return InterpretResult::OK;
}

}