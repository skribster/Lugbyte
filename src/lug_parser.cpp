#include <cstdio>
#include <cstdarg>
#include <stdexcept>

#include "luggage.hpp"
#include "lug_parser.h"

namespace Luggage {

static std::string FormatErrorMessage(const char* format, va_list args) {
    va_list sizeArgs;
    va_copy(sizeArgs, args);
    const int length = std::vsnprintf(nullptr, 0, format, sizeArgs);
    va_end(sizeArgs);
    if (length < 0) {
        ASSERT(false, "error message formatting failed");
    }

    std::string message(static_cast<std::size_t>(length) + 1, '\0');
    va_list formatArgs;
    va_copy(formatArgs, args);
    const int formattedLength = std::vsnprintf(message.data(), message.size(), format, formatArgs);
    va_end(formatArgs);
    if (formattedLength < 0) {
        ASSERT(false, "error message formatting failed");
    }

    message.resize(static_cast<std::size_t>(formattedLength));
    return message;
}

void Parser::Reset(const char* source, std::filesystem::path path) {
    modulePath = path;
    lexer.Reset(source);
    Advance();
}

// PARSER PARSE FUNCTIONS -----------------------------
void Parser::Advance() {
    previous = current;
    current = lexer.ScanToken();

    if (current.type == TokenType::ERROR) {
        ErrorAtCurrent(current.start);
    }
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


bool Parser::IsAtEnd() {
    return Check(TokenType::END_OF_FILE);
}


// ERROR FUNCTIONS -----------------------------
void Parser::ErrorAt(const Token& token, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string message = FormatErrorMessage(format, args);
    va_end(args);

    std::string errorMessage;
    
    errorMessage.append(modulePath.filename().string().c_str());
    errorMessage += ":";
    errorMessage += std::to_string(token.line);
    errorMessage += ": error";

    if (token.type == TokenType::END_OF_FILE) {
        errorMessage += " at end";
    } else if (token.type == TokenType::ERROR) {
        // Nothing.
    } else {
        errorMessage += " at '";
        errorMessage.append(token.start, token.length);
        errorMessage += "'";
    }

    errorMessage += ": ";
    errorMessage += message;

    LugError error;
    error.type = InterpretResult::COMPILE_ERROR;
    error.message = errorMessage;

    throw error;
}

void Parser::ErrorAtCurrent(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string message = FormatErrorMessage(format, args);
    va_end(args);
    ErrorAt(current, message.c_str());
}


void Parser::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string message = FormatErrorMessage(format, args);
    va_end(args);
    ErrorAt(previous, message.c_str());
}


} // Luggage