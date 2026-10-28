#pragma once

#include <string_view>
#include <string>
#include <memory>
#include <filesystem>

namespace Luggage {

enum class ErrorType {
    COMPILE,
    RUNTIME,
    MEMORY
};


enum class LibInclude : unsigned {
    NONE            = 0,
    BASE            = 1 << 0,   // 0001
    MATH            = 1 << 1,   // 0010
    STRING          = 1 << 2,   // 0100...
    COLLECTION      = 1 << 3,
    NUMBER          = 1 << 4,
    ALL             = BASE | MATH | STRING | COLLECTION | NUMBER
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

    /**
     * Loads the given library into the VM's environment.
     * This takes in an enum of LibInclude, which you can also
     * use bitmasking or the | opreator to include multiple 
     * libraries. For instance, 
     * LoadLibraries(LibInclude::BASE | LibInclude::MATH) will
     * include both the base and the math library.
     */
    void LoadLibrary(LibInclude libraries);

    /*
        Takes in a Luggage program and interprets it. Returns
        'true' if success, and 'false' if not.

        You can get the last error with VM::LastError().
    */
    [[nodiscard]] bool Interpret(std::filesystem::path module, std::string_view source);
    const LugError& LastError() const;

private:
    std::unique_ptr<VMCore> core;
};

}