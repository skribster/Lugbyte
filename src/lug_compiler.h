#pragma once

#include <array>

#include "luggage.hpp"

#include "lug_common.h"
#include "lug_vm.h"
#include "lug_chunk.h"
#include "lug_opcodes.h"
#include "lug_value.h"
#include "lug_parser.h"

namespace Luggage {

// Compiler class -------------------------
enum class Precedence {
    NONE,
    ASSIGNMENT,  // =
    OR,          // or
    AND,         // and
    EQUALITY,    // == !=
    COMPARISON,  // < > <= >=
    TERM,        // + -
    FACTOR,      // * /
    UNARY,       // ! -
    CALL,        // . ()
    PRIMARY
};


class Compiler;  
using ParseFn = void (Compiler::*)(bool canAssign);
struct ParseRule {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
};


class Compiler {
public:
    explicit Compiler(VMCore& vm) : vm(vm) {}

    void Compile(Chunk& chunk, const char* source);

private:
    Chunk* compilingChunk{ nullptr };
    Parser parser;
    VMCore& vm;
    bool isValidExprStmt{ false };

    Chunk* CurrentChunk() const;
    
    // Bytecode methods

    void EmitByte(std::uint8_t byte);
    void EmitBytes(std::uint8_t a, std::uint8_t b);
    void EmitReturn();
    void EmitConstant(Value value);
    std::uint8_t MakeConstant(Value value);
    std::uint8_t IdentifierConstant(Token& identifier);
    std::uint8_t ParseVariable(const char* errorMessage);

    void DefineVariable(std::uint8_t index);

    // Parse methods

    static const ParseRule rules[];
    const ParseRule& GetRule(TokenType type) const;

    // Statement methods

    void Statement();
    void PrintStatement();
    void BoxStatement();
    void ExpressionStatement();

    // Expression methods

    void Expression();
    void Grouping(bool canAssign);
    void Unary(bool canAssign);
    void Binary(bool canAssign);
    void Number(bool canAssign);
    void String(bool canAssign);
    void Literal(bool canAssign);
    void Variable(bool canAssign);

    void ParsePrecedence(Precedence minimumPrecedence);
};


}