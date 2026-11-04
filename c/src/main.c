#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcltalk.h"
#include "vm.h"

int main(int argc, char** argv) {
    VM* vm = vm_new();

    if (argc > 1) {
        if (strcmp(argv[1], "-e") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Error: -e requires an expression argument\n");
                vm_free(vm);
                return 1;
            }
            VMResult res = vm_eval_source(vm, argv[2]);
            if (res.error_msg) {
                fprintf(stderr, "Error: %s\n", res.error_msg);
                free(res.error_msg);
                vm_free(vm);
                return 1;
            }
            vm_free(vm);
            return 0;
        }

        // File execution
        const char* filename = argv[1];
        VMResult res = vm_eval_file(vm, filename);
        if (res.error_msg) {
            fprintf(stderr, "Error: %s\n", res.error_msg);
            free(res.error_msg);
            vm_free(vm);
            return 1;
        }
        vm_free(vm);
        return 0;
    }

    // Interactive REPL
    printf("Tick Interactive Console (C implementation)\n");
    printf("Type exit or press Ctrl+C to exit.\n");

    char line[1024];
    while (1) {
        printf("tick> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        // Trim whitespace
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        int len = (int)strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' ')) {
            p[--len] = '\0';
        }

        if (strcmp(p, "exit") == 0 || strcmp(p, "quit") == 0) break;
        if (len == 0) continue;

        VMResult res = vm_eval_source(vm, p);
        if (res.error_msg) {
            printf("Error: %s\n", res.error_msg);
            free(res.error_msg);
        } else if (res.value && res.value->type != OBJ_NIL) {
            char* s = obj_to_string(res.value);
            printf("=> %s\n", s);
            free(s);
        }
    }

    vm_free(vm);
    return 0;
}
