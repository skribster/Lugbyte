#pragma once

#include <string>
#include <vector>
#include <optional>

#include "lug_lexer.h"

namespace Luggage {

class Parser {
public:
    void Reset(
        const char* source, 
        std::filesystem::path scriptPath
    );

	void Advance();
    void Consume(TokenType type, const char* errorMessage);
    bool Match(TokenType expected);

    template<typename... Args>
    bool Check(Args... expected) {
        return ((current.type == expected) || ...);
    };

    bool IsAtEnd();
    
    [[noreturn]] void ErrorAtCurrent(const char* format, ...);
    [[noreturn]] void Error(const char* format, ...);
    [[noreturn]] void ErrorAt(const Token& at, const char* format, ...);

    Token previous;
    Token current;
    
private:
    std::string ErrorDetails(const Token& at) const;

    Lexer lexer;
    std::filesystem::path scriptPath;
};

}