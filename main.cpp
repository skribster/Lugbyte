#include <iostream>
#include <string>
#include <fstream>
#include <ostream>
#include <sstream>

#include "luggage.hpp"


static void Repl(Luggage::VM& vm) {
    std::string line;
    while (true) {
        std::printf("> ");
        std::getline(std::cin, line);

        if (line.empty()) break;

        Luggage::InterpretResult result = vm.Interpret(line);
    }
}


static std::string ReadFile(std::string_view path) {
    std::ifstream file(path.data());
    if (!file.is_open()) {
        std::fprintf(stderr, "Could not open file: %s\n", path.data());
        exit(74);
    }

    // Read the entire file buffer into a string stream
    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

static void RunFile(Luggage::VM& vm, std::string_view path) {
    std::string source = ReadFile(path);
    Luggage::InterpretResult result = vm.Interpret(source);

    if (result == Luggage::InterpretResult::COMPILE_ERROR) exit(65);
    if (result == Luggage::InterpretResult::RUNTIME_ERROR) exit(70);
}


int main(int argc, char* argv[]){
    Luggage::VM vm;
    
    if (argc == 1) {
        Repl(vm);
    } else if (argc == 2) {
        RunFile(vm, argv[1]);
    } else {
        std::fprintf(stderr, "Usage: lox [path]\n");
        exit(64);
    }

    return 0;
}