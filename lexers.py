# scanner.py
import ply.lex as lex

# Parole riservate
reserved = {
    'procedure' : 'PROCEDURE',
    'call'      : 'CALL',
    'uncall'    : 'UNCALL',
    'local'     : 'LOCAL',
    'delocal'   : 'DELOCAL',
    'int'       : 'INT',
    'stack'     : 'STACK',
    'from'      : 'FROM',
    'loop'      : 'LOOP',
    'until'     : 'UNTIL',
    'push'      : 'PUSH',
    'pop'       : 'POP',
    'show'      : 'SHOW',
    'if'        : 'IF',
    'then'      : 'THEN',
    'else'      : 'ELSE',
    'fi'        : 'FI',
    'skip'      : 'SKIP',
}

# Lista completa dei token
tokens = list(reserved.values()) + [
    # Operatori assegnazione
    'PLUS_EQ', 'MINUS_EQ', 'XOR_EQ',
    # Operatori confronto
    'NEQ', 'LTE', 'GTE', 'LT', 'GT', 'EQ',
    # Operatori aritmetici
    'PLUS', 'MINUS', 'STAR', 'SLASH', 'PERCENT',
    'AMP', 'CARET', 'PIPE',
    # Punteggiatura
    'LPAREN', 'RPAREN', 'LBRACKET', 'RBRACKET', 'COMMA',
    # Letterali
    'NUMBER', 'IDENT',
]

# Operatori assegnazione
t_PLUS_EQ  = r'\+='
t_MINUS_EQ = r'-='
t_XOR_EQ   = r'\^='

# Operatori confronto
t_NEQ = r'!='
t_LTE = r'<='
t_GTE = r'>='
t_LT  = r'<'
t_GT  = r'>'
t_EQ  = r'='

# Operatori aritmetici
t_PLUS    = r'\+'
t_MINUS   = r'-'
t_STAR    = r'\*'
t_SLASH   = r'/'
t_PERCENT = r'%'
t_AMP     = r'&'
t_CARET   = r'\^'
t_PIPE    = r'\|'

# Punteggiatura
t_LPAREN   = r'\('
t_RPAREN   = r'\)'
t_LBRACKET = r'\['
t_RBRACKET = r'\]'
t_COMMA    = r','

# Identificatori e parole riservate
def t_IDENT(t):
    r'[a-zA-Z_][a-zA-Z0-9_]*'
    t.type = reserved.get(t.value, 'IDENT')  # controlla se è riservata
    return t

# Numeri
def t_NUMBER(t):
    r'\d+'
    t.value = int(t.value)
    return t

# Commenti — ignorati
def t_COMMENT(t):
    r'//[^\n]*'
    pass

# Newline — traccia i numeri di riga
def t_newline(t):
    r'\n+'
    t.lexer.lineno += len(t.value)

# Spazi e tab — ignorati
t_ignore = ' \t'

# Errore
def t_error(t):
    print(f"Carattere illegale '{t.value[0]}' alla riga {t.lexer.lineno}")
    t.lexer.skip(1)

# Costruisce il lexer
lexer = lex.lex()

# --- Test ---
if __name__ == '__main__':
    source = """
procedure main()
    local int x = 0
    x += 5
    show(x)
    delocal int x = 5
"""
    lexer.input(source)
    for tok in lexer:
        print(tok)

