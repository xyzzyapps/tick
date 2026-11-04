#ifndef TICK_H
#define TICK_H

#include "tcltalk.h"

#ifdef __cplusplus
extern "C" {
#endif

// Tick Public Embedding API
typedef VM TickVM;
typedef VMResult TickResult;

static inline TickVM* tick_new(void) {
    return vm_new();
}

static inline void tick_free(TickVM* vm) {
    vm_free(vm);
}

static inline TickResult tick_eval(TickVM* vm, const char* source) {
    return vm_eval_source(vm, source);
}

static inline TickResult tick_eval_file(TickVM* vm, const char* filename) {
    return vm_eval_file(vm, filename);
}

#ifdef __cplusplus
}
#endif

#endif // TICK_H
