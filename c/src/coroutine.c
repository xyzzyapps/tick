#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Obj* obj_new_coroutine(Obj* block, VM* vm) {
    Coroutine* co = (Coroutine*)malloc(sizeof(Coroutine));
    PT_INIT(&co->pt_state);
    co->pc = 0;
    co->scope = NULL;
    co->block = block;
    co->yield_val = obj_new_nil();
    co->resume_arg = obj_new_nil();
    co->yield_assign_var = NULL;
    co->status = 0; // suspended
    co->started = false;

    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_COROUTINE;
    o->klass = CoroutineClass;
    o->as.coro = co;
    return o;
}

// Protothread step function using Adam Dunkels' PT macros
static int pt_step(Coroutine* co, VM* vm) {
    PT_BEGIN(&co->pt_state);

    if (!co->started) {
        co->started = true;
        co->status = 1; // running
        int64_t frame_id = vm_next_frame_id(vm);
        Scope* captured = co->block->as.block.captured_scope;
        co->scope = scope_new(captured, captured ? captured->self : NULL, frame_id);
        if (co->block->as.block.param_count > 0 && co->resume_arg) {
            scope_define(co->scope, co->block->as.block.params[0], co->resume_arg);
        }
    }

    for (co->pc = 0; co->pc < co->block->as.block.body->count; co->pc++) {
        vm->yield_requested = false;
        VMResult r = vm_eval_stmt(vm, co->block->as.block.body->statements[co->pc], co->scope);

        if (r.error_msg) {
            printf("Coroutine Error: %s\n", r.error_msg);
            co->yield_val = obj_new_nil();
            co->status = 2; // dead
            break;
        }

        if (vm->yield_requested) {
            vm->yield_requested = false;
            co->yield_val = vm->yield_value;
            // Local continuation saved by Duff's device:
            PT_YIELD(&co->pt_state);
            // Resumed! Next iteration continues.
            if (co->yield_assign_var) {
                scope_set(co->scope, co->yield_assign_var, co->resume_arg);
                free(co->yield_assign_var);
                co->yield_assign_var = NULL;
            }
            continue;
        }

        if (vm->unwind_frame_id > 0) {
            co->yield_val = vm->unwind_value;
            vm->unwind_frame_id = 0;
            vm->unwind_value = NULL;
            break;
        }

        co->yield_val = r.value;
    }

    co->status = 2; // dead
    PT_END(&co->pt_state);
}

Obj* coroutine_resume(Coroutine* co, Obj* arg, VM* vm) {
    if (co->status == 2) {
        printf("Error: cannot resume dead coroutine\n");
        return obj_new_nil();
    }

    co->resume_arg = arg ? arg : obj_new_nil();
    co->status = 1; // running

    Coroutine* prev_coro = vm->active_coro;
    vm->active_coro = co;
    vm->yield_requested = false;

    int pt_res = pt_step(co, vm);

    vm->active_coro = prev_coro;

    if (pt_res == PT_YIELDED) {
        co->status = 0; // suspended
    } else {
        co->status = 2; // dead
    }

    return co->yield_val ? co->yield_val : obj_new_nil();
}

Obj* coroutine_yield(VM* vm, Obj* val) {
    if (!vm->active_coro) {
        printf("Error: cannot yield outside of a running coroutine\n");
        return obj_new_nil();
    }
    vm->yield_requested = true;
    vm->yield_value = val ? val : obj_new_nil();
    return vm->active_coro->resume_arg ? vm->active_coro->resume_arg : obj_new_nil();
}

// Built-in methods for coroutine
static Obj* m_coro_create(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1 || argv[0]->type != OBJ_BLOCK) {
        printf("Error: coroutine create requires a block argument\n");
        return obj_new_nil();
    }
    return obj_new_coroutine(argv[0], vm);
}

static Obj* m_coro_yield(Obj* recv, int argc, Obj** argv, VM* vm) {
    Obj* val = (argc > 0) ? argv[0] : obj_new_nil();
    return coroutine_yield(vm, val);
}

static Obj* m_coro_resume(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_COROUTINE) {
        printf("Error: receiver must be a coroutine\n");
        return obj_new_nil();
    }
    Obj* arg = (argc > 0) ? argv[0] : obj_new_nil();
    return coroutine_resume(recv->as.coro, arg, vm);
}

static Obj* m_coro_status(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_COROUTINE) return obj_new_string("unknown");
    switch (recv->as.coro->status) {
        case 0: return obj_new_string("suspended");
        case 1: return obj_new_string("running");
        case 2: return obj_new_string("dead");
    }
    return obj_new_string("unknown");
}

static Obj* m_coro_arg(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (!vm->active_coro) return obj_new_nil();
    return vm->active_coro->resume_arg ? vm->active_coro->resume_arg : obj_new_nil();
}

static Obj* m_coro_is_done(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_COROUTINE) return obj_new_bool(true);
    return obj_new_bool(recv->as.coro->status == 2);
}

void coroutine_init_methods(VM* vm) {
    // Class methods
    class_add_native(CoroutineClass, "create:", m_coro_create);
    class_add_native(CoroutineClass, "new:", m_coro_create);
    class_add_native(CoroutineClass, "yield:", m_coro_yield);
    class_add_native(CoroutineClass, "yield", m_coro_yield);
    class_add_native(CoroutineClass, "arg", m_coro_arg);

    class_add_native(MetaclassClass, "create:", m_coro_create);
    class_add_native(MetaclassClass, "yield:", m_coro_yield);
    class_add_native(MetaclassClass, "yield", m_coro_yield);
    class_add_native(MetaclassClass, "arg", m_coro_arg);

    // Instance methods
    class_add_native(CoroutineClass, "resume", m_coro_resume);
    class_add_native(CoroutineClass, "resume:", m_coro_resume);
    class_add_native(CoroutineClass, "next", m_coro_resume);
    class_add_native(CoroutineClass, "status", m_coro_status);
    class_add_native(CoroutineClass, "isDone", m_coro_is_done);
    class_add_native(CoroutineClass, "isDead", m_coro_is_done);
}
