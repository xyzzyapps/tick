#ifndef TCLTALK_COROUTINE_H
#define TCLTALK_COROUTINE_H

#include "object.h"
#include "pt.h"
#include "vm.h"

typedef struct Coroutine Coroutine;

struct Coroutine {
    pt pt_state;
    int pc;
    Scope* scope;
    Obj* block;
    Obj* yield_val;
    Obj* resume_arg;
    char* yield_assign_var;
    int status; // 0=suspended, 1=running, 2=dead
    bool started;
};

Obj* obj_new_coroutine(Obj* block, VM* vm);
Obj* coroutine_resume(Coroutine* co, Obj* arg, VM* vm);
Obj* coroutine_yield(VM* vm, Obj* val);
void coroutine_init_methods(VM* vm);

#endif // TCLTALK_COROUTINE_H
