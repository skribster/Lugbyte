#include <cstdio>

#include "luggage.hpp"
#include "lug_parser.h"

namespace Luggage {

void Parser::Reset(const char* source) {
    lexer.Reset(source);
    Advance();
}

// PARSER PARSE FUNCTIONS -----------------------------
void Parser::Advance() {
    previous = current;
    current = lexer.ScanToken();
}


void Parser::Consume(TokenType type, const char* errorMessage) {
    if (current.type == type) {
        Advance();
        return;
    }

    ErrorAtCurrent(errorMessage);
}


bool Parser::Match(TokenType expected) {
    if (!Check(expected)) return false;

    Advance();
    return true;
}


bool Parser::Check(TokenType expected) {
    return current.type == expected;
}


bool Parser::IsAtEnd() {
    return Check(TokenType::END_OF_FILE);
}


// ERROR FUNCTIONS -----------------------------
void Parser::ErrorAtCurrent(const char* message) {
    ErrorAt(current, message);
}


void Parser::Error(const char* message) {
    ErrorAt(previous, message);
}


void Parser::ErrorAt(const Token& token, const char* message) {
    std::string errorMessage;
    // [line <line>] Error
    errorMessage += "[line ";
    errorMessage += std::to_string(token.line);
    errorMessage += "] Error";

    if (token.type == TokenType::END_OF_FILE) {
        errorMessage += " at end";
    } else {
        errorMessage += " at '";
        errorMessage.append(token.start, token.length);
        errorMessage += "'";
    }

    errorMessage += ": ";
    errorMessage += message;

    LugError error;
    error.type = ErrorType::COMPILE;
    error.message = errorMessage;

    throw error;
}

} // Luggage