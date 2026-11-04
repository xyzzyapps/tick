#include "vm.h"
#include "lexer.h"
#include "parser.h"
#include "json.h"
#include "coroutine.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t next_id = 1;
static int64_t get_next_frame_id(VM* vm) {
    return next_id++;
}

int64_t vm_next_frame_id(VM* vm) {
    return get_next_frame_id(vm);
}

Scope* scope_new(Scope* parent, Obj* self, int64_t frame_id) {
    Scope* s = (Scope*)malloc(sizeof(Scope));
    s->parent = parent;
    s->self = self;
    s->receiver_instance = (self && self->type == OBJ_INSTANCE) ? self : NULL;
    s->call_frame_id = frame_id;
    s->count = 0;
    s->capacity = 16;
    s->vars = (VarEntry*)malloc(sizeof(VarEntry) * s->capacity);
    return s;
}

void scope_free(Scope* s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++) {
        free(s->vars[i].name);
    }
    free(s->vars);
    free(s);
}

Obj* scope_get(Scope* s, const char* name) {
    if (strcmp(name, "self") == 0 && s->self != NULL) return s->self;
    if (strcmp(name, "true") == 0) return obj_new_bool(true);
    if (strcmp(name, "false") == 0) return obj_new_bool(false);
    if (strcmp(name, "nil") == 0) return obj_new_nil();

    // 1. Local scope
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            return s->vars[i].value;
        }
    }

    // 2. Instance variables on receiver_instance
    if (s->receiver_instance != NULL) {
        for (int i = 0; i < s->receiver_instance->as.instance.count; i++) {
            if (strcmp(s->receiver_instance->as.instance.fields[i].name, name) == 0) {
                return s->receiver_instance->as.instance.fields[i].value;
            }
        }
    }

    // 3. Parent scope
    if (s->parent != NULL) {
        return scope_get(s->parent, name);
    }

    return NULL;
}

void scope_set(Scope* s, const char* name, Obj* val) {
    // 1. Check if name is an instance variable on receiver_instance
    if (s->receiver_instance != NULL) {
        for (int i = 0; i < s->receiver_instance->as.instance.count; i++) {
            if (strcmp(s->receiver_instance->as.instance.fields[i].name, name) == 0) {
                s->receiver_instance->as.instance.fields[i].value = val;
                return;
            }
        }
    }

    // 2. Check if variable exists in this or parent scope
    Scope* curr = s;
    while (curr != NULL) {
        for (int i = 0; i < curr->count; i++) {
            if (strcmp(curr->vars[i].name, name) == 0) {
                curr->vars[i].value = val;
                return;
            }
        }
        if (curr->receiver_instance != NULL) {
            for (int i = 0; i < curr->receiver_instance->as.instance.count; i++) {
                if (strcmp(curr->receiver_instance->as.instance.fields[i].name, name) == 0) {
                    curr->receiver_instance->as.instance.fields[i].value = val;
                    return;
                }
            }
        }
        curr = curr->parent;
    }

    // 3. Define in current local scope
    scope_define(s, name, val);
}

void scope_define(Scope* s, const char* name, Obj* val) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            s->vars[i].value = val;
            return;
        }
    }
    if (s->count >= s->capacity) {
        s->capacity *= 2;
        s->vars = (VarEntry*)realloc(s->vars, sizeof(VarEntry) * s->capacity);
    }
    s->vars[s->count].name = strdup(name);
    s->vars[s->count].value = val;
    s->count++;
}

// Forward declarations of evaluation helpers
static VMResult eval_stmt(VM* vm, ASTStmt* stmt, Scope* scope);
static VMResult eval_expr(VM* vm, ASTExpr* expr, Scope* scope);
static VMResult eval_pipeline(VM* vm, ASTPipeline* pipe, Scope* scope);
static VMResult eval_cascade(VM* vm, ASTCascade* cascade, Obj* piped_recv, Scope* scope);
static VMResult eval_message_send(VM* vm, ASTMessageSend* send, Obj* piped_recv, Scope* scope);
static char* interpolate_string(VM* vm, const char* str, Scope* scope);

VM* vm_new(void) {
    obj_system_init();
    VM* vm = (VM*)malloc(sizeof(VM));
    vm->globals = scope_new(NULL, NULL, 0);
    vm->ffi = ffi_engine_new();
    vm->next_frame_id = 1;
    vm->unwind_frame_id = 0;
    vm->unwind_value = NULL;
    vm->active_coro = NULL;
    vm->yield_requested = false;
    vm->yield_value = NULL;

    // Register global classes
    scope_set(vm->globals, "Object", obj_new_class(ObjectClass));
    scope_set(vm->globals, "Class", obj_new_class(MetaclassClass));
    scope_set(vm->globals, "Boolean", obj_new_class(BooleanClass));
    scope_set(vm->globals, "True", obj_new_class(TrueClass));
    scope_set(vm->globals, "False", obj_new_class(FalseClass));
    scope_set(vm->globals, "Number", obj_new_class(NumberClass));
    scope_set(vm->globals, "number", obj_new_class(NumberClass));
    scope_set(vm->globals, "Integer", obj_new_class(IntegerClass));
    scope_set(vm->globals, "integer", obj_new_class(IntegerClass));
    scope_set(vm->globals, "Float", obj_new_class(FloatClass));
    scope_set(vm->globals, "float", obj_new_class(FloatClass));
    scope_set(vm->globals, "String", obj_new_class(StringClass));
    scope_set(vm->globals, "string", obj_new_class(StringClass));
    scope_set(vm->globals, "Array", obj_new_class(ArrayClass));
    scope_set(vm->globals, "array", obj_new_class(ArrayClass));
    scope_set(vm->globals, "List", obj_new_class(ArrayClass));
    scope_set(vm->globals, "list", obj_new_class(ArrayClass));
    scope_set(vm->globals, "Dictionary", obj_new_class(DictClass));
    scope_set(vm->globals, "dict", obj_new_class(DictClass));
    scope_set(vm->globals, "JSON", obj_new_class(JSONClass));
    scope_set(vm->globals, "json", obj_new_class(JSONClass));
    scope_set(vm->globals, "Coroutine", obj_new_class(CoroutineClass));
    scope_set(vm->globals, "coroutine", obj_new_class(CoroutineClass));

    vm_init_builtins(vm);
    coroutine_init_methods(vm);
    return vm;
}

void vm_free(VM* vm) {
    if (!vm) return;
    ffi_engine_free(vm->ffi);
    scope_free(vm->globals);
    free(vm);
}

VMResult vm_eval_ast(VM* vm, ASTProgram* prog) {
    VMResult res = { obj_new_nil(), 0, NULL };
    int64_t top_frame_id = get_next_frame_id(vm);
    Scope* top_scope = scope_new(vm->globals, NULL, top_frame_id);

    for (int i = 0; i < prog->count; i++) {
        res = eval_stmt(vm, prog->statements[i], top_scope);
        if (res.error_msg != NULL) break;
        if (vm->unwind_frame_id > 0) {
            res.value = vm->unwind_value;
            res.return_frame_id = 0;
            vm->unwind_frame_id = 0;
            vm->unwind_value = NULL;
            break;
        }
    }

    // top_scope preserved for closures
    return res;
}

VMResult vm_eval_source(VM* vm, const char* source) {
    TokenList tokens = lexer_tokenize(source);
    Parser parser;
    parser_init(&parser, tokens);
    ASTProgram* prog = parser_parse_program(&parser);
    if (!prog) {
        VMResult res;
        res.value = obj_new_nil();
        res.return_frame_id = 0;
        res.error_msg = strdup(parser.error_msg ? parser.error_msg : "parse error");
        parser_free(&parser);
        token_list_free(&tokens);
        return res;
    }
    VMResult res = vm_eval_ast(vm, prog);
    ast_program_free(prog);
    parser_free(&parser);
    token_list_free(&tokens);
    return res;
}

VMResult vm_eval_file(VM* vm, const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        VMResult res;
        res.value = obj_new_nil();
        res.return_frame_id = 0;
        char buf[256];
        snprintf(buf, sizeof(buf), "could not open file '%s'", filename);
        res.error_msg = strdup(buf);
        return res;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = (char*)malloc(size + 1);
    fread(src, 1, size, f);
    src[size] = '\0';
    fclose(f);

    VMResult res = vm_eval_source(vm, src);
    free(src);
    return res;
}

VMResult vm_eval_stmt(VM* vm, ASTStmt* stmt, Scope* scope) {
    return eval_stmt(vm, stmt, scope);
}

static VMResult eval_stmt(VM* vm, ASTStmt* stmt, Scope* scope) {
    VMResult res = { obj_new_nil(), 0, NULL };
    if (vm->unwind_frame_id > 0) {
        res.value = vm->unwind_value;
        res.return_frame_id = vm->unwind_frame_id;
        return res;
    }

    switch (stmt->type) {
        case STMT_SET: {
            res = eval_expr(vm, stmt->as.set.value, scope);
            if (res.error_msg || vm->unwind_frame_id > 0) return res;
            if (vm->yield_requested && vm->active_coro) {
                if (vm->active_coro->yield_assign_var) free(vm->active_coro->yield_assign_var);
                vm->active_coro->yield_assign_var = strdup(stmt->as.set.var_name);
            }
            if (stmt->as.set.var_name[0] == '@') {
                ffi_set_var(vm->ffi, stmt->as.set.var_name, res.value);
            } else {
                scope_set(scope, stmt->as.set.var_name, res.value);
            }
            return res;
        }
        case STMT_RETURN: {
            if (stmt->as.ret.value) {
                res = eval_expr(vm, stmt->as.ret.value, scope);
                if (res.error_msg || vm->unwind_frame_id > 0) return res;
            }
            vm->unwind_frame_id = scope->call_frame_id;
            vm->unwind_value = res.value;
            res.return_frame_id = scope->call_frame_id;
            return res;
        }
        case STMT_METHOD_DEF: {
            res = eval_expr(vm, stmt->as.method_def.receiver, scope);
            if (res.error_msg || vm->unwind_frame_id > 0) return res;
            if (res.value->type != OBJ_CLASS) {
                res.error_msg = strdup("method receiver must be a Class");
                return res;
            }
            Class* c = res.value->as.class_val;
            class_add_user(c, stmt->as.method_def.selector,
                           stmt->as.method_def.params,
                           stmt->as.method_def.param_count,
                           stmt->as.method_def.body.body);
            return res;
        }
        case STMT_EXPR: {
            return eval_expr(vm, stmt->as.expr, scope);
        }
    }
    return res;
}

static VMResult eval_expr(VM* vm, ASTExpr* expr, Scope* scope) {
    VMResult res = { obj_new_nil(), 0, NULL };
    if (vm->unwind_frame_id > 0) {
        res.value = vm->unwind_value;
        res.return_frame_id = vm->unwind_frame_id;
        return res;
    }

    switch (expr->type) {
        case EXPR_LITERAL: {
            switch (expr->as.literal.kind) {
                case LIT_NIL: res.value = obj_new_nil(); break;
                case LIT_BOOL: res.value = obj_new_bool(expr->as.literal.bool_val); break;
                case LIT_INT: res.value = obj_new_int(expr->as.literal.int_val); break;
                case LIT_FLOAT: res.value = obj_new_float(expr->as.literal.flt_val); break;
                case LIT_STRING: {
                    char* s = interpolate_string(vm, expr->as.literal.str_val, scope);
                    res.value = obj_new_string(s);
                    free(s);
                    break;
                }
            }
            return res;
        }
        case EXPR_IDENT: {
            Obj* v = scope_get(scope, expr->as.ident);
            if (!v && vm->globals) {
                v = scope_get(vm->globals, expr->as.ident);
            }
            if (!v) {
                if (strcmp(expr->as.ident, "coroutine") == 0 || strcmp(expr->as.ident, "Coroutine") == 0) {
                    res.value = obj_new_class(CoroutineClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "Array") == 0 || strcmp(expr->as.ident, "array") == 0) {
                    res.value = obj_new_class(ArrayClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "dict") == 0 || strcmp(expr->as.ident, "Dict") == 0) {
                    res.value = obj_new_class(DictClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "JSON") == 0 || strcmp(expr->as.ident, "json") == 0) {
                    res.value = obj_new_class(JSONClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "Object") == 0 || strcmp(expr->as.ident, "object") == 0) {
                    res.value = obj_new_class(ObjectClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "integer") == 0 || strcmp(expr->as.ident, "Integer") == 0) {
                    res.value = obj_new_class(IntegerClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "float") == 0 || strcmp(expr->as.ident, "Float") == 0) {
                    res.value = obj_new_class(FloatClass);
                    return res;
                }
                if (strcmp(expr->as.ident, "string") == 0 || strcmp(expr->as.ident, "String") == 0) {
                    res.value = obj_new_class(StringClass);
                    return res;
                }
                // In Tcl-Talk, unquoted words act as symbols / strings (e.g. class names in subclass:)
                res.value = obj_new_string(expr->as.ident);
            } else {
                res.value = v;
            }
            return res;
        }
        case EXPR_FFI: {
            Obj** argv = (Obj**)malloc(sizeof(Obj*) * expr->as.ffi.arg_count);
            for (int i = 0; i < expr->as.ffi.arg_count; i++) {
                VMResult ar = eval_expr(vm, expr->as.ffi.args[i], scope);
                if (ar.error_msg || ar.return_frame_id > 0) {
                    free(argv);
                    return ar;
                }
                argv[i] = ar.value;
            }
            res.value = ffi_handle_call(vm->ffi, expr->as.ffi.name, expr->as.ffi.arg_count, argv, vm);
            free(argv);
            return res;
        }
        case EXPR_SUBEXPR: {
            return eval_pipeline(vm, expr->as.subexpr, scope);
        }
        case EXPR_BLOCK: {
            res.value = obj_new_block(expr->as.block.params,
                                      expr->as.block.param_count,
                                      expr->as.block.body,
                                      scope,
                                      scope->call_frame_id);
            return res;
        }
        case EXPR_PIPELINE: {
            return eval_pipeline(vm, expr->as.pipeline, scope);
        }
    }
    return res;
}

static VMResult eval_pipeline(VM* vm, ASTPipeline* pipe, Scope* scope) {
    VMResult curr = eval_cascade(vm, pipe->first, NULL, scope);
    if (curr.error_msg || curr.return_frame_id > 0) return curr;

    for (int i = 0; i < pipe->step_count; i++) {
        curr = eval_cascade(vm, pipe->steps[i], curr.value, scope);
        if (curr.error_msg || curr.return_frame_id > 0) return curr;
    }
    return curr;
}

static VMResult eval_cascade(VM* vm, ASTCascade* cascade, Obj* piped_recv, Scope* scope) {
    VMResult first = eval_message_send(vm, cascade->first_msg, piped_recv, scope);
    if (first.error_msg || first.return_frame_id > 0) return first;

    if (cascade->message_count == 0) return first;

    // For cascade (with ','): all subsequent messages are sent to first.value
    Obj* target = first.value;
    for (int i = 0; i < cascade->message_count; i++) {
        ASTMessage* m = cascade->messages[i];
        const char* sel = ast_msg_selector(m);
        Obj** argv = NULL;
        int argc = 0;

        if (m->type == MSG_BINARY) {
            argc = 1;
            argv = (Obj**)malloc(sizeof(Obj*));
            VMResult ar = eval_expr(vm, m->as.binary.arg, scope);
            if (ar.error_msg || ar.return_frame_id > 0) { free(argv); return ar; }
            argv[0] = ar.value;
        } else if (m->type == MSG_KEYWORD) {
            argc = m->as.keyword.count;
            argv = (Obj**)malloc(sizeof(Obj*) * argc);
            for (int k = 0; k < argc; k++) {
                VMResult ar = eval_expr(vm, m->as.keyword.args[k], scope);
                if (ar.error_msg || ar.return_frame_id > 0) { free(argv); return ar; }
                argv[k] = ar.value;
            }
        }

        VMResult mr = vm_send_message(vm, target, sel, argc, argv);
        if (argv) free(argv);
        if (mr.error_msg || mr.return_frame_id > 0) return mr;
    }

    first.value = target;
    return first;
}

static VMResult eval_message_send(VM* vm, ASTMessageSend* send, Obj* piped_recv, Scope* scope) {
    if (send->receiver && send->receiver->type == EXPR_FFI && piped_recv != NULL) {
        int total_argc = send->receiver->as.ffi.arg_count + 1;
        Obj** argv = (Obj**)malloc(sizeof(Obj*) * total_argc);
        argv[0] = piped_recv;
        for (int i = 0; i < send->receiver->as.ffi.arg_count; i++) {
            VMResult ar = eval_expr(vm, send->receiver->as.ffi.args[i], scope);
            if (ar.error_msg || ar.return_frame_id > 0) { free(argv); return ar; }
            argv[i + 1] = ar.value;
        }
        Obj* res = ffi_handle_call(vm->ffi, send->receiver->as.ffi.name, total_argc, argv, vm);
        free(argv);
        VMResult r = { res, 0, NULL };
        return r;
    }

    if (send->receiver && send->receiver->type == EXPR_BLOCK && piped_recv != NULL) {
        VMResult br = eval_expr(vm, send->receiver, scope);
        if (br.error_msg || br.return_frame_id > 0) return br;
        return vm_eval_block(vm, br.value, 1, &piped_recv);
    }

    Obj* recv = NULL;
    if (send->receiver != NULL) {
        VMResult rr = eval_expr(vm, send->receiver, scope);
        if (rr.error_msg || rr.return_frame_id > 0) return rr;
        recv = rr.value;
    } else if (piped_recv != NULL) {
        recv = piped_recv;
    } else {
        VMResult err = { obj_new_nil(), 0, strdup("message send has no receiver") };
        return err;
    }

    if (send->msg == NULL) {
        VMResult r = { recv, 0, NULL };
        return r;
    }

    ASTMessage* m = send->msg;
    const char* sel = ast_msg_selector(m);
    Obj** argv = NULL;
    int argc = 0;

    if (m->type == MSG_BINARY) {
        argc = 1;
        argv = (Obj**)malloc(sizeof(Obj*));
        VMResult ar = eval_expr(vm, m->as.binary.arg, scope);
        if (ar.error_msg || ar.return_frame_id > 0) { free(argv); return ar; }
        argv[0] = ar.value;
    } else if (m->type == MSG_KEYWORD) {
        argc = m->as.keyword.count;
        argv = (Obj**)malloc(sizeof(Obj*) * argc);
        for (int k = 0; k < argc; k++) {
            VMResult ar = eval_expr(vm, m->as.keyword.args[k], scope);
            if (ar.error_msg || ar.return_frame_id > 0) { free(argv); return ar; }
            argv[k] = ar.value;
        }
    }

    VMResult res = vm_send_message(vm, recv, sel, argc, argv);
    if (argv) free(argv);
    return res;
}

VMResult vm_send_message(VM* vm, Obj* recv, const char* selector, int argc, Obj** argv) {
    if (!recv) recv = obj_new_nil();
    Class* c = recv->klass;
    Method* m = class_lookup_method(c, selector);

    if (!m && !strchr(selector, ':') && argc == 1) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s:", selector);
        m = class_lookup_method(c, buf);
    }

    if (!m) {
        char buf[256];
        snprintf(buf, sizeof(buf), "MessageNotUnderstood: %s does not understand '%s'", c->name, selector);
        VMResult err = { obj_new_nil(), 0, strdup(buf) };
        return err;
    }

    if (m->type == METHOD_NATIVE) {
        Obj* ret = m->as.native_fn(recv, argc, argv, vm);
        VMResult r = { ret ? ret : obj_new_nil(), 0, NULL };
        return r;
    }

    // User method
    int64_t frame_id = get_next_frame_id(vm);
    Scope* method_scope = scope_new(vm->globals, recv, frame_id);

    for (int i = 0; i < m->as.user.param_count; i++) {
        if (i < argc) {
            scope_define(method_scope, m->as.user.params[i], argv[i]);
        } else {
            scope_define(method_scope, m->as.user.params[i], obj_new_nil());
        }
    }

    VMResult res = { recv, 0, NULL };
    for (int i = 0; i < m->as.user.body->count; i++) {
        res = eval_stmt(vm, m->as.user.body->statements[i], method_scope);
        if (res.error_msg != NULL || vm->unwind_frame_id > 0) break;
    }

    if (vm->unwind_frame_id == frame_id) {
        res.value = vm->unwind_value;
        res.return_frame_id = 0;
        vm->unwind_frame_id = 0;
        vm->unwind_value = NULL;
    } else if (vm->unwind_frame_id == 0 && res.error_msg == NULL) {
        res.value = recv;
    }

    // method_scope preserved for closures
    return res;
}

VMResult vm_eval_block(VM* vm, Obj* block_val, int argc, Obj** argv) {
    if (block_val->type != OBJ_BLOCK) {
        VMResult err = { obj_new_nil(), 0, strdup("cannot evaluate non-block value") };
        return err;
    }

    Scope* block_scope = scope_new(block_val->as.block.captured_scope,
                                   block_val->as.block.captured_scope ? block_val->as.block.captured_scope->self : NULL,
                                   block_val->as.block.enclosing_frame_id);

    for (int i = 0; i < block_val->as.block.param_count; i++) {
        if (i < argc) {
            scope_define(block_scope, block_val->as.block.params[i], argv[i]);
        } else {
            scope_define(block_scope, block_val->as.block.params[i], obj_new_nil());
        }
    }

    VMResult res = { obj_new_nil(), 0, NULL };
    for (int i = 0; i < block_val->as.block.body->count; i++) {
        res = eval_stmt(vm, block_val->as.block.body->statements[i], block_scope);
        if (res.error_msg != NULL || vm->unwind_frame_id > 0) break;
    }

    // block_scope preserved for inner closures
    return res;
}

static char* interpolate_string(VM* vm, const char* str, Scope* scope) {
    int cap = (int)strlen(str) * 2 + 32;
    char* buf = (char*)malloc(cap);
    int len = 0;
    int pos = 0;
    int slen = (int)strlen(str);

    while (pos < slen) {
        char ch = str[pos];

        if (ch == '$' && pos + 1 < slen && str[pos + 1] == '{') {
            // ${varName}
            int start = pos + 2;
            int end = start;
            while (end < slen && str[end] != '}') end++;
            if (end < slen && str[end] == '}') {
                int id_len = end - start;
                char* id = (char*)malloc(id_len + 1);
                memcpy(id, str + start, id_len);
                id[id_len] = '\0';
                Obj* val = scope_get(scope, id);
                char* s = val ? obj_to_string(val) : strdup("");
                int added = (int)strlen(s);
                while (len + added + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                memcpy(buf + len, s, added);
                len += added;
                free(s);
                free(id);
                pos = end + 1;
                continue;
            }
        } else if (ch == '$' && pos + 1 < slen && (isalpha(str[pos + 1]) || str[pos + 1] == '_')) {
            // $varName
            int start = pos + 1;
            int end = start;
            while (end < slen && (isalnum(str[end]) || str[end] == '_')) end++;
            int id_len = end - start;
            char* id = (char*)malloc(id_len + 1);
            memcpy(id, str + start, id_len);
            id[id_len] = '\0';
            Obj* val = scope_get(scope, id);
            char* s = val ? obj_to_string(val) : strdup("");
            int added = (int)strlen(s);
            while (len + added + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            memcpy(buf + len, s, added);
            len += added;
            free(s);
            free(id);
            pos = end;
            continue;
        } else if (ch == '@' && pos + 1 < slen && (str[pos + 1] >= 'A' && str[pos + 1] <= 'Z')) {
            // @VAR_NAME
            int start = pos;
            int end = start + 1;
            while (end < slen && (isalnum(str[end]) || str[end] == '_')) end++;
            int id_len = end - start;
            char* id = (char*)malloc(id_len + 1);
            memcpy(id, str + start, id_len);
            id[id_len] = '\0';
            Obj* val = ffi_get_var(vm->ffi, id);
            char* s = val ? obj_to_string(val) : strdup(id);
            int added = (int)strlen(s);
            while (len + added + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            memcpy(buf + len, s, added);
            len += added;
            free(s);
            free(id);
            pos = end;
            continue;
        } else if (ch == '[') {
            // Subexpression [ ... ]
            int start = pos + 1;
            int depth = 1;
            int end = start;
            while (end < slen && depth > 0) {
                if (str[end] == '[') depth++;
                else if (str[end] == ']') depth--;
                end++;
            }
            if (depth == 0) {
                int sub_len = end - start - 1;
                char* sub_src = (char*)malloc(sub_len + 1);
                memcpy(sub_src, str + start, sub_len);
                sub_src[sub_len] = '\0';

                TokenList toks = lexer_tokenize(sub_src);
                Parser p;
                parser_init(&p, toks);
                ASTProgram* sub_prog = parser_parse_program(&p);
                char* s = NULL;
                if (sub_prog && p.error_msg == NULL && sub_prog->count > 0) {
                    VMResult sub_res = { obj_new_nil(), 0, NULL };
                    for (int k = 0; k < sub_prog->count; k++) {
                        sub_res = eval_stmt(vm, sub_prog->statements[k], scope);
                    }
                    if (sub_res.error_msg == NULL) {
                        s = obj_to_string(sub_res.value);
                    }
                    ast_program_free(sub_prog);
                }
                if (!s) {
                    // Fallback to literal [sub_src] if not a valid subexpression
                    int sub_src_len = (int)strlen(sub_src);
                    s = (char*)malloc(sub_src_len + 3);
                    s[0] = '[';
                    memcpy(s + 1, sub_src, sub_src_len);
                    s[sub_src_len + 1] = ']';
                    s[sub_src_len + 2] = '\0';
                }
                parser_free(&p);
                token_list_free(&toks);
                free(sub_src);

                int added = (int)strlen(s);
                while (len + added + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                memcpy(buf + len, s, added);
                len += added;
                free(s);
                pos = end;
                continue;
            }
        }

        if (len + 1 >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        buf[len++] = ch;
        pos++;
    }

    buf[len] = '\0';
    return buf;
}

// ================= Built-in Smalltalk Library =================

static void extract_names_from_block(ASTBlockExpr* blk, char*** out_names, int* count, int* cap) {
    if (!blk || !blk->body) return;
    for (int i = 0; i < blk->body->count; i++) {
        ASTStmt* s = blk->body->statements[i];
        if (s->type == STMT_EXPR && s->as.expr->type == EXPR_PIPELINE) {
            ASTPipeline* p = s->as.expr->as.pipeline;
            if (p && p->first && p->first->first_msg) {
                ASTMessageSend* ms = p->first->first_msg;
                if (ms->receiver && ms->receiver->type == EXPR_IDENT) {
                    if (*count >= *cap) { *cap *= 2; *out_names = (char**)realloc(*out_names, sizeof(char*) * *cap); }
                    (*out_names)[(*count)++] = strdup(ms->receiver->as.ident);
                }
                if (ms->msg) {
                    if (ms->msg->type == MSG_UNARY) {
                        if (*count >= *cap) { *cap *= 2; *out_names = (char**)realloc(*out_names, sizeof(char*) * *cap); }
                        (*out_names)[(*count)++] = strdup(ms->msg->as.unary_selector);
                    } else if (ms->msg->type == MSG_KEYWORD) {
                        for (int k = 0; k < ms->msg->as.keyword.count; k++) {
                            if (*count >= *cap) { *cap *= 2; *out_names = (char**)realloc(*out_names, sizeof(char*) * *cap); }
                            (*out_names)[(*count)++] = strdup(ms->msg->as.keyword.keywords[k]);
                            if (ms->msg->as.keyword.args[k]->type == EXPR_IDENT) {
                                if (*count >= *cap) { *cap *= 2; *out_names = (char**)realloc(*out_names, sizeof(char*) * *cap); }
                                (*out_names)[(*count)++] = strdup(ms->msg->as.keyword.args[k]->as.ident);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Built-in methods
static Obj* m_obj_as_json(Obj* recv, int argc, Obj** argv, VM* vm) {
    char* s = json_stringify(recv);
    Obj* res = obj_new_string(s);
    free(s);
    return res;
}

static Obj* m_json_parse(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_nil();
    char* s = obj_to_string(argv[0]);
    Obj* res = json_parse(s);
    free(s);
    return res;
}

static Obj* m_json_stringify(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_nil();
    char* s = json_stringify(argv[0]);
    Obj* res = obj_new_string(s);
    free(s);
    return res;
}

static Obj* m_str_parse_json(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING) return obj_new_nil();
    return json_parse(recv->as.string.chars);
}

static Obj* m_obj_print(Obj* recv, int argc, Obj** argv, VM* vm) {
    char* s = obj_to_string(recv);
    fputs(s, stdout);
    free(s);
    return recv;
}

static Obj* m_obj_println(Obj* recv, int argc, Obj** argv, VM* vm) {
    char* s = obj_to_string(recv);
    puts(s);
    free(s);
    return recv;
}

static Obj* m_obj_as_string(Obj* recv, int argc, Obj** argv, VM* vm) {
    char* s = obj_to_string(recv);
    Obj* res = obj_new_string(s);
    free(s);
    return res;
}

static Obj* m_obj_eq(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(false);
    return obj_new_bool(obj_equals(recv, argv[0]));
}

static Obj* m_obj_neq(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(true);
    return obj_new_bool(!obj_equals(recv, argv[0]));
}

static Obj* m_obj_is_nil(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_bool(recv->type == OBJ_NIL);
}

static Obj* m_obj_not_nil(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_bool(recv->type != OBJ_NIL);
}

static Obj* m_obj_class(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_class(recv->klass);
}

static Obj* m_class_new(Obj* recv, int argc, Obj** argv, VM* vm) {
    Class* c = (recv->type == OBJ_CLASS) ? recv->as.class_val : recv->klass;
    if (c == ArrayClass) return obj_new_array();
    if (c == DictClass) return obj_new_dict();
    return obj_new_instance(c);
}

static Obj* m_class_subclass(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_nil();
    Class* super_c = (recv->type == OBJ_CLASS) ? recv->as.class_val : recv->klass;
    char* name = obj_to_string(argv[0]);
    Class* sub = class_new(name, super_c, NULL, 0);
    Obj* c_obj = obj_new_class(sub);
    scope_set(vm->globals, name, c_obj);
    free(name);
    return c_obj;
}

static Obj* m_class_subclass_vars(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 2) return obj_new_nil();
    Class* super_c = (recv->type == OBJ_CLASS) ? recv->as.class_val : recv->klass;
    char* name = obj_to_string(argv[0]);

    int cap = 8;
    int count = 0;
    char** ivars = (char**)malloc(sizeof(char*) * cap);

    if (argv[1]->type == OBJ_BLOCK) {
        ASTBlockExpr blk;
        blk.params = argv[1]->as.block.params;
        blk.param_count = argv[1]->as.block.param_count;
        blk.body = argv[1]->as.block.body;
        extract_names_from_block(&blk, &ivars, &count, &cap);
    } else {
        char* v = obj_to_string(argv[1]);
        ivars[count++] = v;
    }

    Class* sub = class_new(name, super_c, ivars, count);
    Obj* c_obj = obj_new_class(sub);
    scope_set(vm->globals, name, c_obj);
    free(name);
    return c_obj;
}

// Boolean methods
static Obj* m_bool_not(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_bool(!obj_is_truthy(recv));
}
static Obj* m_bool_and(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(false);
    return obj_new_bool(obj_is_truthy(recv) && obj_is_truthy(argv[0]));
}
static Obj* m_bool_or(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(obj_is_truthy(recv));
    return obj_new_bool(obj_is_truthy(recv) || obj_is_truthy(argv[0]));
}
static Obj* m_bool_if_true(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_nil();
    if (obj_is_truthy(recv)) {
        VMResult r = vm_eval_block(vm, argv[0], 0, NULL);
        return r.value;
    }
    return obj_new_nil();
}
static Obj* m_bool_if_false(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_nil();
    if (!obj_is_truthy(recv)) {
        VMResult r = vm_eval_block(vm, argv[0], 0, NULL);
        return r.value;
    }
    return obj_new_nil();
}
static Obj* m_bool_if_true_false(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 2) return obj_new_nil();
    if (obj_is_truthy(recv)) {
        VMResult r = vm_eval_block(vm, argv[0], 0, NULL);
        return r.value;
    } else {
        VMResult r = vm_eval_block(vm, argv[1], 0, NULL);
        return r.value;
    }
}
static Obj* m_bool_if_false_true(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 2) return obj_new_nil();
    if (!obj_is_truthy(recv)) {
        VMResult r = vm_eval_block(vm, argv[0], 0, NULL);
        return r.value;
    } else {
        VMResult r = vm_eval_block(vm, argv[1], 0, NULL);
        return r.value;
    }
}

// Number methods
static Obj* m_num_add(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    if (recv->type == OBJ_INT && argv[0]->type == OBJ_INT) {
        return obj_new_int(recv->as.integer + argv[0]->as.integer);
    }
    return obj_new_float(obj_to_float(recv) + obj_to_float(argv[0]));
}
static Obj* m_num_sub(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    if (recv->type == OBJ_INT && argv[0]->type == OBJ_INT) {
        return obj_new_int(recv->as.integer - argv[0]->as.integer);
    }
    return obj_new_float(obj_to_float(recv) - obj_to_float(argv[0]));
}
static Obj* m_num_mul(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    if (recv->type == OBJ_INT && argv[0]->type == OBJ_INT) {
        return obj_new_int(recv->as.integer * argv[0]->as.integer);
    }
    return obj_new_float(obj_to_float(recv) * obj_to_float(argv[0]));
}
static Obj* m_num_div(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    double denom = obj_to_float(argv[0]);
    if (denom == 0.0) return obj_new_nil();
    if (recv->type == OBJ_INT && argv[0]->type == OBJ_INT && argv[0]->as.integer != 0 && (recv->as.integer % argv[0]->as.integer == 0)) {
        return obj_new_int(recv->as.integer / argv[0]->as.integer);
    }
    return obj_new_float(obj_to_float(recv) / denom);
}
static Obj* m_num_mod(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    int64_t d = obj_to_int(argv[0]);
    if (d == 0) return obj_new_nil();
    return obj_new_int(obj_to_int(recv) % d);
}
static Obj* m_num_lt(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(false);
    return obj_new_bool(obj_to_float(recv) < obj_to_float(argv[0]));
}
static Obj* m_num_gt(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(false);
    return obj_new_bool(obj_to_float(recv) > obj_to_float(argv[0]));
}
static Obj* m_num_lte(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(false);
    return obj_new_bool(obj_to_float(recv) <= obj_to_float(argv[0]));
}
static Obj* m_num_gte(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_bool(false);
    return obj_new_bool(obj_to_float(recv) >= obj_to_float(argv[0]));
}

// Dict methods
static Obj* m_dict_at(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1 || recv->type != OBJ_DICT) return obj_new_nil();
    char* key = obj_to_string(argv[0]);
    for (int i = 0; i < recv->as.dict.count; i++) {
        if (strcmp(recv->as.dict.entries[i].key, key) == 0) {
            free(key);
            return recv->as.dict.entries[i].value;
        }
    }
    free(key);
    return obj_new_nil();
}
static Obj* m_dict_at_put(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 2 || recv->type != OBJ_DICT) return obj_new_nil();
    char* key = obj_to_string(argv[0]);
    for (int i = 0; i < recv->as.dict.count; i++) {
        if (strcmp(recv->as.dict.entries[i].key, key) == 0) {
            recv->as.dict.entries[i].value = argv[1];
            free(key);
            return argv[1];
        }
    }
    if (recv->as.dict.count >= recv->as.dict.capacity) {
        recv->as.dict.capacity *= 2;
        recv->as.dict.entries = (DictEntry*)realloc(recv->as.dict.entries, sizeof(DictEntry) * recv->as.dict.capacity);
    }
    recv->as.dict.entries[recv->as.dict.count].key = key;
    recv->as.dict.entries[recv->as.dict.count].value = argv[1];
    recv->as.dict.count++;
    return argv[1];
}

// String methods
static Obj* m_str_size(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_int(recv->as.string.length);
}
static Obj* m_str_add(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    char* s2 = obj_to_string(argv[0]);
    int len = recv->as.string.length + (int)strlen(s2);
    char* buf = (char*)malloc(len + 1);
    strcpy(buf, recv->as.string.chars);
    strcat(buf, s2);
    free(s2);
    Obj* res = obj_new_string(buf);
    free(buf);
    return res;
}

// Array methods
static Obj* m_arr_size(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_int(recv->as.array.count);
}
static Obj* m_arr_is_empty(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_bool(recv->as.array.count == 0);
}
static Obj* m_arr_first(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->as.array.count == 0) return obj_new_nil();
    return recv->as.array.items[0];
}
static Obj* m_arr_last(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->as.array.count == 0) return obj_new_nil();
    return recv->as.array.items[recv->as.array.count - 1];
}
static Obj* m_arr_at(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return obj_new_nil();
    int idx = (int)obj_to_int(argv[0]);
    // 1-based index support
    if (idx >= 1 && idx <= recv->as.array.count) return recv->as.array.items[idx - 1];
    if (idx >= 0 && idx < recv->as.array.count) return recv->as.array.items[idx];
    return obj_new_nil();
}
static Obj* m_arr_at_put(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 2) return obj_new_nil();
    int idx = (int)obj_to_int(argv[0]);
    if (idx >= 1 && idx <= recv->as.array.count) {
        recv->as.array.items[idx - 1] = argv[1];
        return argv[1];
    }
    if (idx >= 0 && idx < recv->as.array.count) {
        recv->as.array.items[idx] = argv[1];
        return argv[1];
    }
    return obj_new_nil();
}
static Obj* m_arr_add(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    if (recv->as.array.count >= recv->as.array.capacity) {
        recv->as.array.capacity *= 2;
        recv->as.array.items = (Obj**)realloc(recv->as.array.items, sizeof(Obj*) * recv->as.array.capacity);
    }
    recv->as.array.items[recv->as.array.count++] = argv[0];
    return recv;
}
static Obj* m_arr_select(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    Obj* block = argv[0];
    Obj* res = obj_new_array();
    for (int i = 0; i < recv->as.array.count; i++) {
        Obj* item = recv->as.array.items[i];
        VMResult r = vm_eval_block(vm, block, 1, &item);
        if (obj_is_truthy(r.value)) {
            m_arr_add(res, 1, &item, vm);
        }
    }
    return res;
}
static Obj* m_arr_reject(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    Obj* block = argv[0];
    Obj* res = obj_new_array();
    for (int i = 0; i < recv->as.array.count; i++) {
        Obj* item = recv->as.array.items[i];
        VMResult r = vm_eval_block(vm, block, 1, &item);
        if (!obj_is_truthy(r.value)) {
            m_arr_add(res, 1, &item, vm);
        }
    }
    return res;
}
static Obj* m_arr_collect(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    Obj* block = argv[0];
    Obj* res = obj_new_array();
    for (int i = 0; i < recv->as.array.count; i++) {
        Obj* item = recv->as.array.items[i];
        VMResult r = vm_eval_block(vm, block, 1, &item);
        m_arr_add(res, 1, &r.value, vm);
    }
    return res;
}
static Obj* m_arr_inject_into(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 2) return obj_new_nil();
    Obj* acc = argv[0];
    Obj* block = argv[1];
    for (int i = 0; i < recv->as.array.count; i++) {
        Obj* args[2] = { acc, recv->as.array.items[i] };
        VMResult r = vm_eval_block(vm, block, 2, args);
        acc = r.value;
    }
    return acc;
}
static Obj* m_arr_sort_by(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    Obj* block = argv[0];
    Obj* res = obj_new_array();
    for (int i = 0; i < recv->as.array.count; i++) {
        m_arr_add(res, 1, &recv->as.array.items[i], vm);
    }
    // Simple stable insertion sort using block comparator
    for (int i = 1; i < res->as.array.count; i++) {
        Obj* key = res->as.array.items[i];
        int j = i - 1;
        while (j >= 0) {
            Obj* args[2] = { key, res->as.array.items[j] };
            VMResult r = vm_eval_block(vm, block, 2, args);
            if (obj_is_truthy(r.value)) {
                res->as.array.items[j + 1] = res->as.array.items[j];
                j--;
            } else {
                break;
            }
        }
        res->as.array.items[j + 1] = key;
    }
    return res;
}
static Obj* m_arr_do(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    Obj* block = argv[0];
    for (int i = 0; i < recv->as.array.count; i++) {
        vm_eval_block(vm, block, 1, &recv->as.array.items[i]);
    }
    return recv;
}

// Extended string methods
static Obj* m_str_split(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING || argc < 1) return obj_new_nil();
    char* delim = obj_to_string(argv[0]);
    Obj* arr = obj_new_array();
    char* s = strdup(recv->as.string.chars);
    int dlen = (int)strlen(delim);

    char* curr = s;
    while (1) {
        char* next = (dlen > 0) ? strstr(curr, delim) : NULL;
        if (!next) {
            Obj* part = obj_new_string(curr);
            m_arr_add(arr, 1, &part, vm);
            break;
        }
        *next = '\0';
        Obj* part = obj_new_string(curr);
        m_arr_add(arr, 1, &part, vm);
        curr = next + dlen;
    }
    free(s);
    free(delim);
    return arr;
}
static Obj* m_str_contains(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING || argc < 1) return obj_new_bool(false);
    char* sub = obj_to_string(argv[0]);
    bool has = strstr(recv->as.string.chars, sub) != NULL;
    free(sub);
    return obj_new_bool(has);
}
static Obj* m_str_starts_with(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING || argc < 1) return obj_new_bool(false);
    char* sub = obj_to_string(argv[0]);
    int slen = (int)strlen(sub);
    bool starts = (strncmp(recv->as.string.chars, sub, slen) == 0);
    free(sub);
    return obj_new_bool(starts);
}
static Obj* m_str_ends_with(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING || argc < 1) return obj_new_bool(false);
    char* sub = obj_to_string(argv[0]);
    int slen = (int)strlen(sub);
    int rlen = recv->as.string.length;
    bool ends = (rlen >= slen && strcmp(recv->as.string.chars + (rlen - slen), sub) == 0);
    free(sub);
    return obj_new_bool(ends);
}
static Obj* m_str_trim(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING) return recv;
    const char* start = recv->as.string.chars;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
    const char* end = recv->as.string.chars + recv->as.string.length - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    int tlen = (int)(end - start + 1);
    if (tlen < 0) tlen = 0;
    char* buf = (char*)malloc(tlen + 1);
    memcpy(buf, start, tlen);
    buf[tlen] = '\0';
    Obj* res = obj_new_string(buf);
    free(buf);
    return res;
}
static Obj* m_str_to_upper(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING) return recv;
    char* buf = strdup(recv->as.string.chars);
    for (int i = 0; buf[i]; i++) buf[i] = toupper((unsigned char)buf[i]);
    Obj* res = obj_new_string(buf);
    free(buf);
    return res;
}
static Obj* m_str_to_lower(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING) return recv;
    char* buf = strdup(recv->as.string.chars);
    for (int i = 0; buf[i]; i++) buf[i] = tolower((unsigned char)buf[i]);
    Obj* res = obj_new_string(buf);
    free(buf);
    return res;
}
static Obj* m_str_replace_with(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING || argc < 2) return recv;
    char* old_s = obj_to_string(argv[0]);
    char* new_s = obj_to_string(argv[1]);
    int old_len = (int)strlen(old_s);
    int new_len = (int)strlen(new_s);
    if (old_len == 0) { free(old_s); free(new_s); return recv; }

    int cap = recv->as.string.length * 2 + 32;
    char* out = (char*)malloc(cap);
    int out_len = 0;
    const char* p = recv->as.string.chars;

    while (*p) {
        if (strncmp(p, old_s, old_len) == 0) {
            while (out_len + new_len + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
            memcpy(out + out_len, new_s, new_len);
            out_len += new_len;
            p += old_len;
        } else {
            if (out_len + 2 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
            out[out_len++] = *p++;
        }
    }
    out[out_len] = '\0';
    free(old_s); free(new_s);
    Obj* res = obj_new_string(out);
    free(out);
    return res;
}
static Obj* m_str_as_integer(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING) return obj_new_int(0);
    return obj_new_int(atoll(recv->as.string.chars));
}
static Obj* m_str_as_float(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_STRING) return obj_new_float(0.0);
    return obj_new_float(atof(recv->as.string.chars));
}

// Extended array methods
static Obj* m_arr_join(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_ARRAY) return obj_new_string("");
    char* sep = (argc > 0) ? obj_to_string(argv[0]) : strdup("");
    int cap = 64;
    int len = 0;
    char* out = (char*)malloc(cap);
    out[0] = '\0';
    int sep_len = (int)strlen(sep);

    for (int i = 0; i < recv->as.array.count; i++) {
        if (i > 0 && sep_len > 0) {
            while (len + sep_len + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
            memcpy(out + len, sep, sep_len);
            len += sep_len;
            out[len] = '\0';
        }
        char* is = obj_to_string(recv->as.array.items[i]);
        int is_len = (int)strlen(is);
        while (len + is_len + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
        memcpy(out + len, is, is_len);
        len += is_len;
        out[len] = '\0';
        free(is);
    }
    free(sep);
    Obj* res = obj_new_string(out);
    free(out);
    return res;
}
static Obj* m_arr_contains(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_ARRAY || argc < 1) return obj_new_bool(false);
    for (int i = 0; i < recv->as.array.count; i++) {
        if (obj_equals(recv->as.array.items[i], argv[0])) return obj_new_bool(true);
    }
    return obj_new_bool(false);
}
static Obj* m_arr_index_of(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_ARRAY || argc < 1) return obj_new_int(0);
    for (int i = 0; i < recv->as.array.count; i++) {
        if (obj_equals(recv->as.array.items[i], argv[0])) return obj_new_int(i + 1);
    }
    return obj_new_int(0);
}
static Obj* m_arr_reverse(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (recv->type != OBJ_ARRAY) return recv;
    Obj* res = obj_new_array();
    for (int i = recv->as.array.count - 1; i >= 0; i--) {
        m_arr_add(res, 1, &recv->as.array.items[i], vm);
    }
    return res;
}

// Extended number methods
static Obj* m_num_as_float(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_float(obj_to_float(recv));
}
static Obj* m_num_as_integer(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_int(obj_to_int(recv));
}
static Obj* m_num_round(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_int((int64_t)round(obj_to_float(recv)));
}
static Obj* m_num_floor(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_int((int64_t)floor(obj_to_float(recv)));
}
static Obj* m_num_ceil(Obj* recv, int argc, Obj** argv, VM* vm) {
    return obj_new_int((int64_t)ceil(obj_to_float(recv)));
}
static Obj* m_num_min(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    if (obj_to_float(recv) < obj_to_float(argv[0])) return recv;
    return argv[0];
}
static Obj* m_num_max(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    if (obj_to_float(recv) > obj_to_float(argv[0])) return recv;
    return argv[0];
}
static Obj* m_num_pow(Obj* recv, int argc, Obj** argv, VM* vm) {
    if (argc < 1) return recv;
    double res = pow(obj_to_float(recv), obj_to_float(argv[0]));
    if (recv->type == OBJ_INT && argv[0]->type == OBJ_INT && argv[0]->as.integer >= 0) {
        return obj_new_int((int64_t)res);
    }
    return obj_new_float(res);
}

// BlockClosure methods
static Obj* m_block_value(Obj* recv, int argc, Obj** argv, VM* vm) {
    VMResult r = vm_eval_block(vm, recv, argc, argv);
    return r.value;
}

void vm_init_builtins(VM* vm) {
    // Object
    class_add_native(ObjectClass, "print", m_obj_print);
    class_add_native(ObjectClass, "println", m_obj_println);
    class_add_native(ObjectClass, "asString", m_obj_as_string);
    class_add_native(ObjectClass, "asJson", m_obj_as_json);
    class_add_native(ObjectClass, "==", m_obj_eq);
    class_add_native(ObjectClass, "!=", m_obj_neq);
    class_add_native(ObjectClass, "isNil", m_obj_is_nil);
    class_add_native(ObjectClass, "notNil", m_obj_not_nil);
    class_add_native(ObjectClass, "class", m_obj_class);
    class_add_native(ObjectClass, "new", m_class_new);
    class_add_native(ObjectClass, "subclass:", m_class_subclass);
    class_add_native(ObjectClass, "subclass:vars:", m_class_subclass_vars);

    // JSON
    class_add_native(JSONClass, "parse:", m_json_parse);
    class_add_native(JSONClass, "stringify:", m_json_stringify);

    // Class
    class_add_native(MetaclassClass, "new", m_class_new);
    class_add_native(MetaclassClass, "subclass:", m_class_subclass);
    class_add_native(MetaclassClass, "subclass:vars:", m_class_subclass_vars);
    class_add_native(MetaclassClass, "parse:", m_json_parse);
    class_add_native(MetaclassClass, "stringify:", m_json_stringify);

    // Boolean
    class_add_native(BooleanClass, "not", m_bool_not);
    class_add_native(BooleanClass, "&", m_bool_and);
    class_add_native(BooleanClass, "|", m_bool_or);
    class_add_native(BooleanClass, "ifTrue:", m_bool_if_true);
    class_add_native(BooleanClass, "ifFalse:", m_bool_if_false);
    class_add_native(BooleanClass, "ifTrue:ifFalse:", m_bool_if_true_false);
    class_add_native(BooleanClass, "ifFalse:ifTrue:", m_bool_if_false_true);

    class_add_native(TrueClass, "ifTrue:", m_bool_if_true);
    class_add_native(TrueClass, "ifFalse:", m_bool_if_false);
    class_add_native(TrueClass, "ifTrue:ifFalse:", m_bool_if_true_false);
    class_add_native(TrueClass, "ifFalse:ifTrue:", m_bool_if_false_true);

    class_add_native(FalseClass, "ifTrue:", m_bool_if_true);
    class_add_native(FalseClass, "ifFalse:", m_bool_if_false);
    class_add_native(FalseClass, "ifTrue:ifFalse:", m_bool_if_true_false);
    class_add_native(FalseClass, "ifFalse:ifTrue:", m_bool_if_false_true);

    // Number / Integer / Float
    Class* num_classes[3] = { NumberClass, IntegerClass, FloatClass };
    for (int i = 0; i < 3; i++) {
        class_add_native(num_classes[i], "+", m_num_add);
        class_add_native(num_classes[i], "-", m_num_sub);
        class_add_native(num_classes[i], "*", m_num_mul);
        class_add_native(num_classes[i], "/", m_num_div);
        class_add_native(num_classes[i], "%", m_num_mod);
        class_add_native(num_classes[i], "<", m_num_lt);
        class_add_native(num_classes[i], ">", m_num_gt);
        class_add_native(num_classes[i], "<=", m_num_lte);
        class_add_native(num_classes[i], ">=", m_num_gte);
        class_add_native(num_classes[i], "asFloat", m_num_as_float);
        class_add_native(num_classes[i], "asInteger", m_num_as_integer);
        class_add_native(num_classes[i], "asInt", m_num_as_integer);
        class_add_native(num_classes[i], "round", m_num_round);
        class_add_native(num_classes[i], "floor", m_num_floor);
        class_add_native(num_classes[i], "ceil", m_num_ceil);
        class_add_native(num_classes[i], "min:", m_num_min);
        class_add_native(num_classes[i], "max:", m_num_max);
        class_add_native(num_classes[i], "pow:", m_num_pow);
    }

    // String
    class_add_native(StringClass, "size", m_str_size);
    class_add_native(StringClass, "length", m_str_size);
    class_add_native(StringClass, "+", m_str_add);
    class_add_native(StringClass, ",", m_str_add);
    class_add_native(StringClass, "split:", m_str_split);
    class_add_native(StringClass, "contains:", m_str_contains);
    class_add_native(StringClass, "startsWith:", m_str_starts_with);
    class_add_native(StringClass, "endsWith:", m_str_ends_with);
    class_add_native(StringClass, "trim", m_str_trim);
    class_add_native(StringClass, "toUpper", m_str_to_upper);
    class_add_native(StringClass, "toLower", m_str_to_lower);
    class_add_native(StringClass, "replace:with:", m_str_replace_with);
    class_add_native(StringClass, "asInteger", m_str_as_integer);
    class_add_native(StringClass, "asInt", m_str_as_integer);
    class_add_native(StringClass, "asFloat", m_str_as_float);
    class_add_native(StringClass, "parseJson", m_str_parse_json);

    // Array
    class_add_native(ArrayClass, "size", m_arr_size);
    class_add_native(ArrayClass, "count", m_arr_size);
    class_add_native(ArrayClass, "length", m_arr_size);
    class_add_native(ArrayClass, "isEmpty", m_arr_is_empty);
    class_add_native(ArrayClass, "first", m_arr_first);
    class_add_native(ArrayClass, "last", m_arr_last);
    class_add_native(ArrayClass, "at:", m_arr_at);
    class_add_native(ArrayClass, "at:put:", m_arr_at_put);
    class_add_native(ArrayClass, "add:", m_arr_add);
    class_add_native(ArrayClass, "select:", m_arr_select);
    class_add_native(ArrayClass, "reject:", m_arr_reject);
    class_add_native(ArrayClass, "collect:", m_arr_collect);
    class_add_native(ArrayClass, "inject:into:", m_arr_inject_into);
    class_add_native(ArrayClass, "sortBy:", m_arr_sort_by);
    class_add_native(ArrayClass, "do:", m_arr_do);
    class_add_native(ArrayClass, "join:", m_arr_join);
    class_add_native(ArrayClass, "contains:", m_arr_contains);
    class_add_native(ArrayClass, "indexOf:", m_arr_index_of);
    class_add_native(ArrayClass, "reverse", m_arr_reverse);
    class_add_native(ArrayClass, "reversed", m_arr_reverse);

    // Dictionary
    class_add_native(DictClass, "at:", m_dict_at);
    class_add_native(DictClass, "at:put:", m_dict_at_put);

    // BlockClosure
    class_add_native(BlockClosureClass, "value", m_block_value);
    class_add_native(BlockClosureClass, "value:", m_block_value);
    class_add_native(BlockClosureClass, "value:value:", m_block_value);
}
