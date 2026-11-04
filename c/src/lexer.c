#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void token_list_init(TokenList* list) {
    list->count = 0;
    list->capacity = 64;
    list->tokens = (Token*)malloc(sizeof(Token) * list->capacity);
}

static void token_list_add(TokenList* list, TokenType type, const char* lexeme, int line, int col) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->tokens = (Token*)realloc(list->tokens, sizeof(Token) * list->capacity);
    }
    Token* t = &list->tokens[list->count++];
    t->type = type;
    t->lexeme = strdup(lexeme);
    t->line = line;
    t->col = col;
}

void token_list_free(TokenList* list) {
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i].lexeme);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case TOK_EOF: return "EOF";
        case TOK_NEWLINE: return "NEWLINE";
        case TOK_IDENTIFIER: return "IDENTIFIER";
        case TOK_SIGIL_FFI: return "FFI";
        case TOK_INT: return "INT";
        case TOK_FLOAT: return "FLOAT";
        case TOK_STRING: return "STRING";
        case TOK_BINARY_OP: return "BINARY_OP";
        case TOK_LBRACKET: return "[";
        case TOK_RBRACKET: return "]";
        case TOK_LBRACE: return "{";
        case TOK_RBRACE: return "}";
        case TOK_PIPE: return "|";
        case TOK_COMMA: return ",";
        case TOK_SET: return "set";
        case TOK_RETURN: return "return";
        case TOK_METHOD: return "method";
        default: return "UNKNOWN";
    }
}

static bool is_ident_start(char ch) {
    return isalpha(ch) || ch == '_';
}

static bool is_ident_part(char ch) {
    return isalnum(ch) || ch == '_' || ch == ':';
}

static bool is_binary_op_char(char ch) {
    return strchr("+-*/%<>=!&^", ch) != NULL;
}

static TokenList filter_newlines(TokenList* raw, int line, int col) {
    TokenList res;
    token_list_init(&res);

    for (int i = 0; i < raw->count; i++) {
        Token* t = &raw->tokens[i];
        if (t->type != TOK_NEWLINE) {
            token_list_add(&res, t->type, t->lexeme, t->line, t->col);
            continue;
        }

        // Discard leading newlines
        if (res.count == 0) continue;

        Token* prev = &res.tokens[res.count - 1];
        if (prev->type == TOK_LBRACKET || prev->type == TOK_LBRACE ||
            prev->type == TOK_COMMA || prev->type == TOK_PIPE ||
            prev->type == TOK_NEWLINE) {
            continue;
        }

        // Lookahead to next non-newline token
        Token* next = NULL;
        for (int j = i + 1; j < raw->count; j++) {
            if (raw->tokens[j].type != TOK_NEWLINE) {
                next = &raw->tokens[j];
                break;
            }
        }

        if (next != NULL) {
            if (next->type == TOK_RBRACKET || next->type == TOK_RBRACE ||
                next->type == TOK_COMMA || next->type == TOK_PIPE) {
                continue;
            }
        }

        token_list_add(&res, TOK_NEWLINE, "\n", t->line, t->col);
    }

    if (res.count > 0 && res.tokens[res.count - 1].type == TOK_NEWLINE) {
        free(res.tokens[res.count - 1].lexeme);
        res.count--;
    }

    token_list_add(&res, TOK_EOF, "", line, col);
    return res;
}

TokenList lexer_tokenize(const char* source) {
    TokenList raw;
    token_list_init(&raw);

    int pos = 0;
    int line = 1;
    int col = 1;
    int len = (int)strlen(source);

    while (pos < len) {
        char ch = source[pos];
        int start_line = line;
        int start_col = col;

        // Skip whitespace
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            pos++;
            col++;
            continue;
        }

        // Comments
        if (ch == '#') {
            while (pos < len && source[pos] != '\n') {
                pos++;
            }
            continue;
        }

        // Newlines and semicolons
        if (ch == '\n' || ch == ';') {
            token_list_add(&raw, TOK_NEWLINE, "\n", start_line, start_col);
            pos++;
            if (ch == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
            continue;
        }

        if (ch == '[') {
            token_list_add(&raw, TOK_LBRACKET, "[", start_line, start_col);
            pos++; col++;
            continue;
        }
        if (ch == ']') {
            token_list_add(&raw, TOK_RBRACKET, "]", start_line, start_col);
            pos++; col++;
            continue;
        }
        if (ch == '{') {
            token_list_add(&raw, TOK_LBRACE, "{", start_line, start_col);
            pos++; col++;
            continue;
        }
        if (ch == '}') {
            token_list_add(&raw, TOK_RBRACE, "}", start_line, start_col);
            pos++; col++;
            continue;
        }
        if (ch == '|') {
            token_list_add(&raw, TOK_PIPE, "|", start_line, start_col);
            pos++; col++;
            continue;
        }
        if (ch == ',') {
            token_list_add(&raw, TOK_COMMA, ",", start_line, start_col);
            pos++; col++;
            continue;
        }

        // Strings
        if (ch == '"') {
            pos++; col++;
            int buf_cap = 64;
            int buf_len = 0;
            char* buf = (char*)malloc(buf_cap);

            while (pos < len && source[pos] != '"') {
                char sc = source[pos++];
                col++;
                if (sc == '\\' && pos < len) {
                    char esc = source[pos++];
                    col++;
                    if (esc == 'n') sc = '\n';
                    else if (esc == 't') sc = '\t';
                    else if (esc == 'r') sc = '\r';
                    else if (esc == '"') sc = '"';
                    else if (esc == '\\') sc = '\\';
                    else if (esc == '$') sc = '$';
                    else if (esc == '[') sc = '[';
                    else if (esc == ']') sc = ']';
                    else sc = esc;
                }
                if (buf_len + 2 >= buf_cap) {
                    buf_cap *= 2;
                    buf = (char*)realloc(buf, buf_cap);
                }
                buf[buf_len++] = sc;
            }
            if (pos < len && source[pos] == '"') {
                pos++; col++;
            }
            buf[buf_len] = '\0';
            token_list_add(&raw, TOK_STRING, buf, start_line, start_col);
            free(buf);
            continue;
        }

        // @ Sigil (strictly FFI)
        if (ch == '@') {
            pos++; col++;
            if (pos < len && is_ident_start(source[pos])) {
                int start = pos;
                while (pos < len && is_ident_part(source[pos])) {
                    pos++; col++;
                }
                int id_len = pos - start;
                char* ffi_str = (char*)malloc(id_len + 2);
                ffi_str[0] = '@';
                memcpy(ffi_str + 1, source + start, id_len);
                ffi_str[id_len + 1] = '\0';
                token_list_add(&raw, TOK_SIGIL_FFI, ffi_str, start_line, start_col);
                free(ffi_str);
            } else {
                fprintf(stderr, "Lexer error at line %d, col %d: '@' is reserved for FFI identifiers\n", start_line, start_col);
            }
            continue;
        }

        // Numbers
        if (isdigit(ch)) {
            int start = pos;
            bool is_float = false;
            while (pos < len && isdigit(source[pos])) {
                pos++; col++;
            }
            if (pos + 1 < len && source[pos] == '.' && isdigit(source[pos + 1])) {
                is_float = true;
                pos++; col++; // consume '.'
                while (pos < len && isdigit(source[pos])) {
                    pos++; col++;
                }
            }
            int num_len = pos - start;
            char* num_str = (char*)malloc(num_len + 1);
            memcpy(num_str, source + start, num_len);
            num_str[num_len] = '\0';
            token_list_add(&raw, is_float ? TOK_FLOAT : TOK_INT, num_str, start_line, start_col);
            free(num_str);
            continue;
        }

        // Identifiers and Keywords
        if (is_ident_start(ch)) {
            int start = pos;
            while (pos < len && is_ident_part(source[pos])) {
                pos++; col++;
            }
            int id_len = pos - start;
            char* id_str = (char*)malloc(id_len + 1);
            memcpy(id_str, source + start, id_len);
            id_str[id_len] = '\0';

            if (strcmp(id_str, "set") == 0) {
                token_list_add(&raw, TOK_SET, "set", start_line, start_col);
            } else if (strcmp(id_str, "return") == 0) {
                token_list_add(&raw, TOK_RETURN, "return", start_line, start_col);
            } else if (strcmp(id_str, "method") == 0) {
                token_list_add(&raw, TOK_METHOD, "method", start_line, start_col);
            } else {
                token_list_add(&raw, TOK_IDENTIFIER, id_str, start_line, start_col);
            }
            free(id_str);
            continue;
        }

        // Binary Operators
        if (is_binary_op_char(ch)) {
            char op[3] = {ch, '\0', '\0'};
            pos++; col++;
            if (pos < len && is_binary_op_char(source[pos])) {
                op[1] = source[pos++];
                col++;
            }
            token_list_add(&raw, TOK_BINARY_OP, op, start_line, start_col);
            continue;
        }

        // Fallback single character
        char s[2] = {ch, '\0'};
        token_list_add(&raw, TOK_BINARY_OP, s, start_line, start_col);
        pos++; col++;
    }

    TokenList filtered = filter_newlines(&raw, line, col);
    token_list_free(&raw);
    return filtered;
}
