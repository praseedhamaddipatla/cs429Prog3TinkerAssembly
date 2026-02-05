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
    int i;
    for (i = 0; i < numLbls; i++)
    {
        if (strcmp(lbls[i].name, lbl) == 0)
        {
            fprintf(stderr, "Error: duplicate label '%s'\n", lbl);
            exit(1);
        }
    }
    
    if (numLbls >= MAX_LABELS)
    {
        fprintf(stderr, "Error: too many labels\n");
        exit(1);
    }
    
    if (strlen(lbl) > 255)
    {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        exit(1);
    }
    
    strcpy(lbls[numLbls].name, lbl);
    lbls[numLbls].addr = addr;
    numLbls++;
}

uint64_t findLabelAddress(const char *lbl)
{
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
    int pos = 0;
    while (line[pos] != '\0')
    {
        if (line[pos] == ';')
        {
            line[pos] = '\0';
            break;
        }
        pos++;
    }
    
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
    {
        line[len - 1] = '\0';
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
        i++;
    }

    char *ptr = strtok(line, " \t");
    while (ptr != NULL && cnt < MAX_TOK)
    {
        strcpy(toks[cnt], ptr);
        cnt++;
        ptr = strtok(NULL, " \t");
    }
    
    return cnt;
}

int getRegisterNumber(const char *reg)
{
    if (reg == NULL || reg[0] != 'r')
    {
        fprintf(stderr, "Error: invalid register format\n");
        exit(1);
    }
    
    if (reg[1] == '\0' || !isdigit(reg[1]))
    {
        fprintf(stderr, "Error: invalid register format\n");
        exit(1);
    }
    
    int num = atoi(&reg[1]);
    
    if (num < 0 || num > 31)
    {
        fprintf(stderr, "Error: register out of range\n");
        exit(1);
    }
    
    return num;
}

int checkIfNegative(const char *lit)
{
    return (lit[0] == '-') ? 1 : 0;
}

uint64_t convertToNumber(const char *lit)
{
    if (lit == NULL)
    {
        fprintf(stderr, "Error: NULL literal\n");
        exit(1);
    }
    
    if (lit[0] == ':')
    {
        return findLabelAddress(&lit[1]);
    }
    
    return strtoull(lit, NULL, 0);
}

// expand ld macro
void writeLdMacro(FILE *out, int rd, uint64_t val)
{
    fprintf(out, "\txor r%d, r%d, r%d\n", rd, rd, rd);
    
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 52) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 40) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 28) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 16) & 0xFFF));
    fprintf(out, "\tshftli r%d, 12\n", rd);
    
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)((val >> 4) & 0xFFF));
    fprintf(out, "\tshftli r%d, 4\n", rd);
    
    fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)(val & 0xF));
}

void checkMacroArgumentCount(const char *name, int exp, int act)
{
    int args = act - 1;
    if (args != exp)
    {
        fprintf(stderr, "Error: macro '%s' expects %d argument(s), got %d\n", 
                name, exp, args);
        exit(1);
    }
}

int tryExpandMacro(FILE *out, char toks[MAX_TOK][MAX_TOK_LEN], int n, uint64_t *addr)
{
    char *name = toks[0];
    
    if (strcmp(name, "halt") == 0)
    {
        checkMacroArgumentCount("halt", 0, n);
        fprintf(out, "\tpriv r0, r0, r0, 0\n");
        *addr += 4;
        return 1;
    }
    
    if (strcmp(name, "in") == 0)
    {
        checkMacroArgumentCount("in", 2, n);
        fprintf(out, "\tpriv %s, %s, r0, 3\n", toks[1], toks[2]);
        *addr += 4;
        return 1;
    }
    
    if (strcmp(name, "out") == 0)
    {
        checkMacroArgumentCount("out", 2, n);
        fprintf(out, "\tpriv %s, %s, r0, 4\n", toks[1], toks[2]);
        *addr += 4;
        return 1;
    }
    
    if (strcmp(name, "clr") == 0)
    {
        checkMacroArgumentCount("clr", 1, n);
        fprintf(out, "\txor %s, %s, %s\n", toks[1], toks[1], toks[1]);
        *addr += 4;
        return 1;
    }
    
    if (strcmp(name, "push") == 0)
    {
        checkMacroArgumentCount("push", 1, n);
        fprintf(out, "\tsubi r31, 8\n");
        fprintf(out, "\tmov (r31)(0), %s\n", toks[1]);
        *addr += 8;
        return 1;
    }
    
    if (strcmp(name, "pop") == 0)
    {
        checkMacroArgumentCount("pop", 1, n);
        fprintf(out, "\tmov %s, (r31)(0)\n", toks[1]);
        fprintf(out, "\taddi r31, 8\n");
        *addr += 8;
        return 1;
    }
    
    if (strcmp(name, "ld") == 0)
    {
        checkMacroArgumentCount("ld", 2, n);
        
        if (toks[2][0] != ':' && checkIfNegative(toks[2]))
        {
            fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
            exit(1);
        }
        
        uint64_t val = convertToNumber(toks[2]);
        int reg = getRegisterNumber(toks[1]);
        writeLdMacro(out, reg, val);
        *addr += 52;  // 13 instructions * 4 bytes
        return 1;
    }

    return 0;
}

void handleTabLine(FILE *out, char *line, Section sec, uint64_t *addr)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);

    // labels don't go to intermediate
    if (buf[0] == ':')
    {
        addLabelToArray(&buf[1], *addr);
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
        if (!tryExpandMacro(out, toks, n, addr))
        {
            fprintf(out, "\t%s\n", orig);
            *addr += 4;
        }
    }
    else if (sec == DATA)
    {
        uint64_t val = convertToNumber(orig);
        fprintf(out, "\t%llu\n", (unsigned long long)val);
        *addr += 8;
    }
}

void firstPass(FILE *in, FILE *mid)
{
    char line[MAX_LINE];
    Section sec = NONE;
    uint64_t addr = CODE_START;

    while (fgets(line, sizeof(line), in))
    {
        cleanLine(line);
        
        if (line[0] == '\0')
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

        if (line[0] == ':')
        {
            addLabelToArray(&line[1], addr);
            continue;
        }

        if (line[0] == '\t')
        {
            handleTabLine(mid, line, sec, &addr);
        }
    }
}

void checkInstructionOperands(const char *name, int exp, int act, InstrType type)
{
    int ops = act - 1;
    
    if (type == MOV)
    {
        if (ops < 2 || ops > 3)
        {
            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", ops);
            exit(1);
        }
        return;
    }
    
    if (ops != exp)
    {
        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n", 
                name, exp, ops);
        exit(1);
    }
}

void encodeRType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt)
{
    *rd = getRegisterNumber(toks[1]);
    *rs = getRegisterNumber(toks[2]);
    *rt = getRegisterNumber(toks[3]);
}

void encodeIType(char toks[MAX_TOK][MAX_TOK_LEN], const char *instr, uint32_t *rd, uint32_t *imm)
{
    *rd = getRegisterNumber(toks[1]);
    
    int uns = (strcmp(instr, "addi") == 0 || strcmp(instr, "subi") == 0 ||
               strcmp(instr, "shftli") == 0 || strcmp(instr, "shftri") == 0);
    
    if (uns && toks[2][0] != ':' && checkIfNegative(toks[2]))
    {
        fprintf(stderr, "Error: unsigned instruction cannot have negative literal\n");
        exit(1);
    }
    
    uint64_t val = convertToNumber(toks[2]);
    
    if (toks[2][0] != ':')
    {
        if (uns && val > 4095)
        {
            fprintf(stderr, "Error: literal out of range\n");
            exit(1);
        }
        else if (!uns)
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                exit(1);
            }
        }
    }
    
    *imm = (uint32_t)(val & 0x7FF);
}

void encodeBranchType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *op, uint32_t *rd, uint32_t *imm)
{
    if (strcmp(toks[0], "brr") == 0 && toks[1][0] != 'r')
    {
        *op = 0x0a;
        uint64_t val = convertToNumber(toks[1]);
        
        if (toks[1][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                exit(1);
            }
        }
        
        *imm = (uint32_t)(val & 0x7FF);
    }
    else
    {
        *rd = getRegisterNumber(toks[1]);
    }
}

void encodePrivType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt, uint32_t *imm)
{
    *rd = getRegisterNumber(toks[1]);
    *rs = getRegisterNumber(toks[2]);
    *rt = getRegisterNumber(toks[3]);
    uint64_t val = convertToNumber(toks[4]);
    
    if (toks[4][0] != ':' && val > 4095)
    {
        fprintf(stderr, "Error: literal out of range\n");
        exit(1);
    }
    
    *imm = (uint32_t)(val & 0x7FF);
}

void encodeMovType(char toks[MAX_TOK][MAX_TOK_LEN], int n, uint32_t *op, uint32_t *rd, uint32_t *rs, uint32_t *imm)
{
    if (n == 3 && toks[1][0] == 'r' && toks[2][0] == 'r')
    {
        *op = 0x11;
        *rd = getRegisterNumber(toks[1]);
        *rs = getRegisterNumber(toks[2]);
    }
    else if (n == 3 && toks[1][0] == 'r' && toks[2][0] != 'r')
    {
        *op = 0x12;
        *rd = getRegisterNumber(toks[1]);
        
        if (toks[2][0] != ':' && checkIfNegative(toks[2]))
        {
            fprintf(stderr, "Error: unsigned literal cannot be negative\n");
            exit(1);
        }
        
        uint64_t val = convertToNumber(toks[2]);
        
        if (toks[2][0] != ':' && val > 4095)
        {
            fprintf(stderr, "Error: literal out of range\n");
            exit(1);
        }
        
        *imm = (uint32_t)(val & 0x7FF);
    }
    else if (n == 4 && toks[1][0] == 'r' && toks[2][0] == 'r')
    {
        *op = 0x10;
        *rd = getRegisterNumber(toks[1]);
        *rs = getRegisterNumber(toks[2]);
        uint64_t val = convertToNumber(toks[3]);
        
        if (toks[3][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                exit(1);
            }
        }
        
        *imm = (uint32_t)(val & 0x7FF);
    }
    else if (n == 4 && toks[1][0] == 'r' && toks[2][0] != 'r')
    {
        *op = 0x13;
        *rd = getRegisterNumber(toks[1]);
        uint64_t val = convertToNumber(toks[2]);
        *rs = getRegisterNumber(toks[3]);
        
        if (toks[2][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                exit(1);
            }
        }
        
        *imm = (uint32_t)(val & 0x7FF);
    }
    else
    {
        fprintf(stderr, "Error: invalid mov format\n");
        exit(1);
    }
}

InstrType findInstructionInfo(char *name, uint32_t *op, int *exp)
{
    int i;
    for (i = 0; i < tableSize; i++)
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

uint32_t assembleInstruction(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm)
{
    uint32_t res = 0;
    res |= (op & 0x3F) << 26;
    res |= (rd & 0x1F) << 21;
    res |= (rs & 0x1F) << 16;
    res |= (rt & 0x1F) << 11;
    res |= (imm & 0x7FF);
    return res;
}

uint32_t convertToMachineCode(char toks[MAX_TOK][MAX_TOK_LEN], int n)
{
    uint32_t op = 0, rd = 0, rs = 0, rt = 0, imm = 0;
    int exp = 0;

    InstrType type = findInstructionInfo(toks[0], &op, &exp);
    checkInstructionOperands(toks[0], exp, n, type);

    if (type == R)
        encodeRType(toks, &rd, &rs, &rt);
    else if (type == I)
        encodeIType(toks, toks[0], &rd, &imm);
    else if (type == OTHER)
    {
        rd = getRegisterNumber(toks[1]);
        rs = getRegisterNumber(toks[2]);
    }
    else if (type == BR)
        encodeBranchType(toks, &op, &rd, &imm);
    else if (type == PRIV)
        encodePrivType(toks, &rd, &rs, &rt, &imm);
    else if (type == MOV)
        encodeMovType(toks, n, &op, &rd, &rs, &imm);

    return assembleInstruction(op, rd, rs, rt, imm);
}

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
    
    // write as-is (native endianness)
    fwrite(&mc, 4, 1, out);
}

void writeDataValue(FILE *out, char *line)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);
    
    if (checkIfNegative(buf))
    {
        fprintf(stderr, "Error: data values must be unsigned\n");
        exit(1);
    }
    
    uint64_t val = convertToNumber(buf);
    
    // write as-is (native endianness)
    fwrite(&val, 8, 1, out);
}

void secondPass(FILE *mid, FILE *out)
{
    char line[MAX_LINE];
    Section sec = NONE;

    while (fgets(line, sizeof(line), mid))
    {
        cleanLine(line);
        
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

        if (line[0] == ':')
            continue;

        if (line[0] == '\t')
        {
            if (line[1] == ':')
                continue;
            
            if (sec == NONE)
            {
                fprintf(stderr, "Error: instr/data outside of .code/.data section\n");
                exit(1);
            }
                
            if (sec == CODE)
                writeCodeInstruction(out, line);
            else if (sec == DATA)
                writeDataValue(out, line);
        }
    }
}

int testmain(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Error: incorrect number of inputs\n");
        return 1;
    }

    FILE *fIn = fopen(argv[1], "r");
    if (!fIn)
    {
        fprintf(stderr, "Error: cannot open input file\n");
        return 1;
    }

    FILE *fMid = fopen(argv[2], "w+");
    if (!fMid)
    {
        fprintf(stderr, "Error: cannot open intermediate file\n");
        fclose(fIn);
        return 1;
    }

    firstPass(fIn, fMid);
    fclose(fIn);
    fseek(fMid, 0, SEEK_SET);

    FILE *fOut = fopen(argv[3], "wb");
    if (!fOut)
    {
        fprintf(stderr, "Error: cannot open output file\n");
        fclose(fMid);
        return 1;
    }

    secondPass(fMid, fOut);
    fclose(fMid);
    fclose(fOut);

    return 0;
}

int main(int argc, char **argv)
{
    return testmain(argc, argv);
}