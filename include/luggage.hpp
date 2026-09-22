#pragma once

#include <string_view>
#include <memory>


namespace Luggage {

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
};

/**
 * Not to be used directly. This class is the
 * internal implementation to the external VM
 * class.
 */
class VMCore;

/**
 * A virtual machine for executing Luggage bytecode. 
 * It manages the execution state, including the stack, 
 * instruction pointer, and other resources needed to run
 * Luggage programs.
 */
class VM {
public:
    VM();
    ~VM();

     // Takes in a string and interprets it.
    InterpretResult Interpret(std::string_view source);

private:
    std::unique_ptr<VMCore> core;
};

}