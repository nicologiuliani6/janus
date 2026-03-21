#ifndef CHECK_IF_REVERSIBILITY_H
#define CHECK_IF_REVERSIBILITY_H

/*
 * check_if_reversibility.h
 *
 * Analisi statica del bytecode: verifica che la variabile usata come
 * condizione nei blocchi if-else-fi non venga modificata all'interno
 * di quei blocchi, garantendo la reversibilità.
 *
 * Chiamare PRIMA di vm_exec(), passando il buffer grezzo del bytecode.
 *
 * Ritorna il numero di violazioni trovate (0 = tutto ok).
 */
int vm_check_if_reversibility(const char *buffer);

#endif /* CHECK_IF_REVERSIBILITY_H */