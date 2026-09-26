#pragma once

#include <string_view>
#include <string>
#include <memory>

namespace Luggage {

enum class ErrorType {
    COMPILE,
    RUNTIME,
    MEMORY
};


struct LugError {
    ErrorType type;
    std::string message;
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

    // void LoadStandardLibraries();

    /*
        Takes in a Luggage program and interprets it. Returns
        'true' if success, and 'false' if not.

        You can get the last error with VM::LastError().
    */
    [[nodiscard]] bool Interpret(std::string_view source);
    const LugError& LastError() const;

private:
    std::unique_ptr<VMCore> core;
};

}