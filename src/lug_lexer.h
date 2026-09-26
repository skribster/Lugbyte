#pragma once

#include "lug_common.h"


namespace Luggage {

enum class TokenType {
    // Single-character tokens
    LEFT_PAREN,
    RIGHT_PAREN,
    LEFT_BRACE,
    RIGHT_BRACE,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    COMMA,
    DOT,
    MINUS,
    PLUS,
    QUESTION,
    SEMICOLON,
    COLON,
    SLASH,
    STAR,
    MODULO,

	// One-or-two character tokens
    BANG,
    BANG_EQUAL,
    EQUAL,
    EQUAL_EQUAL,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    LEFT_ARROW,

	// Literals
    IDENTIFIER,
    STRING,
    NUMBER,
    ERROR,

    // Keywords
    AND,
    BOX,
    BREAK,
    BY,
    COUNT,
    DO,
    ELSE,
    ELSEIF,
    END,
    FALSE,
    FUNCTION,
    FOREACH,
    FROM,
    IF,
    IN,
    NIL,
    NOT,
    OR,
    PRINT,
    RETURN,
    SHARE,
    SHARED,
    THEN,
    TO,
    TRUE,
    WHILE,

    END_OF_FILE
};


struct Token {
    TokenType type;
    const char* start{ nullptr };
    std::size_t length{ 0 };
    std::size_t line{ 0 };
};


class Lexer {
public:
    inline void Reset(const char* source) {
        start = source;
        current = source;
        line = 1;
    }

    Token ScanToken();
    
private:
    Token MakeToken(TokenType type);
    Token ErrorToken(const char* message);
    
    Token Identifier();
    Token Number();
    Token String();

    bool IsAtEnd();
    void SkipWhitespace();
    char Advance();
    char Peek();
    char PeekNext();
    bool Match(char expected);

    const char* start{ nullptr };
    const char* current{ nullptr };
    std::size_t line{ 1 };
};


}