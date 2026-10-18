#include <cstring>
#include <array>

#include "luggage.hpp"
#include "lug_lexer.h"

namespace Luggage {

bool Token::operator==(const Token& other) const {
    if (length != other.length) return false;
    return std::memcmp(start, other.start, length) == 0;
}


Token Token::Synthetic(const char* lexeme) {
    Token token;
    token.start = lexeme;
    token.length = static_cast<std::size_t>(strlen(lexeme));

    return token;
}

/**
 * For this first prototype, we will use Wren's lexer as a reference for
 * recognizing keywords!!!!
 * 
 * In order to recognize keywords, we'll simply compare the current 
 * token to a list of keywords using memcmp. If it matches, we will 
 * return the appropriate token type.
 */
struct Keyword {
	const char* identifier;
	size_t length;
	TokenType type;
};

static Keyword keywords[] = {
	{"and",			3,	TokenType::AND},
	{"at",			2,	TokenType::AT},
	{"box",			3,	TokenType::BOX},
	{"break",		5,	TokenType::BREAK},
	{"by",			2,	TokenType::BY},
    {"continue",	8,	TokenType::CONTINUE},
	{"count",		5,	TokenType::COUNT},
	{"do",			2,	TokenType::DO},
	{"else",		4,	TokenType::ELSE},
	{"elseif",		6,	TokenType::ELSEIF},
	{"end",			3,	TokenType::END},
	{"false",		5,	TokenType::FALSE},
	{"function",	8,	TokenType::FUNCTION},
	{"foreach",		7,	TokenType::FOREACH},
	{"from",		4,	TokenType::FROM},
	{"if",			2,	TokenType::IF},
	{"in",			2,	TokenType::IN},
	{"inspect",		7,	TokenType::INSPECT},
	{"nil",			3,	TokenType::NIL},
	{"not",			3,	TokenType::NOT},
	{"or",			2,	TokenType::OR},
	{"return",		6,	TokenType::RETURN},
	{"share",		5,	TokenType::SHARE},
	{"shared",		6,	TokenType::SHARED},
	{"then",		4,	TokenType::THEN},
	{"true",		4,	TokenType::TRUE},
	{"while",		5,	TokenType::WHILE},
	{"when",		4,	TokenType::WHEN},
};

// Helper functions -----------------------------
/**
 * Checks if the given character is a letter
 */
static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
          c == '_';
}

/**
 * Checks if the given character is a digit
 */
static bool isDigit(char character) {
    return character >= '0' && character <= '9';
}


// Lexer private methods -----------------------------

/**
 * Produces and returns a new token based on the given type.
 * Its lexeme is the current word being scanned
 * by the lexer.
 */
Token Lexer::MakeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = start;

    // We can get the # of elements inbetween two poionters
    // by subtracting the current pointer by the start pointer
    token.length = static_cast<std::size_t>(current - start);
    token.line = line;
    
    return token;
}

/**
 * Produces an error token that holds an error
 * message.
 */
void Lexer::LexError(const char* message) {
    LugError error;
    error.type = ErrorType::COMPILE;
    
    std::string errorMessage;
    errorMessage += "[line ";
    errorMessage += std::to_string(line);
    errorMessage += "] Error at '";
    errorMessage.append(start, static_cast<std::size_t>(current - start));
    errorMessage += "': ";
    errorMessage += message;

    error.message = errorMessage;

    throw error;
}


Token Lexer::Identifier() {
    while (isAlpha(Peek()) || isDigit(Peek())) Advance();

    TokenType identifierType = TokenType::IDENTIFIER;

    for (const Keyword& keyword : keywords) {
        std::size_t lexemeLength = static_cast<int>(current - start);
        if (lexemeLength != keyword.length) continue;

        bool contentEqual = std::memcmp(start, keyword.identifier, keyword.length) == 0;
        if (!contentEqual) continue;

        identifierType = keyword.type;
        break;
    }

    return MakeToken(identifierType);
}


Token Lexer::Number() {
    // Consume the number, or the left part of the number.
    while (isDigit(Peek())) Advance();

    // Look for a decimal part (check for period)
    // if so, tokenize the right part of the number
    if (Peek() == '.' && isDigit(PeekNext())) {
        // Consume the '.'
        Advance();

        while (isDigit(Peek())) Advance();
    }

    // Like how we did for string literals,
    // we will defer getting the literal of the
    // number later in the process instead of
    // stuffing it inside the token.
    return MakeToken(TokenType::NUMBER);
}


Token Lexer::String() {
    while (Peek() != '"' && !IsAtEnd()) {
        if (Peek() == '\n') LexError("Unterminated string.");
        Advance();
    }

    if (IsAtEnd()) LexError("Unterminated string.");

    // The closing quote.
    Advance();
    return MakeToken(TokenType::STRING);
}


/**
 * Returns true if we have reached the end of the
 * source string.
 */
bool Lexer::IsAtEnd() {
    return *current == '\0';   
}

/**
 * Advances the scanner until it finds
 * no other whitespace. Ensures that scanToken()
 * always runs at the start of a lexable token.
 */
void Lexer::SkipWhitespace() {
    while (true) {
        char character = Peek();
        switch (character) {
            case ' ':
            case '\r':
            case '\t':
                Advance();
                break;

            case '\n':
                line++;
                Advance();
                break;

            case '/':
                if (PeekNext() == '/') {
                    // A comment goes until the end of the line.
                    while (Peek() != '\n' && !IsAtEnd()) Advance();
                } else {
                    return;
                }
                
                break;

            default:
                return;
        }
    }
}


/**
 * Consumes the next character in the source code
 * and returns it
 */
char Lexer::Advance() {
    current++;
    return *(current - 1);
}


/**
 * Returns the about-to-be consumed character
 */
char Lexer::Peek() {
    return *current;
}


/**
 * Returns the character right after the
 * about-to-be consumed character
 */
char Lexer::PeekNext() {
    if (IsAtEnd()) return '\0';
    return *(current + 1);
}


/**
 * A 'conditional' advance. Only consumes the 
 * next character if it's equal to the given character.
 */
bool Lexer::Match(char expected) {
    if (IsAtEnd()) return false;
    if (*current != expected) return false;

    current++;
    return true;
}



// Lexer public methods -----------------------------
Token Lexer::ScanToken() {
    ASSERT(start != nullptr && current != nullptr, "Lexer not initialized with source code.");

    /*
        In a tree-walk interpreter, we could account
        for whitespace in the switch statement
        by doing nothing, because scanToken() was wrapped
        in a loop, scanTokens(). However, scanToken() here
        must return a Token so we can't just do nothing
        in the switch statement. Instead, we account for 
        possible whitespace and THEN scan the token.
    */
    SkipWhitespace();

    start = current;
    if (IsAtEnd()) return MakeToken(TokenType::END_OF_FILE);

    char character = Advance();
    if (isAlpha(character)) return Identifier();
    if (isDigit(character)) return Number();

    switch (character) {
        case '(': return MakeToken(TokenType::LEFT_PAREN);
        case ')': return MakeToken(TokenType::RIGHT_PAREN);
        case '{': return MakeToken(TokenType::LEFT_BRACE);
        case '}': return MakeToken(TokenType::RIGHT_BRACE);
        case '[': return MakeToken(TokenType::LEFT_BRACKET);
        case ']': return MakeToken(TokenType::RIGHT_BRACKET);
        case ';': return MakeToken(TokenType::SEMICOLON);
        case ':': return MakeToken(TokenType::COLON);
        case ',': return MakeToken(TokenType::COMMA);
        case '.': 
            if (Peek() == '.' && PeekNext() == '.') {
                Advance();
                Advance();
                return MakeToken(TokenType::ELLIPSIS);
            }

            return MakeToken(TokenType::DOT);
        case '-': return MakeToken(TokenType::MINUS);
        case '+': return MakeToken(TokenType::PLUS);
        case '/': return MakeToken(TokenType::SLASH);
        case '*': return MakeToken(TokenType::STAR);
        case '=': return MakeToken(TokenType::EQUAL);
        case '%': return MakeToken(TokenType::PERCENT);

        case '!':
            if (Match('=')) return MakeToken(TokenType::BANG_EQUAL);
            break; // breaks to LexError
            
        case '<':
            if (Match('=')) {
                return MakeToken(TokenType::LESS_EQUAL);  // <=
            }     
            
            else if (Match('-')) {
                return MakeToken(TokenType::LEFT_ARROW); // <-
            }
            
            else {
                return MakeToken(TokenType::LESS);
            }

            break;
        
        case '>':
            return MakeToken(Match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
        
        case '"':
            return String();
    }

    LexError("Unexpected character.");
}


}