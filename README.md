# Janus VM

Interprete/VM per un linguaggio Janus reversibile con supporto a:

- procedure e `call` / `uncall`
- variabili `int`, `stack`, `channel`
- blocchi `local` / `delocal`
- controllo `if ... then ... else ... fi ...`
- cicli `from ... loop ... until ...`
- parallelismo `par ... and ... rap`
- canali sincroni (`ssend` / `srecv`)

## Prerequisiti

- Linux
- Python 3
- GCC
- libreria Python `ply`

Installazione dipendenza Python:

```bash
./venv/bin/pip install ply
```

oppure fuori da venv:

```bash
pip install ply
```

## Build (usare il makefile del progetto)

Il repository usa `makefile` (minuscolo) come entrypoint principale.

Build VM:

```bash
make
```

equivale a compilare `libvm.so`.

Build manuale (solo se serve):

```bash
gcc -shared -fPIC -o libvm.so Janus.c -I. -Wall
```

## Esecuzione

Via makefile (consigliato):

```bash
make run FILE=Jprograms/test.janus
```

Questo esegue `Janus.py` e salva anche `bytecode.txt`.

Esegui un programma `.janus`:

```bash
./venv/bin/python Janus.py Jprograms/test.janus
```

Per salvare anche il bytecode generato in `bytecode.txt`:

```bash
./venv/bin/python Janus.py Jprograms/test.janus --dump-bytecode
```

## Toolchain (pipeline)

1. `Jlexer.py` tokenizza il sorgente.
2. `Jparser.py` produce AST.
3. `JBytecode.py` converte AST in bytecode lineare.
4. `Janus.py` passa il bytecode a `libvm.so` via `ctypes`.
5. `Janus.c` esegue il bytecode nella VM.

## Sintassi del linguaggio (quick reference)

### Procedure

```janus
procedure main()
    int x
```

Con parametri:

```janus
procedure sum(int a, int b)
```

Tipi supportati: `int`, `stack`, `channel`.

### Dichiarazioni e assegnamenti

Dichiarazione:

```janus
int x
stack s
channel ch
```

Assegnamenti composti:

- `+=`
- `-=`
- `*=`
- `/=`
- `%=`
- `^=`
- `<=>` (swap)

Esempio:

```janus
x += 1
y <=> x
```

### Local / Delocal

```janus
local int k = key
...
delocal int k = 0
```

Nota: `delocal` verifica il valore finale atteso.

`delocal` supporta anche forma senza valore:

```janus
delocal int x
```

### Call / Uncall

```janus
call encrypt(item, key)
uncall encrypt(item, key)
```

### Chiamate dirette (built-in e procedure)

Il parser supporta anche forma diretta `ID(...)`, oltre a `call ID(...)`.
Esempi:

```janus
show(x)
push(x, s)
pop(x, s)
ssend(x, ch)
srecv(x, ch)
increment(a)
```

### If reversibile

Forma supportata:

```janus
if a = b then
    ...
else
    ...
fi c = d
```

Anche senza `else`:

```janus
if a = b then
    ...
fi c = d
```

### Loop reversibile

```janus
from i = 0 loop
    ...
until i = n
```

### Parallelismo

```janus
par
    call producer(ch, n)
and
    call consumer(ch, result, n)
rap
```

`par` può avere più branch separati da `and`.

## Pattern usati nei programmi di esempio (`Jprograms/`)

Dai file di esempio presenti nel repository, questi pattern sono tutti validi:

- dichiarazioni semplici in body (`int x`, `stack s`, `channel c`)
- inizializzazione locale reversibile (`local int x = 0`, `local stack s = nil`, `local channel ch = empty`)
- produttore/consumatore sia sequenziale (`stack`) sia parallelo (`channel`)
- ricorsione (`fib_ricorsivo`, `encrypt` ricorsiva)
- `call` e `uncall` sulla stessa procedura
- blocchi paralleli annidati (`par` dentro `par`)

## Lessico

- Identificatori: `[a-zA-Z_][a-zA-Z0-9_]*`
- Numeri: interi non negativi (`\d+`)
- Commenti: `// commento`

## File utili nel repository

- `Janus.c`: runtime VM
- `Janus.py`: runner Python che invoca la VM
- `Jlexer.py`: lexer PLY
- `Jparser.py`: parser PLY
- `JBytecode.py`: compilatore AST -> bytecode
- `Jprograms/`: esempi di programmi Janus

## Debug rapido

- Verifica bytecode:

```bash
./venv/bin/python Janus.py Jprograms/test.janus --dump-bytecode
```

- Ricompila sempre `libvm.so` dopo modifiche a `Janus.c`.

## Comandi make utili

- `make` -> compila `libvm.so`
- `make run FILE=...` -> esegue un `.janus`
- `make build_app` -> build standalone con PyInstaller
- `make clean` -> pulizia artefatti
