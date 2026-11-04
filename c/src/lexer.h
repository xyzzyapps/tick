#ifndef TCLTALK_LEXER_H
#define TCLTALK_LEXER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TOK_EOF,
    TOK_NEWLINE,
    TOK_IDENTIFIER,
    TOK_SIGIL_FFI,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_BINARY_OP,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_PIPE,
    TOK_COMMA,
    TOK_SET,
    TOK_RETURN,
    TOK_METHOD
} TokenType;

typedef struct {
    TokenType type;
    char* lexeme;
    int line;
    int col;
} Token;

typedef struct {
    Token* tokens;
    int count;
    int capacity;
} TokenList;

TokenList lexer_tokenize(const char* source);
void token_list_free(TokenList* list);
const char* token_type_name(TokenType type);

#endif // TCLTALK_LEXER_H
