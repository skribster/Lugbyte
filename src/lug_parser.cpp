#include <cstdio>
#include <sstream>
#include <cstdarg>

#include "luggage.hpp"
#include "lug_parser.h"

namespace Luggage {

// Helper functions -----------------------------

static std::string FormatString(const char* format, va_list args) {
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

// Parser public functions -----------------------------

void Parser::Reset(const char* source, std::filesystem::path scrPath) {
    scriptPath = scrPath;

    lexer.Reset(source);
    Advance();
}

// Parser private functions -----------------------------

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

std::string Parser::ErrorDetails(const Token& at) const {
    std::string lineContents = lexer.LineContents(at.line);

    std::string lineNum = std::to_string(at.line);
    std::string lineNumPadding(lineNum.size(), ' ');

    std::string columnPadding;
    for (std::size_t i = 0; i < at.column - 1 && i < lineContents.size(); i++) {
        if (lineContents[i] == '\t') columnPadding += '\t';
        else columnPadding += ' ';
    }
    
    std::string details;
    details += lineNumPadding   + " |\n";
    details += lineNum          + " | " + lineContents + '\n';
    details += lineNumPadding   + " | ";
    details += columnPadding + std::string(at.length != 0 ? at.length : 1, '^');

    return details;
}


void Parser::ErrorAt(const Token& at, const char* format, ...) {
    va_list args;
    va_start(args, format);

    std::string out;
    std::string message = FormatString(format, args);
    
    // error at '...'
    out += "error";
    if (at.type == TokenType::END_OF_FILE) {
        out += " at end";
    } else if (at.type != TokenType::ERROR) {
        out += " at '";
        out.append(at.start, at.length);
        out += "'";
    }

    // : message
    out += ": " + message + "\n";
    
    // --> script_name.lug:line:col
    //      | 
    // line | ...
    //      | 
    //! TODO: we probably should actually display the script's path
    // instead of just the name but this works fine for now
    out += " --> " + scriptPath.filename().string();

    out += ":" + std::to_string(at.line) + ":" + std::to_string(at.column) + '\n';
    out += ErrorDetails(at);

    LugError error;
    error.type = InterpretResult::COMPILE_ERROR;
    error.message = out;

    va_end(args);
    
    throw error;
}


void Parser::ErrorAtCurrent(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ErrorAt(current, format, args);
    va_end(args);
}


void Parser::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ErrorAt(previous, format, args);
    va_end(args);
}

} // Luggage