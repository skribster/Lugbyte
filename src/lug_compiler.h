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
    
// Forward declarations so C++ doesn't get mad at me
class Compiler; 

struct Local {
	bool initialized{ false };
	Token name;
	std::size_t depth{ 0 };
};


class FunctionUnit {
public:
    FunctionUnit(VMCore& vm, Compiler& compiler, Parser& parser);

    void BeginScope();
    void EndScope();
    std::size_t AddLocal(Token name);
    std::size_t DiscardLocals(std::size_t maxDepth);
    void MarkInitialized();

    static constexpr int NOT_FOUND{ -1 };
    int ResolveLocal(const Token& name) const;
    
    Local locals[UINT8_COUNT]; 
    std::size_t localCount{ 0 };
    std::size_t scopeDepth{ 0 };
    FunctionUnit* enclosing{ nullptr };

private:
    VMCore& vm;
    Compiler& compiler;
    Parser& parser;
};


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


using ParseFn = void (Compiler::*)(bool canAssign);
struct ParseRule {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
};


// Bookkeeping information for the current loop being compiled.
struct LoopContext {
	// Sentinel value for the end of the linked list, where there is no other break statement to patch.
	static constexpr std::size_t END_OF_LIST{ UINT16_MAX };

	// Index of the instruction that the loop should jump back to.
	std::size_t start{ 0 };

	// Index of the argument for the JUMP_IF instruction used to exit the
  	// loop. Stored so we can patch it once we know where the loop ends.
	std::size_t exitJump{ 0 };
	
	// Index of the head of the break statement linked list.
	std::size_t breakHead{ END_OF_LIST };

	// Depth of the scope(s) that need to be exited if a break is hit inside the
  	// loop
	std::size_t scopeDepth{ 0 };

	// Index of the first instruction of the body of the loop.
	LoopContext* outerLoop{ nullptr };
};


class Compiler {
public:
    explicit Compiler(VMCore& vm) : vm(vm) {}

    void Compile(Chunk& chunk, const char* source);

private:
    Chunk* compilingChunk{ nullptr };
    FunctionUnit* currentUnit{ nullptr };
    LoopContext* currentLoop{ nullptr };
    Parser parser;
    VMCore& vm;
    bool isValidExprStmt{ false };

    Chunk* CurrentChunk() const;
    void PushUnit(FunctionUnit* unit);
    void PopUnit();
    
    // Bytecode methods

    void EmitByte(std::uint8_t byte);
    void EmitBytes(std::uint8_t a, std::uint8_t b);
    void EmitShort(std::uint16_t byte);
    void EmitReturn();
    void EmitConstant(Value value);
    std::uint8_t MakeConstant(Value value);
    std::uint8_t IdentifierConstant(Token& identifier);

    std::size_t EmitJump(OpCode::Type instruction);
    void PatchJump(std::size_t instructionOffset);
    void EmitLoop(std::size_t loopStart);

    void StartLoop(LoopContext& loop);
    void EndLoop();
    void PatchPlaceholders(std::size_t firstPlaceholder, OpCode::Type instruction);
    void AddPlaceholder(std::size_t* placeholderListHead);
    std::uint16_t ReadShortOperand(std::size_t instructionOffset);
    void Compiler::WriteShortOperand(std::size_t instructionOffset, std::uint16_t value);

    std::uint8_t ParseVariable(const char* errorMessage);
    void DeclareVariable();
    void DefineVariable(std::uint8_t index);

    // Parse methods

    static const ParseRule rules[];
    const ParseRule& GetRule(TokenType type) const;

    // Statement methods

    bool Statement();
    void PrintStatement();
    void BoxStatement();
    void Block();
    void IfStatement();
    void WhileStatement();
    void BreakStatement();
    void MatchStatement();
    void CountStatement();
    void ContinueStatement();
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

    friend class FunctionUnit;
};

}