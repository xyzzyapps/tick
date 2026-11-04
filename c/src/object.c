#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Class* ObjectClass = NULL;
Class* MetaclassClass = NULL;
Class* UndefinedObjectClass = NULL;
Class* BooleanClass = NULL;
Class* TrueClass = NULL;
Class* FalseClass = NULL;
Class* NumberClass = NULL;
Class* IntegerClass = NULL;
Class* FloatClass = NULL;
Class* StringClass = NULL;
Class* ArrayClass = NULL;
Class* DictClass = NULL;
Class* BlockClosureClass = NULL;
Class* FFIHandleClass = NULL;
Class* JSONClass = NULL;
Class* CoroutineClass = NULL;

Class* class_new(const char* name, Class* superclass, char** ivars, int ivar_count) {
    Class* c = (Class*)malloc(sizeof(Class));
    c->name = strdup(name);
    c->superclass = superclass;
    c->ivar_names = ivars;
    c->ivar_count = ivar_count;
    c->method_count = 0;
    c->method_capacity = 16;
    c->methods = (Method**)malloc(sizeof(Method*) * c->method_capacity);
    return c;
}

void class_add_native(Class* c, const char* selector, NativeMethodFn fn) {
    if (c->method_count >= c->method_capacity) {
        c->method_capacity *= 2;
        c->methods = (Method**)realloc(c->methods, sizeof(Method*) * c->method_capacity);
    }
    Method* m = (Method*)malloc(sizeof(Method));
    m->type = METHOD_NATIVE;
    m->selector = strdup(selector);
    m->as.native_fn = fn;
    c->methods[c->method_count++] = m;
}

void class_add_user(Class* c, const char* selector, char** params, int param_count, ASTProgram* body) {
    // Check if method already exists on this class and overwrite
    for (int i = 0; i < c->method_count; i++) {
        if (strcmp(c->methods[i]->selector, selector) == 0) {
            c->methods[i]->type = METHOD_USER;
            c->methods[i]->as.user.params = params;
            c->methods[i]->as.user.param_count = param_count;
            c->methods[i]->as.user.body = body;
            return;
        }
    }
    if (c->method_count >= c->method_capacity) {
        c->method_capacity *= 2;
        c->methods = (Method**)realloc(c->methods, sizeof(Method*) * c->method_capacity);
    }
    Method* m = (Method*)malloc(sizeof(Method));
    m->type = METHOD_USER;
    m->selector = strdup(selector);
    m->as.user.params = params;
    m->as.user.param_count = param_count;
    m->as.user.body = body;
    c->methods[c->method_count++] = m;
}

Method* class_lookup_method(Class* c, const char* selector) {
    Class* curr = c;
    while (curr != NULL) {
        for (int i = 0; i < curr->method_count; i++) {
            if (strcmp(curr->methods[i]->selector, selector) == 0) {
                return curr->methods[i];
            }
        }
        curr = curr->superclass;
    }
    return NULL;
}

bool class_has_ivar(Class* c, const char* name) {
    Class* curr = c;
    while (curr != NULL) {
        for (int i = 0; i < curr->ivar_count; i++) {
            if (strcmp(curr->ivar_names[i], name) == 0) {
                return true;
            }
        }
        curr = curr->superclass;
    }
    return false;
}

void obj_system_init(void) {
    if (ObjectClass != NULL) return;

    ObjectClass = class_new("Object", NULL, NULL, 0);
    MetaclassClass = class_new("Class", ObjectClass, NULL, 0);
    UndefinedObjectClass = class_new("UndefinedObject", ObjectClass, NULL, 0);
    BooleanClass = class_new("Boolean", ObjectClass, NULL, 0);
    TrueClass = class_new("True", BooleanClass, NULL, 0);
    FalseClass = class_new("False", BooleanClass, NULL, 0);
    NumberClass = class_new("Number", ObjectClass, NULL, 0);
    IntegerClass = class_new("integer", NumberClass, NULL, 0);
    FloatClass = class_new("Float", NumberClass, NULL, 0);
    StringClass = class_new("String", ObjectClass, NULL, 0);
    ArrayClass = class_new("Array", ObjectClass, NULL, 0);
    DictClass = class_new("Dictionary", ObjectClass, NULL, 0);
    BlockClosureClass = class_new("BlockClosure", ObjectClass, NULL, 0);
    FFIHandleClass = class_new("FFIHandle", ObjectClass, NULL, 0);
    JSONClass = class_new("JSON", ObjectClass, NULL, 0);
    CoroutineClass = class_new("coroutine", ObjectClass, NULL, 0);
}

// Object constructors
Obj* obj_new_nil(void) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_NIL;
    o->klass = UndefinedObjectClass;
    return o;
}

Obj* obj_new_bool(bool val) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_BOOL;
    o->klass = val ? TrueClass : FalseClass;
    o->as.boolean = val;
    return o;
}

Obj* obj_new_int(int64_t val) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_INT;
    o->klass = IntegerClass;
    o->as.integer = val;
    return o;
}

Obj* obj_new_float(double val) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_FLOAT;
    o->klass = FloatClass;
    o->as.number = val;
    return o;
}

Obj* obj_new_string(const char* s) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_STRING;
    o->klass = StringClass;
    o->as.string.chars = strdup(s ? s : "");
    o->as.string.length = (int)strlen(o->as.string.chars);
    return o;
}

Obj* obj_new_array(void) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_ARRAY;
    o->klass = ArrayClass;
    o->as.array.count = 0;
    o->as.array.capacity = 8;
    o->as.array.items = (Obj**)malloc(sizeof(Obj*) * o->as.array.capacity);
    return o;
}

Obj* obj_new_dict(void) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_DICT;
    o->klass = DictClass;
    o->as.dict.count = 0;
    o->as.dict.capacity = 8;
    o->as.dict.entries = (DictEntry*)malloc(sizeof(DictEntry) * o->as.dict.capacity);
    return o;
}

Obj* obj_new_block(char** params, int param_count, ASTProgram* body, Scope* scope, int64_t frame_id) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_BLOCK;
    o->klass = BlockClosureClass;
    o->as.block.params = params;
    o->as.block.param_count = param_count;
    o->as.block.body = body;
    o->as.block.captured_scope = scope;
    o->as.block.enclosing_frame_id = frame_id;
    return o;
}

Obj* obj_new_class(Class* klass) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_CLASS;
    o->klass = MetaclassClass;
    o->as.class_val = klass;
    return o;
}

Obj* obj_new_instance(Class* klass) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_INSTANCE;
    o->klass = klass;
    o->as.instance.klass = klass;
    o->as.instance.count = 0;
    o->as.instance.capacity = 8;
    o->as.instance.fields = (FieldEntry*)malloc(sizeof(FieldEntry) * o->as.instance.capacity);

    // Initialize instance variables from class hierarchy to nil
    Class* curr = klass;
    while (curr != NULL) {
        for (int i = 0; i < curr->ivar_count; i++) {
            if (o->as.instance.count >= o->as.instance.capacity) {
                o->as.instance.capacity *= 2;
                o->as.instance.fields = (FieldEntry*)realloc(o->as.instance.fields, sizeof(FieldEntry) * o->as.instance.capacity);
            }
            o->as.instance.fields[o->as.instance.count].name = strdup(curr->ivar_names[i]);
            o->as.instance.fields[o->as.instance.count].value = obj_new_nil();
            o->as.instance.count++;
        }
        curr = curr->superclass;
    }
    return o;
}

Obj* obj_new_ffi_handle(void* handle, const char* name) {
    Obj* o = (Obj*)malloc(sizeof(Obj));
    o->type = OBJ_FFI_HANDLE;
    o->klass = FFIHandleClass;
    o->as.ffi_handle.handle = handle;
    o->as.ffi_handle.name = strdup(name ? name : "handle");
    return o;
}

bool obj_is_truthy(Obj* o) {
    if (!o) return false;
    switch (o->type) {
        case OBJ_NIL: return false;
        case OBJ_BOOL: return o->as.boolean;
        case OBJ_INT: return o->as.integer != 0;
        case OBJ_FLOAT: return o->as.number != 0.0;
        case OBJ_STRING: return o->as.string.length > 0;
        case OBJ_ARRAY: return o->as.array.count > 0;
        default: return true;
    }
}

char* obj_to_string(Obj* o) {
    if (!o) return strdup("nil");
    char buf[128];
    switch (o->type) {
        case OBJ_NIL: return strdup("nil");
        case OBJ_BOOL: return strdup(o->as.boolean ? "true" : "false");
        case OBJ_INT:
            sprintf(buf, "%lld", (long long)o->as.integer);
            return strdup(buf);
        case OBJ_FLOAT:
            sprintf(buf, "%g", o->as.number);
            return strdup(buf);
        case OBJ_STRING:
            return strdup(o->as.string.chars);
        case OBJ_ARRAY: {
            int cap = 64;
            char* res = (char*)malloc(cap);
            strcpy(res, "[");
            for (int i = 0; i < o->as.array.count; i++) {
                char* is = obj_to_string(o->as.array.items[i]);
                int added = (int)strlen(is) + (i > 0 ? 2 : 0);
                if ((int)strlen(res) + added + 2 >= cap) {
                    cap = (cap + added + 2) * 2;
                    res = (char*)realloc(res, cap);
                }
                if (i > 0) strcat(res, ", ");
                strcat(res, is);
                free(is);
            }
            strcat(res, "]");
            return res;
        }
        case OBJ_DICT: {
            int cap = 64;
            char* res = (char*)malloc(cap);
            strcpy(res, "{");
            for (int i = 0; i < o->as.dict.count; i++) {
                char* vs = obj_to_string(o->as.dict.entries[i].value);
                int added = (int)strlen(o->as.dict.entries[i].key) + (int)strlen(vs) + 4;
                if ((int)strlen(res) + added + 2 >= cap) {
                    cap = (cap + added + 2) * 2;
                    res = (char*)realloc(res, cap);
                }
                if (i > 0) strcat(res, ", ");
                strcat(res, o->as.dict.entries[i].key);
                strcat(res, ": ");
                strcat(res, vs);
                free(vs);
            }
            strcat(res, "}");
            return res;
        }
        case OBJ_BLOCK: {
            sprintf(buf, "{ block (%d params) }", o->as.block.param_count);
            return strdup(buf);
        }
        case OBJ_CLASS:
            return strdup(o->as.class_val->name);
        case OBJ_INSTANCE: {
            int len = (int)strlen(o->as.instance.klass->name) + 16;
            char* res = (char*)malloc(len);
            sprintf(res, "<%s instance>", o->as.instance.klass->name);
            return res;
        }
        case OBJ_FFI_HANDLE: {
            int len = (int)strlen(o->as.ffi_handle.name) + 32;
            char* res = (char*)malloc(len);
            sprintf(res, "<ffi %s %p>", o->as.ffi_handle.name, o->as.ffi_handle.handle);
            return res;
        }
        case OBJ_COROUTINE:
            return strdup("<coroutine>");
    }
    return strdup("<object>");
}

bool obj_equals(Obj* a, Obj* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) {
        if ((a->type == OBJ_INT || a->type == OBJ_FLOAT) && (b->type == OBJ_INT || b->type == OBJ_FLOAT)) {
            return obj_to_float(a) == obj_to_float(b);
        }
        return false;
    }
    switch (a->type) {
        case OBJ_NIL: return true;
        case OBJ_BOOL: return a->as.boolean == b->as.boolean;
        case OBJ_INT: return a->as.integer == b->as.integer;
        case OBJ_FLOAT: return a->as.number == b->as.number;
        case OBJ_STRING: return strcmp(a->as.string.chars, b->as.string.chars) == 0;
        default: return a == b;
    }
}

double obj_to_float(Obj* o) {
    if (!o) return 0.0;
    if (o->type == OBJ_INT) return (double)o->as.integer;
    if (o->type == OBJ_FLOAT) return o->as.number;
    return 0.0;
}

int64_t obj_to_int(Obj* o) {
    if (!o) return 0;
    if (o->type == OBJ_INT) return o->as.integer;
    if (o->type == OBJ_FLOAT) return (int64_t)o->as.number;
    return 0;
}
