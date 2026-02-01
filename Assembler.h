#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define maxLine 256
#define maxLabel 64
#define maxSymb 1024
#define maxTok 8

#define codeStart 0x1000
#define instrSize 4
#define dataSize 8

typedef enum SectionType {
    NONE,
    CODE,
    DATA
} SectionType;

typedef enum LineType{
    EMPTY,
    COMMENT,
    DIRECTIVE,
    LABEL,
    INSTRUCTION,
    DATA
} LineType;

//symbol table entry
typedef struct Symbol{
    char name[maxLabel];
    uint64_t addr;
} Symbol;

typedef struct Instruction{
    char mnem[16];
    char op[3][32];
    int op_count;
    uint64_t addr;
} Instruction;

typedef struct Data {
    uint64_t val;
    uint64_t addr;
} Data;

int testmain(int argc, char **argv);

int assembleFile(const char *file);

void pass1(FILE *f, FILE *interm);

LineType getLineType(const char *line);
SectionType getSectionType(const char *line);

void labelDef(const char *line, uint64_t currAddr);
void handleMacro(const char *line, FILE *interm);

bool isMacro(const char *mnem);
int expandMacro(const char *line, FILE *interm);

void initTable(void);
void addToTable(const char *name, uint64_t addr);
void lookupInTable(const char *name, uint64_t *addr);

uint64_t getInstrAddr(uint64_t currAddr);
uint64_t getDataAddr(uint64_t currAddr);

void pass2(FILE *interm, FILE *out);

uint32_t encodeInstr(const Instruction *instr);
uint64_t encodeData(uint64_t val);

void trim(char *line);
int tokenize(const char *line, uint64_t *out);

int parseReg(const char *tok);
int parseLiteral(const char *tok, uint64_t *valOut);
int parseLabel(const char *tok, uint64_t *addrOut);

void assemblerError(const char *msg);
void syntaxError(const char *line);
void undefLabel(const char *label);

bool isBlank(const char *line);
bool isTabbed(const char *line);

#endif