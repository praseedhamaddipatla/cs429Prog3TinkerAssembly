build: build.sh
	./build.sh

run: Assembler.c
	./hw3 tests/basic/basic.tk interm.tk output.tko

compinterm: interm.tk
	diff interm.tk tests/basic/basic.tkir

compout: output.tko
	cmp -l output.tko tests/basic/basic.tko

hex: output.tko
	hexdump -C output.tko

exp: tests/basic/basic.tko
	hexdump -C tests/basic/basic.tko