#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTExpr* ast_expr_literal_nil(void) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_LITERAL;
    e->as.literal.kind = LIT_NIL;
    return e;
}

ASTExpr* ast_expr_literal_bool(bool val) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_LITERAL;
    e->as.literal.kind = LIT_BOOL;
    e->as.literal.bool_val = val;
    return e;
}

ASTExpr* ast_expr_literal_int(int64_t val) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_LITERAL;
    e->as.literal.kind = LIT_INT;
    e->as.literal.int_val = val;
    return e;
}

ASTExpr* ast_expr_literal_float(double val) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_LITERAL;
    e->as.literal.kind = LIT_FLOAT;
    e->as.literal.flt_val = val;
    return e;
}

ASTExpr* ast_expr_literal_string(const char* val) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_LITERAL;
    e->as.literal.kind = LIT_STRING;
    e->as.literal.str_val = strdup(val);
    return e;
}

ASTExpr* ast_expr_ident(const char* name) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_IDENT;
    e->as.ident = strdup(name);
    return e;
}

ASTExpr* ast_expr_ffi(const char* name, ASTExpr** args, int arg_count) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_FFI;
    e->as.ffi.name = strdup(name);
    e->as.ffi.args = args;
    e->as.ffi.arg_count = arg_count;
    return e;
}

ASTExpr* ast_expr_subexpr(ASTPipeline* pipe) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_SUBEXPR;
    e->as.subexpr = pipe;
    return e;
}

ASTExpr* ast_expr_block(char** params, int param_count, ASTProgram* body) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_BLOCK;
    e->as.block.params = params;
    e->as.block.param_count = param_count;
    e->as.block.body = body;
    return e;
}

ASTExpr* ast_expr_pipeline(ASTPipeline* pipe) {
    ASTExpr* e = (ASTExpr*)malloc(sizeof(ASTExpr));
    e->type = EXPR_PIPELINE;
    e->as.pipeline = pipe;
    return e;
}

ASTMessage* ast_msg_unary(const char* selector) {
    ASTMessage* m = (ASTMessage*)malloc(sizeof(ASTMessage));
    m->type = MSG_UNARY;
    m->as.unary_selector = strdup(selector);
    return m;
}

ASTMessage* ast_msg_binary(const char* op, ASTExpr* arg) {
    ASTMessage* m = (ASTMessage*)malloc(sizeof(ASTMessage));
    m->type = MSG_BINARY;
    m->as.binary.op = strdup(op);
    m->as.binary.arg = arg;
    return m;
}

ASTMessage* ast_msg_keyword(char** keywords, ASTExpr** args, int count) {
    ASTMessage* m = (ASTMessage*)malloc(sizeof(ASTMessage));
    m->type = MSG_KEYWORD;
    m->as.keyword.keywords = keywords;
    m->as.keyword.args = args;
    m->as.keyword.count = count;

    int total_len = 0;
    for (int i = 0; i < count; i++) {
        total_len += (int)strlen(keywords[i]) + 1;
    }
    char* sel = (char*)malloc(total_len + 1);
    sel[0] = '\0';
    for (int i = 0; i < count; i++) {
        strcat(sel, keywords[i]);
        strcat(sel, ":");
    }
    m->as.keyword.cached_selector = sel;
    return m;
}

const char* ast_msg_selector(ASTMessage* msg) {
    switch (msg->type) {
        case MSG_UNARY: return msg->as.unary_selector;
        case MSG_BINARY: return msg->as.binary.op;
        case MSG_KEYWORD: return msg->as.keyword.cached_selector;
    }
    return "";
}

ASTMessageSend* ast_msg_send(ASTExpr* receiver, ASTMessage* msg) {
    ASTMessageSend* ms = (ASTMessageSend*)malloc(sizeof(ASTMessageSend));
    ms->receiver = receiver;
    ms->msg = msg;
    return ms;
}

ASTCascade* ast_cascade_new(ASTMessageSend* first_msg) {
    ASTCascade* c = (ASTCascade*)malloc(sizeof(ASTCascade));
    c->first_msg = first_msg;
    c->messages = NULL;
    c->message_count = 0;
    return c;
}

void ast_cascade_add_msg(ASTCascade* cascade, ASTMessage* msg) {
    cascade->messages = (ASTMessage**)realloc(cascade->messages, sizeof(ASTMessage*) * (cascade->message_count + 1));
    cascade->messages[cascade->message_count++] = msg;
}

ASTPipeline* ast_pipeline_new(ASTCascade* first) {
    ASTPipeline* p = (ASTPipeline*)malloc(sizeof(ASTPipeline));
    p->first = first;
    p->steps = NULL;
    p->step_count = 0;
    return p;
}

void ast_pipeline_add_step(ASTPipeline* pipe, ASTCascade* step) {
    pipe->steps = (ASTCascade**)realloc(pipe->steps, sizeof(ASTCascade*) * (pipe->step_count + 1));
    pipe->steps[pipe->step_count++] = step;
}

ASTStmt* ast_stmt_set(const char* var_name, ASTExpr* val) {
    ASTStmt* s = (ASTStmt*)malloc(sizeof(ASTStmt));
    s->type = STMT_SET;
    s->as.set.var_name = strdup(var_name);
    s->as.set.value = val;
    return s;
}

ASTStmt* ast_stmt_return(ASTExpr* val) {
    ASTStmt* s = (ASTStmt*)malloc(sizeof(ASTStmt));
    s->type = STMT_RETURN;
    s->as.ret.value = val;
    return s;
}

ASTStmt* ast_stmt_method_def(ASTExpr* receiver, const char* selector, char** params, int param_count, ASTBlockExpr body) {
    ASTStmt* s = (ASTStmt*)malloc(sizeof(ASTStmt));
    s->type = STMT_METHOD_DEF;
    s->as.method_def.receiver = receiver;
    s->as.method_def.selector = strdup(selector);
    s->as.method_def.params = params;
    s->as.method_def.param_count = param_count;
    s->as.method_def.body = body;
    return s;
}

ASTStmt* ast_stmt_expr(ASTExpr* expr) {
    ASTStmt* s = (ASTStmt*)malloc(sizeof(ASTStmt));
    s->type = STMT_EXPR;
    s->as.expr = expr;
    return s;
}

ASTProgram* ast_program_new(void) {
    ASTProgram* p = (ASTProgram*)malloc(sizeof(ASTProgram));
    p->count = 0;
    p->capacity = 16;
    p->statements = (ASTStmt**)malloc(sizeof(ASTStmt*) * p->capacity);
    return p;
}

void ast_program_add_stmt(ASTProgram* prog, ASTStmt* stmt) {
    if (prog->count >= prog->capacity) {
        prog->capacity *= 2;
        prog->statements = (ASTStmt**)realloc(prog->statements, sizeof(ASTStmt*) * prog->capacity);
    }
    prog->statements[prog->count++] = stmt;
}

void ast_program_free(ASTProgram* prog) {
    // Note: for production or testing scripts, memory can be freed, or swept
    if (!prog) return;
    free(prog->statements);
    free(prog);
}
