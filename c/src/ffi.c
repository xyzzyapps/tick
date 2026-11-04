#include "ffi.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#if defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
#define PLATFORM_WINDOWS 1
#include <windows.h>
#else
#define PLATFORM_WINDOWS 0
#include <dlfcn.h>
#include <unistd.h>
#endif

static uint64_t current_time_ms(void) {
#if PLATFORM_WINDOWS
    return (uint64_t)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

FFIEngine* ffi_engine_new(void) {
    FFIEngine* e = (FFIEngine*)malloc(sizeof(FFIEngine));
    e->var_count = 0;
    e->var_capacity = 16;
    e->vars = (FFIVar*)malloc(sizeof(FFIVar) * e->var_capacity);
    e->start_time_ms = current_time_ms();

    ffi_set_var(e, "@PI", obj_new_float(3.14159265358979323846));
    ffi_set_var(e, "@E", obj_new_float(2.71828182845904523536));
#if PLATFORM_WINDOWS
    ffi_set_var(e, "@OS", obj_new_string("windows"));
#elif defined(__APPLE__)
    ffi_set_var(e, "@OS", obj_new_string("darwin"));
#else
    ffi_set_var(e, "@OS", obj_new_string("linux"));
#endif

#if defined(__x86_64__) || defined(_M_X64)
    ffi_set_var(e, "@ARCH", obj_new_string("amd64"));
#elif defined(__aarch64__) || defined(_M_ARM64)
    ffi_set_var(e, "@ARCH", obj_new_string("arm64"));
#else
    ffi_set_var(e, "@ARCH", obj_new_string("x86"));
#endif
    ffi_set_var(e, "@VERSION", obj_new_string("1.0.0"));

    return e;
}

void ffi_engine_free(FFIEngine* e) {
    for (int i = 0; i < e->var_count; i++) {
        free(e->vars[i].name);
    }
    free(e->vars);
    free(e);
}

void ffi_set_var(FFIEngine* e, const char* name, Obj* val) {
    for (int i = 0; i < e->var_count; i++) {
        if (strcmp(e->vars[i].name, name) == 0) {
            e->vars[i].value = val;
            return;
        }
    }
    if (e->var_count >= e->var_capacity) {
        e->var_capacity *= 2;
        e->vars = (FFIVar*)realloc(e->vars, sizeof(FFIVar) * e->var_capacity);
    }
    e->vars[e->var_count].name = strdup(name);
    e->vars[e->var_count].value = val;
    e->var_count++;
}

Obj* ffi_get_var(FFIEngine* e, const char* name) {
    for (int i = 0; i < e->var_count; i++) {
        if (strcmp(e->vars[i].name, name) == 0) {
            return e->vars[i].value;
        }
    }
    return NULL;
}

static Obj* call_dynamic_c(const char* lib_name, const char* sym_name, int argc, Obj** argv) {
#if PLATFORM_WINDOWS
    char full_lib[256];
    strncpy(full_lib, lib_name, sizeof(full_lib) - 1);
    if (!strstr(full_lib, ".dll") && !strstr(full_lib, ".DLL")) {
        strncat(full_lib, ".dll", sizeof(full_lib) - strlen(full_lib) - 1);
    }
    HMODULE h = LoadLibraryA(full_lib);
    if (!h) {
        printf("FFI Error: failed to load DLL '%s'\n", full_lib);
        return obj_new_nil();
    }
    FARPROC proc = GetProcAddress(h, sym_name);
    if (!proc) {
        printf("FFI Error: symbol '%s' not found in '%s'\n", sym_name, full_lib);
        return obj_new_nil();
    }

    void* args[8] = {0};
    for (int i = 0; i < argc && i < 8; i++) {
        if (argv[i]->type == OBJ_INT) {
            args[i] = (void*)(intptr_t)argv[i]->as.integer;
        } else if (argv[i]->type == OBJ_FLOAT) {
            args[i] = (void*)(intptr_t)(int64_t)argv[i]->as.number;
        } else if (argv[i]->type == OBJ_STRING) {
            args[i] = (void*)argv[i]->as.string.chars;
        } else if (argv[i]->type == OBJ_BOOL) {
            args[i] = (void*)(intptr_t)(argv[i]->as.boolean ? 1 : 0);
        } else if (argv[i]->type == OBJ_FFI_HANDLE) {
            args[i] = argv[i]->as.ffi_handle.handle;
        }
    }

    typedef int64_t (*DynFn0)(void);
    typedef int64_t (*DynFn1)(void*);
    typedef int64_t (*DynFn2)(void*, void*);
    typedef int64_t (*DynFn3)(void*, void*, void*);
    typedef int64_t (*DynFn4)(void*, void*, void*, void*);

    int64_t ret = 0;
    switch (argc) {
        case 0: ret = ((DynFn0)proc)(); break;
        case 1: ret = ((DynFn1)proc)(args[0]); break;
        case 2: ret = ((DynFn2)proc)(args[0], args[1]); break;
        case 3: ret = ((DynFn3)proc)(args[0], args[1], args[2]); break;
        default: ret = ((DynFn4)proc)(args[0], args[1], args[2], args[3]); break;
    }
    return obj_new_int(ret);
#else
    void* h = dlopen(lib_name, RTLD_LAZY);
    if (!h) {
        printf("FFI Error: failed to load library '%s'\n", lib_name);
        return obj_new_nil();
    }
    void* proc = dlsym(h, sym_name);
    if (!proc) {
        printf("FFI Error: symbol '%s' not found in '%s'\n", sym_name, lib_name);
        return obj_new_nil();
    }
    typedef int64_t (*DynFn1)(void*);
    void* arg0 = (argc > 0 && argv[0]->type == OBJ_STRING) ? (void*)argv[0]->as.string.chars : NULL;
    int64_t ret = ((DynFn1)proc)(arg0);
    return obj_new_int(ret);
#endif
}

Obj* ffi_handle_call(FFIEngine* e, const char* name, int argc, Obj** argv, VM* vm) {
    if (strncmp(name, "set:", 4) == 0) {
        const char* var_name = name + 4;
        if (argc > 0) {
            ffi_set_var(e, var_name, argv[0]);
            return argv[0];
        }
        return obj_new_nil();
    }

    if (argc == 0) {
        Obj* v = ffi_get_var(e, name);
        if (v != NULL) return v;
    }

    const char* fn_name = name;
    if (fn_name[0] == '@') fn_name++;

    // Lib:func dynamic call e.g. @msvcrt:puts
    if (strchr(fn_name, ':') != NULL) {
        char buf[128];
        strncpy(buf, fn_name, sizeof(buf) - 1);
        char* colon = strchr(buf, ':');
        *colon = '\0';
        char* sym = colon + 1;
        return call_dynamic_c(buf, sym, argc, argv);
    }

    if (strcmp(fn_name, "puts") == 0) {
        if (argc > 0) {
            char* s = obj_to_string(argv[0]);
            puts(s);
            int len = (int)strlen(s);
            free(s);
            return obj_new_int(len + 1);
        }
        putchar('\n');
        return obj_new_int(1);
    }

    if (strcmp(fn_name, "print") == 0) {
        for (int i = 0; i < argc; i++) {
            char* s = obj_to_string(argv[i]);
            fputs(s, stdout);
            free(s);
        }
        fflush(stdout);
        return obj_new_nil();
    }

    if (strcmp(fn_name, "json_parse") == 0) {
        if (argc < 1) return obj_new_nil();
        char* s = obj_to_string(argv[0]);
        Obj* res = json_parse(s);
        free(s);
        return res;
    }

    if (strcmp(fn_name, "json_stringify") == 0) {
        if (argc < 1) return obj_new_nil();
        char* s = json_stringify(argv[0]);
        Obj* res = obj_new_string(s);
        free(s);
        return res;
    }

    if (strcmp(fn_name, "printf") == 0) {
        if (argc == 0) return obj_new_int(0);
        char* fmt = obj_to_string(argv[0]);
        // For basic printf support:
        if (argc == 1) {
            printf("%s", fmt);
        } else if (argc == 2) {
            if (argv[1]->type == OBJ_INT) printf(fmt, (long long)argv[1]->as.integer);
            else if (argv[1]->type == OBJ_FLOAT) printf(fmt, argv[1]->as.number);
            else { char* s = obj_to_string(argv[1]); printf(fmt, s); free(s); }
        } else if (argc == 3) {
            // 2 args
            char* s1 = obj_to_string(argv[1]);
            char* s2 = obj_to_string(argv[2]);
            printf(fmt, s1, s2);
            free(s1); free(s2);
        }
        free(fmt);
        return obj_new_nil();
    }

    if (strcmp(fn_name, "sqrt") == 0) {
        if (argc < 1) return obj_new_nil();
        return obj_new_float(sqrt(obj_to_float(argv[0])));
    }
    if (strcmp(fn_name, "sin") == 0) {
        if (argc < 1) return obj_new_nil();
        return obj_new_float(sin(obj_to_float(argv[0])));
    }
    if (strcmp(fn_name, "cos") == 0) {
        if (argc < 1) return obj_new_nil();
        return obj_new_float(cos(obj_to_float(argv[0])));
    }
    if (strcmp(fn_name, "tan") == 0) {
        if (argc < 1) return obj_new_nil();
        return obj_new_float(tan(obj_to_float(argv[0])));
    }
    if (strcmp(fn_name, "pow") == 0) {
        if (argc < 2) return obj_new_nil();
        return obj_new_float(pow(obj_to_float(argv[0]), obj_to_float(argv[1])));
    }
    if (strcmp(fn_name, "abs") == 0) {
        if (argc < 1) return obj_new_nil();
        if (argv[0]->type == OBJ_INT) {
            int64_t v = argv[0]->as.integer;
            return obj_new_int(v < 0 ? -v : v);
        }
        return obj_new_float(fabs(obj_to_float(argv[0])));
    }
    if (strcmp(fn_name, "floor") == 0) {
        if (argc < 1) return obj_new_nil();
        return obj_new_float(floor(obj_to_float(argv[0])));
    }
    if (strcmp(fn_name, "ceil") == 0) {
        if (argc < 1) return obj_new_nil();
        return obj_new_float(ceil(obj_to_float(argv[0])));
    }

    if (strcmp(fn_name, "getenv") == 0) {
        if (argc < 1) return obj_new_nil();
        char* k = obj_to_string(argv[0]);
        char* val = getenv(k);
        free(k);
        return obj_new_string(val ? val : "");
    }
    if (strcmp(fn_name, "setenv") == 0) {
        if (argc >= 2) {
            char* k = obj_to_string(argv[0]);
            char* v = obj_to_string(argv[1]);
#if PLATFORM_WINDOWS
            _putenv_s(k, v);
#else
            setenv(k, v, 1);
#endif
            free(k); free(v);
        }
        return obj_new_nil();
    }
    if (strcmp(fn_name, "system") == 0) {
        if (argc < 1) return obj_new_int(1);
        char* cmd = obj_to_string(argv[0]);
        int res = system(cmd);
        free(cmd);
        return obj_new_int(res);
    }
    if (strcmp(fn_name, "clock") == 0) {
        uint64_t elapsed = current_time_ms() - e->start_time_ms;
        return obj_new_int((int64_t)elapsed);
    }
    if (strcmp(fn_name, "time") == 0) {
        return obj_new_int((int64_t)time(NULL));
    }
    if (strcmp(fn_name, "sleep") == 0) {
        if (argc > 0) {
            double s = obj_to_float(argv[0]);
#if PLATFORM_WINDOWS
            Sleep((DWORD)(s * 1000.0));
#else
            usleep((useconds_t)(s * 1000000.0));
#endif
        }
        return obj_new_nil();
    }
    if (strcmp(fn_name, "exit") == 0) {
        int code = (argc > 0) ? (int)obj_to_int(argv[0]) : 0;
        exit(code);
        return obj_new_nil();
    }
    if (strcmp(fn_name, "call") == 0) {
        if (argc < 2) return obj_new_nil();
        char* lib = obj_to_string(argv[0]);
        char* sym = obj_to_string(argv[1]);
        Obj* res = call_dynamic_c(lib, sym, argc - 2, argv + 2);
        free(lib); free(sym);
        return res;
    }

    Obj* v = ffi_get_var(e, name);
    if (v != NULL) return v;

    printf("FFI Warning: unknown FFI symbol '%s'\n", name);
    return obj_new_nil();
}
