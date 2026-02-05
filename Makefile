build: build.sh
	./build.sh

run: Assembler.c
	./hw3 tests/basic/basic.tk interm.tk output.tko

compinterm: interm.tk
	diff interm.tk tests/basic/basic.tkir

compout: output.tko
	xxd output.tko tests/basic/basic.tko