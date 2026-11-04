#ifndef TCLTALK_PARSER_H
#define TCLTALK_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    TokenList tokens;
    int curr;
    char* error_msg;
} Parser;

void parser_init(Parser* p, TokenList tokens);
ASTProgram* parser_parse_program(Parser* p);
void parser_free(Parser* p);

#endif // TCLTALK_PARSER_H
