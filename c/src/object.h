#ifndef TCLTALK_OBJECT_H
#define TCLTALK_OBJECT_H

#include <stdint.h>
#include <stdbool.h>
#include "ast.h"

typedef struct Obj Obj;
typedef struct Class Class;
typedef struct Method Method;
typedef struct VM VM;
typedef struct Scope Scope;

typedef enum {
    OBJ_NIL,
    OBJ_BOOL,
    OBJ_INT,
    OBJ_FLOAT,
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_DICT,
    OBJ_BLOCK,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_FFI_HANDLE,
    OBJ_COROUTINE
} ObjType;

typedef Obj* (*NativeMethodFn)(Obj* recv, int argc, Obj** argv, VM* vm);

typedef enum {
    METHOD_NATIVE,
    METHOD_USER
} MethodType;

struct Method {
    MethodType type;
    char* selector;
    union {
        NativeMethodFn native_fn;
        struct {
            char** params;
            int param_count;
            ASTProgram* body;
        } user;
    } as;
};

struct Class {
    char* name;
    Class* superclass;
    char** ivar_names;
    int ivar_count;
    Method** methods;
    int method_count;
    int method_capacity;
};

typedef struct {
    char* key;
    Obj* value;
} DictEntry;

typedef struct {
    char* name;
    Obj* value;
} FieldEntry;

struct Obj {
    ObjType type;
    Class* klass;
    union {
        bool boolean;
        int64_t integer;
        double number;
        struct {
            char* chars;
            int length;
        } string;
        struct {
            Obj** items;
            int count;
            int capacity;
        } array;
        struct {
            DictEntry* entries;
            int count;
            int capacity;
        } dict;
        struct {
            char** params;
            int param_count;
            ASTProgram* body;
            Scope* captured_scope;
            int64_t enclosing_frame_id;
        } block;
        Class* class_val;
        struct {
            Class* klass;
            FieldEntry* fields;
            int count;
            int capacity;
        } instance;
        struct {
            void* handle;
            char* name;
        } ffi_handle;
        struct Coroutine* coro;
    } as;
};

// Global Class Singletons
extern Class* ObjectClass;
extern Class* MetaclassClass;
extern Class* UndefinedObjectClass;
extern Class* BooleanClass;
extern Class* TrueClass;
extern Class* FalseClass;
extern Class* NumberClass;
extern Class* IntegerClass;
extern Class* FloatClass;
extern Class* StringClass;
extern Class* ArrayClass;
extern Class* DictClass;
extern Class* BlockClosureClass;
extern Class* FFIHandleClass;
extern Class* JSONClass;
extern Class* CoroutineClass;

void obj_system_init(void);

// Object constructors
Obj* obj_new_nil(void);
Obj* obj_new_bool(bool val);
Obj* obj_new_int(int64_t val);
Obj* obj_new_float(double val);
Obj* obj_new_string(const char* s);
Obj* obj_new_array(void);
Obj* obj_new_dict(void);
Obj* obj_new_block(char** params, int param_count, ASTProgram* body, Scope* scope, int64_t frame_id);
Obj* obj_new_class(Class* klass);
Obj* obj_new_instance(Class* klass);
Obj* obj_new_ffi_handle(void* handle, const char* name);

// Class operations
Class* class_new(const char* name, Class* superclass, char** ivars, int ivar_count);
void class_add_native(Class* c, const char* selector, NativeMethodFn fn);
void class_add_user(Class* c, const char* selector, char** params, int param_count, ASTProgram* body);
Method* class_lookup_method(Class* c, const char* selector);
bool class_has_ivar(Class* c, const char* name);

// Value operations
bool obj_is_truthy(Obj* o);
char* obj_to_string(Obj* o);
bool obj_equals(Obj* a, Obj* b);
double obj_to_float(Obj* o);
int64_t obj_to_int(Obj* o);

#endif // TCLTALK_OBJECT_H
