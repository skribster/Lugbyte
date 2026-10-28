#pragma once

#include <filesystem>
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
    ELLIPSIS,
    MINUS,
    PLUS,
    QUESTION,
    SEMICOLON,
    COLON,
    SLASH,
    STAR,
    PERCENT,

	// One-or-two character tokens
    EQUAL,
    BANG_EQUAL,
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
    AT,
    BOX,
    BREAK,
    BY,
    CATEGORY,
    CONTINUE,
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
    INSPECT,
    NIL,
    NOT,
    OF,
    OR,
    RETURN,
    SHARE,
    SHARED,
    THEN,
    THING,
    TRUE,
    WHEN,
    WHILE,

    END_OF_FILE
};


struct Token {
    TokenType type;
    const char* start{ nullptr };
    std::size_t length{ 0 };
    std::size_t line{ 0 };

    bool operator==(const Token& other) const;

    static Token Synthetic(const char* lexeme);
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
    Token ErrorToken(const char* message);
    Token MakeToken(TokenType type);
    
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