#ifndef TCLTALK_FFI_H
#define TCLTALK_FFI_H

#include "object.h"

typedef struct FFIVar {
    char* name;
    Obj* value;
} FFIVar;

typedef struct FFIEngine {
    FFIVar* vars;
    int var_count;
    int var_capacity;
    uint64_t start_time_ms;
} FFIEngine;

FFIEngine* ffi_engine_new(void);
void ffi_engine_free(FFIEngine* e);
Obj* ffi_handle_call(FFIEngine* e, const char* name, int argc, Obj** argv, VM* vm);
void ffi_set_var(FFIEngine* e, const char* name, Obj* val);
Obj* ffi_get_var(FFIEngine* e, const char* name);

#endif // TCLTALK_FFI_H
