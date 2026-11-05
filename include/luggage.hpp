#pragma once

#include <string_view>
#include <string>
#include <memory>
#include <filesystem>
#include <functional>
#include <optional>

namespace Luggage {

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
    MEMORY_ERROR,
    FILE_ERROR,
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
    InterpretResult type;
    std::string message;
    int line;
};  


/**
 * Not to be used directly. This class is the
 * internal implementation to the external VM
 * class.
 */
class VMCore;



// VM -------------------------------------------


std::optional<std::filesystem::path> DefaultResolveModule(
    const std::filesystem::path& importerPath, 
    const std::filesystem::path& requestedPath
);

std::optional<std::string> DefaultFileLoader(const std::filesystem::path& filePath);
void DefaultWriter(std::string_view string);


/**
 * The configuration for a single Luggage VM.
 */
struct VMConfig {
    using ResolveModuleFn = std::function<std::optional<std::filesystem::path>
        (const std::filesystem::path& importerPath, const std::filesystem::path& requestedPath)>;

    using LoadModuleFn = std::function<std::optional<std::string>(const std::filesystem::path& filePath)>;
    using WriteFn = std::function<void(std::string_view s)>;
    using ErrorFn = std::function<void(const LugError& error)>;

    /**
     * A function to resolve a module's relative path into an absolute path.
     */
    ResolveModuleFn resolveModuleFn { DefaultResolveModule };

    /**
     * A function to load a .lug module and return back its contents.
     */
    LoadModuleFn loadModuleFn { DefaultFileLoader };
    
    /**
     * A function for when the program wants to write something to
     * a console.
     */
    WriteFn writeFn { DefaultWriter };

    /**
     * A function that runs immediately after an error.
     */
    ErrorFn errorFn { [](const LugError& error) {} };
};

/**
 * A virtual machine for executing Luggage bytecode. 
 * It manages the execution state, including the stack, 
 * instruction pointer, and other resources needed to run
 * Luggage programs.
 */
class VM {
public:
    


    explicit VM(const VMConfig& config = {});
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

    /**
     * Collects the VM's garbage immediately.
     */
    void CollectGarbage();

    /** 
     * Takes in a Luggage program and interprets it. Returns
     * 'true' if success, and 'false' if not.
     *
     * You can get the last error with VM::LastError().
     */
    [[nodiscard]] InterpretResult InterpretFile(std::filesystem::path module);
    [[nodiscard]] InterpretResult Interpret(std::string_view source, std::string_view scriptName = "script");
    const LugError& LastError() const;

private:
    std::unique_ptr<VMCore> core;
    VMConfig config;
};


// Config ---------------------------------------

}