#include <iostream>
#include <string>
#include <fstream>
#include <ostream>
#include <sstream>

#include "luggage.hpp"

namespace fs = std::filesystem;

static void Repl(Luggage::VM& vm) {
    std::cout << "Luggage Interactive REPL\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, line);

        if (line.empty()) break;

        Luggage::InterpretResult result = vm.Interpret(line);
        if (result != Luggage::InterpretResult::OK) {
            const Luggage::LugError& error = vm.LastError();
            std::cerr << error.message << '\n';
        }
    }
}


static std::string ReadFile(std::string_view path) {
    std::ifstream file(path.data());
    if (!file.is_open()) {
        std::cerr << "could not open file: " << path << std::endl;
        exit(74);
    }

    // Read the entire file buffer into a string stream
    std::ostringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

static void RunFile(Luggage::VM& vm, std::string_view path) {
    std::string source = ReadFile(path);
    Luggage::InterpretResult result = vm.InterpretFile(path);

    if (result == Luggage::InterpretResult::OK) return;
    
    const Luggage::LugError& error = vm.LastError();
    std::cerr << error.message << std::endl;

    if (result == Luggage::InterpretResult::COMPILE_ERROR) exit(65);
    if (result == Luggage::InterpretResult::RUNTIME_ERROR) exit(70);
    if (result == Luggage::InterpretResult::MEMORY_ERROR) exit(1); 
}


int main(int argc, char* argv[]) {
    Luggage::VMConfig config;
    config.resolveModuleFn = [](fs::path importer, fs::path requested)
        -> std::optional<fs::path> {
        fs::path p = fs::path("bootstrap/") / requested;
        p.replace_extension(".lug");
        if (!fs::is_regular_file(p)) return std::nullopt;   // "module not found"
        
        return p;
    };

    Luggage::VM vm(config);
    vm.LoadLibrary(Luggage::LibInclude::ALL);
    
    if (argc == 1) {
        Repl(vm);
    } else if (argc == 2) {
        RunFile(vm, argv[1]);
    } else {
        std::cerr << "usage: luggage [path]\n";
        exit(64);
    }

    return 0;
}