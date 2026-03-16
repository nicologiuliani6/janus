import sys
from Jast import parser, lexer
from queue import Queue

# ───────────────────────────────────────────
#  Indirizzo per backpatching dei salti
# ───────────────────────────────────────────
class ByteCode_Compiler:
    def __init__(self):
        self.queue   = Queue()
        self.addr    = 0          # contatore istruzioni
        self.labels  = {}         # label → indirizzo

    def emit(self, instr):
        self.queue.put((self.addr, instr))
        self.addr += 1

    # ── entry point ──────────────────────────
    def process(self, ast):
        if not ast:
            return
        head = ast[0]

        match head:

            case 'program':
                # ('program', [procedure, ...])
                self.emit("START")
                procs = ast[1] if len(ast) > 1 else []
                for child in procs:
                    if isinstance(child, (list, tuple)):
                        self.process(child)
                self.emit("HALT")

            case 'procedure':
                # ('procedure', nome, params, body)
                name   = ast[1]
                body   = ast[3] if len(ast) > 3 else []
                self.labels[name] = self.addr
                self.emit(f"PROC {name}")
                for stmt in body:
                    self.process(stmt)
                self.emit(f"END_PROC {name}")

            case 'decl':
                # (DECL, type, ID)
                type = ast[1]
                ID = ast[2]
                self.emit(f"DECL {type} {ID}")
            case 'local':
                #(LOCAL, 'type', ID, value)
                type = ast[1]
                ID = ast[2]
                value = ast[3]
                self.emit(f"LOCAL {type} {ID} {value}")
            case 'delocal':
                #(DELOCAL, 'type', ID, value)
                type = ast[1]
                ID = ast[2]
                value = ast[3]
                self.emit(f"DELOCAL {type} {ID} {value}")

            case _:
                print(f"[WARN] nodo AST non gestito: {head}  →  {ast}")


# ───────────────────────────────────────────
#  Main
# ───────────────────────────────────────────
if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Uso: python Janus.py <file>")
        sys.exit(1)

    with open(sys.argv[1], 'r') as f:
        source = f.read()

    ast = list(parser.parse(source, lexer=lexer))

    BT_Compiler= ByteCode_Compiler()
    BT_Compiler.process(ast)

    #print("=== Bytecode generato ===")
    with open("bytecode.txt", "w") as f:
        while not BT_Compiler.queue.empty():
            addr, instr = BT_Compiler.queue.get()
            line = f"{addr:04d}  {instr}\n"
            f.write(line)
            #print(line, end="")  # opzionale: stampa anche a video
    
