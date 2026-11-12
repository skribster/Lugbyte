#include "lug_compiler.h"
#include "lug_debug.h"
#include "lug_vm.h"

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
    scopeDepth--;
    std::size_t popped = DiscardLocals(scopeDepth);
    localCount -= popped;
}


/**
 * Adds a local and returns the slot where the local resides on the
 * vm's stack.
 */
std::size_t FunctionUnit::AddLocal(Token name) {
    if (localCount == UINT8_COUNT) {
        parser.Error("too many local variables in function");
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
    for (int i = localCount - 1; i >= 0; i--) {
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

        if (local.name == name) {
            parser.Error("already defined in this scope");
        }
    }

    Local& local = locals[localCount++];
    local.name = name;
    local.depth = scopeDepth;

    // Return the local's slot index.
    return localCount - 1;
}


void FunctionUnit::AddHiddenLocal(const char* name) {
    Variable variable;
    variable.localSlot = AddLocal(Token::Synthetic(name));;
    variable.isLocal = true;

    compiler.DefineVariable(variable);
}


std::size_t FunctionUnit::AddUpvalue(std::uint8_t slotIndex, bool isLocal) {
    std::size_t upvalueCount = function->upvalueCount;

    /*
        We'll check whether if we already have an upvalue pointing
        to the given slot index. If so, we don't need to waste memory
        making another upvalue that points to the same slot. Instead,
        we can just reuse it.

        Loop over the upvalue array and return the first item that
        shares the same slot index.
    */
    for (std::size_t i = 0; i < upvalueCount; i++) {
        Upvalue& upvalue = upvalues[i];
        if (upvalue.index == slotIndex && upvalue.isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        parser.Error("too many closure variables in function");
        return 0;
    }
    
    upvalues[upvalueCount].isLocal = isLocal;
    upvalues[upvalueCount].index = slotIndex;

    return function->upvalueCount++;
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
                parser.Error("can't read '%.*s' in its own initializer", parser.previous.length, parser.previous.start);
            }

            return slot;
        }
    }

    return -1;
}


/**
 * Tries to look for a local value in the surrounding 
 * function. Returns the upvalue index for the variable
 * if found, returns -1 otherwise.
 */
int FunctionUnit::ResolveUpvalue(const Token& name) {
    if (enclosing == nullptr) return -1;

    // We will try and find the local inside the enclosing
    // function. enclosing->ResolveLocal will return the local's
    // slot number relative to the *enclosing's* callframe. 
    int local = enclosing->ResolveLocal(name);
    if (local != -1) {
        enclosing->locals[local].isCaptured = true;
        return AddUpvalue(static_cast<std::uint8_t>(local), true);
    }

    // By calling this function recursively, we can work our
    // way through the linked list of function units until we
    // reach one of our base cases: finding an actual local,
    // or reaching the end of the list.
    int upvalue = enclosing->ResolveUpvalue(name);
    if (upvalue != -1) {
        return AddUpvalue(static_cast<std::uint8_t>(upvalue), false);
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
        && locals[unpopped - 1].depth > maxDepth
    ) {
        /*
            Local variables occupy slots on the stack, so we should
            pop it from the stack when it goes out of scope.

            For every variable, we emit an OP_POP instruction to
            pop it from the stack.

            If said local has been captured by an inner function, however,
            we'd like to signal to the VM to close over it.
        */
        if (locals[unpopped - 1].isCaptured) {
            compiler.EmitByte(OpCode::CLOSE_UPVALUE);
        } else {
            compiler.EmitByte(OpCode::POP);
        }
        unpopped--;
    }

    return localCount - unpopped;
}


/**
 * Marks the local in the given slot index as initialized.
 * Initialized means that it is given a value.
 */
void FunctionUnit::MarkInitialized(std::size_t slot) {
    if (scopeDepth == 0) return;

    Local& local = locals[slot];
    local.initialized = true;
}


// Compiler public methods -----------------------------

ObjModule* Compiler::Compile(const char* source, std::filesystem::path modulePath) {
    parser.Reset(source, modulePath);
    currentUnit = nullptr;
    currentLoop = nullptr;
    pendingName = Token::Synthetic("anonymous");
    currentScriptPath = modulePath;

    FunctionUnit unit{ vm, *this, parser };
    PushUnit(&unit, FunctionType::SCRIPT);

    while (!parser.Match(TokenType::END_OF_FILE)) {
        Statement();
    }

    ObjFunction* function = PopUnit();
    
    // ObjModule::New may trigger a collection, protect function* from being collected
    vm.stack.Push(function);
    ObjModule* module = ObjModule::New(vm, function, modulePath);
    vm.stack.Pop(); // function

    return module;
}


void Compiler::MarkRoots() {
    FunctionUnit* unit = currentUnit;
    while (unit != nullptr) {
        ObjFunction* function = unit->function;
        vm.gc.MarkObject(unit->function);
        unit = unit->enclosing;
    }
}

// Compiler private methods -----------------------------

Chunk* Compiler::CurrentChunk() const {
    return &currentUnit->function->chunk;
}


void Compiler::PushUnit(FunctionUnit* unit, FunctionType type) {
    unit->function = ObjFunction::New(vm);

    // Register the unit into the function linked list 
    // BEFORE we allocate the function name, otherwise if
    // ObjString::FromSource leads to an allocation, the ObjFunction
    // of this unit will be freed because it's unable to be marked
    // if it's not in the linked list.
    unit->enclosing = currentUnit;
    currentUnit = unit;
    
    if (type != FunctionType::SCRIPT) {
        unit->function->name = ObjString::FromSource(vm, pendingName.start, pendingName.length);
    }

    /*
        The first slot of the unit will be reserved for storing
        the ObjFunction* on the stack. We add a hidden local to 
        reserve an empty slot there.
    */
    unit->AddLocal(Token::Synthetic("$fn slot"));
}


ObjFunction* Compiler::PopUnit() {
    EmitReturn();

    ObjFunction* function = currentUnit->function;
#if LUG_DEBUG_PRINT_CODE
    Debug::DisassembleChunk(
        *CurrentChunk(), 
        function->name != nullptr 
        ? function->name->characters 
        : "script"
    );
#endif

    // Remove the unit from the unit linked list
    currentUnit = currentUnit->enclosing;
    return function;
}


// Bytecode methods -----------------------------------
/**
 * Emits a byte into the current chunk's bytecode array
 */
void Compiler::EmitByte(std::uint8_t byte) {
    CurrentChunk()->Write(byte, parser.previous);
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
     CurrentChunk()->WriteShort(byte, parser.previous);
}


/**
 * Emits a return instruction into
 * the current chunk's bytecode array
 */
void Compiler::EmitReturn() {
    EmitByte(OpCode::NIL);
    EmitByte(OpCode::RETURN);
}


/**
 * Emits a constant instruction of
 * OP_CONSTANT [constantIndex],
 * where constantIndex is the index of
 * the constant in the constant pool
 */
void Compiler::EmitConstant(Value value) {
    CurrentChunk()->WriteConstant(value, parser.previous);
}


/**
 * Emits an opcode with a possible short-able operand.
 * What that means is that the operand, if bigger than
 * UINT8_MAX will be prefixed with a SHORT instruction,
 * so that it will be read as a short during runtime.
 * For instance, when passing in OP_CONSTANT 260,
 * this function will emit:
 * 
 * OP_SHORT
 * OP_CONSTANT
 * 255
 * 5
 */
void Compiler::EmitShortable(OpCode::Type op, std::uint16_t index) {
    if (index > UINT8_MAX) {
        EmitByte(OpCode::SHORT);
        EmitByte(op);
        EmitShort(index);
    } else {
        EmitBytes(op, static_cast<std::uint8_t>(index));
    }
}

/**
 * Adds the value in the constant pool and
 * returns the index of the constant.
 */
Chunk::ConstantIndex Compiler::MakeConstant(Value value) {
    // Since the OP_CONSTANT instruction
    // can only store a 8-bit index operand, we need to
    // make sure the indices don't exceed that limit. 
    std::size_t constant = CurrentChunk()->AddConstant(value);
    if (constant > UINT16_MAX) {
        parser.Error("too many constants in one chunk");
        return 0;
    }

    return static_cast<std::uint16_t>(constant);
}                                                                                                

/**
 * Adds the identifier as an ObjString constant in the 
 * constant pool and returns the index of the constant.
 */
Chunk::ConstantIndex Compiler::IdentifierConstant(Token& identifier) {
    ObjString* string = ObjString::FromSource(vm, identifier.start, identifier.length);
    return MakeConstant(string);
}


void Compiler::EmitIdentifier(Token& identifier) {
    ObjString* string = ObjString::FromSource(vm, identifier.start, identifier.length);
    EmitConstant(string);
}

/**
 * Takes the next token (presumed identifier) and 
 * parses it as a variable name.
 * 
 * If the variable is a global, this function uses the 
 * identifier to create a new ObjString* constant, in which
 * MakeVariable returns the constant index to.
 * 
 * If the variable is a local, it returns the slot index
 * in which the local variable resides.
 */
Variable Compiler::ParseVariable(const char* errorMessage) {
    parser.Consume(TokenType::IDENTIFIER, errorMessage);
    Variable variable;

    bool isLocal = currentUnit->scopeDepth > 0;
    if (isLocal) {
        std::uint16_t localSlot = DeclareVariable();
        variable.localSlot = localSlot;
        variable.isLocal = true;
        
    } else {
        Chunk::ConstantIndex identifierIndex = IdentifierConstant(parser.previous);
        variable.isLocal = false;
        variable.globalIndex = identifierIndex;
    }

    return variable;
}


/**
 * Declares the parser's previous token (presumed identifier)
 * as a variable. For any local variable, that means
 * adding it to the current unit's locals array, and returning
 * the slot index the local is in.
 * 
 * For global variables, 'declaring' a variable is already handled 
 * in bytecode via OpCode::DEFINE_GLOBAL, so this does nothing.
 */
std::size_t Compiler::DeclareVariable() {
    if (currentUnit->scopeDepth == 0) return 0;

    Token& name = parser.previous;
    return currentUnit->AddLocal(name);
}


/**
 * Defines a given variable. 
 * 
 * If this variable is a global, it expects the variable's constant index as an input.
 * If this variable is a local, it expects the local's slot number as an input.
 */
void Compiler::DefineVariable(Variable variable) {
    if (variable.isLocal) {
        currentUnit->MarkInitialized(variable.localSlot);
        return;
    } else {
        EmitShortable(OpCode::DEFINE_GLOBAL, variable.globalIndex);
    }
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
    return CurrentChunk()->bytecode.Count() - 2;
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
    std::size_t numBytesToJump = CurrentChunk()->bytecode.Count() - instructionOffset - 2;
    if (numBytesToJump > UINT16_MAX) {
        parser.Error("too much code to jump over");
    }

    CurrentChunk()->bytecode[instructionOffset] = (numBytesToJump >> 8) & 0xff;
    CurrentChunk()->bytecode[instructionOffset + 1] = (numBytesToJump) & 0xff;
}


/**
 * Emits a loop instruction.
 */
void Compiler::EmitLoop(OpCode::Type loopInstruction, std::size_t loopStart) {
    EmitByte(loopInstruction);

    // Get the amount of bytes between the current byte and the loop start.
    // the +2 is to account for OP_LOOP's operands, which we also need
    // to jump over.
    std::size_t loopOffset = CurrentChunk()->bytecode.Count() - loopStart + 2;
    if (loopOffset > UINT16_MAX) parser.Error("loop body too large");

    EmitByte((loopOffset >> 8) & 0xff);
    EmitByte(loopOffset & 0xff);
}

// Loop methods ---------------------------------------------

void Compiler::StartLoop(LoopContext& loop) {
    loop.outerLoop = currentLoop;
    loop.start = CurrentChunk()->bytecode.Count();

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
    EmitLoop(currentLoop->loopInstruction, currentLoop->start);
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
    PatchPlaceholders(currentLoop->breakHead, OpCode::JUMP);
    currentLoop = currentLoop->outerLoop;
}


/**
 * Creates a new placeholder list.
 */
std::size_t Compiler::PlaceholderList() {
    return UINT16_MAX;
}


/**
 * Patches all the break statements that emitted an OP_PLACEHOLDER
 * with actual OP_JUMP instructions that jump out of the loop.
 */
void Compiler::PatchPlaceholders(std::size_t firstPlaceholder, OpCode::Type instruction) {
    if (firstPlaceholder == UINT16_MAX) return;
    
    Chunk& chunk = *CurrentChunk();
    std::size_t to = CurrentChunk()->bytecode.Count() - 1;
    std::size_t currentPlaceholder = firstPlaceholder;


    /*
        Every PATCH_PLACEHOLDER holds an operand of where
        the NEXT placeholder is relative to the current patch
        we're reading. 

        e.g. the bytecode stream might look like:

        PLACEHOLDER UINT16_MAX      // There is no placeholder after this one. 
        .
        . 
        PLACEHOLDER 3               // 3 bytes before this, there is another patch placeholder.
        . 
        .
        .
        .
        .
        PLACEHOLDER 6               // 6 bytes before this, there is another patch placeholder.

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
        ASSERT(nextPlaceholder > 0, "next-break index in the patch loop is negative");
        currentPlaceholder = nextPlaceholder;

        bool isPlaceholder = chunk.bytecode[nextPlaceholder] == OpCode::PLACEHOLDER;
        ASSERT(isPlaceholder, "op_placeholder index led to a non-placeholder opcode");
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
    std::size_t newBreak = CurrentChunk()->bytecode.Count() - 1;

    // If this is the first 'break' in the break statement linked list...
    if (*placeholderListHead == UINT16_MAX) {
        EmitShort(UINT16_MAX);

    } else {
        // We will have to get the number of bytes BACKWARDS the last
        // break OP_JUMP statement is in the bytecode stream.
        std::size_t lastBreak = *placeholderListHead;
        EmitShort(CurrentChunk()->bytecode.Count() - lastBreak - 1);
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

    if (parser.Match(TokenType::BOX)) {
        BoxStatement();
    }

    else if (parser.Match(TokenType::DO)) {
        currentUnit->BeginScope();
        Block();
        currentUnit->EndScope();
        parser.Consume(TokenType::END, "expected 'end' after block");
    }

    else if (parser.Match(TokenType::IF)) {
        IfStatement();
    }

    else if (parser.Match(TokenType::WHILE)) {
        WhileStatement();
    }

    else if (parser.Match(TokenType::INSPECT)) {
        InspectStatement();
    }

    else if (parser.Match(TokenType::COUNT)) {
        CountStatement();
    }

    else if (parser.Match(TokenType::FOREACH)) {
        ForeachStatement();
    }

    else if (parser.Match(TokenType::BREAK)) {
        BreakStatement();
        lastStatement = true;
    }
    
    else if (parser.Match(TokenType::CONTINUE)) {
        ContinueStatement();
        lastStatement = true;
    }

    else if (parser.Match(TokenType::RETURN)) {
        ReturnStatement();
        lastStatement = true;
    }

    else {
        ExpressionStatement();
    }

    // Optional leading semicolons :)
    do {} while (parser.Match(TokenType::SEMICOLON));

    return lastStatement;
}


void Compiler::BoxStatement() {
    Variable variable = ParseVariable("expected variable name");
    pendingName = parser.previous;

    // box a <- 5
    if (parser.Match(TokenType::LEFT_ARROW)) {
        StoredExpression();
    // box a
    } else {
        EmitByte(OpCode::NIL);
    }

    DefineVariable(variable);
    pendingName = Token::Synthetic("anonymous");
}


void Compiler::Block() {
    while (!parser.Check(TokenType::END, TokenType::ELSEIF, TokenType::ELSE)) {
        // If we reach a 'return' or 'break', we know that it's
        // the last statement of the block.
        bool isLastStatement = Statement();
        if (isLastStatement) break;
    }
}


void Compiler::IfStatement() {
    Expression(); // Condition
    parser.Consume(TokenType::THEN, "expected 'then' after condition");

    // Then clause
    std::size_t jumpOverThen = EmitJump(OpCode::POP_JUMP_IF_FALSE);
    Block();

    // Once the 'then' block runs, jump past any elseif/else branches.
    std::size_t jumpToEnd = EmitJump(OpCode::JUMP);

    // Else clause
    PatchJump(jumpOverThen);
    
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
        currentUnit->BeginScope();
        Block();
        currentUnit->EndScope();
    }

    PatchJump(jumpToEnd);
    parser.Consume(TokenType::END, "expected 'end' after if statement");
}


void Compiler::WhileStatement() {
    LoopContext loop(OpCode::LOOP);
    StartLoop(loop);

    // Loop condition
    Expression();
    loop.exitJump = EmitJump(OpCode::POP_JUMP_IF_FALSE);
    parser.Consume(TokenType::DO, "expected 'do' after while condition");
    
    // Inside loop
    currentUnit->BeginScope();
    Block();
    currentUnit->EndScope();

    parser.Consume(TokenType::END, "expected 'end' after while loop");

    EndLoop();
}


void Compiler::InspectStatement() {
    std::size_t exitJumps = PlaceholderList();
    int jumpToNext{ -1 };

    // "inspect" expression
    // Parse the expression we'll be comparing to.
    Expression();

    // ( "when" expression ( "when" expression )* "do" block "end" )*
    while (parser.Match(TokenType::WHEN)) {
        if (jumpToNext != -1) {
            PatchJump(jumpToNext);
        }

        std::size_t bodyJumps = PlaceholderList();

        do {
            // Since 'equal' pops both values off the stack, we actually have to
            // duplicate the top value of the stack so that the expression that we're
            // comparing to gets preserved.
            EmitByte(OpCode::DUPLICATE);
            Expression();
            EmitByte(OpCode::EQUAL);

            // POP_JUMP_IF_TRUE
            AddPlaceholder(&bodyJumps);

        } while (parser.Match(TokenType::WHEN));

        jumpToNext = EmitJump(OpCode::JUMP);
        PatchPlaceholders(bodyJumps, OpCode::POP_JUMP_IF_TRUE);
        
        // Parse the body.
        parser.Consume(TokenType::DO, "expected 'do' after case conditions");
        currentUnit->BeginScope();
        Block();
        currentUnit->EndScope();
        parser.Consume(TokenType::END, "expected 'end' after 'when' body");

        // After the block, we want to skip every other case.
        AddPlaceholder(&exitJumps);
    }

    if (jumpToNext != -1) PatchJump(jumpToNext);
    
    // Check for an optional 'catch-all' else clause.
    // ( "else" block "end" )?
    if (parser.Match(TokenType::ELSE)) {
        currentUnit->BeginScope();
        Block();
        currentUnit->EndScope();
        parser.Consume(TokenType::END, "expected 'end' after else body");
    }
    
    // After all the when statements, we want to patch all the end-of-when jumps
    PatchPlaceholders(exitJumps, OpCode::JUMP);
    
    // To get rid of the original expression we compared to.
    EmitByte(OpCode::POP);

    parser.Consume(TokenType::END, "expected 'end' after 'when' clauses");
}


void Compiler::CountStatement() {
    // 'count' IDENTIFIER 'from' expression 'to' expression 'by' expression 'do' block 'end'

    LoopContext loop{ OpCode::COUNT_LOOP };

    currentUnit->BeginScope();

    // Parse the counter as a new local.
    Variable counterVariable = ParseVariable("expected variable name");
    DefineVariable(counterVariable);
    
    int counter = currentUnit->ResolveLocal(parser.previous);
    ASSERT(counter != -1, "counter's slot is -1 and cannot be found");
    
    // Start --------------------------
    parser.Consume(TokenType::FROM, "expected 'from' after incrementer name");
    Expression();
    EmitBytes(OpCode::SET_LOCAL, counter);
    
    // Condition --------------------------
    parser.Consume(TokenType::TO, "expected 'to' after start value");
    Expression();
    currentUnit->AddHiddenLocal("$limit");
    
    // Step --------------------------
    if (parser.Match(TokenType::BY)) {
        Expression();
    } else {
        EmitConstant(1);
    }
    
    currentUnit->AddHiddenLocal("$step");
    
    // Check at runtime whether these values are 
    // valid (if they're numbers, if they run, etc.)
    loop.exitJump = EmitJump(OpCode::COUNT_PREP);
    
    // Actual loop body --------------------------
    currentUnit->BeginScope();
    StartLoop(loop);

    parser.Consume(TokenType::DO, "expected 'do' after count clauses");
    Block();
    parser.Consume(TokenType::END, "expected 'end' after count statement");

    currentUnit->EndScope();
    EndLoop();

    currentUnit->EndScope();
}


void Compiler::ForeachStatement() {
    // 'foreach' IDENTIFIER ( 'at' IDENTIFIER )? 'in' expression 'do' block 'end'

    LoopContext loop{ OpCode::FOREACH_LOOP };
    currentUnit->BeginScope();

    // If the iterator is a dictionary, we'll hold the current probing index
    // in the first local slot. If not, then this will just be unused.
    EmitByte(OpCode::NIL);
    currentUnit->AddHiddenLocal("$probingIndex");

    // Start by parsing the 'value' local and adding it as a local variable.
    EmitByte(OpCode::NIL);
    Variable valueVariable = ParseVariable("expected item name after 'foreach'");
    DefineVariable(valueVariable);
    
    // If we match 'at', that means we also have a variable for the key.
    // The key will hold the value of '0' for now.
    EmitByte(OpCode::NIL);
    if (parser.Match(TokenType::AT)) {
        Variable keyVariable = ParseVariable("expected key name after 'at'");
        DefineVariable(keyVariable);

    } else {
        // Make sure to add it as a local so that it actually pops off
        // the stack when the scope ends!!!!!!!!!!!!!!!!
        currentUnit->AddHiddenLocal("$keyVariable");
    }
    
    parser.Consume(TokenType::IN, "expected 'in' after item name");
    Expression();
    currentUnit->AddHiddenLocal("$iterator");

    // Check at runtime whether these values are 
    // valid (e.g. if the iterator is a valid type
    // that can be iterated on)
    loop.exitJump = EmitJump(OpCode::FOREACH_PREP);
    
    // Actual loop body --------------------------
    currentUnit->BeginScope();
    StartLoop(loop);

    parser.Consume(TokenType::DO, "expected 'do' after foreach clauses");
    Block();
    parser.Consume(TokenType::END, "expected 'end' after foreach statement");

    currentUnit->EndScope();
    EndLoop();

    currentUnit->EndScope();
}


void Compiler::BreakStatement() {
    if (currentLoop == nullptr) {
        parser.Error("can't use 'break' outside of a loop");
    }

    // Discard any locals currently inside the loop.
    currentUnit->DiscardLocals(currentLoop->scopeDepth + 1);
    AddPlaceholder(&currentLoop->breakHead);
}


void Compiler::ContinueStatement() {
    if (currentLoop == nullptr) {
        parser.Error("can't use 'continue' outside of a loop");
    }

    /*
        To continue, we need to discard any locals that are in the
        current scope of the loop.
    */
    currentUnit->DiscardLocals(currentLoop->scopeDepth + 1);
    EmitLoop(currentLoop->loopInstruction, currentLoop->start);
}


void Compiler::ReturnStatement() {
    if (currentUnit->type == FunctionType::SCRIPT) {
        if (parser.IsAtEnd()) parser.Error("top-level module return statement must return a value");
        Expression();
        EmitByte(OpCode::RETURN);

        return;
    }

    if (!parser.Check(TokenType::END, TokenType::ELSEIF, TokenType::ELSE)) {
        Expression();
        EmitByte(OpCode::RETURN);
    } else {
        EmitReturn();
    }
}


void Compiler::ExpressionStatement() {
    isValidExprStmt = false;
    Expression();

    if (!isValidExprStmt) {
        parser.Error("expected a statement");
    }

    EmitByte(OpCode::POP);
}


// Expression methods ------------------------------------------

const ParseRule Compiler::rules[] = {
    // Single-character tokens
    { &Compiler::GroupingExpr,      &Compiler::CallExpr,        Precedence::CALL },             // LEFT_PAREN
    { nullptr,                      nullptr,                    Precedence::NONE },             // RIGHT_PAREN
    { &Compiler::DictExpr,          nullptr,                    Precedence::NONE },             // LEFT_BRACE
    { nullptr,                      nullptr,                    Precedence::NONE },             // RIGHT_BRACE
    { &Compiler::ListExpr,          &Compiler::IndexExpr,       Precedence::CALL },             // LEFT_BRACKET
    { nullptr,                      nullptr,                    Precedence::NONE },             // RIGHT_BRACKET
    { nullptr,                      nullptr,                    Precedence::NONE },             // COMMA
    { nullptr,                      &Compiler::DotExpr,         Precedence::CALL },             // DOT
    { nullptr,                      nullptr,                    Precedence::NONE },             // ELLIPSIS
    { &Compiler::UnaryExpr,         &Compiler::BinaryExpr,      Precedence::TERM },             // MINUS
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::TERM },             // PLUS
    { nullptr,                      nullptr,                    Precedence::NONE },             // QUESTION
    { nullptr,                      nullptr,                    Precedence::NONE },             // SEMICOLON
    { nullptr,                      &Compiler::DotExpr,         Precedence::CALL },             // COLON
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::FACTOR },           // SLASH
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::FACTOR },           // STAR
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::FACTOR },           // PERCENT
    
    // One-or-two character tokens
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::EQUALITY },         // EQUAL
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::EQUALITY },         // BANG_EQUAL
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::COMPARISON },       // GREATER
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::COMPARISON },       // GREATER_EQUAL
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::COMPARISON },       // LESS
    { nullptr,                      &Compiler::BinaryExpr,      Precedence::COMPARISON },       // LESS_EQUAL
    { nullptr,                      nullptr,                    Precedence::NONE },             // LEFT_ARROW

    // Literals         
    { &Compiler::VariableExpr,      nullptr,                    Precedence::NONE },             // IDENTIFIER
    { &Compiler::StringExpr,        nullptr,                    Precedence::NONE },             // STRING
    { &Compiler::NumberExpr,        nullptr,                    Precedence::NONE },             // NUMBER
    { nullptr,                      nullptr,                    Precedence::NONE },             // ERROR

    // Keywords         
    { nullptr,                      &Compiler::AndExpr,         Precedence::AND },              // AND
    { nullptr,                      nullptr,                    Precedence::NONE },             // AT
    { nullptr,                      nullptr,                    Precedence::NONE },             // BOX
    { nullptr,                      nullptr,                    Precedence::NONE },             // BREAK
    { nullptr,                      nullptr,                    Precedence::NONE },             // BY
    { &Compiler::CategoryExpr,      nullptr,                    Precedence::NONE },             // CATEGORY
    { nullptr,                      nullptr,                    Precedence::NONE },             // CONTINUE
    { nullptr,                      nullptr,                    Precedence::NONE },             // COUNT
    { nullptr,                      nullptr,                    Precedence::NONE },             // DO
    { nullptr,                      nullptr,                    Precedence::NONE },             // ELSE
    { nullptr,                      nullptr,                    Precedence::NONE },             // ELSEIF
    { nullptr,                      nullptr,                    Precedence::NONE },             // END
    { &Compiler::LiteralExpr,       nullptr,                    Precedence::NONE },             // FALSE
    { &Compiler::FunctionExpr,      nullptr,                    Precedence::NONE },             // FUNCTION
    { nullptr,                      nullptr,                    Precedence::NONE },             // FOREACH
    { nullptr,                      nullptr,                    Precedence::NONE },             // FROM
    { &Compiler::TernaryExpr,       nullptr,                    Precedence::NONE },             // IF
    { nullptr,                      nullptr,                    Precedence::NONE },             // IN
    { nullptr,                      nullptr,                    Precedence::NONE },             // INSPECT
    { &Compiler::LiteralExpr,       nullptr,                    Precedence::NONE },             // NIL
    { &Compiler::UnaryExpr,         nullptr,                    Precedence::NONE },             // NOT
    { nullptr,                      nullptr,                    Precedence::NONE },             // OF
    { nullptr,                      &Compiler::OrExpr,          Precedence::OR },               // OR
    { nullptr,                      nullptr,                    Precedence::NONE },             // RETURN
    { &Compiler::ShareExpr,         nullptr,                    Precedence::NONE },             // SHARE
    { nullptr,                      nullptr,                    Precedence::NONE },             // SHARED
    { nullptr,                      nullptr,                    Precedence::NONE },             // THEN
    { &Compiler::ThingExpr,         nullptr,                    Precedence::NONE },             // THING
    { nullptr,                      nullptr,                    Precedence::NONE },             // TO
    { &Compiler::LiteralExpr,       nullptr,                    Precedence::NONE },             // TRUE
    { nullptr,                      nullptr,                    Precedence::NONE },             // WHEN
    { nullptr,                      nullptr,                    Precedence::NONE },             // WHILE

    { nullptr,                      nullptr,                    Precedence::NONE },             // END_OF_FILE
};

/**
 * Returns the associated ParseRule of the given token type.
 */
const ParseRule& Compiler::GetRule(TokenType type) const {
    static_assert(
        sizeof(Compiler::rules) / sizeof(ParseRule)
        == static_cast<int>(TokenType::END_OF_FILE) + 1, 
        "rule array and tokens are mismatched"
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
 * Parses an expression that will be STORED somewhere,
 * such as:
 * 
 * box a <- b
 *          ^ stored expression
 * 
 * box whatever <- coolList[2]
 *                 ^^^^^^^^^^^ stored expression
 */
bool Compiler::StoredExpression() {
    bool shared = true;
    Expression();

    /*
        As we compiled the last expression, any getters that
        we encountered (get local, get global, get index, etc.)
        marked the last get expression with their current bytecode
        count.
    
        We can check whether or not the last expression was a getter,
        and if so, we should copy the entire thing.
    */
    if (CurrentChunk()->bytecode.Count() == lastGetExpr) {
        EmitByte(OpCode::COPY);
        shared = false;
    }

    lastGetExpr = SIZE_MAX;
    return shared;
}


void Compiler::MarkGetter() {
    lastGetExpr = CurrentChunk()->bytecode.Count();
}


/**
 * Parses an expression in parentheses.
 */
void Compiler::GroupingExpr(bool canAssign) {
    Expression();
    parser.Consume(TokenType::RIGHT_PAREN, "expected ')' after expression");

    // This is to allow bare expressions like (42) as statements.
    isValidExprStmt = true;
}


/**
 * Parses () postfix.
 */
void Compiler::CallExpr(bool canAssign) {
    ObjFunction::Arguments arguments = ArgumentList(UINT8_COUNT);

    /*
        We have two separate call instructions for performance sake.
        Since most functions don't need to 'share' anything, we can
        split a call operator into two cases where there's a shared
        argument and when there's not.

        A CALL_SHARED instruction has an additional std::uint16_t at 
        the end that's supposed to represent which parameters  were
        shared at the call site. 

        Otherwise, we will emit a CALL instruction which doesn't have
        to handle the messy share stuff.
    */
    if (arguments.shared == 0) {
        EmitBytes(OpCode::CALL, arguments.arity);
    } else {
        EmitBytes(OpCode::CALL_SHARED, arguments.arity);
        EmitByte(arguments.shared);
    }
    
    
    isValidExprStmt = true;
}


/**
 * Parses a list of arguments ended with a parenthesis.
 * Returns an Arguments struct containing info about the arguments.
 */
ObjFunction::Arguments Compiler::ArgumentList(std::size_t argcMax) {
    // Ideally we don't want to have to pass in the VM here
    // since the arguments are just data, but we have to  
    ObjFunction::Arguments arguments;

    if (!parser.Check(TokenType::RIGHT_PAREN)) {
        do {
            /*
                Users must mark parameters with 'share' if they don't want
                a copy to be passed inside the function. For this reason, we
                use a StoredExpression(), which will 
            */
            bool shared = parser.Check(TokenType::SHARE);
            if (shared) {
                arguments.MarkShared(arguments.arity);
            }

            StoredExpression(); 
            
            if (arguments.arity == argcMax) {
                parser.Error("can't have more than '%zu' arguments", argcMax);
            }

            arguments.arity++;

        } while (parser.Match(TokenType::COMMA));
    }   

    parser.Consume(TokenType::RIGHT_PAREN, "expected ')' after arguments");

    return arguments;
}


/**
 * Parses [] postfix.
 */
void Compiler::IndexExpr(bool canAssign) {
    Expression();

    // expression[ a to b ]
    if (parser.Match(TokenType::TO)) {
        if (parser.Match(TokenType::RIGHT_BRACKET)) {
            EmitByte(OpCode::NIL);
        } else {
            Expression();
        }

        EmitByte(OpCode::SLICE);
        parser.Consume(TokenType::RIGHT_BRACKET, "expected ']' after slice");
        
        return;
    }

    // collection[ a ]
    parser.Consume(TokenType::RIGHT_BRACKET, "expected ']' after index");

    if (canAssign && parser.Match(TokenType::LEFT_ARROW)) {
        StoredExpression();
        EmitByte(OpCode::SET_INDEX);
        isValidExprStmt = true;
    } else {
        EmitByte(OpCode::GET_INDEX);
        MarkGetter();
    }
}


void Compiler::DotExpr(bool canAssign) {
    // Can either be a . or :
    Token dotted = parser.previous;
    if (dotted.type == TokenType::COLON) {
        parser.Consume(TokenType::IDENTIFIER, "expected attribute name after ':'");
    } else {
        parser.Consume(TokenType::IDENTIFIER, "expected attribute name after '.'");
    }

    Token name = parser.previous;
    std::uint16_t nameConstant = IdentifierConstant(name);

    /*
        A colon operator such as:
        a:append(5)

        Desugars to:
        a.append(share a)
    */
    if (dotted.type == TokenType::COLON) {
        parser.Consume(TokenType::LEFT_PAREN, "expected function call after colon-accessed attribute");

        EmitShortable(OpCode::SELF, nameConstant);
        ObjFunction::Arguments arguments = ArgumentList(ObjFunction::Arguments::MAX_ARITY - 1);

        // The first argument is always shared in a self call.
        arguments.MarkShared(0);
        
        // ArgumentList() parsed all the arguments AFTER the self argument,
        // so we have to add +1 to account for that.
        arguments.arity++;
        EmitBytes(OpCode::CALL_SHARED, arguments.arity);
        EmitByte(arguments.shared);

        isValidExprStmt = true;

        return;
    }

    if (canAssign && parser.Match(TokenType::LEFT_ARROW)) {
        pendingName = name;
        
        StoredExpression();
        EmitShortable(OpCode::SET_ATTRIBUTE, nameConstant);
        isValidExprStmt = true;
    } else {
        EmitShortable(OpCode::GET_ATTRIBUTE, nameConstant);
        MarkGetter();
        isValidExprStmt = false;
    }
}


/**
 * Parses a unary expression and emits the corresponding bytecode. 
 */
void Compiler::UnaryExpr(bool canAssign) {
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
 * Parses a 'share' expression, which marks the given expression
 * as not copied when stored.
 */
void Compiler::ShareExpr(bool canAssign) {
    ParsePrecedence(Precedence::UNARY);

    /*
        When we encounter a share expression, we will set the last
        get expression to nothing. This will signal to any StoredExpression
        to not emit OpCode::COPY, as it checks lastGetExpr for
        if the last expression was a getter. By setting it to SIZE_MAX,
        we essentially erase any sign of a getter of the expression in 
        front of 'share'.

        For instance...

        box value <- share thing
                            ^^^^^
            When parsing 'thing', we set lastGetExpr to the current bytecode
            count to mark the last getter. Then 'share' will erase lastGetExpr
            back to SIZE_MAX so SharedExpression() doesn't emit a copy operation.
    */
    lastGetExpr = SIZE_MAX;
}


/**
 * Parses a binary expression with two operands.
 */
void Compiler::BinaryExpr(bool canAssign) {
    // When we reach this function, the 'previous' token
    // points to the infix operator. (+, -, /, *, etc)
    TokenType operatorType = parser.previous.type;
    const ParseRule& rule = GetRule(operatorType);

    // Compile the right-hand operand.
    ParsePrecedence(static_cast<Precedence>((int)rule.precedence + 1));

    switch (operatorType) {
        case TokenType::EQUAL:          EmitByte(OpCode::EQUAL); break;
        case TokenType::BANG_EQUAL:     EmitBytes(OpCode::EQUAL, OpCode::NOT); break; 
        case TokenType::GREATER:        EmitByte(OpCode::GREATER); break;
        case TokenType::GREATER_EQUAL:  EmitByte(OpCode::GREATER_EQUAL); break;
        case TokenType::LESS:           EmitByte(OpCode::LESS); break;
        case TokenType::LESS_EQUAL:     EmitByte(OpCode::LESS_EQUAL); break;

        case TokenType::PLUS:           EmitByte(OpCode::ADD); break;
        case TokenType::MINUS:          EmitByte(OpCode::SUBTRACT); break;
        case TokenType::STAR:           EmitByte(OpCode::MULTIPLY); break;
        case TokenType::SLASH:          EmitByte(OpCode::DIVIDE); break;
        case TokenType::PERCENT:        EmitByte(OpCode::MODULO); break;

        default: UNREACHABLE(); // Unreachable.
    }
}


/**
 * Parses an 'and' expression.
 * <left> and <right>
 */
void Compiler::AndExpr(bool canAssign) {
    // At this point, we already compiled the lefthand prefix 
    // expression. If it's false, we can just jump over compiling
    // the right hand side, and leave the lefthand side value
    // on the stack.
    std::size_t endJump = EmitJump(OpCode::JUMP_IF_FALSE_OR_POP);

    // If it's true, however, get rid of the left hand side
    // and parse the right hand side, placing it on the stack.
    ParsePrecedence(Precedence::AND);

    PatchJump(endJump);
}

/**
 * Parses an 'or' expression.
 * <left> or <right>
 */
void Compiler::OrExpr(bool canAssign) {
    // If the lefthand expression is true, jump to the end and 
    // leave the lefthand value on the stack.
    std::size_t endJump = EmitJump(OpCode::JUMP_IF_TRUE_OR_POP);
    
    // If it's false, however, get rid of the left hand side
    // and parse the right hand side, placing it on the stack.
    ParsePrecedence(Precedence::OR);

    PatchJump(endJump);
}


/**
 * Parses a ternary statement
 */
void Compiler::TernaryExpr(bool canAssign) {
    // <condition> ? <then-expression> : <else-expression>
    //               ^^^^^^^^^^^^^^^^^ You are here.
    Expression(); // condition
    parser.Consume(TokenType::THEN, "expected 'then' after condition.");
    std::size_t skipThen = EmitJump(OpCode::POP_JUMP_IF_FALSE); // Pop the condition value
    
    // then-expression
    Expression();
    std::size_t skipElse = EmitJump(OpCode::JUMP);

    // else-expression
    PatchJump(skipThen);
    parser.Consume(TokenType::ELSE, "expected 'else' after then-branch.");
    ParsePrecedence(Precedence::TERNARY);

    PatchJump(skipElse);
}


/**
 * Emits a literal (true, false, nil)
 */
void Compiler::LiteralExpr(bool canAssign) {
    TokenType literalType = parser.previous.type;

    switch (literalType) {
        case TokenType::TRUE: EmitByte(OpCode::TRUE); break;
        case TokenType::FALSE: EmitByte(OpCode::FALSE); break;
        case TokenType::NIL: EmitByte(OpCode::NIL); break;
        
        default: UNREACHABLE();
    }
}


/**
 * Emits a number into the bytecode stream.
 */
void Compiler::NumberExpr(bool canAssign) {
    double value = std::strtod(parser.previous.start, nullptr);
    EmitConstant(value);
}


/**
 * Emits a string into the bytecode stream.
 */
void Compiler::StringExpr(bool canAssign) {
    Vector<char> buffer(vm);
    Token& string = parser.previous;
    std::size_t stringLength = string.length - 2; // to account for surrounding quotes

    for (int i = 1; i <= stringLength; i++) {
        char character = string.start[i];
        if (character != '\\') {
            buffer.PushBack(character);  
            continue;
        }
        
        // Escape sequence handling ----------------
        if (i + 1 > stringLength) break;
        i++; // Consume the backslash
        char nextCharacter = string.start[i];
        switch (nextCharacter) {
            case '\\': buffer.PushBack('\\'); break;
            case '"':  buffer.PushBack('"'); break;
            case '0':  buffer.PushBack('\0'); break;
            case 'a':  buffer.PushBack('\a'); break;
            case 'b':  buffer.PushBack('\b'); break;
            case 'e':  buffer.PushBack('\33'); break;
            case 'f':  buffer.PushBack('\f'); break;
            case 'n':  buffer.PushBack('\n'); break;
            case 'r':  buffer.PushBack('\r'); break;
            case 't':  buffer.PushBack('\t'); break;

            default:
                parser.Error("Invalid escape character '%c'.", nextCharacter);
        }
    }

    ObjString* stringObject = ObjString::FromBuffer(vm, buffer);
    EmitConstant(stringObject);
}


/**
 * Handles a function literal/anonymous function.
 */
void Compiler::FunctionExpr(bool canAssign) {
    FunctionUnit unit{ vm, *this, parser };
    PushUnit(&unit, FunctionType::FUNCTION);

    currentUnit->BeginScope();
    parser.Consume(TokenType::LEFT_PAREN, "expected '(' after 'function'");
    
    // Parse arguments
    
    if (!parser.Check(TokenType::RIGHT_PAREN)) {
        ParameterList(currentUnit->function);
    }

    parser.Consume(TokenType::RIGHT_PAREN, "expected ')' after parameters");

    Block();
    parser.Consume(TokenType::END, "expected 'end' after function body");

    ObjFunction* function = PopUnit();
    EmitShortable(OpCode::CLOSURE, MakeConstant(function));

    // For every upvalue, we will emit two bytes: isLocal and index.
    // OP_CLOSURE has a variadic number of operands.
    for (std::size_t i = 0; i < function->upvalueCount; i++) {
        EmitByte(unit.upvalues[i].isLocal ? 1 : 0);
        EmitByte(unit.upvalues[i].index);
    }
}


void Compiler::ParameterList(ObjFunction* function) {
    bool hasPrebody = false;
    ObjFunction::Arguments& parameters = function->parameters;

    do {
        if (parser.Match(TokenType::SHARED)) {
            if (parameters.arity >= 8) {
                parser.Error("can't define any more shared parameters after the 8th parameter");
            }

            /*
                When we reach a shared parameter, we store it in the function
                as a bitmask. sharedParameters is simply a uint where each bit
                corresponds to one parameter, 1 meaning it's shared, and 0 meaning
                it's not.

                e.g.
                1011 - first, second, fourth values are shared, third one is not
            */
            parameters.MarkShared(parameters.arity);
        }

        Variable parameter = ParseVariable("expected parameter name");
        DefineVariable(parameter);

        // e.g. args...)
        if (parser.Match(TokenType::ELLIPSIS)) {
            parameters.variadic = true;
            hasPrebody = true;

            function->MarkDefault();
            EmitByte(OpCode::LIST);
            break;
        }

        parameters.arity++;
        if (parameters.arity > UINT8_MAX) {
            parser.ErrorAtCurrent("can't have more than 255 parameters");
        }

        /*
            Default arguments are handled by compiling the default
            expressions inside the function's chunk. Each function chunk
            has a little prebody where it pushes the expressions on the
            stack. Those expressions on the stack are the arguments that
            the function will use.

            Then during runtime, the VM will select where to start on the
            function depending on its arg count + default count.

            The function chunk code for a function like...

            box test <- function(a <- 1, b <- 2)

            end
            
            may look like...

            == test ==
                    --------- MARK DEFAULT
            0000    2 OP_CONSTANT             0 '1'
                    --------- MARK DEFAULT
            0002    | OP_CONSTANT             1 '2'
            ...

            where if the user omitted B, the VM will start
            the function at 0002. If the user omitted both,
            the VM will start the function at 0000. 
        */
        if (parser.Match(TokenType::LEFT_ARROW)) {
            function->MarkDefault();
            Expression();
            hasPrebody = true;

        } else if (function->defaults.Count() > 0) {
            parser.Error("can't define a non-default parameter after a default parameter");
        }
        
        else {
            parameters.minArity++;
        }

    } while (parser.Match(TokenType::COMMA));

    // If we had any prebody, we'll make sure to mark the end of it
    // so that the VM can skip it if needed.
    if (hasPrebody) {
        // An args... left out will just be filled in with an empty list.
        function->MarkDefault();
    }
}


/**
 * Handles a list literal 
 * e.g. [1, 2, 3, 4, 5...]
 */
void Compiler::ListExpr(bool canAssign) {
    EmitByte(OpCode::LIST);
    if (!parser.Check(TokenType::RIGHT_BRACKET)) {
        do {
            StoredExpression();
            EmitByte(OpCode::APPEND);

        } while (parser.Match(TokenType::COMMA));
    }

    parser.Consume(TokenType::RIGHT_BRACKET, "expected ']' after list literal");
}


void Compiler::DictExpr(bool canAssign) {
    EmitByte(OpCode::DICT);
    
    if (!parser.Check(TokenType::RIGHT_BRACE)) {
        do {
            if (parser.Match(TokenType::LEFT_BRACKET)) {
                // Luggage allows [expr] <- value for more permissive keys.
                // The main reason we don't allow for a more range of expressions in
                // the default case (no brackets) is because of parsing troubles. Having
                // something like a <- 4 <- "hello" confuses the parser because we
                // don't know which '<-' denotes the key-value separation.
                StoredExpression();
                parser.Consume(TokenType::RIGHT_BRACKET, "expected ']' after key");

            } else if (parser.Match(TokenType::IDENTIFIER)) { 
                EmitIdentifier(parser.previous);

            } else {

                // Otherwise, if we don't have a square bracket we can just parse primary.
                ParsePrecedence(Precedence::PRIMARY);
            }

            parser.Consume(TokenType::LEFT_ARROW, "expected '<-' after key");
            Expression();
            EmitByte(OpCode::DEFINE_ENTRY);
            
        } while (parser.Match(TokenType::COMMA));
    }

    parser.Consume(TokenType::RIGHT_BRACE, "expected '}' after list literal");
}


/**
 * Handles a thing expression
 */
void Compiler::ThingExpr(bool canAssign) {
    EmitByte(OpCode::THING);

    if (parser.Match(TokenType::OF)) {
        EmitByte(OpCode::DUPLICATE);
        Expression();
        
        ObjString* protoAttribute = ObjString::FromLiteral(vm, "__proto__");
        std::uint16_t protoConstant = MakeConstant(protoAttribute);
        EmitShortable(OpCode::SET_ATTRIBUTE, protoConstant);
        EmitByte(OpCode::POP);
    }

    if (parser.Match(TokenType::LEFT_BRACE)) {
        if (!parser.Match(TokenType::RIGHT_BRACE)) {
            do {
                EmitByte(OpCode::DUPLICATE);
                parser.Consume(TokenType::IDENTIFIER, "expected attribute name");
                std::uint16_t attributeName = IdentifierConstant(parser.previous);

                parser.Consume(TokenType::LEFT_ARROW, "expected '<-' after attribute name");

                StoredExpression();

                EmitShortable(OpCode::SET_ATTRIBUTE, attributeName);
                EmitByte(OpCode::POP);
            } while (parser.Match(TokenType::COMMA));

            parser.Consume(TokenType::RIGHT_BRACE, "expected '}' after initializers");
        }
    }
}


/**
 * Parses a category expression.
 * e.g.
 * box Color <- category 
 *      RED,
 *      ORANGE,
 *      YELLOW,
 *      GREEN,
 *      BLUE,
 *      INDIGO,
 *      VIOLET  
 * end
 */
void Compiler::CategoryExpr(bool canAssign) {
    EmitByte(OpCode::CATEGORY);

    if (!parser.Match(TokenType::END)) {
        EmitByte(OpCode::DUPLICATE);
        do {
            parser.Consume(TokenType::IDENTIFIER, "expected category kind");
            std::uint16_t type = IdentifierConstant(parser.previous);
            
            // Optional assignment
            if (parser.Match(TokenType::LEFT_ARROW)) {
                StoredExpression();
            } else {
                EmitByte(OpCode::NIL);
            }

            EmitShortable(OpCode::SET_ATTRIBUTE, type);
            
        } while (parser.Match(TokenType::COMMA));
        
        parser.Consume(TokenType::END, "expected 'end' after category kinds");
        EmitByte(OpCode::POP);
    }

}


/**
 * Handles a variable expression
 */
void Compiler::VariableExpr(bool canAssign) {
    Token& name = parser.previous;
    NamedVariable(name, canAssign);
}


void Compiler::NamedVariable(Token& name, bool canAssign) {
    OpCode::Type getOp, setOp;

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

    // If we can't find the variable name in the current function,
    // look inside the enclosing function's values.
    } else if ((arg = currentUnit->ResolveUpvalue(name)) != -1) {
        getOp = OpCode::GET_UPVALUE;
        setOp = OpCode::SET_UPVALUE;
        
    // This probably means that the variable is a global variable.
    } else {
        arg = IdentifierConstant(parser.previous);
        getOp = OpCode::GET_GLOBAL;
        setOp = OpCode::SET_GLOBAL;
    }
    
    if (canAssign && parser.Match(TokenType::LEFT_ARROW)) {
        StoredExpression();
        EmitShortable(setOp, arg);
        isValidExprStmt = true;
    } else {
        EmitShortable(getOp, arg);
        MarkGetter();
        isValidExprStmt = false;
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
        parser.Error("expected expression");
        return;
    }

    bool canAssign = minimumPrecedence <= Precedence::ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (GetRule(parser.current.type).precedence >= minimumPrecedence) {
        parser.Advance();
        ParseFn infixRule = GetRule(parser.previous.type).infix;
        ASSERT(infixRule != nullptr, "infix rule is not defined");
        (this->*infixRule)(canAssign);
    }

    if (canAssign && parser.Match(TokenType::LEFT_ARROW)) {
        parser.Error("invalid assignment target");
    }
}

}