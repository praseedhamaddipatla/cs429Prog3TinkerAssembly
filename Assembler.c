#define _GNU_SOURCE
#include "Assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

int main(int argc, char **argv){
    return testmain(argc, argv);
}

int testmain(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "Error: invalid number of arguments");
        return 1;
    }
    assembleFile(argv[1]);
    return 0;
}

int assembleFile(const char *file){
    FILE *input = fopen(file, "r");
    FILE *interm = fopen("intermediate.tmp", "w+");
    FILE *output = fopen("output.tko", "wb");

    initTable();
    pass1(input, interm);
    rewind(interm);
    pass2(interm, output);

    fclose(input);
    fclose(interm);
    fclose(output);
    freeAllMemory();

    return 0;
}

void pass1(FILE *in, FILE *interm){
    uint64_t currAddr = codeStart;
    SectionType currSec = NONE;

    char *line = NULL;
    size_t len = 0;

    while(getline(&line, &len, in)!=-1){
        trim(line);

        if(isBlank(line)){
            continue;
        }

        LineType type = getLineType(line);
        if(type==DIRECTIVE){
            currSec=getSectionType(line);
            continue;
        }
        if(type==LABEL){
            labelDef(line, currAddr);
            continue;
        }
        if(type==INSTRUCTION){
            if (currSec==CODE){
                if(isMacro(line)){
                    int count = expandMacro(line, interm);
                    currAddr = currAddr+count*4;

                }
                else{
                    fprintf(interm, "%s\n", line);
                    currAddr=currAddr+4;
                }
            }
            else if (currSec==DATA){
                fprintf(interm, "%s\n", line);
                currAddr=currAddr+8;
            }
            else{
                syntaxError(line);
            }
        }
    }
}

LineType getLineType(const char *line){
    if (line[0] == ';') {
        return COMMENT;
    } else if (line[0] == '.') {
        return DIRECTIVE;
    } else if (line[0] == ':') {
        return LABEL;
    } else if (line[0] == '\t') {
        return INSTRUCTION;
    }
    return EMPTY;
}

SectionType getSectionType(const char *line){
    if (strncmp(line, ".code", 5) == 0){
        return CODE;
    }
    if (strncmp(line, ".data", 5) == 0){
        return DATA;
    }
    return NONE;
}

void labelDef(const char *line, uint64_t currAddr){

}

void handleMacro(const char *line, FILE *interm){

}

bool isMacro(const char *mnem){

}

int expandMacro(const char *line, FILE *interm){

}

void initTable(void){

}

void addToTable(const char *name, uint64_t addr){

}

void lookupInTable(const char *name, uint64_t *addr){

}

uint64_t getInstrAddr(uint64_t currAddr){

}

uint64_t getDataAddr(uint64_t currAddr){

}

void pass2(FILE *interm, FILE *out){

}

uint32_t encodeInstr(const Instruction *instr){

}

uint64_t encodeData(uint64_t val){

}

void trim(char *line){
    int read = 0;
    int write = 0;

    if(line[0]==";"){
        while(line[read]!='\0'){
            line[write]=='\0';
            read++;
            write++;
        }
    }

    while (line[read] != '\0') {
        // copy non-whitespace characters
        if (line[read] != ' ' &&
            line[read] != '\t' &&
            line[read] != '\n') 
        {
            line[write] = line[read];
            write++;
        }
        read++;
    }

    line[write] = '\0';
}

int tokenize(const char *line, uint64_t *out){

}

int parseReg(const char *tok){

}

int parseLiteral(const char *tok, uint64_t *valOut){

}

int parseLabel(const char *tok, uint64_t *addrOut){

}

void assemblerError(const char *msg){

}

void syntaxError(const char *line){

}

void undefLabel(const char *label){

}

bool isBlank(const char *line){

}

bool isTabbed(const char *line){

}

void freeAllMemory(){

}