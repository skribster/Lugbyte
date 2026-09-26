#include "lug_compiler.h"


namespace Luggage {


bool Compiler::Compile(Chunk& chunk, const char* source) {
    Lexer lexer;
    lexer.Reset(source);
    
    int line = -1;
    while (true) {
        Token token = lexer.ScanToken();
        if (token.line != line) {
            std::printf("%4zu ", token.line);
            line = token.line;
        } else {
            std::printf("   | ");
        }
        std::printf("%2d '%.*s'\n", token.type, (int)token.length, token.start); 

        if (token.type == TokenType::END_OF_FILE) break;
    }
    
    return true;
}

}