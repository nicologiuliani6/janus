#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "char_id_map.h" //per la ricerca del frame in base al nome della procedura
CharIdMap FrameIndexer;
#include "stack.h"

#define uint unsigned int
#define perror(msg) {printf(msg); exit(EXIT_FAILURE);}

typedef enum {
    TYPE_INT = 0,
    TYPE_STACK = 1,
    TYPE_PARAM = 2 
} ValueType;

#define VAR_NAME_LENGTH 100
#define VAR_STACK_MAX_SIZE 128 //byte
typedef struct Var{
    ValueType T; //da qui capiamo se e' INT o STACK
    int*  value;
    size_t stack_len;  //indica la lunghezza dello stack
    int  is_local;   // 1 = local/delocal, 0 = decl normale
    char name[VAR_NAME_LENGTH]; // nome della variabile
} Var;

//cancellare elemento n delle variabili del frame
// NON shiftiamo: char_id_map assegna indici stabili e permanenti,
// shiftare romperebbe la corrispondenza nome->indice
void delete_var(Var *vars[], int *size, int n) {
    if (n < 0 || n >= *size) {
        printf("Indice fuori range!\n");
        return;
    }
    free(vars[n]->value);  // libera memoria del valore
    free(vars[n]);         // libera la struttura Var
    vars[n] = NULL;        // azzera il puntatore (slot libero)
    // non shiftiamo e non decrementiamo size:
    // lo slot e' libero (vars[n]==NULL) e puo' essere riusato
}

#define MAX_VARS 100
#define MAX_LABEL 100
typedef struct {
    CharIdMap VarIndexer;
    Stack LocalVariables;
    Var  *vars[MAX_VARS];
    int  var_count;  // high-water mark: indice massimo usato + 1
    CharIdMap LabelIndexer;
    uint label[MAX_LABEL];
    char name[VAR_NAME_LENGTH]; // nome del frame (nome della procedura)
    uint addr; //indirizzo della procedure
    uint val_IF; //bool per vedere se la IF era vera o fare JMPF
    int param_indices[64];
    int param_count;
} Frame;

#define MAX_FRAMES 100
typedef struct {
    Frame frames[MAX_FRAMES];
    int   frame_top;   // indice del frame corrente (-1 = vuoto)
} VM;
void delete_frame(VM *vm, int n) {
    if (n < 0 || n > vm->frame_top) {
        printf("Indice frame non valido\n");
        return;
    }

    // liberare eventuali risorse del frame da cancellare
    // free(vm->frames[n].vars); ecc.

    // shift dei frame successivi
    for (int i = n; i < vm->frame_top; i++) {
        vm->frames[i] = vm->frames[i + 1];
    }

    vm->frame_top--;  // decrementa il numero di frame
}

char* go_to_line(char* buffer, uint line) {
    if (buffer == NULL) return NULL;
    if (line == 0) return buffer;

    uint current_line = 1;
    char* ptr = buffer;

    while (*ptr != '\0') {
        if (current_line == line) {
            return ptr;
        }
        if (*ptr == '\n') {
            current_line++;
        }
        ptr++;
    }

    return NULL;
}

void vm_run_BT(VM *vm, char* buffer, char* frame_name_init) {

    // --- setup ---
    char* original_buffer = strdup(buffer);  // copia pulita, mai modificata da strtok

    char frame_name[VAR_NAME_LENGTH];
    strncpy(frame_name, frame_name_init, VAR_NAME_LENGTH - 1);
    frame_name[VAR_NAME_LENGTH - 1] = '\0';

    // --- call stack locale per gestire CALL/END_PROC senza ricorsione ---
    typedef struct {
        char* return_ptr;
        char  caller_frame[VAR_NAME_LENGTH];
    } CallRecord;
    CallRecord call_stack[MAX_FRAMES];
    int call_top = -1;

    // --- posiziona ptr all'inizio della procedura iniziale ---
    uint start_index = char_id_map_get(&FrameIndexer, frame_name);
    char *ptr = go_to_line(original_buffer, vm->frames[start_index].addr + 1);
    if (!ptr) {
        fprintf(stderr, "ERROR: indirizzo procedura '%s' non trovato\n", frame_name);
        free(original_buffer);
        return;
    }

    // ================================================================
    while (*ptr != '\0') {
        char *newline = strchr(ptr, '\n');
        if (newline == NULL) break;

        *newline = '\0';  // termina temporaneamente la riga

        // copia la riga in un buffer locale per strtok (non tocchiamo original_buffer)
        char line_buf[512];
        strncpy(line_buf, ptr + 6, sizeof(line_buf) - 1);
        line_buf[sizeof(line_buf) - 1] = '\0';

        char *firstWord = strtok(line_buf, " \t");

        // ---- fine procedura ----
        if (strcmp(firstWord, "END_PROC") == 0) {
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if (stack_size(&vm->frames[Findex].LocalVariables) > -1)
                perror("[VM] END_PROC: variabili LOCAL non chiuse con DELOCAL!\n");

            if (call_top >= 0) {
                // ritorno al chiamante: ripristina frame e posizione
                *newline = '\n';
                ptr = call_stack[call_top].return_ptr;
                strncpy(frame_name, call_stack[call_top].caller_frame, VAR_NAME_LENGTH - 1);
                call_top--;
                continue;
            } else {
                // fine del main (o della procedura iniziale)
                *newline = '\n';
                break;
            }

        // ---- chiamata a procedura ----
        } else if (strcmp(firstWord, "CALL") == 0) {
            char* proc_name = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, proc_name);
            uint cur_Findex = char_id_map_get(&FrameIndexer, frame_name); // <-- salva il chiamante ORA

            if (call_top + 1 >= MAX_FRAMES)
                perror("[VM] CALL: call stack overflow!\n");
            call_top++;
            *newline = '\n';
            call_stack[call_top].return_ptr = newline + 1;
            strncpy(call_stack[call_top].caller_frame, frame_name, VAR_NAME_LENGTH - 1);
            strncpy(frame_name, proc_name, VAR_NAME_LENGTH - 1); // da qui frame_name = callee

            // 1) Pre-raccogliamo gli indici TYPE_PARAM del callee
            int param_count   = vm->frames[Findex].param_count;
            int *param_indices = vm->frames[Findex].param_indices;

            // 2) Linking usando cur_Findex (il chiamante) invece di vm->frame_top
            char* param = NULL;
            int i = 0;
            while ((param = strtok(NULL, " \t")) != NULL) {
                if (i >= param_count) {
                    fprintf(stderr, "ERROR: too many parameters for '%s'\n", proc_name);
                    exit(EXIT_FAILURE);
                }
                int j = param_indices[i];
                if (!char_id_map_exists(&vm->frames[cur_Findex].VarIndexer, param)) {
                    fprintf(stderr, "[VM] CALL: '%s' non definito nel frame chiamante!\n", param);
                    exit(EXIT_FAILURE);
                }
                int VtoLink_index = char_id_map_get(&vm->frames[cur_Findex].VarIndexer, param);
                if (vm->frames[cur_Findex].vars[VtoLink_index] == NULL) {
                    fprintf(stderr, "[VM] CALL: '%s' è NULL nel frame chiamante!\n", param);
                    exit(EXIT_FAILURE);
                }
                vm->frames[Findex].vars[j] = vm->frames[cur_Findex].vars[VtoLink_index];
                i++;
            }
            if (i != param_count) {
                fprintf(stderr, "ERROR: attesi %d params, ricevuti %d per '%s'\n",
                        param_count, i, proc_name);
                exit(EXIT_FAILURE);
            }

            ptr = go_to_line(original_buffer, vm->frames[Findex].addr + 1);
            if (!ptr) perror("[VM] CALL: indirizzo procedura non trovato!\n");
            continue;
        } else if (strcmp(firstWord, "UNCALL") == 0) {
            // TODO: reversione
        } else if (strcmp(firstWord, "LOCAL") == 0) {
    char* Vtype    = strtok(NULL, " \t");
    char* Vname    = strtok(NULL, " \t");
    char* c_Vvalue = strtok(NULL, " \t");

    uint Findex = char_id_map_get(&FrameIndexer, frame_name);
    uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, Vname);

    // alloca sempre: DELOCAL azzera lo slot, LOCAL rialloca
    vm->frames[Findex].vars[Vindex] = malloc(sizeof(Var));
    memset(vm->frames[Findex].vars[Vindex], 0, sizeof(Var));

    if (strcmp(Vtype, "int") == 0) {
        vm->frames[Findex].vars[Vindex]->T = TYPE_INT;
        vm->frames[Findex].vars[Vindex]->value = malloc(sizeof(int));
        *(vm->frames[Findex].vars[Vindex]->value) = 0;
    } else if (strcmp(Vtype, "stack") == 0) {
        vm->frames[Findex].vars[Vindex]->T = TYPE_STACK;
        vm->frames[Findex].vars[Vindex]->stack_len = 0;
        vm->frames[Findex].vars[Vindex]->value = malloc(VAR_STACK_MAX_SIZE * sizeof(int));
    } else perror("[VM] LOCAL: tipo non esistente\n");

    strncpy(vm->frames[Findex].vars[Vindex]->name, Vname, VAR_NAME_LENGTH - 1);
    vm->frames[Findex].vars[Vindex]->name[VAR_NAME_LENGTH - 1] = '\0';
    vm->frames[Findex].vars[Vindex]->is_local = 1;

    if (Vindex >= vm->frames[Findex].var_count)
        vm->frames[Findex].var_count = Vindex + 1;

    Var* dst = vm->frames[Findex].vars[Vindex];

    // assegnazione valore iniziale
    if (char_id_map_exists(&vm->frames[Findex].VarIndexer, c_Vvalue)) {
        int SrcIndex = char_id_map_get(&vm->frames[Findex].VarIndexer, c_Vvalue);
        Var* src = vm->frames[Findex].vars[SrcIndex];
        if (src->T == TYPE_INT)
            *(dst->value) = *(src->value);
        else if (src->T == TYPE_STACK) {
            dst->stack_len = src->stack_len;
            memcpy(dst->value, src->value, src->stack_len * sizeof(int));
        } else perror("[VM] LOCAL: copia da PARAM non linkato\n");
    } else {
        if (dst->T == TYPE_INT) {
            *(dst->value) = (int) strtol(c_Vvalue, NULL, 10);
        } else if (dst->T == TYPE_STACK) {
            if (strcmp(c_Vvalue, "nil") == 0)
                dst->stack_len = 0;
            else perror("[VM] LOCAL: valore stack non compatibile\n");
        }
    }

    stack_push(&vm->frames[Findex].LocalVariables, dst);} else if (strcmp(firstWord, "DELOCAL") == 0) {
            char* Vtype   = strtok(NULL, " \t");
            char* Vname   = strtok(NULL, " \t");
            char* c_Vvalue = strtok(NULL, " \t");

            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            int Vvalue = 0;
            if (char_id_map_exists(&vm->frames[Findex].VarIndexer, c_Vvalue)) {
                int SrcIndex = char_id_map_get(&vm->frames[Findex].VarIndexer, c_Vvalue);
                Vvalue = *(vm->frames[Findex].vars[SrcIndex]->value);
            } else {
                Vvalue = (int) strtoul(c_Vvalue, NULL, 10);
            }

            Var *V = stack_pop(&vm->frames[Findex].LocalVariables);
            if (strcmp(Vtype, (V->T == 0 ? "int" : "stack")) == 0) {
                if (strcmp(Vtype, "stack") == 0) {
                    if (V->stack_len == 0) {
                        if (strcmp(c_Vvalue, "nil") == 0)
                            delete_var(vm->frames[Findex].vars, &vm->frames[Findex].var_count,
                                       char_id_map_get(&vm->frames[Findex].VarIndexer, Vname));
                        else perror("[VM] DEALLOC stack deve essere nil!\n");
                    } else perror("[VM] DEALLOC valore finale di stack diverso da quello aspettato!\n");
                } else if (Vvalue == *(V->value)) {
                    delete_var(vm->frames[Findex].vars, &vm->frames[Findex].var_count,
                               char_id_map_get(&vm->frames[Findex].VarIndexer, Vname));
                } else {
                    printf("[VM] DEALLOC variabile o valore finale diverso da quello aspettato! (%d, %d)\n", Vvalue, *(V->value));
                    exit(1);
                }
            } else perror("[VM] DEALLOC errato (tipo o variabile)\n");

        } else if (strcmp(firstWord, "SHOW") == 0) {
            char* ID = strtok(NULL, " \t");
            if (strtok(NULL, " \t") != NULL) perror("[VM] SHOW: troppi parametri!\n");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID)) {
                fprintf(stderr, "[VM] SHOW: variabile '%s' non definita!\n", ID);
                exit(EXIT_FAILURE);
            }
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

            if (vm->frames[Findex].vars[Vindex] == NULL) {
                fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
                exit(EXIT_FAILURE);
            }
            if (vm->frames[Findex].vars[Vindex]->T == TYPE_INT)
                printf("%s: %d\n", ID, *(vm->frames[Findex].vars[Vindex]->value));
            else if (vm->frames[Findex].vars[Vindex]->T == TYPE_STACK) {
                Var* sv = vm->frames[Findex].vars[Vindex];
                printf("%s: [", ID);
                for (size_t k = 0; k < sv->stack_len; k++) {
                    printf("%d", sv->value[k]);
                    if (k + 1 < sv->stack_len) printf(", ");
                }
                printf("]\n");
            } else perror("[VM] SHOW su variabile PARAM non linkata!\n");

        } else if (strcmp(firstWord, "PUSHEQ") == 0) {
            char* ID = strtok(NULL, " \t");
            char* C_Vvalue = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if(!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] PUSHEQ su variabile iniziale non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            if (vm->frames[Findex].vars[Vindex]->T != TYPE_INT) perror("[VM] PUSHEQ non su INT!\n");
            int Vvalue = char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)
                ? *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue)]->value)
                : (int) strtoul(C_Vvalue, NULL, 10);
            *(vm->frames[Findex].vars[Vindex]->value) += Vvalue;

        } else if (strcmp(firstWord, "MINEQ") == 0) {
            char* ID = strtok(NULL, " \t");
            char* C_Vvalue = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if(!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] MINEQ su variabile iniziale non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            
            if (vm->frames[Findex].vars[Vindex]->T != TYPE_INT) perror("[VM] MINEQ non su INT!\n");
            int Vvalue = char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)
                ? *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue)]->value)
                : (int) strtoul(C_Vvalue, NULL, 10);
            
            //printf("%d %d\n", *(vm->frames[Findex].vars[Vindex]->value), Vvalue);
            *(vm->frames[Findex].vars[Vindex]->value) -= Vvalue;

        } else if (strcmp(firstWord, "PRODEQ") == 0) {
            char* ID = strtok(NULL, " \t");
            char* C_Vvalue = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if(!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] PRODEQ su variabile iniziale non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            if (vm->frames[Findex].vars[Vindex]->T != TYPE_INT) perror("[VM] PRODEQ non su INT!\n");
            int Vvalue = char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)
                ? *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue)]->value)
                : (int) strtoul(C_Vvalue, NULL, 10);
            *(vm->frames[Findex].vars[Vindex]->value) *= Vvalue;

        } else if (strcmp(firstWord, "DIVEQ") == 0) {
            char* ID = strtok(NULL, " \t");
            char* C_Vvalue = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if(!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] DIVEQ su variabile iniziale non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            if (vm->frames[Findex].vars[Vindex]->T != TYPE_INT) perror("[VM] DIVEQ non su INT!\n");
            int Vvalue = char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)
                ? *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue)]->value)
                : (int) strtoul(C_Vvalue, NULL, 10);
            if (Vvalue == 0) perror("[VM] Divisione per zero!\n");
            *(vm->frames[Findex].vars[Vindex]->value) /= Vvalue;

        } else if (strcmp(firstWord, "MODEQ") == 0) {
            char* ID = strtok(NULL, " \t");
            char* C_Vvalue = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if(!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] MODEQ su variabile iniziale non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            if (vm->frames[Findex].vars[Vindex]->T != TYPE_INT) perror("[VM] MODEQ non su INT!\n");
            int Vvalue = char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)
                ? *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue)]->value)
                : (int) strtoul(C_Vvalue, NULL, 10);
            if (Vvalue == 0) perror("[VM] Modulo per zero!\n");
            *(vm->frames[Findex].vars[Vindex]->value) %= Vvalue;

        } else if (strcmp(firstWord, "EXPEQ") == 0) {
            char* ID = strtok(NULL, " \t");
            char* C_Vvalue = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if(!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] EXPEQ su variabile iniziale non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            if (vm->frames[Findex].vars[Vindex]->T != TYPE_INT) perror("[VM] EXPEQ non su INT!\n");
            int Vvalue = char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)
                ? *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue)]->value)
                : (int) strtoul(C_Vvalue, NULL, 10);
            int base = *(vm->frames[Findex].vars[Vindex]->value), result = 1;
            for (int i = 0; i < Vvalue; i++) result *= base;
            *(vm->frames[Findex].vars[Vindex]->value) = result;

        } else if (strcmp(firstWord, "SWAP") == 0) {
            char* ID1 = strtok(NULL, " \t");
            char* ID2 = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            uint V1index = char_id_map_get(&vm->frames[Findex].VarIndexer, ID1);
            uint V2index = char_id_map_get(&vm->frames[Findex].VarIndexer, ID2);
            int TMP = *(vm->frames[Findex].vars[V1index]->value);
            *(vm->frames[Findex].vars[V1index]->value) = *(vm->frames[Findex].vars[V2index]->value);
            *(vm->frames[Findex].vars[V2index]->value) = TMP;

        } else if (strcmp(firstWord, "PUSH") == 0) {
            char* C_Vvalue = strtok(NULL, " \t");
            char* C_stack  = strtok(NULL, " \t");
            if (strtok(NULL, " \t") != NULL) perror("[VM] troppi parametri per PUSH!\n");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            int value_to_push;
            if (char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vvalue)) {
                uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vvalue);
                Var* src_var = vm->frames[Findex].vars[Vindex];
                value_to_push = *(src_var->value);
                *(src_var->value) = 0;
            } else {
                value_to_push = (int) strtoul(C_Vvalue, NULL, 10);
            }
            if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, C_stack))
                perror("[VM] PUSH: stack destinazione non trovato!\n");
            uint Sindex = char_id_map_get(&vm->frames[Findex].VarIndexer, C_stack);
            Var* stack_var = vm->frames[Findex].vars[Sindex];
            if (stack_var->T != TYPE_STACK) perror("[VM] PUSH: destinazione non e' stack!\n");
            stack_var->value = realloc(stack_var->value, (stack_var->stack_len + 1) * sizeof(int));
            if (!stack_var->value) perror("realloc failed");
            stack_var->value[stack_var->stack_len++] = value_to_push;

        } else if (strcmp(firstWord, "POP") == 0) {
            char* C_Vdest = strtok(NULL, " \t");
            char* C_stack = strtok(NULL, " \t");
            if (strtok(NULL, " \t") != NULL) perror("[VM] troppi parametri per POP!\n");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, C_stack))
                perror("[VM] POP: stack non trovato!\n");
            uint Sindex = char_id_map_get(&vm->frames[Findex].VarIndexer, C_stack);
            Var* stack_var = vm->frames[Findex].vars[Sindex];
            if (stack_var->T != TYPE_STACK) perror("[VM] POP: sorgente non e' stack!\n");
            if (stack_var->stack_len == 0) perror("[VM] POP: stack vuoto!\n");
            int popped = stack_var->value[--stack_var->stack_len];
            
            // FIX: realloc(ptr, 0) == free() su Linux → heap corruption
            // non ridurre sotto 1 elemento di spazio
            if (stack_var->stack_len > 0)
                stack_var->value = realloc(stack_var->value, stack_var->stack_len * sizeof(int));
            // se stack_len == 0 lasciamo l'allocazione esistente intatta
            
            if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, C_Vdest))
                perror("[VM] POP: destinazione non trovata!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, C_Vdest);
            *(vm->frames[Findex].vars[Vindex]->value) += popped;
        } else if (strcmp(firstWord, "EVAL") == 0) {
            char* ID = strtok(NULL, " \t");
            char* c_CValue = strtok(NULL, " \t");
            int Vvalue;
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            //prima controllaiamo se c_CValue e' un ID o un numero
            if(char_id_map_exists(&vm->frames[Findex].VarIndexer, c_CValue)){
                //abbiamo un ID
                uint c_CVindex = char_id_map_get(&vm->frames[Findex].VarIndexer, c_CValue);
                Vvalue = *(vm->frames[Findex].vars[c_CVindex]->value);
            }
            else{
                Vvalue = (int) strtoul(c_CValue, NULL, 10);
            }
            
            if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID))
                perror("[VM] EVAL di variabile non esistente!\n");
            uint Vindex = char_id_map_get(&vm->frames[Findex].VarIndexer, ID);

// AGGIUNGI QUESTO OVUNQUE
if (vm->frames[Findex].vars[Vindex] == NULL) {
    fprintf(stderr, "[VM] ERRORE: variabile '%s' e' NULL (gia' deallocata?)\n", ID);
    exit(EXIT_FAILURE);
}
            vm->frames[Findex].val_IF = (*(vm->frames[Findex].vars[Vindex]->value) == Vvalue);

        } else if (strcmp(firstWord, "JMPF") == 0) {
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            if (!vm->frames[Findex].val_IF) {
                char* c_LABEL = strtok(NULL, " \t");
                uint Lindex = char_id_map_get(&vm->frames[Findex].LabelIndexer, c_LABEL);
                *newline = '\n';
                ptr = go_to_line(original_buffer, vm->frames[Findex].label[Lindex] + 1);
                continue;
            }

        } else if (strcmp(firstWord, "JMP") == 0) {
            char* c_LABEL = strtok(NULL, " \t");
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            uint Lindex = char_id_map_get(&vm->frames[Findex].LabelIndexer, c_LABEL);
            *newline = '\n';
            ptr = go_to_line(original_buffer, vm->frames[Findex].label[Lindex] + 1);
            if (!ptr) perror("[VM] JMP: label non trovata!\n");
            continue;

        } else if (strcmp(firstWord, "ASSERT") == 0) {
            char *ID1 = strtok(NULL, " \t");
            char *ID2 = strtok(NULL, " \t");
            if (!ID1 || !ID2) { fprintf(stderr, "[VM] ASSERT: argomenti mancanti\n"); goto next_line; }
            uint Findex = char_id_map_get(&FrameIndexer, frame_name);
            char *ep1, *ep2;
            unsigned long val1 = strtoul(ID1, &ep1, 10);
            unsigned long val2 = strtoul(ID2, &ep2, 10);
            if (*ep1 != '\0') {
                if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID1))
                    perror("[VM] ASSERT: prima variabile non trovata!\n");
                val1 = *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, ID1)]->value);
            }
            if (*ep2 != '\0') {
                if (!char_id_map_exists(&vm->frames[Findex].VarIndexer, ID2))
                    perror("[VM] ASSERT: seconda variabile non trovata!\n");
                val2 = *(vm->frames[Findex].vars[char_id_map_get(&vm->frames[Findex].VarIndexer, ID2)]->value);
            }
            if (val1 != val2) {
                printf("[VM] ASSERT fallito! (v1=%ld, v2=%ld)\n", val1, val2);
                exit(EXIT_FAILURE);
            }
        }
        next_line:
        *newline = '\n';
        ptr = newline + 1;
    }

    free(original_buffer);
}

void vm_exec(VM *vm, char* buffer) {
    char* original_buffer = strdup(buffer); //vera copia non linkata con ptr e le sue modifiche
    char *ptr = buffer;
    int current_line = 1;
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
                    char_id_map_init(&FrameIndexer);
                    vm->frame_top = -1; //settiamo il frame corrente come vuoto
                } else if (strcmp(firstWord, "HALT") == 0) {
                    //vm->frame_top = -1; //frame corrente vuoto
                } else if (strcmp(firstWord, "PROC") == 0) {
                    //char_id_map_init(&vm->frames[vm->frame_top].LabelIndexer); //init della map per il frame
                    char* name = strtok(NULL, " \t"); //prendiamo il nome della funzione
                    uint index = char_id_map_get(&FrameIndexer, name); //segnamoci il numero del frame corrispondente
                    vm->frame_top = index; //ora l'ultimo frame è quello creato
                    //printf("Creazione stack per new proc %s : %d\n", name, vm->frame_top);

                    //ora nel frame appena creato inizializziamo il frame
                    char_id_map_init(&vm->frames[vm->frame_top].VarIndexer); //inizializziamo la mappa degli indici delle variabili del frame
                    stack_init(&vm->frames[vm->frame_top].LocalVariables); //init. dello stack LIFO delle variabili locali del frame
                    strncpy(vm->frames[vm->frame_top].name, name, VAR_NAME_LENGTH - 1); // memorizziamo il nome del frame
                    vm->frames[vm->frame_top].name[VAR_NAME_LENGTH - 1] = '\0';
                    //copiamo indirizzo del frame
                    vm->frames[vm->frame_top].addr = current_line;
                } else if (strcmp(firstWord, "END_PROC") == 0){
                    char* name = strtok(NULL, " \t"); //prendiamo il nome della funzione
                    //printf("%d\n",stack_size(&vm->frames[vm->frame_top].LocalVariables));
                    
                    //printf("%d\n",stack_size(&vm->frames[vm->frame_top].LocalVariables));
                    if (strcmp(name, "main") == 0){
                        //printf("%s\n", name);
                        //INIZIAMO A ESEGUIRE
                        //printf("%s\n", original_buffer);
                        char* main_name = "main";
                        vm_run_BT(vm, original_buffer, main_name);
                    }
                } else if (strcmp(firstWord, "DECL") == 0) {
                    //segniamo la crezione di una nuova variabile
                    char* type = strtok(NULL, " \t");
                    //printf("%s ", type);
                    char* Vname = strtok(NULL, " \t");
                    //printf("%s \t", Vname);

                    // prendiamo Vindex PRIMA di assegnare T, cosi' usiamo sempre l'indice stabile
                    int Vindex = char_id_map_get(&vm->frames[vm->frame_top].VarIndexer, Vname); //int perche puo essere -1
                    //printf("INDEX %d\n", Vindex);
                    //printf("STACK SIZE DECL: %d\n",stack_size(&vm->frames[vm->frame_top].LocalVariables));
                    if (stack_size(&vm->frames[vm->frame_top].LocalVariables) > -1){
                        perror("[VM] DECL non permessa: ci sono ancora variabili LOCAL aperte!\n");
                    }
                    if (vm->frames[vm->frame_top].vars[Vindex] != NULL){
                        //variabile gia definita con DECL
                        perror("[VM] Varriabile gia definita precedente!\n");
                    }

                    // allochiamo la struttura Var per questo slot
                    vm->frames[vm->frame_top].vars[Vindex] = malloc(sizeof(Var));
                    memset(vm->frames[vm->frame_top].vars[Vindex], 0, sizeof(Var));

                    // assegniamo T direttamente su Vindex (non su var_count)
                    if (strcmp(type, "int") == 0){
                        vm->frames[vm->frame_top].vars[Vindex]->T = TYPE_INT;
                    } else if (strcmp(type, "stack") == 0){
                        vm->frames[vm->frame_top].vars[Vindex]->T = TYPE_STACK;
                    } else perror("[VM] type variabile non esistente\n");

                    // aggiorniamo var_count come high-water mark
                    if (Vindex >= vm->frames[vm->frame_top].var_count)
                        vm->frames[vm->frame_top].var_count = Vindex + 1;

                    //ASSEGNIAZIONE VALORI DEFAULT
                    //printf("Frame %d, Var: %s, IndexFrame: %d\n", vm->frame_top, Vname, Vindex);
                    if (vm->frames[vm->frame_top].vars[Vindex]->T == TYPE_STACK){
                        vm->frames[vm->frame_top].vars[Vindex]->stack_len = 0; //segniamo che e' uno stack mettendo 0 di lunghezza
                        vm->frames[vm->frame_top].vars[Vindex]->value = malloc(VAR_STACK_MAX_SIZE * sizeof(int)); //dimensione dello stack
                        //printf("%s\n", c_Vvalue);
                    } 
                    else {
                        //se e' INT
                        vm->frames[vm->frame_top].vars[Vindex]->value = malloc(sizeof(int));
                        int Vvalue = 0; 
                        *(vm->frames[vm->frame_top].vars[Vindex]->value) = Vvalue; //la variabile DECL viene inizializzata a 0
                        //printf("%d\n",*(vm->frames[vm->frame_top].vars[Vindex]->value));
                    }

                    vm->frames[vm->frame_top].vars[Vindex]->is_local = 0; //la variabile DECL non è definita locale
                    strncpy(vm->frames[vm->frame_top].vars[Vindex]->name, Vname, VAR_NAME_LENGTH - 1); // memorizziamo il nome della variabile
                    vm->frames[vm->frame_top].vars[Vindex]->name[VAR_NAME_LENGTH - 1] = '\0';
                    //printf("DECL finito\n");
                    //printf("FINE STACK SIZE DECL: %d\n",stack_size(&vm->frames[Vindex].LocalVariables));
                } else if (strcmp(firstWord, "LOCAL") == 0){
                    // tutto a runtime
                } else if (strcmp(firstWord, "DELOCAL") == 0){
                    //da controllare in esecuzione
                } else if (strcmp(firstWord, "CALL") == 0) {
                    //fatto a runtime
                } else if (strcmp(firstWord, "UNCALL") == 0){
                    //implementato nel run
                } else if (strcmp(firstWord, "PARAM") == 0){
                    //inizializziamo le variabili a NULL
                    //prendiamo il frame attuale
                    char* Vtype = strtok(NULL, " \t");
                    char* Vname = strtok(NULL, " \t");
                    //printf("%s %s\n", Vtype, Vname);
                    int Vindex = char_id_map_get(&vm->frames[vm->frame_top].VarIndexer, Vname);
                    //printf("%d\n", Vindex);
                    if (vm->frames[vm->frame_top].vars[Vindex] != NULL){
                        //variabile gia definita con DECL
                        perror("[VM] Non puoi definire piu' volte la stessa variabile nei parametri!\n");
                    }

                    // allochiamo la struttura Var per questo slot
                    vm->frames[vm->frame_top].vars[Vindex] = malloc(sizeof(Var));
                    memset(vm->frames[vm->frame_top].vars[Vindex], 0, sizeof(Var));

                     if (Vindex >= vm->frames[vm->frame_top].var_count)
                        vm->frames[vm->frame_top].var_count = Vindex + 1;

                    // assegniamo T direttamente su Vindex (non su var_count)
                    if (strcmp(Vtype, "int") == 0){
                        vm->frames[vm->frame_top].vars[Vindex]->T = TYPE_INT;
                    } else if (strcmp(Vtype, "stack") == 0){
                        vm->frames[vm->frame_top].vars[Vindex]->T = TYPE_STACK;
                    } else perror("[VM] type variabile non esistente\n");

                    //vm->frames[vm->frame_top].var_count[(int*)(Vindex)] = NULL;

                    //NON DOBBIAMO DEFINIRE VALORI DI DEFAULT PERCHE POI LA LINKIAMO CON QUELLA DI RIFERIMENTO
                    //per non avere seg fault mettiamo valori nul
                    vm->frames[vm->frame_top].vars[Vindex]->value = NULL;
                    vm->frames[vm->frame_top].vars[Vindex]->T = TYPE_PARAM;

                    vm->frames[vm->frame_top].vars[Vindex]->is_local = 0; //la variabile PARAM non è definita locale
                    strncpy(vm->frames[vm->frame_top].vars[Vindex]->name, Vname, VAR_NAME_LENGTH - 1); // memorizziamo il nome della variabile
                    vm->frames[vm->frame_top].vars[Vindex]->name[VAR_NAME_LENGTH - 1] = '\0';
                    //printf("PARAM Frame %d, Var: %s, IndexFrame: %d\n", vm->frame_top, Vname, Vindex);
                    vm->frames[vm->frame_top].param_indices[vm->frames[vm->frame_top].param_count++] = Vindex;
                } else if (strcmp(firstWord, "SHOW") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "PUSHEQ") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "MINEQ") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "PRODEQ") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "DIVEQ") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "MODEQ") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "EXPEQ") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "SWAP") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "PUSH") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "POP") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "EVAL") == 0){
                    //tempo di esecuzione
                   
                } else if (strcmp(firstWord, "JMPF") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "JMP") == 0){
                    //tempo di esecuzione
                } else if (strcmp(firstWord, "LABEL") == 0){
                    //linkiamo la label per il frame attuale
                    char* Lname = strtok(NULL, " \t");
                    //printf("%s\n", Lname);
                    uint Lindex = char_id_map_get(&vm->frames[vm->frame_top].LabelIndexer, Lname); 
                    vm->frames[vm->frame_top].label[Lindex] = current_line;//salviamo la riga della label
                    //printf("LABEL riga %d\n", vm->frames[vm->frame_top].label[Lindex]);
                } else if (strcmp(firstWord, "ASSERT") == 0){
                    //tempo di esecuzione
                } else {
                    printf("[VM] Istruzione sconosciuta: %s\n", firstWord);
                    exit(EXIT_FAILURE);
                }
            } else {
                printf("[VM] Bytecode formattato male!\n");
            }

            *newline = '\n';  // ripristina il carattere
            ptr = newline + 1; // passa alla prossima riga
            current_line++;
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
        if(strcmp(f->name, "main")==0){
            //stampiamo solo main
            //printf("frame[%d] (%s): \n", i, f->name);
            for (int j = 0; j < f->var_count; j++) {
                Var *v = f->vars[j];
                if (v == NULL) continue; // slot libero (variabile gia' deallocata), saltiamo
                //printf("\tVar[%d] Name: %s, Type: %s, is_local: %d, value: ", j, v->name, (v->T == 0 ? "INT" : (v->T == 1 ? "STACK" : "PARAM")), v->is_local);
                printf("%s: ", v->name);
                if (v->T == 0) {  // INT
                    printf("%d", *(v->value));
                } else { // STACK
                    printf("[");
                    for (size_t k = 0; k < v->stack_len; k++) {
                        printf("%d", v->value[k]);
                        if (k + 1 < v->stack_len) printf(", ");
                    }
                    printf("]");
                }
                printf("\n");
            }
        }
        
    }
}

#define START_BUFFER 256
#define AST_BUFFER 1024*10
int main(){
    char buffer[START_BUFFER];
    char ast[AST_BUFFER];  // buffer più grande per contenere tutto il file
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
    memset(&vm, 0, sizeof(VM)); 
    //size_t length = sizeof(ast);
    vm_exec(&vm, ast);
    vm_dump(&vm);
    return 0;
}