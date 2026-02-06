build: build.sh
	./build.sh

run: Assembler.c
	./hw3 tests/full_valid/full_valid.tk interm.tk output.tko

compinterm: interm.tk
	diff interm.tk tests/full_valid/full_valid.tkir

compout: output.tko
	cmp -l output.tko tests/full_valid/full_valid.tko

hex: output.tko
	hexdump -C output.tko

exp: tests/full_valid/full_valid.tko
	hexdump -C tests/full_valid/full_valid.tko