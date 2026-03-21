/*
 * check_if_reversibility.c
 *
 * Analisi statica del bytecode: verifica che la variabile usata come
 * condizione nei blocchi if-else-fi non venga modificata all'interno
 * di quei blocchi.
 *
 * PATTERN RICONOSCIUTO
 * --------------------
 *
 *   if-else-fi (con ramo else):
 *     EVAL  var val          ← condizione di entrata
 *     JMPF  L_else           ← salto forward al ramo else
 *       [then-block]
 *     JMP   L_fi             ← salto forward alla fine (segnala else)
 *     LABEL L_else
 *       [else-block]
 *     LABEL L_fi
 *     EVAL  var val          ← condizione di uscita (uguale)
 *
 *   if-fi (senza ramo else):
 *     EVAL  var val
 *     JMPF  L_fi             ← salto forward
 *       [then-block]
 *     LABEL L_fi
 *     EVAL  var val
 *
 * DISTINZIONE DA LOOP
 * -------------------
 *   Un loop contiene almeno un JMPF *backward* all'interno del corpo.
 *   Se troviamo un JMPF backward nel range allora non è un if-fi e
 *   saltiamo il controllo.
 *
 * ISTRUZIONI CHE "SCRIVONO" UNA VARIABILE
 * ----------------------------------------
 *   PUSHEQ x ...   → x += ...
 *   MINEQ  x ...   → x -= ...
 *   PRODEQ x ...   → x *= ...
 *   DIVEQ  x ...   → x /= ...
 *   MODEQ  x ...   → x %= ...
 *   EXPEQ  x ...   → x ^= ...
 *   POP    x stack → x += top(stack)
 *   PUSH   x stack → x viene azzerata (il valore è spostato nello stack)
 *   SWAP   x y     → modifica entrambi
 *   LOCAL  t x v   → crea (o ricrea) x  [conservativo]
 *   DELOCAL t x v  → distrugge x        [conservativo]
 */

#include "check_if_reversibility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Costanti interne                                                    */
/* ------------------------------------------------------------------ */

#define CIR_MAX_LINES   4096
#define CIR_MAX_ARGS       8
#define CIR_TOK_LEN      128
#define CIR_MAX_LABELS   256
#define CIR_LINE_PREFIX    6   /* ogni riga del bytecode ha 6 car. di prefisso */

/* ------------------------------------------------------------------ */
/*  Strutture interne                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    int  lineno;
    char op  [CIR_TOK_LEN];
    char arg [CIR_MAX_ARGS][CIR_TOK_LEN];
    int  argc;
} CIR_Line;

typedef struct {
    char name  [CIR_TOK_LEN];
    int  lineno;          /* numero di riga della LABEL nel sorgente */
} CIR_Label;

/* ------------------------------------------------------------------ */
/*  Predicato: l'istruzione scrive nella variabile var_name?           */
/* ------------------------------------------------------------------ */

static int writes_to(const CIR_Line *L, const char *var_name)
{
    const char *op = L->op;

    /* Operatori aritmetici: primo argomento è la destinazione */
    if (strcmp(op, "PUSHEQ") == 0 || strcmp(op, "MINEQ")  == 0 ||
        strcmp(op, "PRODEQ") == 0 || strcmp(op, "DIVEQ")  == 0 ||
        strcmp(op, "MODEQ")  == 0 || strcmp(op, "EXPEQ")  == 0)
        return L->argc >= 1 && strcmp(L->arg[0], var_name) == 0;

    /* POP dest stack: dest riceve il valore tolto dallo stack */
    if (strcmp(op, "POP") == 0)
        return L->argc >= 1 && strcmp(L->arg[0], var_name) == 0;

    /* PUSH val stack: se val è una variabile, viene azzerata */
    if (strcmp(op, "PUSH") == 0)
        return L->argc >= 1 && strcmp(L->arg[0], var_name) == 0;

    /* SWAP x y: entrambi i lati vengono modificati */
    if (strcmp(op, "SWAP") == 0)
        return (L->argc >= 1 && strcmp(L->arg[0], var_name) == 0) ||
               (L->argc >= 2 && strcmp(L->arg[1], var_name) == 0);

    /*
     * LOCAL / DELOCAL: conservativamente segnaliamo se il nome coincide,
     * perché una LOCAL potrebbe oscurare la variabile condizione, rendendo
     * l'analisi ambigua.
     * arg layout: [tipo] [nome] [valore]
     */
    if (strcmp(op, "LOCAL") == 0 || strcmp(op, "DELOCAL") == 0)
        return L->argc >= 2 && strcmp(L->arg[1], var_name) == 0;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Utilità: cerca label per nome, ritorna lineno o -1                 */
/* ------------------------------------------------------------------ */

static int label_lineno(const CIR_Label *labels, int nlabels,
                        const char *name)
{
    for (int i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0)
            return labels[i].lineno;
    return -1;
}

/* Cerca l'indice in lines[] della prima riga con lineno >= target */
static int idx_at_lineno(const CIR_Line *lines, int nlines, int target)
{
    for (int i = 0; i < nlines; i++)
        if (lines[i].lineno >= target)
            return i;
    return nlines;
}

/* ------------------------------------------------------------------ */
/*  Parsing del buffer in righe tokenizzate                            */
/* ------------------------------------------------------------------ */

static int parse_lines(const char *buffer,
                       CIR_Line   *lines,
                       int         max_lines)
{
    int nlines = 0;
    const char *p = buffer;
    int lineno = 1;

    while (*p && nlines < max_lines) {
        const char *nl = strchr(p, '\n');
        if (!nl) break;

        int row_len = (int)(nl - p);

        if (row_len > CIR_LINE_PREFIX) {
            const char *content = p + CIR_LINE_PREFIX;
            int clen = row_len - CIR_LINE_PREFIX;
            if (clen > 511) clen = 511;

            char tmp[512];
            strncpy(tmp, content, clen);
            tmp[clen] = '\0';

            char *tok = strtok(tmp, " \t\r");
            if (tok) {
                CIR_Line *L = &lines[nlines];
                L->lineno = lineno;
                L->argc   = 0;
                strncpy(L->op, tok, CIR_TOK_LEN - 1);
                L->op[CIR_TOK_LEN - 1] = '\0';

                while ((tok = strtok(NULL, " \t\r")) != NULL &&
                       L->argc < CIR_MAX_ARGS) {
                    strncpy(L->arg[L->argc], tok, CIR_TOK_LEN - 1);
                    L->arg[L->argc][CIR_TOK_LEN - 1] = '\0';
                    L->argc++;
                }
                nlines++;
            }
        }

        p = nl + 1;
        lineno++;
    }

    return nlines;
}

/* ------------------------------------------------------------------ */
/*  Funzione principale                                                 */
/* ------------------------------------------------------------------ */

int vm_check_if_reversibility(const char *buffer)
{
    static CIR_Line  lines [CIR_MAX_LINES];
    static CIR_Label labels[CIR_MAX_LABELS];

    int nlines  = parse_lines(buffer, lines, CIR_MAX_LINES);
    int errors  = 0;
    int nlabels = 0;

    /* Nome della PROC corrente (per i messaggi di errore) */
    char proc_name[CIR_TOK_LEN] = "<globale>";
    int  in_proc = 0;

    /* ----------------------------------------------------------------
     *  Prima passata per PROC corrente: raccoglie label della procedura.
     *  Seconda: scansiona i blocchi if-fi.
     *  Le due passate vengono eseguite insieme: ogni volta che si entra
     *  in una PROC si fa prima una mini-passata forward per raccogliere
     *  le label, poi si riparte dall'inizio della PROC per l'analisi.
     * ---------------------------------------------------------------- */

    for (int i = 0; i < nlines; i++) {
        CIR_Line *L = &lines[i];

        /* ---- Entrata / uscita da PROC ---- */
        if (strcmp(L->op, "PROC") == 0) {
            in_proc = 1;
            if (L->argc > 0)
                strncpy(proc_name, L->arg[0], CIR_TOK_LEN - 1);
            else
                strncpy(proc_name, "?", CIR_TOK_LEN - 1);

            /* Raccolta label della PROC corrente */
            nlabels = 0;
            for (int j = i + 1; j < nlines; j++) {
                if (strcmp(lines[j].op, "END_PROC") == 0) break;
                if (strcmp(lines[j].op, "LABEL") == 0 &&
                    lines[j].argc > 0 &&
                    nlabels < CIR_MAX_LABELS) {
                    strncpy(labels[nlabels].name,
                            lines[j].arg[0], CIR_TOK_LEN - 1);
                    labels[nlabels].lineno = lines[j].lineno;
                    nlabels++;
                }
            }
            continue;
        }

        if (strcmp(L->op, "END_PROC") == 0) {
            in_proc = 0;
            continue;
        }

        if (!in_proc) continue;

        /* ---- Cerca pattern: EVAL + JMPF(forward) ---- */
        if (strcmp(L->op, "EVAL") != 0 || L->argc < 2) continue;
        if (i + 1 >= nlines)                            continue;

        CIR_Line *Ljmpf = &lines[i + 1];
        if (strcmp(Ljmpf->op, "JMPF") != 0 || Ljmpf->argc < 1) continue;

        /* Risolvi il target del JMPF */
        int jmpf_target = label_lineno(labels, nlabels, Ljmpf->arg[0]);
        if (jmpf_target < 0) continue;                    /* label sconosciuta */
        if (jmpf_target <= Ljmpf->lineno) continue;       /* salto BACKWARD → loop */

        /* Indice (in lines[]) della riga target del JMPF */
        int jmpf_tgt_idx = idx_at_lineno(lines, nlines, jmpf_target);

        /* ---- Distingui loop da if-fi ----
         *
         * Un loop ha un JMPF *backward* nel corpo (tra i+2 e jmpf_tgt_idx).
         * Se ne troviamo uno, non è un if-fi → saltiamo.
         */
        int is_loop = 0;
        for (int k = i + 2; k < jmpf_tgt_idx; k++) {
            if (strcmp(lines[k].op, "JMPF") == 0 && lines[k].argc > 0) {
                int kt = label_lineno(labels, nlabels, lines[k].arg[0]);
                if (kt >= 0 && kt < lines[k].lineno) { /* backward */
                    is_loop = 1;
                    break;
                }
            }
        }
        if (is_loop) continue;

        /*
         * È un if-fi.  Determina se esiste un ramo else:
         * cerca un JMP forward nel then-block (prima di LABEL L_else)
         * che punta a una label *dopo* L_else.
         *
         * Se trovato:  range da controllare = [i+2 .. fi_tgt_idx)
         * Se non trovato: range = [i+2 .. jmpf_tgt_idx)
         */
        int  fi_tgt_lineno = jmpf_target; /* default: if senza else */
        int  found_else    = 0;

        for (int k = i + 2; k < jmpf_tgt_idx; k++) {
            if (strcmp(lines[k].op, "JMP") == 0 && lines[k].argc > 0) {
                int jt = label_lineno(labels, nlabels, lines[k].arg[0]);
                if (jt > jmpf_target) {       /* forward oltre L_else */
                    fi_tgt_lineno = jt;
                    found_else    = 1;
                    break;
                }
            }
        }

        int fi_tgt_idx = idx_at_lineno(lines, nlines, fi_tgt_lineno);

        /* ---- Variabile condizione ---- */
        const char *cond_var = L->arg[0];

        /* ---- Scansione del range per scritture a cond_var ---- */
        for (int k = i + 2; k < fi_tgt_idx; k++) {
            /* Salta istruzioni di struttura */
            if (strcmp(lines[k].op, "LABEL") == 0 ||
                strcmp(lines[k].op, "JMP")   == 0 ||
                strcmp(lines[k].op, "JMPF")  == 0 ||
                strcmp(lines[k].op, "EVAL")  == 0)
                continue;

            if (writes_to(&lines[k], cond_var)) {
                printf("[WARKING] La procedure \"%s\" dentro un blocco %s ha la variabile di controllo '%s' modificata da istruzione: %s (line %d)\n",
                       proc_name,
                       found_else ? "if-else-fi" : "if-fi",
                       cond_var,
                       lines[k].op, 
                    lines[k].lineno);
                errors++;
            }
        }
    }

    /* ---- Report finale ---- */
    return errors;
}