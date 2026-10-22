#include <iostream>
#include <string>
#include <fstream>
#include <ostream>
#include <sstream>

#include "luggage.hpp"


static void Repl(Luggage::VM& vm) {
    std::string line;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, line);

        if (line.empty()) break;

        bool success = vm.Interpret(line);
        if (!success) {
            const Luggage::LugError& error = vm.LastError();
            std::cerr << error.message << '\n';
        }
    }
}


static std::string ReadFile(std::string_view path) {
    std::ifstream file(path.data());
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << path << std::endl;
        exit(74);
    }

    // Read the entire file buffer into a string stream
    std::stringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

static void RunFile(Luggage::VM& vm, std::string_view path) {
    std::string source = ReadFile(path);
    bool success = vm.Interpret(source);

    if (success) return;
    
    const Luggage::LugError& error = vm.LastError();
    std::cerr << error.message << '\n';

    if (error.type == Luggage::ErrorType::COMPILE) exit(65);
    if (error.type == Luggage::ErrorType::RUNTIME) exit(70); 
}


int main(int argc, char* argv[]){
    Luggage::VM vm;
    vm.LoadLibrary(Luggage::LibInclude::ALL);
    
    if (argc == 1) {
        Repl(vm);
    } else if (argc == 2) {
        RunFile(vm, argv[1]);
    } else {
        std::cerr << "Usage: luggage [path]\n";
        exit(64);
    }

    return 0;
}