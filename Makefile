build: build.sh
	./build.sh

run: Assembler.c
	./hw3 tests/full.tk interm.tk output.tko

compinterm: interm.tk
	diff interm.tk tests/full.tkir

compout: output.tko
	cmp -l output.tko tests/full.tko

hex: output.tko
	hexdump -C output.tko

exp: tests/full.tko
	hexdump -C tests/full.tko