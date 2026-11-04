#ifndef TCLTALK_AST_H
#define TCLTALK_AST_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LIT_NIL,
    LIT_BOOL,
    LIT_INT,
    LIT_FLOAT,
    LIT_STRING
} ASTLiteralKind;

typedef enum {
    EXPR_LITERAL,
    EXPR_IDENT,
    EXPR_FFI,
    EXPR_SUBEXPR,
    EXPR_BLOCK,
    EXPR_PIPELINE
} ASTExprType;

typedef enum {
    MSG_UNARY,
    MSG_BINARY,
    MSG_KEYWORD
} ASTMessageType;

typedef enum {
    STMT_SET,
    STMT_RETURN,
    STMT_METHOD_DEF,
    STMT_EXPR
} ASTStmtType;

typedef struct ASTExpr ASTExpr;
typedef struct ASTMessage ASTMessage;
typedef struct ASTMessageSend ASTMessageSend;
typedef struct ASTCascade ASTCascade;
typedef struct ASTPipeline ASTPipeline;
typedef struct ASTStmt ASTStmt;
typedef struct ASTProgram ASTProgram;
typedef struct ASTBlockExpr ASTBlockExpr;

struct ASTBlockExpr {
    char** params;
    int param_count;
    ASTProgram* body;
};

struct ASTExpr {
    ASTExprType type;
    union {
        struct {
            ASTLiteralKind kind;
            bool bool_val;
            int64_t int_val;
            double flt_val;
            char* str_val;
        } literal;
        char* ident;
        struct {
            char* name;
            ASTExpr** args;
            int arg_count;
        } ffi;
        ASTPipeline* subexpr;
        ASTBlockExpr block;
        ASTPipeline* pipeline;
    } as;
};

struct ASTMessage {
    ASTMessageType type;
    union {
        char* unary_selector;
        struct {
            char* op;
            ASTExpr* arg;
        } binary;
        struct {
            char** keywords;
            ASTExpr** args;
            int count;
            char* cached_selector;
        } keyword;
    } as;
};

struct ASTMessageSend {
    ASTExpr* receiver; // NULL if piped
    ASTMessage* msg;
};

struct ASTCascade {
    ASTMessageSend* first_msg;
    ASTMessage** messages;
    int message_count;
};

struct ASTPipeline {
    ASTCascade* first;
    ASTCascade** steps;
    int step_count;
};

struct ASTStmt {
    ASTStmtType type;
    union {
        struct {
            char* var_name;
            ASTExpr* value;
        } set;
        struct {
            ASTExpr* value;
        } ret;
        struct {
            ASTExpr* receiver;
            char* selector;
            char** params;
            int param_count;
            ASTBlockExpr body;
        } method_def;
        ASTExpr* expr;
    } as;
};

struct ASTProgram {
    ASTStmt** statements;
    int count;
    int capacity;
};

// AST allocation helpers
ASTExpr* ast_expr_literal_nil(void);
ASTExpr* ast_expr_literal_bool(bool val);
ASTExpr* ast_expr_literal_int(int64_t val);
ASTExpr* ast_expr_literal_float(double val);
ASTExpr* ast_expr_literal_string(const char* val);
ASTExpr* ast_expr_ident(const char* name);
ASTExpr* ast_expr_ffi(const char* name, ASTExpr** args, int arg_count);
ASTExpr* ast_expr_subexpr(ASTPipeline* pipe);
ASTExpr* ast_expr_block(char** params, int param_count, ASTProgram* body);
ASTExpr* ast_expr_pipeline(ASTPipeline* pipe);

ASTMessage* ast_msg_unary(const char* selector);
ASTMessage* ast_msg_binary(const char* op, ASTExpr* arg);
ASTMessage* ast_msg_keyword(char** keywords, ASTExpr** args, int count);
const char* ast_msg_selector(ASTMessage* msg);

ASTMessageSend* ast_msg_send(ASTExpr* receiver, ASTMessage* msg);
ASTCascade* ast_cascade_new(ASTMessageSend* first_msg);
void ast_cascade_add_msg(ASTCascade* cascade, ASTMessage* msg);

ASTPipeline* ast_pipeline_new(ASTCascade* first);
void ast_pipeline_add_step(ASTPipeline* pipe, ASTCascade* step);

ASTStmt* ast_stmt_set(const char* var_name, ASTExpr* val);
ASTStmt* ast_stmt_return(ASTExpr* val);
ASTStmt* ast_stmt_method_def(ASTExpr* receiver, const char* selector, char** params, int param_count, ASTBlockExpr body);
ASTStmt* ast_stmt_expr(ASTExpr* expr);

ASTProgram* ast_program_new(void);
void ast_program_add_stmt(ASTProgram* prog, ASTStmt* stmt);

void ast_program_free(ASTProgram* prog);

#endif // TCLTALK_AST_H
