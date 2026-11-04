#ifndef TCLTALK_VM_H
#define TCLTALK_VM_H

#include "object.h"
#include "ast.h"
#include "ffi.h"
#include "tcltalk.h"

typedef struct VarEntry {
    char* name;
    Obj* value;
} VarEntry;

struct Scope {
    Scope* parent;
    Obj* self;
    Obj* receiver_instance;
    int64_t call_frame_id;
    VarEntry* vars;
    int count;
    int capacity;
};

struct VM {
    Scope* globals;
    FFIEngine* ffi;
    int64_t next_frame_id;
    int64_t unwind_frame_id;
    Obj* unwind_value;
    struct Coroutine* active_coro;
    bool yield_requested;
    Obj* yield_value;
};

Scope* scope_new(Scope* parent, Obj* self, int64_t frame_id);
void scope_free(Scope* s);
Obj* scope_get(Scope* s, const char* name);
void scope_set(Scope* s, const char* name, Obj* val);
void scope_define(Scope* s, const char* name, Obj* val);

VMResult vm_eval_ast(VM* vm, ASTProgram* prog);
VMResult vm_eval_stmt(VM* vm, ASTStmt* stmt, Scope* scope);
int64_t vm_next_frame_id(VM* vm);
VMResult vm_send_message(VM* vm, Obj* recv, const char* selector, int argc, Obj** argv);
VMResult vm_eval_block(VM* vm, Obj* block_val, int argc, Obj** argv);

void vm_init_builtins(VM* vm);

#endif // TCLTALK_VM_H
