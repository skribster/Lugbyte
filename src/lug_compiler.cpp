#include "lug_compiler.h"
#include "lug_debug.h"

namespace Luggage {

// Compiler public functions -----------------------------

void Compiler::Compile(Chunk& chunk, const char* source) {
    parser.Reset(source);

    compilingChunk = &chunk;

    parser.Advance();
    Expression();
    parser.Consume(TokenType::END_OF_FILE, "Expected end of expression.");

    //! Factor into EndCompiler() when the time is right.
    EmitReturn();
    Debug::DisassembleChunk(*CurrentChunk(), "code");

    compilingChunk = nullptr;
}

// Compiler private functions -----------------------------

Chunk* Compiler::CurrentChunk() const {
    return compilingChunk;
}

// Bytecode functions -----------------------------------

/**
 * Emits a byte into the current chunk's bytecode array
 */
void Compiler::EmitByte(std::uint8_t byte) {
    CurrentChunk()->Write(byte, parser.previous.line);
}

/**
 * Emits two bytes into the current chunk's bytecode array
 */
void Compiler::EmitBytes(std::uint8_t a, std::uint8_t b) {
    EmitByte(a);
    EmitByte(b);
}


/**
 * Emits a return instruction into
 * the current chunk's bytecode array
 */
void Compiler::EmitReturn() {
    EmitByte(OpCode::RETURN);
}


/**
 * Emits a constant instruction of
 * OP_CONSTANT [constantIndex],
 * where constantIndex is the index of
 * the constant in the constant pool
 */
void Compiler::EmitConstant(Value value) {
    CurrentChunk()->WriteConstant(value, parser.previous.line);
}


/**
 * Adds the value in the constant pool and
 * returns the index of the constant.
 */
std::uint8_t Compiler::MakeConstant(Value value) {
    // MakeConstant returns the location of the
    // constant in the chunk's constant pool.
    
    // Since the OP_CONSTANT instruction
    // can only store a 8-bit index operand, we need to
    // make sure the indices don't exceed that limit. 
    std::size_t constant = CurrentChunk()->AddConstant(value);
    if (constant > UINT8_MAX) {
        parser.Error("Too many constants in one chunk.");
        return 0;
    }

    return static_cast<std::uint8_t>(constant);
}


// Expression methods ------------------------------------------

const ParseRule Compiler::rules[] = {
    // Single-character tokens
    { &Compiler::Grouping,      nullptr,                Precedence::NONE },             // LEFT_PAREN
    { nullptr,                  nullptr,                Precedence::NONE },             // RIGHT_PAREN
    { nullptr,                  nullptr,                Precedence::NONE },             // LEFT_BRACE
    { nullptr,                  nullptr,                Precedence::NONE },             // RIGHT_BRACE
    { nullptr,                  nullptr,                Precedence::NONE },             // LEFT_BRACKET
    { nullptr,                  nullptr,                Precedence::NONE },             // RIGHT_BRACKET
    { nullptr,                  nullptr,                Precedence::NONE },             // COMMA
    { nullptr,                  nullptr,                Precedence::NONE },             // DOT
    { &Compiler::Unary,         &Compiler::Binary,      Precedence::TERM },             // MINUS
    { nullptr,                  &Compiler::Binary,      Precedence::TERM },             // PLUS
    { nullptr,                  nullptr,                Precedence::NONE },             // QUESTION
    { nullptr,                  nullptr,                Precedence::NONE },             // SEMICOLON
    { nullptr,                  nullptr,                Precedence::NONE },             // COLON
    { nullptr,                  &Compiler::Binary,      Precedence::FACTOR },           // SLASH
    { nullptr,                  &Compiler::Binary,      Precedence::FACTOR },           // STAR
    { nullptr,                  nullptr,                Precedence::NONE },             // MODULO
    
    // One-or-two character tokens
    { nullptr,                  &Compiler::Binary,      Precedence::EQUALITY },         // EQUAL
    { nullptr,                  &Compiler::Binary,      Precedence::EQUALITY },       // BANG_EQUAL
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // GREATER
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // GREATER_EQUAL
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // LESS
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // LESS_EQUAL
    { nullptr,                  nullptr,                Precedence::NONE },             // LEFT_ARROW

    // Literals         
    { nullptr,                  nullptr,                Precedence::NONE },             // IDENTIFIER
    { nullptr,                  nullptr,                Precedence::NONE },             // STRING
    { &Compiler::Number,        nullptr,                Precedence::NONE },             // NUMBER

    // Keywords         
    { nullptr,                  nullptr,                Precedence::NONE },             // AND
    { nullptr,                  nullptr,                Precedence::NONE },             // BOX
    { nullptr,                  nullptr,                Precedence::NONE },             // BREAK
    { nullptr,                  nullptr,                Precedence::NONE },             // BY
    { nullptr,                  nullptr,                Precedence::NONE },             // COUNT
    { nullptr,                  nullptr,                Precedence::NONE },             // DO
    { nullptr,                  nullptr,                Precedence::NONE },             // ELSE
    { nullptr,                  nullptr,                Precedence::NONE },             // ELSEIF
    { nullptr,                  nullptr,                Precedence::NONE },             // END
    { &Compiler::Literal,       nullptr,                Precedence::NONE },             // FALSE
    { nullptr,                  nullptr,                Precedence::NONE },             // FUNCTION
    { nullptr,                  nullptr,                Precedence::NONE },             // FOREACH
    { nullptr,                  nullptr,                Precedence::NONE },             // FROM
    { nullptr,                  nullptr,                Precedence::NONE },             // IF
    { nullptr,                  nullptr,                Precedence::NONE },             // IN
    { &Compiler::Literal,       nullptr,                Precedence::NONE },             // NIL
    { &Compiler::Unary,         nullptr,                Precedence::NONE },             // NOT
    { nullptr,                  nullptr,                Precedence::NONE },             // OR
    { nullptr,                  nullptr,                Precedence::NONE },             // PRINT
    { nullptr,                  nullptr,                Precedence::NONE },             // RETURN
    { nullptr,                  nullptr,                Precedence::NONE },             // SHARE
    { nullptr,                  nullptr,                Precedence::NONE },             // SHARED
    { nullptr,                  nullptr,                Precedence::NONE },             // THEN
    { nullptr,                  nullptr,                Precedence::NONE },             // TO
    { &Compiler::Literal,       nullptr,                Precedence::NONE },             // TRUE
    { nullptr,                  nullptr,                Precedence::NONE },             // WHILE

    { nullptr,                  nullptr,                Precedence::NONE },             // END_OF_FILE
};


const ParseRule& Compiler::GetRule(TokenType type) const {
    static_assert(
        sizeof(Compiler::rules) / sizeof(ParseRule)
        == static_cast<int>(TokenType::END_OF_FILE) + 1, 
        "Rule array and tokens are mismatched"
    );

    std::size_t ruleIndex = static_cast<std::size_t>(type);
    return Compiler::rules[ruleIndex];
}


/**
 * Parses an expression, or anything that produces
 * a value.
 */
void Compiler::Expression() {
    ParsePrecedence(Precedence::ASSIGNMENT);
}

/**
 * Parses an expression in parentheses.
 */
void Compiler::Grouping() {
    Expression();
    parser.Consume(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
}

/**
 * Parses a unary expression and emits the corresponding bytecode. 
 */
void Compiler::Unary() {
    /*
        Recall that the instruction goes after its operand, such as:
        OP_CONSTANT 5
        OP_NEGATE
        
        We have to compile the right hand side of the expression
        first, writing its bytecode in the stream, and only then
        can we negate it.
    */

    // We have already been consumed the leading unary operator
    // (i.e. - or !) and it's sitting in parser.previous
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    ParsePrecedence(Precedence::UNARY);

    switch (operatorType) {
        case TokenType::MINUS:      EmitByte(OpCode::NEGATE); break;
        case TokenType::NOT:        EmitByte(OpCode::NOT); break;

        default: UNREACHABLE();
    }
}

/**
 * Parses a binary expression with two operands.
 */
void Compiler::Binary() {
    // When we reach this function, the 'previous' token
    // points to the infix operator. (+, -, /, *, etc)
    TokenType operatorType = parser.previous.type;
    const ParseRule& rule = GetRule(operatorType);

    // Compile the right-hand operand.
    ParsePrecedence(static_cast<Precedence>((int)rule.precedence + 1));

    switch (operatorType) {
        case TokenType::EQUAL:          EmitByte(OpCode::EQUAL); break;
        case TokenType::GREATER:        EmitByte(OpCode::GREATER); break;
        case TokenType::GREATER_EQUAL:  EmitByte(OpCode::GREATER_EQUAL); break;
        case TokenType::LESS:           EmitByte(OpCode::LESS); break;
        case TokenType::LESS_EQUAL:     EmitByte(OpCode::LESS_EQUAL); break;

        case TokenType::PLUS:           EmitByte(OpCode::ADD); break;
        case TokenType::MINUS:          EmitByte(OpCode::SUBTRACT); break;
        case TokenType::STAR:           EmitByte(OpCode::MULTIPLY); break;
        case TokenType::SLASH:          EmitByte(OpCode::DIVIDE); break;

        default: UNREACHABLE(); // Unreachable.
    }
}


void Compiler::Number() {
    double value = std::strtod(parser.previous.start, nullptr);
    EmitConstant(value);
}


void Compiler::Literal() {
    TokenType literalType = parser.previous.type;

    switch (literalType) {
        case TokenType::TRUE: EmitByte(OpCode::TRUE); break;
        case TokenType::FALSE: EmitByte(OpCode::FALSE); break;
        case TokenType::NIL: EmitByte(OpCode::NIL); break;
        
        default: UNREACHABLE();
    }
}

// Compiler parse methods -----------------------------------------------
/**
 * Starts at the current token and parses any expression
 * at the given precedence level or higher. (i.e. if the
 * expression is to evaluate/happen first in the code)
 */
void Compiler::ParsePrecedence(Precedence minimumPrecedence) {
    parser.Advance();

    ParseFn prefixRule = GetRule(parser.previous.type).prefix;
    if (prefixRule == nullptr) {
        parser.ErrorAt(parser.previous, "Expected expression.");
        return;
    }

    (this->*prefixRule)();

    while (GetRule(parser.current.type).precedence >= minimumPrecedence) {
        parser.Advance();
        ParseFn infixRule = GetRule(parser.previous.type).infix;
        (this->*infixRule)();
    }
}

}