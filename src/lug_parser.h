#pragma once

#include "lug_lexer.h"

namespace Luggage {

class Parser {
public:
    void Reset(const char* source);

	void Advance();
    void Consume(TokenType type, const char* errorMessage);
    bool Match(TokenType expected);

    template<typename... Args>
    bool Check(Args... expected) {
        return ((current.type == expected) || ...);
    };

    bool IsAtEnd();

    [[noreturn]] void ErrorAtCurrent(const char* message);
    [[noreturn]] void Error(const char* message);
    [[noreturn]] void ErrorAt(const Token& token, const char* message);

    Token previous;
    Token current;
    
private:
    Lexer lexer;
};

}