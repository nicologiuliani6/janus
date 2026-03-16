#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VAR_NAME_LENGTH 100
typedef struct {
    char name[VAR_NAME_LENGTH];
    int  value;
    int  is_local;   // 1 = local/delocal, 0 = decl normale
} Var;

#define MAX_VARS 100
typedef struct {
    char proc_name[VAR_NAME_LENGTH];
    Var  vars[MAX_VARS];
    int  var_count;
} Frame;

#define MAX_FRAMES 10
typedef struct {
    Frame frames[MAX_FRAMES];
    int   frame_top;   // indice del frame corrente (-1 = vuoto)
} VM;
void vm_init(VM *vm) {
    vm->frame_top = -1;
}
void vm_exec(VM *vm, char* buffer) {
    char *ptr = buffer;

    while (*ptr != '\0') {
        // trova la fine della riga
        char *newline = strchr(ptr, '\n');

        if (newline != NULL) {
            *newline = '\0';  // temporaneamente terminate la riga

            if (strlen(ptr) > 6) {
                char *line = ptr + 6;  // salto i primi 6 caratteri
                //printf("%s\n", line);  // salta i primi 6 caratteri
                char *firstWord = strtok(line, " \t");  // divide per spazi o tab
                //printf("%s\n", firstWord);
                if (strcmp(firstWord, "START") == 0) {
                    printf("Inizio VM\n");
                } else if (strcmp(firstWord, "HALT") == 0) {
                    printf("Fine VM");
                } else if (strcmp(firstWord, "PROC") == 0) {
                    printf("Creazione stack per new proc\n");
                } else {
                    printf("Istruzione sconosciuta: %s\n", firstWord);
                }
            } else {
                printf("[VM] Bytecode formattato male!\n");
            }

            *newline = '\n';  // ripristina il carattere
            ptr = newline + 1; // passa alla prossima riga
        } else {
            // ultima riga senza '\n'
            if (strlen(ptr) > 6) {
                printf("%s\n", ptr + 6);
            } else {
                printf("[VM] Bytecode formattato male!\n");
            }
            break;
        }
    }
}


void vm_dump(VM *vm) {
    printf("=== VM dump ===\n");
    for (int i = 0; i <= vm->frame_top; i++) {
        Frame *f = &vm->frames[i];
        printf("  frame[%d] proc=%s\n", i, f->proc_name);
        for (int j = 0; j < f->var_count; j++) {
            printf("%s = %d\n", f->vars[j].name, f->vars[j].value);
        }
    }
}
int main(){
    char buffer[256];
    char ast[1024*10];  // buffer più grande per contenere tutto il file
    ast[0] = '\0';      // inizializza stringa vuota

    FILE *fp = fopen("bytecode.txt", "r");
    if (fp == NULL) {
        perror("Errore nell'apertura del file");
        return 1;
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        // concatena ogni riga al buffer completo
        strncat(ast, buffer, sizeof(ast) - strlen(ast) - 1);
    }
    fclose(fp);
    VM vm;
    vm_init(&vm);
    size_t length = sizeof(ast);
    vm_exec(&vm, ast);
    vm_dump(&vm);
    return 0;
}