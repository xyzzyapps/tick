#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parser_init(Parser* p, TokenList tokens) {
    p->tokens = tokens;
    p->curr = 0;
    p->error_msg = NULL;
}

void parser_free(Parser* p) {
    if (p->error_msg) {
        free(p->error_msg);
        p->error_msg = NULL;
    }
}

static bool is_at_end(Parser* p) {
    return p->curr >= p->tokens.count || p->tokens.tokens[p->curr].type == TOK_EOF;
}

static Token* peek(Parser* p) {
    if (p->curr >= p->tokens.count) return &p->tokens.tokens[p->tokens.count - 1];
    return &p->tokens.tokens[p->curr];
}

static Token* peek_next(Parser* p) {
    if (p->curr + 1 >= p->tokens.count) return &p->tokens.tokens[p->tokens.count - 1];
    return &p->tokens.tokens[p->curr + 1];
}

static Token* advance(Parser* p) {
    Token* t = peek(p);
    if (!is_at_end(p)) p->curr++;
    return t;
}

static void skip_newlines(Parser* p) {
    while (!is_at_end(p) && peek(p)->type == TOK_NEWLINE) {
        advance(p);
    }
}

static bool can_start_primary(Token* t) {
    switch (t->type) {
        case TOK_IDENTIFIER:
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_STRING:
        case TOK_SIGIL_FFI:
        case TOK_LBRACKET:
        case TOK_LBRACE:
            return true;
        default:
            return false;
    }
}

static bool is_stmt_terminator(Token* t) {
    switch (t->type) {
        case TOK_EOF:
        case TOK_NEWLINE:
        case TOK_COMMA:
        case TOK_PIPE:
        case TOK_RBRACKET:
        case TOK_RBRACE:
            return true;
        default:
            return false;
    }
}

// Forward declarations
static ASTStmt* parse_statement(Parser* p);
static ASTPipeline* parse_pipeline(Parser* p);
static ASTCascade* parse_cascade(Parser* p);
static ASTCascade* parse_cascade_after_pipe(Parser* p);
static ASTMessageSend* parse_message_send(Parser* p);
static ASTMessage* parse_message(Parser* p);
static ASTExpr* parse_primary(Parser* p);
static ASTExpr* parse_block(Parser* p);

static bool is_message_ahead(Parser* p) {
    if (is_at_end(p)) return false;
    Token* t = peek(p);
    if (is_stmt_terminator(t)) return false;
    return t->type == TOK_BINARY_OP || t->type == TOK_IDENTIFIER;
}

static bool is_method_def_ahead(Parser* p) {
    if (peek_next(p)->type == TOK_METHOD) return true;
    return false;
}

static ASTStmt* parse_method_def(Parser* p, ASTExpr* recv) {
    int cap = 8;
    int count = 0;
    Token** sig_tokens = (Token**)malloc(sizeof(Token*) * cap);

    while (!is_at_end(p) && peek(p)->type != TOK_LBRACE) {
        if (peek(p)->type == TOK_NEWLINE) {
            p->error_msg = strdup("unexpected newline in method signature");
            free(sig_tokens);
            return NULL;
        }
        if (count >= cap) {
            cap *= 2;
            sig_tokens = (Token**)realloc(sig_tokens, sizeof(Token*) * cap);
        }
        sig_tokens[count++] = advance(p);
    }

    if (peek(p)->type != TOK_LBRACE) {
        p->error_msg = strdup("expected '{' for method body");
        free(sig_tokens);
        return NULL;
    }

    ASTExpr* block_expr = parse_block(p);
    if (!block_expr) {
        free(sig_tokens);
        return NULL;
    }

    if (count == 0) {
        p->error_msg = strdup("method signature cannot be empty");
        free(sig_tokens);
        return NULL;
    }

    char* selector = NULL;
    char** params = NULL;
    int param_count = 0;

    if (sig_tokens[0]->type == TOK_BINARY_OP) {
        selector = strdup(sig_tokens[0]->lexeme);
        if (count > 1) {
            param_count = 1;
            params = (char**)malloc(sizeof(char*));
            params[0] = strdup(sig_tokens[1]->lexeme);
        }
    } else if (count == 1) {
        selector = strdup(sig_tokens[0]->lexeme);
    } else if (count == 2) {
        int slen = (int)strlen(sig_tokens[0]->lexeme);
        selector = (char*)malloc(slen + 2);
        sprintf(selector, "%s:", sig_tokens[0]->lexeme);
        param_count = 1;
        params = (char**)malloc(sizeof(char*));
        params[0] = strdup(sig_tokens[1]->lexeme);
    } else if (count % 2 == 0) {
        int total_len = 0;
        param_count = count / 2;
        params = (char**)malloc(sizeof(char*) * param_count);
        for (int i = 0; i < count; i += 2) {
            total_len += (int)strlen(sig_tokens[i]->lexeme) + 1;
            params[i / 2] = strdup(sig_tokens[i + 1]->lexeme);
        }
        selector = (char*)malloc(total_len + 1);
        selector[0] = '\0';
        for (int i = 0; i < count; i += 2) {
            strcat(selector, sig_tokens[i]->lexeme);
            strcat(selector, ":");
        }
    } else {
        selector = strdup(sig_tokens[0]->lexeme);
        param_count = count - 1;
        params = (char**)malloc(sizeof(char*) * param_count);
        for (int i = 1; i < count; i++) {
            params[i - 1] = strdup(sig_tokens[i]->lexeme);
        }
    }

    free(sig_tokens);

    return ast_stmt_method_def(recv, selector, params, param_count, block_expr->as.block);
}

static ASTStmt* parse_statement(Parser* p) {
    skip_newlines(p);
    if (is_at_end(p)) return NULL;

    // 1. Assignment: set IDENTIFIER Expression
    if (peek(p)->type == TOK_SET) {
        advance(p); // consume 'set'
        Token* id = peek(p);
        if (id->type != TOK_IDENTIFIER && id->type != TOK_SIGIL_FFI) {
            p->error_msg = strdup("expected identifier after 'set'");
            return NULL;
        }
        advance(p);
        ASTPipeline* expr = parse_pipeline(p);
        if (!expr) return NULL;
        return ast_stmt_set(id->lexeme, ast_expr_pipeline(expr));
    }

    // 2. Return: return Expression
    if (peek(p)->type == TOK_RETURN) {
        advance(p); // consume 'return'
        ASTExpr* expr = NULL;
        if (!is_at_end(p) && !is_stmt_terminator(peek(p))) {
            ASTPipeline* pipe = parse_pipeline(p);
            if (pipe) expr = ast_expr_pipeline(pipe);
        }
        return ast_stmt_return(expr);
    }

    // 3. Method definition: Receiver "method" <signature> Block
    if (can_start_primary(peek(p)) && is_method_def_ahead(p)) {
        ASTExpr* recv = parse_primary(p);
        advance(p); // consume 'method'
        return parse_method_def(p, recv);
    }

    // 4. Default: Pipeline expression
    ASTPipeline* pipe = parse_pipeline(p);
    if (!pipe) return NULL;
    return ast_stmt_expr(ast_expr_pipeline(pipe));
}

static ASTPipeline* parse_pipeline(Parser* p) {
    ASTCascade* first = parse_cascade(p);
    if (!first) return NULL;

    ASTPipeline* pipeline = ast_pipeline_new(first);
    while (true) {
        skip_newlines(p);
        if (peek(p)->type == TOK_PIPE) {
            advance(p); // consume '|'
            skip_newlines(p);
            ASTCascade* next = parse_cascade_after_pipe(p);
            if (!next) return NULL;
            ast_pipeline_add_step(pipeline, next);
        } else {
            break;
        }
    }
    return pipeline;
}

static ASTCascade* parse_cascade(Parser* p) {
    ASTMessageSend* msg_send = parse_message_send(p);
    if (!msg_send) return NULL;

    ASTCascade* cascade = ast_cascade_new(msg_send);
    while (peek(p)->type == TOK_COMMA) {
        advance(p); // consume ','
        skip_newlines(p);
        ASTMessage* msg = parse_message(p);
        if (!msg) return NULL;
        ast_cascade_add_msg(cascade, msg);
    }
    return cascade;
}

static ASTCascade* parse_cascade_after_pipe(Parser* p) {
    ASTMessageSend* msg_send = NULL;

    if (is_message_ahead(p)) {
        ASTMessage* msg = parse_message(p);
        if (!msg) return NULL;
        msg_send = ast_msg_send(NULL, msg);
    } else if (peek(p)->type == TOK_SIGIL_FFI) {
        Token* tok = advance(p);
        int cap = 4;
        int count = 0;
        ASTExpr** args = (ASTExpr**)malloc(sizeof(ASTExpr*) * cap);
        while (can_start_primary(peek(p)) && !is_stmt_terminator(peek(p))) {
            if (count >= cap) {
                cap *= 2;
                args = (ASTExpr**)realloc(args, sizeof(ASTExpr*) * cap);
            }
            args[count++] = parse_primary(p);
        }
        ASTExpr* ffi_expr = ast_expr_ffi(tok->lexeme, args, count);
        msg_send = ast_msg_send(ffi_expr, NULL);
    } else if (peek(p)->type == TOK_LBRACE) {
        ASTExpr* block = parse_block(p);
        if (!block) return NULL;
        msg_send = ast_msg_send(block, NULL);
    } else {
        msg_send = parse_message_send(p);
        if (!msg_send) return NULL;
    }

    ASTCascade* cascade = ast_cascade_new(msg_send);
    while (peek(p)->type == TOK_COMMA) {
        advance(p); // consume ','
        skip_newlines(p);
        ASTMessage* msg = parse_message(p);
        if (!msg) return NULL;
        ast_cascade_add_msg(cascade, msg);
    }
    return cascade;
}

static ASTMessageSend* parse_message_send(Parser* p) {
    ASTExpr* primary = parse_primary(p);
    if (!primary) return NULL;

    if (is_message_ahead(p)) {
        ASTMessage* msg = parse_message(p);
        if (!msg) return NULL;
        return ast_msg_send(primary, msg);
    }
    return ast_msg_send(primary, NULL);
}

static ASTMessage* parse_message(Parser* p) {
    Token* tok = peek(p);

    if (tok->type == TOK_BINARY_OP) {
        advance(p);
        ASTExpr* arg = parse_primary(p);
        if (!arg) return NULL;
        return ast_msg_binary(tok->lexeme, arg);
    }

    if (tok->type == TOK_IDENTIFIER) {
        char* first_ident = strdup(advance(p)->lexeme);

        if (can_start_primary(peek(p)) && !is_stmt_terminator(peek(p))) {
            int cap = 4;
            int count = 0;
            char** keywords = (char**)malloc(sizeof(char*) * cap);
            ASTExpr** args = (ASTExpr**)malloc(sizeof(ASTExpr*) * cap);

            keywords[count] = first_ident;
            args[count] = parse_primary(p);
            count++;

            while (peek(p)->type == TOK_IDENTIFIER) {
                Token* next = peek_next(p);
                if (can_start_primary(next) && !is_stmt_terminator(next)) {
                    if (count >= cap) {
                        cap *= 2;
                        keywords = (char**)realloc(keywords, sizeof(char*) * cap);
                        args = (ASTExpr**)realloc(args, sizeof(ASTExpr*) * cap);
                    }
                    keywords[count] = strdup(advance(p)->lexeme);
                    args[count] = parse_primary(p);
                    count++;
                } else {
                    break;
                }
            }

            return ast_msg_keyword(keywords, args, count);
        }

        ASTMessage* m = ast_msg_unary(first_ident);
        free(first_ident);
        return m;
    }

    p->error_msg = strdup("expected message (identifier or binary operator)");
    return NULL;
}

static ASTExpr* parse_primary(Parser* p) {
    Token* tok = peek(p);

    if (tok->type == TOK_INT) {
        advance(p);
        return ast_expr_literal_int(atoll(tok->lexeme));
    }
    if (tok->type == TOK_FLOAT) {
        advance(p);
        return ast_expr_literal_float(atof(tok->lexeme));
    }
    if (tok->type == TOK_STRING) {
        advance(p);
        return ast_expr_literal_string(tok->lexeme);
    }
    if (tok->type == TOK_IDENTIFIER) {
        advance(p);
        if (strcmp(tok->lexeme, "nil") == 0) return ast_expr_literal_nil();
        if (strcmp(tok->lexeme, "true") == 0) return ast_expr_literal_bool(true);
        if (strcmp(tok->lexeme, "false") == 0) return ast_expr_literal_bool(false);
        return ast_expr_ident(tok->lexeme);
    }
    if (tok->type == TOK_SIGIL_FFI) {
        Token* t = advance(p);
        int cap = 4;
        int count = 0;
        ASTExpr** args = (ASTExpr**)malloc(sizeof(ASTExpr*) * cap);
        while (can_start_primary(peek(p)) && !is_stmt_terminator(peek(p))) {
            if (count >= cap) {
                cap *= 2;
                args = (ASTExpr**)realloc(args, sizeof(ASTExpr*) * cap);
            }
            args[count++] = parse_primary(p);
        }
        return ast_expr_ffi(t->lexeme, args, count);
    }
    if (tok->type == TOK_LBRACKET) {
        advance(p); // consume '['
        skip_newlines(p);
        ASTPipeline* pipe = parse_pipeline(p);
        skip_newlines(p);
        if (peek(p)->type != TOK_RBRACKET) {
            p->error_msg = strdup("expected ']' after inline subexpression");
            return NULL;
        }
        advance(p); // consume ']'
        return ast_expr_subexpr(pipe);
    }
    if (tok->type == TOK_LBRACE) {
        return parse_block(p);
    }

    p->error_msg = strdup("unexpected token at start of expression");
    return NULL;
}

static ASTExpr* parse_block(Parser* p) {
    advance(p); // consume '{'
    skip_newlines(p);

    // Lookahead for BlockParams: IDENTIFIER+ "|"
    int save_curr = p->curr;
    bool has_pipe = false;
    while (!is_at_end(p) && peek(p)->type == TOK_IDENTIFIER) {
        advance(p);
        if (peek(p)->type == TOK_PIPE) {
            has_pipe = true;
            break;
        }
    }

    char** params = NULL;
    int param_count = 0;

    if (has_pipe) {
        p->curr = save_curr;
        int cap = 4;
        params = (char**)malloc(sizeof(char*) * cap);
        while (peek(p)->type == TOK_IDENTIFIER) {
            if (param_count >= cap) {
                cap *= 2;
                params = (char**)realloc(params, sizeof(char*) * cap);
            }
            params[param_count++] = strdup(advance(p)->lexeme);
        }
        advance(p); // consume '|'
    } else {
        p->curr = save_curr;
    }

    skip_newlines(p);
    ASTProgram* body = ast_program_new();
    while (!is_at_end(p) && peek(p)->type != TOK_RBRACE) {
        ASTStmt* stmt = parse_statement(p);
        if (stmt) ast_program_add_stmt(body, stmt);
        skip_newlines(p);
    }

    if (peek(p)->type != TOK_RBRACE) {
        p->error_msg = strdup("expected '}' to close block");
        return NULL;
    }
    advance(p); // consume '}'

    return ast_expr_block(params, param_count, body);
}

ASTProgram* parser_parse_program(Parser* p) {
    ASTProgram* prog = ast_program_new();
    skip_newlines(p);
    while (!is_at_end(p)) {
        ASTStmt* stmt = parse_statement(p);
        if (p->error_msg != NULL) {
            ast_program_free(prog);
            return NULL;
        }
        if (stmt) ast_program_add_stmt(prog, stmt);
        skip_newlines(p);
    }
    return prog;
}
