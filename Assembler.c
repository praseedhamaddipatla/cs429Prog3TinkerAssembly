#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#define MAX_LINE 256
#define MAX_TOKENS 4
#define MAX_LABELS 512
#define MAX_INSTS 2048
#define START_ADDR 0x1000

// global arrays for labels and instructions
typedef struct
{
    char name[50];
    int addr;
} Label;

typedef struct
{
    char op[12];
    char args[4][20];
    int numArgs;
    int addr;
    int isCode; // 1 for code, 0 for data
    uint64_t dataValue;
} Instruction;

Label labels[MAX_LABELS];
int numLabels = 0;

Instruction instructions[MAX_INSTS];
int numInstructions = 0;

char *intermediateFile;
char *binaryFile;

// utility functions

void cleanupAndExit()
{
    if (intermediateFile)
        remove(intermediateFile);

    if (binaryFile)
        remove(binaryFile);

    exit(1);
}

void cleanLine(char *s)
{
    // remove newline
    s[strcspn(s, "\n")] = 0;
}

void addLabel(const char *name, int addr)
{
    strcpy(labels[numLabels].name, name);
    labels[numLabels].addr = addr;
    numLabels++;
}

int findLabel(const char *name)
{
    for (int i = 0; i < numLabels; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
            return labels[i].addr;
    }
    return -1;
}

void addInstruction(Instruction inst)
{
    if (numInstructions >= MAX_INSTS)
    {
        fprintf(stderr, "too many instructions\n");
        cleanupAndExit();
    }
    instructions[numInstructions++] = inst;
}

int getRegisterNumber(const char *s)
{
    if (s[0] != 'r')
        return -1;
    return atoi(s + 1);
}

void verifyArgs(Instruction inst, int expected)
{
    if (inst.numArgs != expected)
    {
        fprintf(stderr, "wrong number of args for %s\n", inst.op);
    }
}

// first pass - just collect label addresses

int calculateInstructionSize(const char *opcode)
{
    // calculate how much space this instruction will take after macro expansion
    if (strcmp(opcode, "ld") == 0)
        return 48;
    if (strcmp(opcode, "push") == 0)
        return 8;
    if (strcmp(opcode, "pop") == 0)
        return 8;
    return 4;
}

void firstPass(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "cant open file\n");
        cleanupAndExit();
    }

    char buffer[MAX_LINE];
    int address = START_ADDR;
    int mode = -1; // -1 = unknown, 0 = data, 1 = code

    while (fgets(buffer, sizeof(buffer), f))
    {
        cleanLine(buffer);

        if (buffer[0] == ';' || strlen(buffer) == 0)
        {
            continue; // skip comments and empty lines
        }

        if (buffer[0] == ':')
        {
            const char *name = buffer + 1;

            // empty label
            if (*name == '\0')
            {
                fprintf(stderr, "error: invalid label\n");
                cleanupAndExit();
            }

            // first character
            if (!isalpha((unsigned char)*name) && *name != '_')
            {
                fprintf(stderr, "error: invalid label\n");
                cleanupAndExit();
            }

            // validate entire label
            for (const char *p = name; *p; p++)
            {
                if (!isalnum((unsigned char)*p) && *p != '_')
                {
                    fprintf(stderr, "error: invalid label\n");
                    cleanupAndExit();
                }
            }

            addLabel(name, address);
            continue;
        }

        if (buffer[0] == '.')
        {
            // section directive
            if (buffer[1] == 'c')
                mode = 1;
            else if (buffer[1] == 'd')
                mode = 0;
            continue;
        }

        if (buffer[0] == '\t')
        {
            // actual instruction or data
            char *line = buffer + 1;

            if (mode == 1)
            {
                // code section - parse opcode to determine size
                char temp[MAX_LINE];
                strcpy(temp, line);
                char *opcode = strtok(temp, " ,");
                if (opcode)
                {
                    address += calculateInstructionSize(opcode);
                }
            }
            else
            {
                // data section
                address += 8;
            }
        }
    }

    fclose(f);
}

// macro expansion helpers

void expandLoadInstruction(Instruction current)
{
    // ld rd, L -> series of xor, addi, shftli instructions to load 64-bit literal
    uint64_t value = strtoull(current.args[1], NULL, 0);
    Instruction temp;
    memset(&temp, 0, sizeof(Instruction));
    temp.isCode = 1;

    // xor rd, rd, rd (clear register)
    strcpy(temp.op, "xor");
    strcpy(temp.args[0], current.args[0]);
    strcpy(temp.args[1], current.args[0]);
    strcpy(temp.args[2], current.args[0]);
    temp.numArgs = 3;
    temp.addr = current.addr;
    addInstruction(temp);
    int address = current.addr + 4;

    // load literal in 12-bit chunks from high to low
    int shifts[] = {52, 40, 28, 16};
    for (int i = 0; i < 4; i++)
    {
        strcpy(temp.op, "addi");
        snprintf(temp.args[1], sizeof(temp.args[1]), "%llu",
                 (unsigned long long)((value >> shifts[i]) & 0xFFF));
        temp.args[2][0] = '\0';
        temp.numArgs = 2;
        temp.addr = address;
        addInstruction(temp);
        address += 4;

        strcpy(temp.op, "shftli");
        strcpy(temp.args[1], "12");
        temp.addr = address;
        addInstruction(temp);
        address += 4;
    }

    // last chunk (4 bits)
    strcpy(temp.op, "addi");
    snprintf(temp.args[1], sizeof(temp.args[1]), "%llu",
             (unsigned long long)((value >> 4) & 0xFFF));
    temp.args[2][0] = '\0';
    temp.numArgs = 2;
    temp.addr = address;
    addInstruction(temp);
    address += 4;

    strcpy(temp.op, "shftli");
    strcpy(temp.args[1], "4");
    temp.addr = address;
    addInstruction(temp);
    address += 4;

    strcpy(temp.op, "addi");
    snprintf(temp.args[1], sizeof(temp.args[1]), "%llu",
             (unsigned long long)(value & 0xF));
    temp.args[2][0] = '\0';
    temp.addr = address;
    addInstruction(temp);
}

int tryExpandMacro(Instruction current)
{
    // returns new address after expansion
    Instruction temp;
    memset(&temp, 0, sizeof(Instruction));
    temp.isCode = 1;

    if (strcmp(current.op, "in") == 0)
    {
        verifyArgs(current, 2);
        strcpy(current.op, "priv");
        strcpy(current.args[2], "r0");
        strcpy(current.args[3], "3");
        current.numArgs = 4;
        addInstruction(current);
        return current.addr + 4;
    }

    if (strcmp(current.op, "clr") == 0)
    {
        verifyArgs(current, 1);
        strcpy(current.op, "xor");
        strcpy(current.args[1], current.args[0]);
        strcpy(current.args[2], current.args[0]);
        current.numArgs = 3;
        addInstruction(current);
        return current.addr + 4;
    }

    if (strcmp(current.op, "out") == 0)
    {
        verifyArgs(current, 2);
        strcpy(current.op, "priv");
        strcpy(current.args[2], "r0");
        strcpy(current.args[3], "4");
        current.numArgs = 4;
        addInstruction(current);
        return current.addr + 4;
    }

    if (strcmp(current.op, "ld") == 0)
    {
        verifyArgs(current, 2);
        expandLoadInstruction(current);
        return current.addr + 48;
    }

    if (strcmp(current.op, "push") == 0)
    {
        verifyArgs(current, 1);
        // mov (r31)(-8), rx
        strcpy(temp.op, "mov");
        strcpy(temp.args[0], "(r31)(-8)");
        strcpy(temp.args[1], current.args[0]);
        temp.numArgs = 2;
        temp.addr = current.addr;
        addInstruction(temp);

        // subi r31, 8
        strcpy(temp.op, "subi");
        strcpy(temp.args[0], "r31");
        strcpy(temp.args[1], "8");
        temp.numArgs = 2;
        temp.addr = current.addr + 4;
        addInstruction(temp);
        return current.addr + 8;
    }

    if (strcmp(current.op, "pop") == 0)
    {
        verifyArgs(current, 1);
        // mov rx, (r31)(0)
        strcpy(temp.op, "mov");
        strcpy(temp.args[0], current.args[0]);
        strcpy(temp.args[1], "(r31)(0)");
        temp.numArgs = 2;
        temp.addr = current.addr;
        addInstruction(temp);

        // addi r31, 8
        strcpy(temp.op, "addi");
        strcpy(temp.args[0], "r31");
        strcpy(temp.args[1], "8");
        temp.numArgs = 2;
        temp.addr = current.addr + 4;
        addInstruction(temp);
        return current.addr + 8;
    }

    if (strcmp(current.op, "halt") == 0)
    {
        strcpy(temp.op, "priv");
        strcpy(temp.args[0], "r0");
        strcpy(temp.args[1], "r0");
        strcpy(temp.args[2], "r0");
        strcpy(temp.args[3], "0");
        temp.numArgs = 4;
        temp.addr = current.addr;
        addInstruction(temp);
        return current.addr + 4;
    }

    // not a macro, just add it
    addInstruction(current);
    return current.addr + 4;
}

// second pass - parse and expand macros

int parseCodeLine(char *line, int address)
{
    // parse instruction and expand macros
    const char *delimiter = " ,";
    char *token = strtok(line, delimiter);

    if (!token)
    {
        fprintf(stderr, "empty instruction\n");
        cleanupAndExit();
    }

    Instruction current;
    memset(&current, 0, sizeof(Instruction));
    strcpy(current.op, token);
    current.addr = address;
    current.isCode = 1;

    // parse arguments
    int numArgs = 0;
    token = strtok(NULL, delimiter);
    while (token && numArgs < 4)
    {
        // check for register validity
        if (token[0] == 'r' && isdigit((unsigned char)token[1]))
        {
            int registerNum = atoi(token + 1);
            if (registerNum < 0 || registerNum > 31)
            {
                fprintf(stderr, "invalid register number\n");
                cleanupAndExit();
            }
        }

        // check for label reference
        if (token[0] == ':')
        {
            int labelAddress = findLabel(token);
            if (labelAddress != -1)
            {
                sprintf(current.args[numArgs], "%d", labelAddress);
            }
        }
        else
        {
            strcpy(current.args[numArgs], token);
        }

        numArgs++;
        token = strtok(NULL, delimiter);
    }

    current.numArgs = numArgs;
    return tryExpandMacro(current);
}

int parseDataLine(char *line, int address)
{
    // parse data value
    if (line[0] == '-')
    {
        fprintf(stderr, "data must be unsigned\n");
        cleanupAndExit();
    }

    Instruction current;
    memset(&current, 0, sizeof(Instruction));
    current.addr = address;
    current.isCode = 0;
    current.dataValue = strtoull(line, NULL, 0);

    addInstruction(current);
    return address + 8;
}

void secondPass(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "cant open file\n");
        cleanupAndExit();
    }

    char buffer[MAX_LINE];
    int address = START_ADDR;
    int mode = -1; // -1 = unknown, 0 = data, 1 = code

    while (fgets(buffer, sizeof(buffer), f))
    {
        cleanLine(buffer);

        if (buffer[0] == ';' || strlen(buffer) == 0)
            continue;

        if (buffer[0] == ':')
            continue; // already processed labels

        if (buffer[0] == '.')
        {
            // section marker
            Instruction marker;
            memset(&marker, 0, sizeof(Instruction));

            if (strcmp(buffer, ".code") == 0)
            {
                mode = 1;
                marker.isCode = 1;
                strcpy(marker.op, ".code");
            }
            else if (strcmp(buffer, ".data") == 0)
            {
                mode = 0;
                marker.isCode = 0;
                strcpy(marker.op, ".data");
            }
            addInstruction(marker);
            continue;
        }

        if (buffer[0] == '\t')
        {
            char *line = buffer + 1;

            if (mode == 1)
            {
                address = parseCodeLine(line, address);
            }
            else
            {
                address = parseDataLine(line, address);
            }
        }
    }

    fclose(f);
}

// output intermediate file

void writeIntermediate(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f)
    {
        fprintf(stderr, "cant write intermediate\n");
        return;
    }

    int previousType = -1;

    for (int i = 0; i < numInstructions; i++)
    {
        Instruction current = instructions[i];

        // print section changes
        if (current.op[0] == '.')
        {
            if (current.isCode != previousType)
            {
                fprintf(f, "%s\n", current.isCode ? ".code" : ".data");
                previousType = current.isCode;
            }
            continue;
        }

        fprintf(f, "\t");

        if (current.isCode)
        {
            // code instruction
            fprintf(f, "%s ", current.op);

            if (strcmp(current.op, "return") == 0)
            {
                fprintf(f, "\n");
                continue;
            }

            for (int j = 0; j < current.numArgs - 1; j++)
            {
                fprintf(f, "%s, ", current.args[j]);
            }
            fprintf(f, "%s\n", current.args[current.numArgs - 1]);
        }
        else
        {
            // data value
            fprintf(f, "%llu\n", (unsigned long long)current.dataValue);
        }
    }

    fclose(f);
}

// binary output

void writeBinaryInstruction(FILE *f, int opcode, char *rd, char *rs, char *rt, char *immediate)
{
    unsigned int binaryInstruction = 0;

    binaryInstruction |= (opcode & 0x1F) << 27;

    if (rd)
    {
        int registerNum = atoi(rd + 1);
        binaryInstruction |= (registerNum & 0x1F) << 22;
    }

    if (rs)
    {
        int registerNum = atoi(rs + 1);
        binaryInstruction |= (registerNum & 0x1F) << 17;
    }

    if (rt)
    {
        int registerNum = atoi(rt + 1);
        binaryInstruction |= (registerNum & 0x1F) << 12;
    }

    if (immediate)
    {
        int value = atoi(immediate);
        binaryInstruction |= (value & 0xFFF);
    }

    fwrite(&binaryInstruction, 4, 1, f);
}

void encodeInstruction(Instruction inst, FILE *f)
{
    // convert instruction to binary

    // arithmetic
    if (strcmp(inst.op, "add") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x18, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "addi") == 0)
    {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x19, inst.args[0], NULL, NULL, inst.args[1]);
    }
    else if (strcmp(inst.op, "sub") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1a, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "subi") == 0)
    {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x1b, inst.args[0], NULL, NULL, inst.args[1]);
    }
    else if (strcmp(inst.op, "mul") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1c, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "div") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1d, inst.args[0], inst.args[1], inst.args[2], NULL);
    }

    // logic
    else if (strcmp(inst.op, "and") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x0, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "or") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x1, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "xor") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x2, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "not") == 0)
    {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x3, inst.args[0], inst.args[1], NULL, NULL);
    }
    else if (strcmp(inst.op, "shftr") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x4, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "shftri") == 0)
    {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x5, inst.args[0], NULL, NULL, inst.args[1]);
    }
    else if (strcmp(inst.op, "shftl") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x6, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "shftli") == 0)
    {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0x7, inst.args[0], NULL, NULL, inst.args[1]);
    }

    // control flow
    else if (strcmp(inst.op, "br") == 0)
    {
        verifyArgs(inst, 1);
        writeBinaryInstruction(f, 0x8, inst.args[0], NULL, NULL, NULL);
    }
    else if (strcmp(inst.op, "brr") == 0)
    {
        verifyArgs(inst, 1);
        if (inst.args[0][0] == 'r')
        {
            writeBinaryInstruction(f, 0x9, inst.args[0], NULL, NULL, NULL);
        }
        else
        {
            writeBinaryInstruction(f, 0xa, NULL, NULL, NULL, inst.args[0]);
        }
    }
    else if (strcmp(inst.op, "brnz") == 0)
    {
        verifyArgs(inst, 2);
        writeBinaryInstruction(f, 0xb, inst.args[0], inst.args[1], NULL, NULL);
    }
    else if (strcmp(inst.op, "call") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0xc, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "return") == 0)
    {
        verifyArgs(inst, 0);
        writeBinaryInstruction(f, 0xd, NULL, NULL, NULL, NULL);
    }
    else if (strcmp(inst.op, "brgt") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0xe, inst.args[0], inst.args[1], inst.args[2], NULL);
    }

    // privileged
    else if (strcmp(inst.op, "priv") == 0)
    {
        verifyArgs(inst, 4);
        writeBinaryInstruction(f, 0xf, inst.args[0], inst.args[1], inst.args[2], inst.args[3]);
    }

    // floating point
    else if (strcmp(inst.op, "addf") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x14, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "subf") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x15, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "mulf") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x16, inst.args[0], inst.args[1], inst.args[2], NULL);
    }
    else if (strcmp(inst.op, "divf") == 0)
    {
        verifyArgs(inst, 3);
        writeBinaryInstruction(f, 0x17, inst.args[0], inst.args[1], inst.args[2], NULL);
    }

    // mov instruction
    else if (strcmp(inst.op, "mov") == 0)
    {
        verifyArgs(inst, 2);

        if (inst.args[0][0] == '(')
        {
            // mov (rd)(L), rs - store to memory
            char register1[10], offset[10];
            sscanf(inst.args[0], "(%[^)])(%[^)])", register1, offset);
            writeBinaryInstruction(f, 0x13, register1, inst.args[1], NULL, offset);
        }
        else if (inst.args[1][0] == '(')
        {
            // mov rd, (rs)(L) - load from memory
            char register2[10], offset[10];
            sscanf(inst.args[1], "(%[^)])(%[^)])", register2, offset);
            writeBinaryInstruction(f, 0x10, inst.args[0], register2, NULL, offset);
        }
        else
        {
            // mov rd, rs or mov rd, imm
            if (inst.args[1][0] == 'r')
            {
                writeBinaryInstruction(f, 0x11, inst.args[0], inst.args[1], NULL, NULL);
            }
            else
            {
                writeBinaryInstruction(f, 0x12, inst.args[0], NULL, NULL, inst.args[1]);
            }
        }
    }
}

void writeBinary(const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
    {
        fprintf(stderr, "cant write binary\n");
        cleanupAndExit();
    }

    for (int i = 0; i < numInstructions; i++)
    {
        if (instructions[i].op[0] == '.')
        {
            continue; // skip section markers
        }
        else if (instructions[i].isCode == 0)
        {
            // write data value (8 bytes)
            fwrite(&instructions[i].dataValue, 8, 1, f);
        }
        else
        {
            // encode and write instruction (4 bytes)
            encodeInstruction(instructions[i], f);
        }
    }

    fclose(f);
}

// validation functions

int hasError = 0;

int isNegative(const char *str)
{
    // check if string represents a negative number
    return str[0] == '-';
}

int validateRegister(const char *reg)
{
    // validate register format and range
    if (reg[0] != 'r')
    {
        fprintf(stderr, "error: invalid register '%s'\n", reg);
        hasError = 1;
        return -1;
    }

    if (!isdigit((unsigned char)reg[1]))
    {
        fprintf(stderr, "error: invalid register '%s'\n", reg);
        hasError = 1;
        return -1;
    }

    int regNum = atoi(reg + 1);
    if (regNum < 0 || regNum > 31)
    {
        fprintf(stderr, "error: register number must be 0-31, got %d\n", regNum);
        hasError = 1;
        return -1;
    }

    return regNum;
}

uint64_t parseNumber(const char *str)
{
    // parse number literal, handling labels
    if (str[0] == ':')
    {
        // it's a label reference, validation will happen later
        return 0;
    }

    // check for valid number
    char *end;
    uint64_t value = strtoull(str, &end, 0);

    if (end == str || *end != '\0')
    {
        fprintf(stderr, "error: invalid number '%s'\n", str);
        hasError = 1;
        return 0;
    }

    return value;
}

void validateMacroArgs(const char *macroName, int expected, int actual)
{
    // check macro argument count
    if (actual != expected)
    {
        fprintf(stderr, "error: macro '%s' expects %d args, got %d\n",
                macroName, expected, actual);
        hasError = 1;
    }
}

static int isValidLabelName(const char *s)
{
    if (!isalpha(*s) && *s != '_')
        return 0;
    for (const char *p = s; *p; p++)
    {
        if (!isalnum(*p) && *p != '_')
            return 0;
    }
    return 1;
}

void validateFile(const char *filename)
{
    // validate assembly file before processing
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "error: cannot open file for validation\n");
        hasError = 1;
        return;
    }

    char line[MAX_LINE];

    int currentMode = -1; // -1 = unknown, 0 = data, 1 = code
    int lineNum = 0;
    int hasCode = 0;

    while (fgets(line, sizeof(line), f))
    {
        lineNum++;

        char rawLine[MAX_LINE];
        strcpy(rawLine, line);

        cleanLine(line);

        // skip empty lines and comments
        if (line[0] == '\0' || line[0] == ';')
        {
            continue;
        }

        // check for invalid leading spaces
        if (line[0] == ' ')
        {
            fprintf(stderr, "error line %d: instructions must start with tab\n", lineNum);
            hasError = 1;
            fclose(f);
            return;
        }

        // handle section directives
        if (line[0] == '.')
        {
            if (strcmp(line, ".code") == 0)
            {
                currentMode = 1;
                hasCode = 1;
            }
            else if (strcmp(line, ".data") == 0)
            {
                currentMode = 0;
            }
            else
            {
                fprintf(stderr, "error line %d: invalid directive '%s'\n", lineNum, line);
                hasError = 1;
                fclose(f);
                return;
            }
            continue;
        }

        // handle labels
        if (rawLine[0] == ':')
        {
            int i = 1;

            // no empty label
            if (rawLine[i] == '\n' || rawLine[i] == '\0')
            {
                fprintf(stderr, "error line %d: empty label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            // first char must be letter or underscore
            if (!isalpha(rawLine[i]) && rawLine[i] != '_')
            {
                fprintf(stderr, "error line %d: invalid label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            // consume valid identifier characters
            while (isalnum(rawLine[i]) || rawLine[i] == '_')
            {
                i++;
            }

            // after identifier, ONLY newline or end is allowed
            if (rawLine[i] != '\n' && rawLine[i] != '\0')
            {
                fprintf(stderr, "error line %d: invalid label\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            continue;
        }

        // handle instructions/data
        if (line[0] == '\t')
        {
            char *content = line + 1; // skip tab

            // skip inline labels
            if (content[0] == ':')
            {
                fprintf(stderr, "error line %d: inline labels are not allowed\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }

            if (currentMode == 1)
            {
                // code section - validate instruction
                char buffer[MAX_LINE];
                strcpy(buffer, content);

                // tokenize
                const char *delim = " ,()";
                char *token = strtok(buffer, delim);
                if (!token)
                    continue;

                char tokens[10][MAX_LINE];
                int numTokens = 0;

                strcpy(tokens[numTokens++], token);
                while ((token = strtok(NULL, delim)) != NULL && numTokens < 10)
                {
                    strcpy(tokens[numTokens++], token);
                }

                if (token != NULL)
                {
                    fprintf(stderr, "error line %d: too many tokens\n", lineNum);
                    hasError = 1;
                    fclose(f);
                    return;
                }

                char *opcode = tokens[0];
                int argCount = numTokens - 1;

                // validate macros
                if (strcmp(opcode, "halt") == 0)
                {
                    validateMacroArgs("halt", 0, argCount);
                }
                else if (strcmp(opcode, "in") == 0)
                {
                    validateMacroArgs("in", 2, argCount);
                    if (!hasError && argCount >= 2)
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                }
                else if (strcmp(opcode, "out") == 0)
                {
                    validateMacroArgs("out", 2, argCount);
                    if (!hasError && argCount >= 2)
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                }
                else if (strcmp(opcode, "clr") == 0)
                {
                    validateMacroArgs("clr", 1, argCount);
                    if (!hasError && argCount >= 1)
                    {
                        validateRegister(tokens[1]);
                    }
                }
                else if (strcmp(opcode, "push") == 0)
                {
                    validateMacroArgs("push", 1, argCount);
                    if (!hasError && argCount >= 1)
                    {
                        validateRegister(tokens[1]);
                    }
                }
                else if (strcmp(opcode, "pop") == 0)
                {
                    validateMacroArgs("pop", 1, argCount);
                    if (!hasError && argCount >= 1)
                    {
                        validateRegister(tokens[1]);
                    }
                }
                else if (strcmp(opcode, "ld") == 0)
                {
                    validateMacroArgs("ld", 2, argCount);
                    if (!hasError && argCount >= 2)
                    {
                        validateRegister(tokens[1]);
                        // second arg can be label or literal
                        if (tokens[2][0] != ':')
                        {
                            if (isNegative(tokens[2]))
                            {
                                fprintf(stderr, "error: 'ld' literal cannot be negative\n");
                                hasError = 1;
                            }
                            else
                            {
                                parseNumber(tokens[2]);
                            }
                        }
                    }
                }
                // regular instructions - basic validation
                else if (strcmp(opcode, "add") == 0 || strcmp(opcode, "sub") == 0 ||
                         strcmp(opcode, "mul") == 0 || strcmp(opcode, "div") == 0 ||
                         strcmp(opcode, "and") == 0 || strcmp(opcode, "or") == 0 ||
                         strcmp(opcode, "xor") == 0 || strcmp(opcode, "shftr") == 0 ||
                         strcmp(opcode, "shftl") == 0 || strcmp(opcode, "addf") == 0 ||
                         strcmp(opcode, "subf") == 0 || strcmp(opcode, "mulf") == 0 ||
                         strcmp(opcode, "divf") == 0)
                {
                    // three register instructions
                    if (argCount != 3)
                    {
                        fprintf(stderr, "error: instruction '%s' expects 3 args\n", opcode);
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                    }
                }
                else if (strcmp(opcode, "addi") == 0 || strcmp(opcode, "subi") == 0 ||
                         strcmp(opcode, "shftli") == 0 || strcmp(opcode, "shftri") == 0)
                {
                    // register + immediate instructions
                    if (argCount != 2)
                    {
                        fprintf(stderr, "error: instruction '%s' expects 2 args\n", opcode);
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        // immediate can be literal only (no labels)
                        if (tokens[2][0] != ':')
                        {
                            parseNumber(tokens[2]);
                        }
                    }
                }
                else if (strcmp(opcode, "not") == 0)
                {
                    if (argCount != 2)
                    {
                        fprintf(stderr, "error: instruction 'not' expects 2 args\n");
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                }
                else if (strcmp(opcode, "br") == 0)
                {
                    if (argCount != 1)
                    {
                        fprintf(stderr, "error: instruction 'br' expects 1 arg\n");
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                    }
                }
                else if (strcmp(opcode, "brr") == 0)
                {
                    if (argCount != 1)
                    {
                        fprintf(stderr, "error: instruction 'brr' expects 1 arg\n");
                        hasError = 1;
                    }
                    // can be register or immediate
                }
                else if (strcmp(opcode, "brnz") == 0)
                {
                    if (argCount != 2)
                    {
                        fprintf(stderr, "error: instruction 'brnz' expects 2 args\n");
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                    }
                }
                else if (strcmp(opcode, "call") == 0)
                {
                    if (argCount != 3)
                    {
                        fprintf(stderr, "error: instruction 'call' expects 3 args\n");
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                    }
                }
                else if (strcmp(opcode, "return") == 0)
                {
                    if (argCount != 0)
                    {
                        fprintf(stderr, "error: instruction 'return' expects 0 args\n");
                        hasError = 1;
                    }
                }
                else if (strcmp(opcode, "brgt") == 0)
                {
                    if (argCount != 3)
                    {
                        fprintf(stderr, "error: instruction 'brgt' expects 3 args\n");
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                    }
                }
                else if (strcmp(opcode, "priv") == 0)
                {
                    if (argCount != 4)
                    {
                        fprintf(stderr, "error: instruction 'priv' expects 4 args\n");
                        hasError = 1;
                    }
                    else
                    {
                        validateRegister(tokens[1]);
                        validateRegister(tokens[2]);
                        validateRegister(tokens[3]);
                        // fourth arg is immediate
                        if (tokens[4][0] != ':')
                        {
                            parseNumber(tokens[4]);
                        }
                    }
                }
                else if (strcmp(opcode, "mov") == 0)
                {
                    // mov has variable args due to memory addressing
                    // basic validation - at least 2 args
                    if (argCount < 2)
                    {
                        fprintf(stderr, "error: instruction 'mov' expects at least 2 args\n");
                        hasError = 1;
                    }
                }
                else
                {
                    fprintf(stderr, "error: unknown instruction '%s'\n", opcode);
                    hasError = 1;
                }

                if (hasError)
                {
                    fclose(f);
                    return;
                }
            }
            else if (currentMode == 0)
            {
                // data section - validate unsigned 64-bit integer
                // no negative values
                if (isNegative(content))
                {
                    fprintf(stderr, "error line %d: data values must be unsigned\n", lineNum);
                    hasError = 1;
                    fclose(f);
                    return;
                }

                // strict unsigned 64-bit range check
                errno = 0;
                char *end;
                unsigned long long val = strtoull(content, &end, 0);

                // invalid characters
                while (*end == ' ' || *end == '\t')
                    end++;

                if (*end != '\0')
                {
                    fprintf(stderr, "error line %d: invalid data value '%s'\n", lineNum, content);
                    hasError = 1;
                    fclose(f);
                    return;
                }

                // overflow
                if (errno == ERANGE)
                {
                    fprintf(stderr, "error line %d: data value out of 64-bit range\n", lineNum);
                    hasError = 1;
                    fclose(f);
                    return;
                }
                (void)val;
            }

            else
            {
                fprintf(stderr, "error line %d: instruction without .code or .data section\n", lineNum);
                hasError = 1;
                fclose(f);
                return;
            }
        }
        else
        {
            fprintf(stderr, "error line %d: invalid line format\n", lineNum);
            hasError = 1;
            fclose(f);
            return;
        }
    }
    if (!hasCode)
    {
        fprintf(stderr, "error: no .code in file");
    }
    fclose(f);
}

// main function

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s input.asm intermediate.txt output.bin\n", argv[0]);
        return 1;
    }

    intermediateFile = argv[2];
    binaryFile = argv[3];

    // validate input file first
    validateFile(argv[1]);
    if (hasError)
    {
        return 1;
    }

    // run two-pass assembly
    firstPass(argv[1]);
    secondPass(argv[1]);

    // write outputs
    writeIntermediate(argv[2]);
    writeBinary(argv[3]);

    return 0;
}