#ifndef TCLTALK_H
#define TCLTALK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct Obj Obj;
typedef struct Class Class;
typedef struct Scope Scope;
typedef struct VM VM;
typedef struct ASTProgram ASTProgram;

typedef struct {
    Obj* value;
    int64_t return_frame_id; // >0 if unwinding for non-local return
    char* error_msg;         // Non-NULL on error
} VMResult;

VM* vm_new(void);
void vm_free(VM* vm);
VMResult vm_eval_source(VM* vm, const char* source);
VMResult vm_eval_file(VM* vm, const char* filename);

#ifdef __cplusplus
}
#endif

#endif // TCLTALK_H
