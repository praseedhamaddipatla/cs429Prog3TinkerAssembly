#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_TOK 8
#define MAX_TOK_LEN 64
#define MAX_LABELS 512
#define CODE_START 0x1000
#define DATA_START 0x2000

typedef enum InstrType
{
    R,     // rd, rs, rt
    I,     // rd, imm
    BR,    // br types
    MOV,   // special handling for mov
    PRIV,  // rd, rs, rt, imm
    NO_OP, // return, etc
    OTHER  // rd, rs
} InstrType;

typedef struct InstrInfo
{
    char name[16];
    uint32_t opcode;
    InstrType type;
    int numOps;
} InstrInfo;

// instruction table to search for match
InstrInfo instrTable[] = {
    {"and", 0x00, R, 3}, {"or", 0x01, R, 3},
    {"xor", 0x02, R, 3}, {"not", 0x03, OTHER, 2},
    {"shftr", 0x04, R, 3}, {"shftri", 0x05, I, 2},
    {"shftl", 0x06, R, 3}, {"shftli", 0x07, I, 2},
    {"br", 0x08, BR, 1}, {"brr", 0x09, BR, 1},
    {"brnz", 0x0b, OTHER, 2}, {"call", 0x0c, BR, 1},
    {"return", 0x0d, NO_OP, 0}, {"brgt", 0x0e, R, 3},
    {"priv", 0x0f, PRIV, 4}, {"mov", 0x10, MOV, 2},
    {"addf", 0x14, R, 3}, {"subf", 0x15, R, 3},
    {"mulf", 0x16, R, 3}, {"divf", 0x17, R, 3},
    {"add", 0x18, R, 3}, {"addi", 0x19, I, 2},
    {"sub", 0x1a, R, 3}, {"subi", 0x1b, I, 2},
    {"mul", 0x1c, R, 3}, {"div", 0x1d, R, 3}};

int tableSize = sizeof(instrTable) / sizeof(InstrInfo);

typedef enum Section
{
    NONE,
    CODE,
    DATA
} Section;

typedef struct Label
{
    char name[256];
    uint64_t addr;
} Label;

Label labels[MAX_LABELS];
int labelCount = 0;

// add labels, check for duplicates
void addLabel(const char *name, uint64_t addr)
{
    int i;
    for (i = 0; i < labelCount; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
        {
            fprintf(stderr, "Error: duplicate label '%s'\n", name);
            exit(1);
        }
    }
    // prevent overflow of label array
    if (labelCount >= MAX_LABELS)
    {
        fprintf(stderr, "Error: too many labels\n");
        exit(1);
    }
    
    // check label name length
    if (strlen(name) > 255)
    {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        exit(1);
    }
    
    strcpy(labels[labelCount].name, name);
    labels[labelCount].addr = addr;
    labelCount++;
}

uint64_t lookupLabel(const char *name)
{
    int i;
    for (i = 0; i < labelCount; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
        {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Error: undefined label '%s'\n", name);
    exit(1);
}

// remove comments and trailing whitespace
void trim(char *s)
{
    int i;
    for (i = 0; s[i] != '\0'; i++)
    {
        if (s[i] == ';')
        {
            s[i] = '\0';
            break;
        }
    }
    // strip newline
    int len = strlen(s);
    if (len > 0 && s[len - 1] == '\n')
        s[len - 1] = '\0';
}

int tokenize(char *line, char tok[MAX_TOK][MAX_TOK_LEN])
{
    int n = 0;
    // replace punctuation with spaces
    for (int i = 0; line[i] != '\0'; i++)
    {
        if (line[i] == ',' || line[i] == '(' || line[i] == ')')
        {
            line[i] = ' ';
        }
    }

    char *p = strtok(line, " \t");
    while (p != NULL && n < MAX_TOK)
    {
        strcpy(tok[n], p);
        n++;
        p = strtok(NULL, " \t");
    }
    return n;
}

int parseReg(const char *s)
{
    if (s == NULL || s[0] != 'r')
    {
        fprintf(stderr, "Error: invalid register format '%s'\n", s ? s : "NULL");
        exit(1);
    }
    
    // check if there are digits after 'r'
    if (s[1] == '\0' || !isdigit(s[1]))
    {
        fprintf(stderr, "Error: invalid register format '%s'\n", s);
        exit(1);
    }
    
    // skip 'r' and get the number
    int r = atoi(&s[1]);
    if (r < 0 || r > 31)
    {
        fprintf(stderr, "Error: register out of range (r%d)\n", r);
        exit(1);
    }
    return r;
}

// check if a literal is negative
int isNegativeLiteral(const char *s)
{
    if (s[0] == '-')
        return 1;
    return 0;
}

uint64_t parseLiteral(const char *s)
{
    if (s == NULL)
    {
        fprintf(stderr, "Error: NULL literal\n");
        exit(1);
    }
    
    if (s[0] == ':')
    {
        return lookupLabel(&s[1]);
    }
    return strtoull(s, NULL, 0);
}

// expand ld macro into multiple instructions
void expandld(FILE *out, int rd, uint64_t val)
{
    fprintf(out, "\txor r%d, r%d, r%d\n", rd, rd, rd);
    
    // need to load 64 bits in chunks
    // bits 52-63 (12 bits)
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 52) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 40-51 (12 bits)
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 40) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 28-39 (12 bits)
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 28) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 16-27 (12 bits)
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 16) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 4-15 (12 bits)
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 4) & 0xFFF));
    fprintf(out, "\tshftli r%d, 4\n", rd);
    
    // bits 0-3 (4 bits)
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)(val & 0xF));
    // no shift after last addi
}

// validate number of arguments for macros
void checkMacroArgs(const char *name, int expected, int actual)
{
    // subtract 1 for macro name
    int actualArgs = actual - 1;
    
    if (actualArgs != expected)
    {
        fprintf(stderr, "Error: macro '%s' expects %d argument(s), got %d\n", 
                name, expected, actualArgs);
        exit(1);
    }
}

// handle different macro expansions
int handleMacros(FILE *out, char tok[MAX_TOK][MAX_TOK_LEN], int n, uint64_t *addr)
{
    // check if it's a macro
    if (strcmp(tok[0], "halt") == 0)
    {
        checkMacroArgs("halt", 0, n);
        fprintf(out, "\tpriv r0, r0, r0, 0\n");
        *addr += 4;
        return 1;
    }
    else if (strcmp(tok[0], "in") == 0)
    {
        checkMacroArgs("in", 2, n);
        fprintf(out, "\tpriv %s, %s, r0, 3\n", tok[1], tok[2]);
        *addr += 4;
        return 1;
    }
    else if (strcmp(tok[0], "out") == 0)
    {
        checkMacroArgs("out", 2, n);
        fprintf(out, "\tpriv %s, %s, r0, 4\n", tok[1], tok[2]);
        *addr += 4;
        return 1;
    }
    else if (strcmp(tok[0], "clr") == 0)
    {
        checkMacroArgs("clr", 1, n);
        fprintf(out, "\txor %s, %s, %s\n", tok[1], tok[1], tok[1]);
        *addr += 4;
        return 1;
    }
    else if (strcmp(tok[0], "push") == 0)
    {
        checkMacroArgs("push", 1, n);
        fprintf(out, "\tsubi r31, 8\n");
        fprintf(out, "\tmov (r31)(0), %s\n", tok[1]);
        *addr += 8;
        return 1;
    }
    else if (strcmp(tok[0], "pop") == 0)
    {
        checkMacroArgs("pop", 1, n);
        fprintf(out, "\tmov %s, (r31)(0)\n", tok[1]);
        fprintf(out, "\taddi r31, 8\n");
        *addr += 8;
        return 1;
    }
    else if (strcmp(tok[0], "ld") == 0)
    {
        checkMacroArgs("ld", 2, n);
        
        // validate that literal is not negative (unsigned instruction)
        if (tok[2][0] != ':' && isNegativeLiteral(tok[2]))
        {
            fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
            exit(1);
        }
        
        uint64_t val = parseLiteral(tok[2]);
        expandld(out, parseReg(tok[1]), val);
        *addr += (4 * 13);
        return 1;
    }

    return 0; // not a macro
}

// process a line that starts with tab
void processTabLine(FILE *out, char *line, Section sec, uint64_t *addr)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]); // skip the tab

    // check if label
    if (buf[0] == ':')
    {
        addLabel(&buf[1], *addr);
        fprintf(out, "\t%s\n", buf);
        return;
    }

    char original[MAX_LINE];
    strcpy(original, buf);

    char tok[MAX_TOK][MAX_TOK_LEN];
    int n = tokenize(buf, tok);
    if (n == 0)
        return;

    if (sec == CODE)
    {
        // try to handle as macro first
        if (!handleMacros(out, tok, n, addr))
        {
            // regular instruction
            fprintf(out, "\t%s\n", original);
            *addr += 4;
        }
    }
    else if (sec == DATA)
    {
        fprintf(out, "\t%s\n", original);
        *addr += 8; // data is 8 bytes
    }
}

void pass1(FILE *in, FILE *out)
{
    char line[MAX_LINE];
    Section sec = NONE;
    uint64_t codeAddr = CODE_START;
    uint64_t dataAddr = DATA_START;
    uint64_t *currentAddr = &codeAddr;

    while (fgets(line, sizeof(line), in))
    {
        trim(line);
        if (line[0] == '\0' || line[0] == '\n')
            continue;

        // check for directives
        if (strcmp(line, ".code") == 0)
        {
            sec = CODE;
            currentAddr = &codeAddr;
            fprintf(out, ".code\n");
            continue;
        }
        if (strcmp(line, ".data") == 0)
        {
            sec = DATA;
            currentAddr = &dataAddr;
            fprintf(out, ".data\n");
            continue;
        }

        // label definition without tab
        if (line[0] == ':')
        {
            addLabel(&line[1], *currentAddr);
            fprintf(out, "%s\n", line);
            continue;
        }

        // instruction or data with tab
        if (line[0] == '\t')
        {
            processTabLine(out, line, sec, currentAddr);
        }
    }
}

// validate operand count for regular instructions
void checkInstrArgs(const char *name, int expected, int actual, InstrType type)
{
    // substract instr name count
    int actualOps = actual - 1;
    
    // mov has variable operand count
    if (type == MOV)
    {
        if (actualOps < 2 || actualOps > 3)
        {
            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", actualOps);
            exit(1);
        }
        return;
    }
    
    // for other instructions, check exact count
    if (actualOps != expected)
    {
        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n", 
                name, expected, actualOps);
        exit(1);
    }
}

// encode R instruction
void encodeR(char tok[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt)
{
    *rd = parseReg(tok[1]);
    *rs = parseReg(tok[2]);
    *rt = parseReg(tok[3]);
}

// encode I instruction
void encodeI(char tok[MAX_TOK][MAX_TOK_LEN], const char *instrName, uint32_t *rd, uint32_t *imm)
{
    *rd = parseReg(tok[1]);
    
    // check if instruction requires unsigned literal
    if (strcmp(instrName, "addi") == 0 || strcmp(instrName, "subi") == 0 ||
        strcmp(instrName, "shftli") == 0 || strcmp(instrName, "shftri") == 0)
    {
        if (tok[2][0] != ':' && isNegativeLiteral(tok[2]))
        {
            fprintf(stderr, "Error: unsigned instruction '%s' cannot have negative literal\n", instrName);
            exit(1);
        }
    }
    
    *imm = (uint32_t)parseLiteral(tok[2]);
}

// encode branch instruction
void encodeBranch(char tok[MAX_TOK][MAX_TOK_LEN], uint32_t *op, uint32_t *rd, uint32_t *imm)
{
    // changes opcodes based on input
    if (strcmp(tok[0], "brr") == 0 && tok[1][0] != 'r')
    {
        *op = 0x0a;
        // brr with literal can be signed
        *imm = (uint32_t)parseLiteral(tok[1]);
    }
    else
    {
        *rd = parseReg(tok[1]);
    }
}

// encode privileged instruction
void encodePriv(char tok[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt, uint32_t *imm)
{
    *rd = parseReg(tok[1]);
    *rs = parseReg(tok[2]);
    *rt = parseReg(tok[3]);
    *imm = (uint32_t)parseLiteral(tok[4]);
}

// encode mov instruction with different formats
void encodeMov(char tok[MAX_TOK][MAX_TOK_LEN], int n, uint32_t *op, uint32_t *rd, uint32_t *rs, uint32_t *imm)
{
    // format: mov rd, rs (opcode 0x11)
    if (n == 3 && tok[1][0] == 'r' && tok[2][0] == 'r')
    {
        *op = 0x11;
        *rd = parseReg(tok[1]);
        *rs = parseReg(tok[2]);
    }
    // format: mov rd, L (opcode 0x12) - unsigned literal
    else if (n == 3 && tok[1][0] == 'r' && tok[2][0] != 'r')
    {
        *op = 0x12;
        *rd = parseReg(tok[1]);
        
        // unsigned literal check
        if (tok[2][0] != ':' && isNegativeLiteral(tok[2]))
        {
            fprintf(stderr, "Error: 'mov rd, L' cannot have negative literal\n");
            exit(1);
        }
        
        *imm = (uint32_t)parseLiteral(tok[2]);
    }
    // format: mov rd, (rs)(L) (opcode 0x10) - signed literal
    else if (n == 4 && tok[1][0] == 'r' && tok[2][0] == 'r')
    {
        *op = 0x10;
        *rd = parseReg(tok[1]);
        *rs = parseReg(tok[2]);
        *imm = (uint32_t)parseLiteral(tok[3]);
    }
    // format: mov (rd)(L), rs (opcode 0x13) - signed literal
    else if (n == 4 && tok[1][0] == 'r' && tok[2][0] != 'r')
    {
        *op = 0x13;
        *rd = parseReg(tok[1]);
        *imm = (uint32_t)parseLiteral(tok[2]);
        *rs = parseReg(tok[3]);
    }
    else
    {
        fprintf(stderr, "Error: invalid mov format\n");
        exit(1);
    }
}

// find instruction in table and get type
InstrType findInstrType(char *name, uint32_t *opcode, int *expectedOps)
{
    for (int i = 0; i < tableSize; i++)
    {
        if (strcmp(name, instrTable[i].name) == 0)
        {
            *opcode = instrTable[i].opcode;
            *expectedOps = instrTable[i].numOps;
            return instrTable[i].type;
        }
    }
    fprintf(stderr, "Error: invalid instruction '%s'\n", name);
    exit(1);
}

// combine fields into 32-bit instruction
uint32_t buildInstr(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm)
{
    uint32_t result = 0;
    result = result | (op << 26);
    result = result | ((rd & 0x1F) << 21);
    result = result | ((rs & 0x1F) << 16);
    result = result | ((rt & 0x1F) << 11);
    result = result | (imm & 0x7FF);
    return result;
}

uint32_t encode(char tok[MAX_TOK][MAX_TOK_LEN], int n)
{
    uint32_t op = 0, rd = 0, rs = 0, rt = 0, imm = 0;
    int expectedOps = 0;

    // find instruction type
    InstrType type = findInstrType(tok[0], &op, &expectedOps);
    
    // validate argument count
    checkInstrArgs(tok[0], expectedOps, n, type);

    // encode based on type
    switch (type)
    {
    case R:
        encodeR(tok, &rd, &rs, &rt);
        break;
    case I:
        encodeI(tok, tok[0], &rd, &imm);
        break;
    case OTHER:
        rd = parseReg(tok[1]);
        rs = parseReg(tok[2]);
        break;
    case BR:
        encodeBranch(tok, &op, &rd, &imm);
        break;
    case PRIV:
        encodePriv(tok, &rd, &rs, &rt, &imm);
        break;
    case MOV:
        encodeMov(tok, n, &op, &rd, &rs, &imm);
        break;
    case NO_OP:
        // return instruction, nothing to parse
        break;
    default:
        fprintf(stderr, "Error: invalid instruction type\n");
        exit(1);
    }

    return buildInstr(op, rd, rs, rt, imm);
}

// process code section line in pass2
void processCode(FILE *out, char *line)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);
    char tok[MAX_TOK][MAX_TOK_LEN];
    int n = tokenize(buf, tok);
    
    if (n == 0)
    {
        fprintf(stderr, "Error: empty instruction line\n");
        exit(1);
    }
    
    uint32_t instr = encode(tok, n);
    fwrite(&instr, 4, 1, out);
}

// process data section line in pass2
void processData(FILE *out, char *line)
{
    // validate data is not negative (unsigned)
    if (isNegativeLiteral(&line[1]))
    {
        fprintf(stderr, "Error: data values must be unsigned\n");
        exit(1);
    }
    
    uint64_t val = parseLiteral(&line[1]);
    fwrite(&val, 8, 1, out);
}

void pass2(FILE *in, FILE *out)
{
    char line[MAX_LINE];
    Section sec = NONE;

    while (fgets(line, sizeof(line), in))
    {
        trim(line);
        if (line[0] == '\0')
            continue;

        if (strcmp(line, ".code") == 0)
        {
            sec = CODE;
            continue;
        }
        if (strcmp(line, ".data") == 0)
        {
            sec = DATA;
            continue;
        }

        // skip labels in pass2
        if (line[0] == ':')
            continue;

        if (line[0] == '\t')
        {
            // skip labels with tabs
            if (line[1] == ':')
                continue;
            
            if (sec == NONE)
            {
                fprintf(stderr, "Error: instr/data outside of .code/.data section\n");
                exit(1);
            }
                
            if (sec == CODE)
            {
                processCode(out, line);
            }
            else if (sec == DATA)
            {
                processData(out, line);
            }
        }
    }
}

int testmain(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <input.tk> <intermediate.tk> <output.tko>\n", 
                argc > 0 ? argv[0] : "assembler");
        return 1;
    }

    FILE *f_in = fopen(argv[1], "r");
    FILE *f_mid = fopen(argv[2], "w+");
    FILE *f_out = fopen(argv[3], "wb");

    if (!f_in)
    {
        fprintf(stderr, "Error: cannot open input file '%s'\n", argv[1]);
        return 1;
    }
    if (!f_mid)
    {
        fprintf(stderr, "Error: cannot open intermediate file '%s'\n", argv[2]);
        if (f_in) fclose(f_in);
        return 1;
    }
    if (!f_out)
    {
        fprintf(stderr, "Error: cannot open output file '%s'\n", argv[3]);
        if (f_in) fclose(f_in);
        if (f_mid) fclose(f_mid);
        return 1;
    }

    pass1(f_in, f_mid);
    // back to the start for the second pass
    fseek(f_mid, 0, SEEK_SET);
    pass2(f_mid, f_out);

    fclose(f_in);
    fclose(f_mid);
    fclose(f_out);
    return 0;
}

/*int main(int argc, char **argv){
    return testmain(argc, argv);
}*/