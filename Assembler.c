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

Label lbls[MAX_LABELS];
int numLbls = 0;

// add labels, check for duplicates
void addLabelToArray(const char *lbl, uint64_t addr)
{
    // check if label already exists
    int i;
    for (i = 0; i < numLbls; i++)
    {
        if (strcmp(lbls[i].name, lbl) == 0)
        {
            fprintf(stderr, "Error: duplicate label '%s'\n", lbl);
            exit(1);
        }
    }
    
    // prevent overflow of label array
    if (numLbls >= MAX_LABELS)
    {
        fprintf(stderr, "Error: too many labels\n");
        exit(1);
    }
    
    // check label name length
    if (strlen(lbl) > 255)
    {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        exit(1);
    }
    
    strcpy(lbls[numLbls].name, lbl);
    lbls[numLbls].addr = addr;
    numLbls = numLbls + 1;
}

uint64_t findLabelAddress(const char *lbl)
{
    // search through all labels
    int i;
    for (i = 0; i < numLbls; i++)
    {
        if (strcmp(lbls[i].name, lbl) == 0)
        {
            return lbls[i].addr;
        }
    }
    
    fprintf(stderr, "Error: undefined label '%s'\n", lbl);
    exit(1);
}

// remove comments and trailing whitespace
void cleanLine(char *line)
{
    // find comment and remove everything after
    int pos = 0;
    while (line[pos] != '\0')
    {
        if (line[pos] == ';')
        {
            line[pos] = '\0';
            break;
        }
        pos = pos + 1;
    }
    
    // strip newline
    int len = strlen(line);
    
    if (len > 0)
    {
        if (line[len - 1] == '\n')
        {
            line[len - 1] = '\0';
        }
    }
}

int splitIntoTokens(char *line, char toks[MAX_TOK][MAX_TOK_LEN])
{
    int cnt = 0;
    
    // replace punctuation with spaces
    int i = 0;
    while (line[i] != '\0')
    {
        char ch = line[i];
        if (ch == ',' || ch == '(' || ch == ')')
        {
            line[i] = ' ';
        }
        i = i + 1;
    }

    // tokenize by spaces
    char *ptr = strtok(line, " \t");
    while (ptr != NULL)
    {
        if (cnt >= MAX_TOK)
        {
            break;
        }
        
        strcpy(toks[cnt], ptr);
        cnt++;
        ptr = strtok(NULL, " \t");
    }
    
    return cnt;
}

int getRegisterNumber(const char *reg)
{
    if (reg == NULL)
    {
        fprintf(stderr, "Error: invalid register format '%s'\n", "NULL");
        exit(1);
    }
    
    // check first character is 'r'
    if (reg[0] != 'r')
    {
        fprintf(stderr, "Error: invalid register format '%s'\n", reg);
        exit(1);
    }
    
    // check if there are digits after 'r'
    if (reg[1] == '\0')
    {
        fprintf(stderr, "Error: invalid register format '%s'\n", reg);
        exit(1);
    }
    
    // check if second character is digit
    char ch = reg[1];
    if (ch < '0' || ch > '9')
    {
        fprintf(stderr, "Error: invalid register format '%s'\n", reg);
        exit(1);
    }
    
    // get the number after 'r'
    int num = atoi(&reg[1]);
    
    if (num < 0 || num > 31)
    {
        fprintf(stderr, "Error: register out of range (r%d)\n", num);
        exit(1);
    }
    
    return num;
}

// check if a literal is negative
int checkIfNegative(const char *lit)
{
    if (lit[0] == '-')
    {
        return 1;
    }
    return 0;
}

uint64_t convertToNumber(const char *lit)
{
    if (lit == NULL)
    {
        fprintf(stderr, "Error: NULL literal\n");
        exit(1);
    }
    
    // check if it's a label reference
    if (lit[0] == ':')
    {
        return findLabelAddress(&lit[1]);
    }
    
    // convert string to number
    uint64_t res = strtoull(lit, NULL, 0);
    return res;
}

// expand ld macro into multiple instructions
void writeLdMacro(FILE *out, int rd, uint64_t val)
{
    // clear register first
    fprintf(out, "\txor r%d, r%d, r%d\n", rd, rd, rd);
    
    // load 64 bits in chunks
    uint64_t tmp = val;
    
    // bits 52-63
    uint64_t c1 = (tmp >> 52) & 0xFFF;
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)c1);
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 40-51
    uint64_t c2 = (tmp >> 40) & 0xFFF;
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)c2);
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 28-39
    uint64_t c3 = (tmp >> 28) & 0xFFF;
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)c3);
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 16-27
    uint64_t c4 = (tmp >> 16) & 0xFFF;
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)c4);
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    // bits 4-15
    uint64_t c5 = (tmp >> 4) & 0xFFF;
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)c5);
    fprintf(out, "\tshftli r%d, 4\n", rd);
    
    // bits 0-3
    uint64_t c6 = tmp & 0xF;
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)c6);
    // no shift after last addi
}

// validate number of arguments for macros
void checkMacroArgumentCount(const char *name, int exp, int act)
{
    // substract 1 for macro name
    int args = act - 1;
    
    if (args != exp)
    {
        fprintf(stderr, "Error: macro '%s' expects %d argument(s), got %d\n", 
                name, exp, args);
        exit(1);
    }
}

// handle different macro expansions
int tryExpandMacro(FILE *out, char toks[MAX_TOK][MAX_TOK_LEN], int n, uint64_t *addr)
{
    // check which macro it is
    char *name = toks[0];
    
    if (strcmp(name, "halt") == 0)
    {
        checkMacroArgumentCount("halt", 0, n);
        fprintf(out, "\tpriv r0, r0, r0, 0\n");
        *addr = *addr + 4;
        return 1;
    }
    
    if (strcmp(name, "in") == 0)
    {
        checkMacroArgumentCount("in", 2, n);
        fprintf(out, "\tpriv %s, %s, r0, 3\n", toks[1], toks[2]);
        *addr = *addr + 4;
        return 1;
    }
    
    if (strcmp(name, "out") == 0)
    {
        checkMacroArgumentCount("out", 2, n);
        fprintf(out, "\tpriv %s, %s, r0, 4\n", toks[1], toks[2]);
        *addr = *addr + 4;
        return 1;
    }
    
    if (strcmp(name, "clr") == 0)
    {
        checkMacroArgumentCount("clr", 1, n);
        fprintf(out, "\txor %s, %s, %s\n", toks[1], toks[1], toks[1]);
        *addr = *addr + 4;
        return 1;
    }
    
    if (strcmp(name, "push") == 0)
    {
        checkMacroArgumentCount("push", 1, n);
        fprintf(out, "\tsubi r31, 8\n");
        fprintf(out, "\tmov (r31)(0), %s\n", toks[1]);
        *addr = *addr + 8;
        return 1;
    }
    
    if (strcmp(name, "pop") == 0)
    {
        checkMacroArgumentCount("pop", 1, n);
        fprintf(out, "\tmov %s, (r31)(0)\n", toks[1]);
        fprintf(out, "\taddi r31, 8\n");
        *addr = *addr + 8;
        return 1;
    }
    
    if (strcmp(name, "ld") == 0)
    {
        checkMacroArgumentCount("ld", 2, n);
        
        // validate that literal is not negative (unsigned instruction)
        if (toks[2][0] != ':')
        {
            if (checkIfNegative(toks[2]))
            {
                fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
                exit(1);
            }
        }
        
        uint64_t val = convertToNumber(toks[2]);
        int reg = getRegisterNumber(toks[1]);
        writeLdMacro(out, reg, val);
        *addr = *addr + (4 * 13);
        return 1;
    }

    return 0; // not a macro
}

// process a line that starts with tab
void handleTabLine(FILE *out, char *line, Section sec, uint64_t *addr)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);

    // check if label - DON'T output it to intermediate!
    if (buf[0] == ':')
    {
        addLabelToArray(&buf[1], *addr);
        // DO NOT write label to intermediate file
        return;
    }

    char orig[MAX_LINE];
    strcpy(orig, buf);

    char toks[MAX_TOK][MAX_TOK_LEN];
    int n = splitIntoTokens(buf, toks);
    
    if (n == 0)
        return;

    if (sec == CODE)
    {
        int macro = tryExpandMacro(out, toks, n, addr);
        
        if (macro == 0)
        {
            // regular instruction
            fprintf(out, "\t%s\n", orig);
            *addr = *addr + 4;
        }
    }
    else if (sec == DATA)
    {
        // Parse the literal and output as decimal
        uint64_t val = convertToNumber(orig);
        fprintf(out, "\t%llu\n", (unsigned long long)val);
        *addr = *addr + 8;
    }
}

void firstPass(FILE *in, FILE *mid)
{
    char line[MAX_LINE];
    Section sec = NONE;
    uint64_t addr = CODE_START;  // code and data incremented together

    while (fgets(line, sizeof(line), in) != NULL)
    {
        cleanLine(line);
        
        if (line[0] == '\0' || line[0] == '\n')
            continue;

        if (strcmp(line, ".code") == 0)
        {
            sec = CODE;
            fprintf(mid, ".code\n");
            continue;
        }
        
        if (strcmp(line, ".data") == 0)
        {
            sec = DATA;
            fprintf(mid, ".data\n");
            continue;
        }

        // label without tab
        if (line[0] == ':')
        {
            addLabelToArray(&line[1], addr);
            continue;
        }

        // instruction/data with tab
        if (line[0] == '\t')
        {
            handleTabLine(mid, line, sec, &addr);
        }
    }
}

// validate operand count for regular instructions
void checkInstructionOperands(const char *name, int exp, int act, InstrType type)
{
    // substract instr name count
    int ops = act - 1;
    
    // mov has variable operand count
    if (type == MOV)
    {
        if (ops < 2 || ops > 3)
        {
            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", ops);
            exit(1);
        }
        return;
    }
    
    // for other instructions, check exact count
    if (ops != exp)
    {
        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n", 
                name, exp, ops);
        exit(1);
    }
}

// encode R instruction
void encodeRType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt)
{
    int r1 = getRegisterNumber(toks[1]);
    int r2 = getRegisterNumber(toks[2]);
    int r3 = getRegisterNumber(toks[3]);
    
    *rd = r1;
    *rs = r2;
    *rt = r3;
}

// encode I instruction
void encodeIType(char toks[MAX_TOK][MAX_TOK_LEN], const char *instr, uint32_t *rd, uint32_t *imm)
{
    int r = getRegisterNumber(toks[1]);
    *rd = r;
    
    // check if instruction requires unsigned literal
    int uns = 0;
    
    if (strcmp(instr, "addi") == 0) uns = 1;
    if (strcmp(instr, "subi") == 0) uns = 1;
    if (strcmp(instr, "shftli") == 0) uns = 1;
    if (strcmp(instr, "shftri") == 0) uns = 1;
    
    if (uns)
    {
        if (toks[2][0] != ':')
        {
            if (checkIfNegative(toks[2]))
            {
                fprintf(stderr, "Error: unsigned instruction '%s' cannot have negative literal\n", instr);
                exit(1);
            }
        }
    }
    
    uint64_t val = convertToNumber(toks[2]);
    
    // check 12-bit range for literals (not labels)
    if (toks[2][0] != ':')
    {
        if (uns)
        {
            // unsigned: 0 to 4095
            if (val > 4095)
            {
                fprintf(stderr, "Error: literal out of range for instruction '%s' (max 4095)\n", instr);
                exit(1);
            }
        }
        else
        {
            // signed: -2048 to 2047
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range for instruction '%s' (-2048 to 2047)\n", instr);
                exit(1);
            }
        }
    }
    
    *imm = (uint32_t)val;
}

// encode branch instruction
void encodeBranchType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *op, uint32_t *rd, uint32_t *imm)
{
    if (strcmp(toks[0], "brr") == 0 && toks[1][0] != 'r')
    {
        *op = 0x0a;
        // brr with literal can be signed
        uint64_t val = convertToNumber(toks[1]);
        
        // check 12-bit range for literals (not labels)
        if (toks[1][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range for brr (-2048 to 2047)\n");
                exit(1);
            }
        }
        
        *imm = (uint32_t)val;
    }
    else
    {
        int r = getRegisterNumber(toks[1]);
        *rd = r;
    }
}

// encode privileged instruction
void encodePrivType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt, uint32_t *imm)
{
    int r1 = getRegisterNumber(toks[1]);
    int r2 = getRegisterNumber(toks[2]);
    int r3 = getRegisterNumber(toks[3]);
    uint64_t val = convertToNumber(toks[4]);
    
    // check 12-bit range for literals (not labels)
    if (toks[4][0] != ':')
    {
        // unsigned: 0 to 4095
        if (val > 4095)
        {
            fprintf(stderr, "Error: literal out of range for priv (max 4095)\n");
            exit(1);
        }
    }
    
    *rd = r1;
    *rs = r2;
    *rt = r3;
    *imm = (uint32_t)val;
}

// encode mov instruction with different formats
void encodeMovType(char toks[MAX_TOK][MAX_TOK_LEN], int n, uint32_t *op, uint32_t *rd, uint32_t *rs, uint32_t *imm)
{
    // format: mov rd, rs (opcode 0x11)
    if (n == 3 && toks[1][0] == 'r' && toks[2][0] == 'r')
    {
        *op = 0x11;
        int r1 = getRegisterNumber(toks[1]);
        int r2 = getRegisterNumber(toks[2]);
        *rd = r1;
        *rs = r2;
    }
    // format: mov rd, L (opcode 0x12) - unsigned literal
    else if (n == 3 && toks[1][0] == 'r' && toks[2][0] != 'r')
    {
        *op = 0x12;
        int r1 = getRegisterNumber(toks[1]);
        *rd = r1;
        
        // unsigned literal check
        if (toks[2][0] != ':')
        {
            if (checkIfNegative(toks[2]))
            {
                fprintf(stderr, "Error: 'mov rd, L' cannot have negative literal\n");
                exit(1);
            }
        }
        
        uint64_t val = convertToNumber(toks[2]);
        
        // check 12-bit range for literals (not labels)
        if (toks[2][0] != ':')
        {
            if (val > 4095)
            {
                fprintf(stderr, "Error: literal out of range for 'mov rd, L' (max 4095)\n");
                exit(1);
            }
        }
        
        *imm = (uint32_t)val;
    }
    // format: mov rd, (rs)(L) (opcode 0x10) - signed literal
    else if (n == 4 && toks[1][0] == 'r' && toks[2][0] == 'r')
    {
        *op = 0x10;
        int r1 = getRegisterNumber(toks[1]);
        int r2 = getRegisterNumber(toks[2]);
        uint64_t val = convertToNumber(toks[3]);
        
        // check 12-bit range for literals (not labels)
        if (toks[3][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range for 'mov rd, (rs)(L)' (-2048 to 2047)\n");
                exit(1);
            }
        }
        
        *rd = r1;
        *rs = r2;
        *imm = (uint32_t)val;
    }
    // format: mov (rd)(L), rs (opcode 0x13) - signed literal
    else if (n == 4 && toks[1][0] == 'r' && toks[2][0] != 'r')
    {
        *op = 0x13;
        int r1 = getRegisterNumber(toks[1]);
        uint64_t val = convertToNumber(toks[2]);
        int r2 = getRegisterNumber(toks[3]);
        
        // check 12-bit range for literals (not labels)
        if (toks[2][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range for 'mov (rd)(L), rs' (-2048 to 2047)\n");
                exit(1);
            }
        }
        
        *rd = r1;
        *imm = (uint32_t)val;
        *rs = r2;
    }
    else
    {
        fprintf(stderr, "Error: invalid mov format\n");
        exit(1);
    }
}

// find instruction in table and get type
InstrType findInstructionInfo(char *name, uint32_t *op, int *exp)
{
    // search through instruction table
    int i;
    for (i = 0; i < tableSize; i = i + 1)
    {
        if (strcmp(instrTable[i].name, name) == 0)
        {
            *op = instrTable[i].opcode;
            *exp = instrTable[i].numOps;
            return instrTable[i].type;
        }
    }
    
    fprintf(stderr, "Error: invalid instruction '%s'\n", name);
    exit(1);
}

// combine fields into 32-bit instruction
uint32_t assembleInstruction(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm)
{
    uint32_t res = 0;
    
    // add opcode
    uint32_t opShift = op << 26;
    res = res | opShift;
    
    // add rd
    uint32_t rdMask = rd & 0x1F;
    uint32_t rdShift = rdMask << 21;
    res = res | rdShift;
    
    // add rs
    uint32_t rsMask = rs & 0x1F;
    uint32_t rsShift = rsMask << 16;
    res = res | rsShift;
    
    // add rt
    uint32_t rtMask = rt & 0x1F;
    uint32_t rtShift = rtMask << 11;
    res = res | rtShift;
    
    // add immediate
    uint32_t immMask = imm & 0x7FF;
    res = res | immMask;
    
    return res;
}

uint32_t convertToMachineCode(char toks[MAX_TOK][MAX_TOK_LEN], int n)
{
    uint32_t op = 0;
    uint32_t rd = 0;
    uint32_t rs = 0;
    uint32_t rt = 0;
    uint32_t imm = 0;
    int exp = 0;

    // find instruction type
    InstrType type = findInstructionInfo(toks[0], &op, &exp);
    
    // validate argument count
    checkInstructionOperands(toks[0], exp, n, type);

    // encode based on type
    if (type == R)
    {
        encodeRType(toks, &rd, &rs, &rt);
    }
    else if (type == I)
    {
        encodeIType(toks, toks[0], &rd, &imm);
    }
    else if (type == OTHER)
    {
        int r1 = getRegisterNumber(toks[1]);
        int r2 = getRegisterNumber(toks[2]);
        rd = r1;
        rs = r2;
    }
    else if (type == BR)
    {
        encodeBranchType(toks, &op, &rd, &imm);
    }
    else if (type == PRIV)
    {
        encodePrivType(toks, &rd, &rs, &rt, &imm);
    }
    else if (type == MOV)
    {
        encodeMovType(toks, n, &op, &rd, &rs, &imm);
    }
    else if (type == NO_OP)
    {
        // return instruction, nothing to parse
    }
    else
    {
        fprintf(stderr, "Error: invalid instruction type\n");
        exit(1);
    }

    uint32_t mc = assembleInstruction(op, rd, rs, rt, imm);
    return mc;
}

// process code section line in pass2
void writeCodeInstruction(FILE *out, char *line)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);
    
    char toks[MAX_TOK][MAX_TOK_LEN];
    int n = splitIntoTokens(buf, toks);
    
    if (n == 0)
    {
        fprintf(stderr, "Error: empty instruction line\n");
        exit(1);
    }
    
    uint32_t mc = convertToMachineCode(toks, n);
    
    // write to binary file
    fwrite(&mc, 4, 1, out);
}

// process data section line in pass2
void writeDataValue(FILE *out, char *line)
{
    // validate data is not negative (unsigned)
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);
    
    if (checkIfNegative(buf))
    {
        fprintf(stderr, "Error: data values must be unsigned\n");
        exit(1);
    }
    
    uint64_t val = convertToNumber(buf);
    
    // write to binary file
    fwrite(&val, 8, 1, out);
}

void secondPass(FILE *mid, FILE *out)
{
    char line[MAX_LINE];
    Section sec = NONE;

    while (fgets(line, sizeof(line), mid) != NULL)
    {
        cleanLine(line);
        
        if (line[0] == '\0')
        {
            continue;
        }

        // check for code directive
        if (strcmp(line, ".code") == 0)
        {
            sec = CODE;
            continue;
        }
        
        // check for data directive
        if (strcmp(line, ".data") == 0)
        {
            sec = DATA;
            continue;
        }

        // skip labels in pass2
        if (line[0] == ':')
        {
            continue;
        }

        if (line[0] == '\t')
        {
            // skip labels with tabs
            if (line[1] == ':')
            {
                continue;
            }
            
            if (sec == NONE)
            {
                fprintf(stderr, "Error: instr/data outside of .code/.data section\n");
                exit(1);
            }
                
            if (sec == CODE)
            {
                writeCodeInstruction(out, line);
            }
            else if (sec == DATA)
            {
                writeDataValue(out, line);
            }
        }
    }
}

int testmain(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Error: incorrect number of inputs to main\n");
        return 1;
    }

    FILE *fIn = NULL;
    FILE *fMid = NULL;
    FILE *fOut = NULL;

    fIn = fopen(argv[1], "r");
    if (!fIn)
    {
        fprintf(stderr, "Error: cannot open input file '%s'\n", argv[1]);
        return 1;
    }

    fMid = fopen(argv[2], "w+");
    if (!fMid)
    {
        fprintf(stderr, "Error: cannot open intermediate file '%s'\n", argv[2]);
        fclose(fIn);
        return 1;
    }

    // if it errors, files are auto-deleted by exit()
    firstPass(fIn, fMid);
    fclose(fIn);
    fseek(fMid, 0, SEEK_SET);

    fOut = fopen(argv[3], "wb");
    if (!fOut)
    {
        fprintf(stderr, "Error: cannot open output file '%s'\n", argv[3]);
        fclose(fMid);
        remove(argv[2]);
        return 1;
    }

    secondPass(fMid, fOut);
    fclose(fMid);
    fclose(fOut);

    return 0;
}

int main(int argc, char **argv){
    return testmain(argc, argv);
}