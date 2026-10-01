#include "lug_compiler.h"
#include "lug_debug.h"

namespace Luggage {

// FunctionUnit methods -----------------------------

FunctionUnit::FunctionUnit(VMCore& vm, Compiler& compiler, Parser& parser) 
: vm(vm)
, compiler(compiler)
, parser(parser)
{
    
}


void FunctionUnit::BeginScope() {
    scopeDepth++;
}


void FunctionUnit::EndScope() {
    /*
        To make sure those local variables don't linger
        even when we exit their scope, we will take out 
        every local that greater or equal scope than the 
        one that we just exited out of.
    */
    std::size_t popped = DiscardLocals(scopeDepth);

    localCount -= popped;
    scopeDepth--;
}


/**
 * Adds a local and returns the slot where the local resides on the
 * vm's stack.
 */
std::size_t FunctionUnit::AddLocal(Token name) {
    if (localCount == UINT8_COUNT) {
        parser.Error("Too many local variables in function.");
    }

    /*
        When we declare a new variable, we start at the end (deepest scope)
        and work backward, looking for an existing variable with the
        same name. If we find one in the current scope...
        
        {
            box a <- 5
            box a <- 8 // Variable with the same name declared in the same scope, error!!!
        }
        
        We throw an error.
    */
    for (std::size_t i = localCount; i > 0; i--) {
        const Local& local = locals[i];
        if (local.initialized && local.depth < scopeDepth) {
            /*
                We know that if we encounter a name with a LOWER
                scope depth, there can't be any name with an equal
                or higher scope depth than the one we're looking for,
                so we break out of the loop.
            */
            break;
        }

        if (name == local.name) {
            parser.Error("Already a variable with this name in this scope.");
        }
    }

    Local& local = locals[localCount++];
    local.name = name;
    local.depth = scopeDepth;

    return localCount - 1;
}


/**
 * Takes in a the name of a local variable, and
 * returns which slot it will be in on the stack.
 * If we can't find it, return -1. 
 */
int FunctionUnit::ResolveLocal(const Token& name) const {
    for (int slot = localCount - 1; slot >= 0; slot--) {
        const Local& local = locals[slot];

        if (local.name == name) {
            /*
                We check to see that the variable we're 
                referencing is already initialized. Initialized in
                this case, meaning that it's been assigned a unique
                starting value.

                For instance, when declaring a new local variable in
                a new scope:

                box a <- 5;

                {
                    box a <- a; <-- here
                }

                This code will not run. When we reach the righthand 'a'
                variable expression, the new 'a' on the left is still
                not initialized with a unique starting value. 

                Checking to see if local.initialized == true will ensure
                this error is caught.
            */

            if (!local.initialized) {
                parser.Error("Can't read local variable in its own initializer.");
            }

            return slot;
        }
    }

    return -1;
}

/**
 * Pops all of the locals from a given max depth.
 * Any scope with a depth higher or equal to than the maxDepth
 * will have its locals discarded.
 * 
 * Returns how many locals were discarded.
 */
std::size_t FunctionUnit::DiscardLocals(std::size_t maxDepth) {
    std::size_t unpopped = localCount;
    while (
        unpopped > 0 
        && locals[unpopped - 1].depth >= maxDepth
    ) {
        /*
        Local variables occupy slots on the stack, so we should
        pop it from the stack when it goes out of scope.
        For every variable, we emit an OP_POP instruction to
        pop it from the stack.
        */
       compiler.EmitByte(OpCode::POP);
       unpopped--;
    }

    return localCount - unpopped;
}


/**
 * Marks the most recent local as initialized.
 */
void FunctionUnit::MarkInitialized() {
    if (scopeDepth == 0) return;

    Local& recentLocal = locals[localCount - 1];
    recentLocal.initialized = true;
}


// Compiler public methods -----------------------------

void Compiler::Compile(Chunk& chunk, const char* source) {
    parser.Reset(source);

    compilingChunk = &chunk;
    FunctionUnit unit{ vm, *this, parser };
    PushUnit(&unit);

    while (!parser.Match(TokenType::END_OF_FILE)) {
        Statement();
    }

    //! Factor into EndCompiler() when the time is right.
    EmitReturn();
#ifdef LUG_DEBUG_PRINT_CODE
    Debug::DisassembleChunk(*CurrentChunk(), "code");
#endif

    PopUnit();

    compilingChunk = nullptr;
}

// Compiler private methods -----------------------------

Chunk* Compiler::CurrentChunk() const {
    return compilingChunk;
}


void Compiler::PushUnit(FunctionUnit* unit) {
    unit->enclosing = currentUnit;
    currentUnit = unit;
}


void Compiler::PopUnit() {
    currentUnit = currentUnit->enclosing;
}


// Bytecode methods -----------------------------------

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
 * Emits a short as two std::uint8_t's.
 */
void Compiler::EmitShort(std::uint16_t byte) {
     CurrentChunk()->WriteShort(byte, parser.previous.line);
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

/**
 * Adds the identifier as an ObjString constant in the 
 * constant pool and returns the index of the constant.
 */
std::uint8_t Compiler::IdentifierConstant(Token& identifier) {
    ObjString* string = ObjString::CopyFrom(vm, identifier.start, identifier.length);
    return MakeConstant(string);
}

/**
 * Takes the next token (presumed identifier) and 
 * parses it as a variable name.
 * 
 * If the variable is a global, this function uses the 
 * identifier to create a new ObjString* constant, in which
 * MakeVariable returns the index to.
 */
std::uint8_t Compiler::ParseVariable(const char* errorMessage) {
    parser.Consume(TokenType::IDENTIFIER, errorMessage);

    DeclareVariable();
    if (currentUnit->scopeDepth > 0 ) return 0; 

    return IdentifierConstant(parser.previous);
}


void Compiler::DeclareVariable() {
    if (currentUnit->scopeDepth == 0) return;

    Token& name = parser.previous;
    currentUnit->AddLocal(name);
}


void Compiler::DefineVariable(std::uint8_t index) {
    if (currentUnit->scopeDepth > 0) {
        currentUnit->MarkInitialized();
        return;
    }

    EmitBytes(OpCode::DEFINE_GLOBAL, index);
}


/**
 * Emits the given JUMP instruction. Then gives
 * the index/offset of the jump instruction in the 
 * bytecode stream for you to use when patching the 
 * jump with PatchJump.
 */
std::size_t Compiler::EmitJump(OpCode::Type instruction) {
    EmitByte(instruction);
    
    /**
     * From the operand, jump THIS many bytes.
     * 
     * Because we don't know how many bytes to jump over, we
     * leave that for us to patch up later. A jump instruction
     * takes two uint8_t's as operands, but they both represent
     * one giant uint16_t that represents how many bytes to
     * jump over in the bytecode stream. 
     * 
     * So we simply use two dummy values (255 for each
     * uint8_t) for us to fill later.
     */
    EmitShort(0xFFFF);

    /** 
     * Subtract -2 to get the index of the JUMP INSTRUCTION.
     * [ OP_JUMP ] [ 2 ] [ 5 ] 
     *             ^^^^^^^^^^^ Short instruction
     *                   ^^^^^ We are here.
     *  ^^^^^^^^^
     * Subtract 2 to be here.
     */
    return CurrentChunk()->byteCount - 2;
}


/**
 * Patches a jump instruction.
 */
void Compiler::PatchJump(std::size_t instructionOffset) {
    /**
     * -2 to adjust for the bytecode for the jump offset itself.
     * 
     * For instance, if the chunk bytecode looked like:
     * [ OP_JUMP ][ 0 ][ 2 ] [ OP_PUSH ][ 3 ][ OP_PRINT ]
     *                   ^ At this point...              ^ We are at this point currently.
     *                     Bytecount : 3                   Bytecount : 6
     *                     Instruction Offset: 3 - 2 = 1
     *   
     *                     In the VM, we will have to jump
     *                     from THIS point. 
     * 
     * Since we only get the instruction offset, here is how
     * we get the number of bytes to jump:
     * byteCount - instructionOffset - 2
     * 
     * This translates to, in our example:
     * 6 - 1 - 2 = 3
     * Which translates to how many bytes we need to jump over in the VM. Recall that
     * the VM consumes the short with READ_SHORT(), and its IP will be pointing to the 
     * byte just past the operand. So we have to adjust for it accordingly.
     */
    std::size_t numBytesToJump = CurrentChunk()->byteCount - instructionOffset - 2;

    if (numBytesToJump > UINT16_MAX) {
        parser.Error("Too much code to jump over.");
    }

    CurrentChunk()->bytecode[instructionOffset] = (numBytesToJump >> 8) & 0xff;
    CurrentChunk()->bytecode[instructionOffset + 1] = (numBytesToJump) & 0xff;
}


/**
 * Emits a loop instruction.
 */
void Compiler::EmitLoop(std::size_t loopStart) {
    EmitByte(OpCode::LOOP);

    // Get the amount of bytes between the current byte and the loop start.
    // the +2 is to account for OP_LOOP's operands, which we also need
    // to jump over.
    std::size_t loopOffset = CurrentChunk()->byteCount - loopStart + 2;
    if (loopOffset > UINT16_MAX) parser.Error("Loop body too large.");

    EmitByte((loopOffset >> 8) & 0xff);
    EmitByte(loopOffset & 0xff);
}

// Loop methods ---------------------------------------------

void Compiler::StartLoop(LoopContext& loop) {
    loop.outerLoop = currentLoop;
    loop.start = CurrentChunk()->byteCount;

    /*
        Be careful! loop.scopeDepth is the depth of the scope
        right before the loop starts. For instance, this code:

        do
            box a <- 0
            while a < 10 do
                ... // <- depth = 1
            end
        end

        ...will have the while loop's scopeDepth be 1, and not 2,
        because usually we start the loop before we actually push 
        a new scope. 

        Recall WhileStatement(), where we first StartLoop() and THEN
        call Block(), which creates a new scope. If you want to discard
        locals on the while loop with DiscardLocals(), you need to 
        call DiscardLocals(loop.scopeDepth + 1) to discard the locals 
        inside the loop, and prevent accidental pops of the outer scope's locals
    */
    loop.scopeDepth = currentUnit->scopeDepth;

    currentLoop = &loop;
}


void Compiler::EndLoop() {
    EmitLoop(currentLoop->start);
    PatchJump(currentLoop->exitJump);

    /*
        We will pop the value from the condition expression.
        When we don't enter the loop the first time
        we will end up here.

        However, all of the other break statements don't
        need to pop the condition expressions since they already
        popped it off the stack in the loop body. It actually
        jumps to the byte just after OP_POP.
    */
    EmitByte(OpCode::POP);
    PatchPlaceholders(currentLoop->breakHead, OpCode::JUMP);
    currentLoop = currentLoop->outerLoop;
}


/**
 * Patches all the break statements that emitted an OP_PLACEHOLDER
 * with actual OP_JUMP instructions that jump out of the loop.
 */
void Compiler::PatchPlaceholders(std::size_t firstPlaceholder, OpCode::Type instruction) {
    if (firstPlaceholder == UINT16_MAX) return;
    
    Chunk& chunk = *CurrentChunk();
    std::size_t to = CurrentChunk()->byteCount - 1;
    std::size_t currentPlaceholder = firstPlaceholder;


    /*
        Every PATCH_PLACEHOLDER holds an operand of where
        the NEXT placeholder is relative to the current patch
        we're reading. 

        e.g. the bytecode stream might look like:

        PATCH NO_BREAK  // There is no placeholder after this one. 
        .
        . 
        PATCH 3         // 3 bytes before this, there is another patch placeholder.
        . 
        .
        .
        .
        .
        PATCH 6         // 6 bytes before this, there is another patch placeholder.

        We will go from the bottom patch up to the top, filling their operand with
        the correct number of bytes to skip to exit the loop. We will then use the old
        operand to find out where the next patch is in the list.
    */
    while (true) {
        // The operand of the PATCH_PLACEHOLDER opcode is the number of bytes to
        // jump backwards to find the next patch placeholder to patch up.
        // Once we get that, we can replace it with the actual number of bytes
        // to jump to exit the loop.

        // Get the operand of the break.
        std::uint16_t operand = ReadShortOperand(currentPlaceholder);

        // Patch the placeholder with the actual number of bytes it needs
        // to exit the loop.
        // -2 to account for the 2-byte operand of OP_PLACEHOLDER.
        std::uint16_t numBytesToJump = to - currentPlaceholder - 2;
        chunk.bytecode[currentPlaceholder] = instruction;
        WriteShortOperand(currentPlaceholder, numBytesToJump);
        
        // Check if we have reached the end of the 'break' linked list.
        // We know if we did if the operand carries the sentinel value of
        // END_OF_LIST.
        if (operand == LoopContext::END_OF_LIST) {
            break;
        }

        // The past operand was how many bytes the next break statement was
        // relative to the current one we're on. 
        int nextPlaceholder = currentPlaceholder - operand;
        ASSERT(nextPlaceholder > 0, "Next-break index in the patch loop is negative.");
        currentPlaceholder = nextPlaceholder;

        bool isPlaceholder = chunk.bytecode[nextPlaceholder] == OpCode::PLACEHOLDER;
        ASSERT(isPlaceholder, "OP_PLACEHOLDER index led to a non-placeholder opcode.");
    }
}


std::uint16_t Compiler::ReadShortOperand(std::size_t instructionOffset) {
    std::uint16_t operand = static_cast<std::uint16_t>(CurrentChunk()->bytecode[instructionOffset + 1] << 8);
    operand |= CurrentChunk()->bytecode[instructionOffset + 2];

    return operand;
}


void Compiler::WriteShortOperand(std::size_t instructionOffset, std::uint16_t value) {
    CurrentChunk()->bytecode[instructionOffset + 1] = (value >> 8) & 0xff;
    CurrentChunk()->bytecode[instructionOffset + 2] = (value) & 0xff;
}


void Compiler::AddPlaceholder(std::size_t* placeholderListHead) {
    EmitByte(OpCode::PLACEHOLDER);

    // The index of the placeholder in the chunk's bytecode.
    // We subtract 1 because indices start at 0, not 1.
    std::size_t newBreak = CurrentChunk()->byteCount - 1;

    // If this is the first 'break' in the break statement linked list...
    if (*placeholderListHead == UINT16_MAX) {
        EmitShort(UINT16_MAX);

    } else {
        // We will have to get the number of bytes BACKWARDS the last
        // break OP_JUMP statement is in the bytecode stream.
        std::size_t lastBreak = *placeholderListHead;
        EmitShort(CurrentChunk()->byteCount - lastBreak - 1);
    }

    *placeholderListHead = newBreak;
}



// Statement methods ------------------------------------------

/**
 * Parses a statement. Returns 'true' if the statement is the
 * last statement of a block. For instance, this function
 * would return 'true' if it parses a 'return' statement, because
 * anything after a 'return' statement is disregarded.
 */
bool Compiler::Statement() {
    bool lastStatement = false;

    if (parser.Match(TokenType::PRINT)) {
        PrintStatement();    
    } 

    else if (parser.Match(TokenType::BOX)) {
        BoxStatement();
    }

    else if (parser.Match(TokenType::DO)) {
        currentUnit->BeginScope();
        Block();
        currentUnit->EndScope();
        parser.Consume(TokenType::END, "Expected 'end' after block.");
    }

    else if (parser.Match(TokenType::IF)) {
        IfStatement();
    }

    else if (parser.Match(TokenType::WHILE)) {
        WhileStatement();
    }

    else if (parser.Match(TokenType::MATCH)) {
        MatchStatement();
    }

    else if (parser.Match(TokenType::COUNT)) {
        CountStatement();
    }

    else if (parser.Match(TokenType::BREAK)) {
        BreakStatement();
        lastStatement = true;
    }
    
    else if (parser.Match(TokenType::CONTINUE)) {
        ContinueStatement();
        lastStatement = true;
    }

    else {
        ExpressionStatement();
    }

    // Optional leading semicolons :)
    do {} while (parser.Match(TokenType::SEMICOLON));

    return lastStatement;
}


void Compiler::PrintStatement() {
    Expression();
    EmitByte(OpCode::PRINT);
}


void Compiler::BoxStatement() {
    std::uint8_t global = ParseVariable("Expected variable name.");

    if (parser.Match(TokenType::LEFT_ARROW)) {
        Expression();
    // box a
    } else {
        EmitByte(OpCode::NIL);
    }

    DefineVariable(global);
}


void Compiler::Block() {
    currentUnit->BeginScope();

    while (!parser.Check(TokenType::END, TokenType::ELSEIF, TokenType::ELSE)) {
        // If we reach a 'return' or 'break', we know that it's
        // the last statement of the block.
        bool isLastStatement = Statement();
        if (isLastStatement) break;
    }

    currentUnit->EndScope();
}


void Compiler::IfStatement() {
    Expression(); // Condition
    parser.Consume(TokenType::THEN, "Expected 'then' after condition.");

    // Then clause
    std::size_t jumpOverThen = EmitJump(OpCode::JUMP_IF_FALSE);
    EmitByte(OpCode::POP); // Pop the value of the condition.
    Block();

    // Once the 'then' block runs, jump past any elseif/else branches.
    std::size_t jumpToEnd = EmitJump(OpCode::JUMP);

    // Else clause
    PatchJump(jumpOverThen);
    EmitByte(OpCode::POP);
    
    if (parser.Match(TokenType::ELSEIF)) {
        /*
            "elseif ..." is really just syntactic sugar for parsing a 
            nested if-statement. It will parse its own condition/then/elseif/else 
            and consume the trailing "end" itself.

            This:

            if ... then 
                 ... 
            elseif ... then
                 ...
            end

            desugars to:

            if ... then

            else
                 if ... then

                 end
            <-- We don't need an end here since the inner 'if' already handles it for us.
        */

        // We have reached the end of the if statement. The last call 
        // to IfStatement() already takes care of eating the last 'end'.
        IfStatement();
        PatchJump(jumpToEnd);
        return;

    }  else if (parser.Match(TokenType::ELSE)) {
        Block();
    }

    PatchJump(jumpToEnd);
    parser.Consume(TokenType::END, "Expected 'end' after if statement.");
}


void Compiler::WhileStatement() {
    LoopContext loop;
    StartLoop(loop);

    // Loop condition
    Expression();
    loop.exitJump = EmitJump(OpCode::JUMP_IF_FALSE);
    parser.Consume(TokenType::DO, "Expected 'do' after while condition.");

    // Inside loop
    EmitByte(OpCode::POP);
    Block();
    parser.Consume(TokenType::END, "Expected 'end' after while loop.");

    EndLoop();
}


void Compiler::MatchStatement() {
    std::size_t caseHead{ UINT16_MAX };
    int currentCaseJump{ -1 };

    // match <expression> ... end
    Expression();

    while (parser.Match(TokenType::CASE)) {
        if (currentCaseJump != -1) {
            PatchJump(currentCaseJump);
            EmitByte(OpCode::POP);
        }
        
        // case <expression> ... end
        EmitByte(OpCode::DUPLICATE);
        Expression();
        EmitByte(OpCode::EQUAL);
        currentCaseJump = EmitJump(OpCode::JUMP_IF_FALSE);

        EmitByte(OpCode::POP);

        Block();
        parser.Consume(TokenType::END, "Expected 'end' after case body.");

        AddPlaceholder(&caseHead);
    }

    parser.Consume(TokenType::END, "Expected 'end' after match statement.");

    if (currentCaseJump != -1) {
        PatchJump(currentCaseJump);
        EmitByte(OpCode::POP);
    }

    PatchPlaceholders(caseHead, OpCode::JUMP);

    EmitByte(OpCode::POP);
}


void Compiler::CountStatement() {
    // count <identifier> from <expression> to <expression [by <expression>] do ... end
    LoopContext loop;

    currentUnit->BeginScope();

    // Parse the identifier as a new local.
    ParseVariable("Expected variable name.");
    DefineVariable(0);

    int counter = currentUnit->ResolveLocal(parser.previous);
    ASSERT(counter != -1, "Counter's slot is -1 and cannot be found");

    // Start --------------------------
    parser.Consume(TokenType::FROM, "Expected 'from' after incrementer name.");
    Expression();
    EmitBytes(OpCode::SET_LOCAL, counter);

    // Condition --------------------------
    parser.Consume(TokenType::TO, "Expected 'to' after start value.");
    Expression();
    std::size_t condition = currentUnit->AddLocal(Token::Synthetic(" condition"));

    // Step --------------------------
    if (parser.Match(TokenType::BY)) {
        Expression();
    } else {
        EmitConstant(1);
    }

    std::size_t step = currentUnit->AddLocal(Token::Synthetic(" step"));
    
    // Actual loop body --------------------------
    
    // The loop starts here.
    StartLoop(loop);

    // Check the condition.
    EmitBytes(OpCode::GET_LOCAL, step);
    EmitConstant(0);
    EmitByte(OpCode::GREATER);
    std::size_t jumpToNeg = EmitJump(OpCode::JUMP_IF_FALSE);

    // If the step is positive, we will check <= to see if the loop should keep running.
    EmitByte(OpCode::POP);
    EmitBytes(OpCode::GET_LOCAL, counter);
    EmitBytes(OpCode::GET_LOCAL, condition);
    EmitByte(OpCode::LESS_EQUAL);
    std::size_t jumpToCheck = EmitJump(OpCode::JUMP);

    // Otherwise, we will check >= to see if the loop should keep running.
    PatchJump(jumpToNeg);
    EmitByte(OpCode::POP);
    EmitBytes(OpCode::GET_LOCAL, counter);
    EmitBytes(OpCode::GET_LOCAL, condition);
    EmitByte(OpCode::GREATER_EQUAL);
    
    // Now we'll check.
    PatchJump(jumpToCheck);
    loop.exitJump = EmitJump(OpCode::JUMP_IF_FALSE);
    EmitByte(OpCode::POP);

    parser.Consume(TokenType::DO, "Expected 'do' after count clauses.");
    Block();
    parser.Consume(TokenType::END, "Expected 'end' after loop body.");

    // Increment the counter
    EmitBytes(OpCode::GET_LOCAL, counter);
    EmitBytes(OpCode::GET_LOCAL, step);
    EmitByte(OpCode::ADD);
    EmitBytes(OpCode::SET_LOCAL, counter);
    EmitByte(OpCode::POP);

    EndLoop();

    currentUnit->EndScope();
}


void Compiler::BreakStatement() {
    if (currentLoop == nullptr) {
        parser.Error("Can't use 'break' outside of a loop.");
    }

    // Discard any locals currently inside the loop.
    currentUnit->DiscardLocals(currentLoop->scopeDepth + 1);
    AddPlaceholder(&currentLoop->breakHead);
}


void Compiler::ContinueStatement() {
    if (currentLoop == nullptr) {
        parser.Error("Can't use 'continue' outside of a loop.");
    }

    /*
        To continue, we need to discard any locals that are in the
        current scope of the loop.
    */
    currentUnit->DiscardLocals(currentLoop->scopeDepth);
    EmitLoop(currentLoop->start);
}


void Compiler::ExpressionStatement() {
    isValidExprStmt = false;
    Expression();

    if (!isValidExprStmt) {
        parser.Error("Expected a statement.");
    }

    EmitByte(OpCode::POP);
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
    { nullptr,                  &Compiler::Binary,      Precedence::EQUALITY },         // BANG_EQUAL
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // GREATER
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // GREATER_EQUAL
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // LESS
    { nullptr,                  &Compiler::Binary,      Precedence::COMPARISON },       // LESS_EQUAL
    { nullptr,                  nullptr,                Precedence::NONE },             // LEFT_ARROW

    // Literals         
    { &Compiler::Variable,      nullptr,                Precedence::NONE },             // IDENTIFIER
    { &Compiler::String,        nullptr,                Precedence::NONE },             // STRING
    { &Compiler::Number,        nullptr,                Precedence::NONE },             // NUMBER

    // Keywords         
    { nullptr,                  nullptr,                Precedence::NONE },             // AND
    { nullptr,                  nullptr,                Precedence::NONE },             // BOX
    { nullptr,                  nullptr,                Precedence::NONE },             // BREAK
    { nullptr,                  nullptr,                Precedence::NONE },             // BY
    { nullptr,                  nullptr,                Precedence::NONE },             // CASE
    { nullptr,                  nullptr,                Precedence::NONE },             // CONTINUE
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
    { nullptr,                  nullptr,                Precedence::NONE },             // MATCH
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

/**
 * Returns the associated ParseRule of the given token type.
 */
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
void Compiler::Grouping(bool canAssign) {
    Expression();
    parser.Consume(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
}


/**
 * Parses a unary expression and emits the corresponding bytecode. 
 */
void Compiler::Unary(bool canAssign) {
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
void Compiler::Binary(bool canAssign) {
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


/**
 * Emits a number into the bytecode stream.
 */
void Compiler::Number(bool canAssign) {
    double value = std::strtod(parser.previous.start, nullptr);
    EmitConstant(value);
}


/**
 * Emits a string into the bytecode stream.
 */
void Compiler::String(bool canAssign) {
    ObjString* stringObject = ObjString::CopyFrom(
        vm,
        parser.previous.start + 1, // Start right after the first quote
        parser.previous.length - 2 // Account for the two quotes from beginning to end   
    );

    EmitConstant(stringObject);
}


/**
 * Emits a literal (true, false, nil)
 */
void Compiler::Literal(bool canAssign) {
    TokenType literalType = parser.previous.type;

    switch (literalType) {
        case TokenType::TRUE: EmitByte(OpCode::TRUE); break;
        case TokenType::FALSE: EmitByte(OpCode::FALSE); break;
        case TokenType::NIL: EmitByte(OpCode::NIL); break;
        
        default: UNREACHABLE();
    }
}


/**
 * Handles a variable expression
 */
void Compiler::Variable(bool canAssign) {
    OpCode::Type getOp, setOp;
    Token& name = parser.previous;

    /*
        If 'name' is a local variable, arg will be the slot index
        in the VM's stack in which the local variable is stored.

        If 'name' is a global variable, arg will instead be the
        string constant of the variable identifier. Recall that in the VM, 
        we use an ObjString* constant to look up global variables in the
        VM's globals hashmap, with the constant as the key. 
    */
    int arg = currentUnit->ResolveLocal(name);

    // We were able to successfully resolve the variable name.
    if (arg != FunctionUnit::NOT_FOUND) {
        getOp = OpCode::GET_LOCAL;
        setOp = OpCode::SET_LOCAL;

    // This probably means that the variable is a global variable.
    } else {
        arg = IdentifierConstant(parser.previous);
        getOp = OpCode::GET_GLOBAL;
        setOp = OpCode::SET_GLOBAL;
    }
    
    if (canAssign && parser.Match(TokenType::LEFT_ARROW)) {
        Expression();
        EmitBytes(setOp, arg);
        isValidExprStmt = true;
    } else {
        EmitBytes(getOp, arg);
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

    bool canAssign = minimumPrecedence <= Precedence::ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (GetRule(parser.current.type).precedence >= minimumPrecedence) {
        parser.Advance();
        ParseFn infixRule = GetRule(parser.previous.type).infix;
        (this->*infixRule)(canAssign);
    }

    if (canAssign && parser.Match(TokenType::LEFT_ARROW)) {
        parser.Error("Invalid assignment target.");
    }
}

}