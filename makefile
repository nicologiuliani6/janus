# Makefile per Janus – run con file come argomento
.PHONY: all run build_app clean

PYTHON=./venv/bin/python
JANUS_SCRIPT=Janus.py
LIBVM=libvm.so
DIST_DIR=dist

# Default: build VM
all: $(LIBVM)

# Compila la VM condivisa
$(LIBVM): Janus.c
	gcc -shared -fPIC -o $(LIBVM) Janus.c -I. -Wall
	@echo "VM compilata: $(LIBVM)"

# -----------------------------
# Run: prende il file da argomento
# -----------------------------
run: $(LIBVM)
ifndef FILE
	$(error Devi passare il file da eseguire! Es: make run FILE=Jprograms/test.janus)
endif
	$(PYTHON) $(JANUS_SCRIPT) $(FILE) --dump-bytecode

# Build eseguibile standalone usando PyInstaller globale (pipx)
build_app: $(LIBVM)
	pyinstaller --onefile \
		--name JanusApp \
		--add-binary "$(LIBVM):." \
		--hidden-import=ply \
		--hidden-import=ply.yacc \
		--hidden-import=ply.lex \
		$(JANUS_SCRIPT)
	@echo "Build standalone completata: $(DIST_DIR)/JanusApp"

# Pulizia
clean:
	rm -f $(LIBVM)
	rm -rf build $(DIST_DIR) __pycache__ *.spec
	@echo "Pulizia completata"