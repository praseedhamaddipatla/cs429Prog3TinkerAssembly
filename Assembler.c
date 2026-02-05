#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>


#define MAX_LINE 512
#define MAX_TOK 8
#define MAX_TOK_LEN 64
#define MAX_LABELS 512
#define CODE_START 0x1000

int hadError = 0;
int inFirst = 0;

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
    {"and", 0x00, R, 3}, {"or", 0x01, R, 3}, {"xor", 0x02, R, 3}, {"not", 0x03, OTHER, 2}, {"shftr", 0x04, R, 3}, {"shftri", 0x05, I, 2}, {"shftl", 0x06, R, 3}, {"shftli", 0x07, I, 2}, {"br", 0x08, BR, 1}, {"brr", 0x09, BR, 1}, {"brnz", 0x0b, OTHER, 2}, {"call", 0x0c, BR, 1}, {"return", 0x0d, NO_OP, 0}, {"brgt", 0x0e, R, 3}, {"priv", 0x0f, PRIV, 4}, {"mov", 0x10, MOV, 2}, {"addf", 0x14, R, 3}, {"subf", 0x15, R, 3}, {"mulf", 0x16, R, 3}, {"divf", 0x17, R, 3}, {"add", 0x18, R, 3}, {"addi", 0x19, I, 2}, {"sub", 0x1a, R, 3}, {"subi", 0x1b, I, 2}, {"mul", 0x1c, R, 3}, {"div", 0x1d, R, 3}};

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
            hadError = 1;
            exit(1);
        }
    }

    if (numLbls >= MAX_LABELS)
    {
        fprintf(stderr, "Error: too many labels\n");
        hadError = 1;
        exit(1);
    }

    if (strlen(lbl) > 255)
    {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        hadError = 1;
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
    hadError = 1;
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
    if (!reg || reg[0] != 'r' || !isdigit(reg[1]) || (reg[2] && !isdigit(reg[2])))
        goto bad;

    int num = atoi(&reg[1]);
    if (num < 0 || num > 31)
        goto bad;

    return num;

bad:
    fprintf(stderr, "Error: invalid register '%s'\n", reg);
    hadError = 1;
    exit(1);
}

int checkIfNegative(const char *lit)
{
    return (lit[0] == '-') ? 1 : 0;
}

uint64_t convertToNumber(const char *lit)
{
    if (lit == NULL || *lit == '\0')
    {
        fprintf(stderr, "Error: empty literal\n");
        hadError = 1;
        exit(1);
    }

    if (lit[0] == ':')
        return findLabelAddress(&lit[1]);

    errno = 0;
    char *end;
    uint64_t result = strtoull(lit, &end, 0);

    if (errno == ERANGE || *end != '\0')
    {
        fprintf(stderr, "Error: invalid or overflow literal '%s'\n", lit);
        hadError = 1;
        exit(1);
    }

    return result;
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
        hadError = 1;
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
        getRegisterNumber(toks[1]);
        fprintf(out, "\tmov (r31)(-8), %s\n", toks[1]);
        fprintf(out, "\tsubi r31, 8\n");
        *addr += 8;
        return 1;
    }

    if (strcmp(name, "pop") == 0)
    {
        checkMacroArgumentCount("pop", 1, n);
        getRegisterNumber(toks[1]);
        fprintf(out, "\tmov %s, (r31)(0)\n", toks[1]);
        fprintf(out, "\taddi r31, 8\n");
        *addr += 8;
        return 1;
    }

    if (strcmp(name, "ld") == 0)
    {

        checkMacroArgumentCount("ld", 2, n);

        checkMacroArgumentCount("ld", 2, n);

    if (toks[2][0] != ':' && checkIfNegative(toks[2]))
    {
        fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
        hadError = 1;
        exit(1);
    }

    uint64_t val = convertToNumber(toks[2]);
    int reg = getRegisterNumber(toks[1]);
    writeLdMacro(out, reg, val);
    *addr += 52;
    return 1;
    }

    return 0;
}

void printResolvedInstr(FILE *out, char toks[MAX_TOK][MAX_TOK_LEN], int n)
{
    if (strcmp(toks[0], "mov") == 0)
    {
        fprintf(out, "\t%s ", toks[0]);

        if (n == 3)
        {
            fprintf(out, "%s, %s\n", toks[1], toks[2]);
        }
        else if (n == 4)
        {
            if (toks[1][0] == 'r' && toks[2][0] != 'r')
            {
                uint64_t val = convertToNumber(toks[2]);
                fprintf(out, "(%s)(%lld), %s\n",
                        toks[1], (long long)(int64_t)val, toks[3]);
            }
            else
            {
                uint64_t val = convertToNumber(toks[3]);
                fprintf(out, "%s, (%s)(%lld)\n",
                        toks[1], toks[2], (long long)(int64_t)val);
            }
        }
        else if (n == 5)
        {
            uint64_t v1 = convertToNumber(toks[2]);
            uint64_t v2 = convertToNumber(toks[4]);
            fprintf(out, "(%s)(%lld), (%s)(%lld)\n",
                    toks[1], (long long)(int64_t)v1,
                    toks[3], (long long)(int64_t)v2);
        }
        else
        {
            fprintf(stderr, "Error: invalid mov format\n");
            hadError = 1;
            exit(1);
        }
    }
    else
    {
        fprintf(out, "\t%s", toks[0]);
        for (int i = 1; i < n; i++)
        {
            if (toks[i][0] == ':')
            {
                uint64_t val = findLabelAddress(&toks[i][1]);
                fprintf(out, "%s%llu", (i == 1 ? " " : ", "),
                        (unsigned long long)val);
            }
            else
            {
                fprintf(out, "%s%s", (i == 1 ? " " : ", "), toks[i]);
            }
        }
        fprintf(out, "\n");
    }
}

void handleTabLine(FILE *out, char *line, Section sec, uint64_t *addr)
{
    if (sec == CODE && strstr(line, "mov"))
    {
        int open = 0;
        for (int i = 0; line[i]; i++)
        {
            if (line[i] == '(') open++;
            if (line[i] == ')') open--;
        }
        if (open != 0)
        {
            fprintf(stderr, "Error: unmatched parentheses in mov\n");
            hadError = 1;
            exit(1);
        }
    }

    char buf[MAX_LINE];
    strcpy(buf, &line[1]);

    if (buf[0] == ':')
        return;

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
            printResolvedInstr(out, toks, n);
            *addr += 4;
        }
    }
    else if (sec == DATA)
    {
        uint64_t val = convertToNumber(orig);
        fprintf(out, "\t%llu\n", (unsigned long long)val);
        *addr += 4;
    }
}

void collectLabels(FILE *in)
{
    char line[MAX_LINE];
    Section sec = NONE;
    uint64_t addr = CODE_START;
    int lineNum = 0;

    while (fgets(line, sizeof(line), in))
    {
        lineNum++;
        cleanLine(line);

        if (line[0] == '\0')
        {
            continue;
        }

        if (strncmp(line, ".code", 5) == 0 && strcmp(line, ".code") != 0)
        {
            fprintf(stderr, "Error: invalid directive '%s'\n", line);
            hadError = 1;
            exit(1);
        }

        if (strcmp(line, ".code") == 0 && line[0] == '.')

        {
            sec = CODE;
            continue;
        }

        if (strncmp(line, ".data", 5) == 0 && strcmp(line, ".data") != 0)
        {
            fprintf(stderr, "Error: invalid directive '%s'\n", line);
            hadError = 1;
            exit(1);
        }

        if (strcmp(line, ".data") == 0 && line[0] == '.')
        {
            sec = DATA;
            continue;
        }

        if (line[0] == ':')
        {
            if (strchr(line, '\t'))
            {
                fprintf(stderr, "Error: label must be alone on its line\n");
                hadError = 1;
                exit(1);
            }
            addLabelToArray(&line[1], addr);
            continue;
        }

        if (line[0] == '\t')
        {
            if (line[1] == ':')
            {
                continue;
            }

            // Estimate address increment for macro expansion
            char buf[MAX_LINE];
            strcpy(buf, &line[1]);
            char toks[MAX_TOK][MAX_TOK_LEN];
            int n = splitIntoTokens(buf, toks);

            if (n == 0)
            {
                continue;
            }

            if (sec == CODE)
            {
                char *name = toks[0];

                // Account for macro expansions
                if (strcmp(name, "halt") == 0 || strcmp(name, "in") == 0 ||
                    strcmp(name, "out") == 0 || strcmp(name, "clr") == 0)
                {
                    addr += 4;
                }
                else if (strcmp(name, "push") == 0 || strcmp(name, "pop") == 0)
                {
                    addr += 8;
                }
                else if (strcmp(name, "ld") == 0)
                {
                    addr += 52; // 13 instructions * 4 bytes
                }
                else
                {
                    addr += 4; // Regular instruction
                }
            }
            if (sec == DATA)
            {
                addr += 4;
            }
        }
    }
}

void firstPass(FILE *in, FILE *mid)
{
    inFirst = 1;
    char line[MAX_LINE];
    Section sec = NONE;
    Section lastWritten = NONE;
    uint64_t addr = CODE_START;
    int lineNum = 0;

    while (fgets(line, sizeof(line), in))
    {
        lineNum++;
        cleanLine(line);

        if (line[0] == '\0')
        {
            continue;
        }

        if (line[0] == ' ')
        {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            hadError = 1;
            exit(1);
        }

        if (strncmp(line, ".code", 5) == 0 && strcmp(line, ".code") != 0)
        {
            fprintf(stderr, "Error: invalid directive '%s'\n", line);
            hadError = 1;
            exit(1);
        }

        if (strcmp(line, ".code") == 0 && line[0] == '.')

        {
            sec = CODE;
            if (lastWritten != CODE)
            {
                fprintf(mid, ".code\n");
                lastWritten = CODE;
            }
            continue;
        }

        if (strncmp(line, ".data", 5) == 0 && strcmp(line, ".data") != 0)
        {
            fprintf(stderr, "Error: invalid directive '%s'\n", line);
            hadError = 1;
            exit(1);
        }

        if (strcmp(line, ".data") == 0 && line[0] == '.')

        {
            sec = DATA;
            if (lastWritten != DATA)
            {
                fprintf(mid, ".data\n");
                lastWritten = DATA;
            }

            continue;
        }

        if (line[0] == ':')
        {
            // Labels already collected, skip
            continue;
        }

        if (line[0] == '\t')
        {
            handleTabLine(mid, line, sec, &addr);
        }
    }
    inFirst = 0;
}

void checkInstructionOperands(const char *name, int exp, int act, InstrType type)
{
    int ops = act - 1;

    if (type == MOV)
    {
        if (ops < 2 || ops > 3)
        {
            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", ops);
            hadError = 1;
            exit(1);
        }
        return;
    }

    if (ops != exp)
    {
        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n",
                name, exp, ops);
        hadError = 1;
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

    if (toks[2][0] == ':' &&
        (strcmp(instr, "addi") == 0 ||
         strcmp(instr, "subi") == 0 ||
         strcmp(instr, "shftli") == 0 ||
         strcmp(instr, "shftri") == 0))
    {
        fprintf(stderr, "Error: label not allowed as immediate\n");
        hadError = 1;
        exit(1);
    }

    if (uns && toks[2][0] != ':' && checkIfNegative(toks[2]))
    {
        fprintf(stderr, "Error: unsigned instruction cannot have negative literal\n");
        hadError = 1;
        exit(1);
    }

    uint64_t val = convertToNumber(toks[2]);

    if (toks[2][0] != ':')
    {
        if (uns && val > 4095)
        {
            fprintf(stderr, "Error: literal out of range\n");
            hadError = 1;
            exit(1);
        }
        else if (!uns)
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                hadError = 1;
                exit(1);
            }
        }
    }

    *imm = (uint32_t)(val & 0x7FF);
}

void encodeBranchType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *op, uint32_t *rd, uint32_t *imm)
{
    if (strcmp(toks[0], "call") == 0)
    {
        *rd = getRegisterNumber(toks[1]);
        return;
    }

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
                hadError = 1;
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
        hadError = 1;
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
            hadError = 1;
            exit(1);
        }

        uint64_t val = convertToNumber(toks[2]);

        if (toks[2][0] != ':' && val > 4095)
        {
            fprintf(stderr, "Error: literal out of range\n");
            hadError = 1;
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
                hadError = 1;
                exit(1);
            }
        }

        *imm = (uint32_t)(llabs((int64_t)val) & 0x7FF);
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
                hadError = 1;
                exit(1);
            }
        }

        *imm = (uint32_t)(llabs((int64_t)val) & 0x7FF);
    }
    else
    {
        fprintf(stderr, "Error: invalid mov format\n");
        hadError = 1;
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
    hadError = 1;
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

void writeCodeInstr(FILE *out, char *line)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);

    char toks[MAX_TOK][MAX_TOK_LEN];
    int n = splitIntoTokens(buf, toks);

    if (n == 0)
    {
        fprintf(stderr, "Error: empty instruction line\n");
        hadError = 1;
        exit(1);
    }

    if (n > MAX_TOK)
    {
        fprintf(stderr, "Error: too many operands\n");
        hadError = 1;
        exit(1);
    }

    uint32_t mc = convertToMachineCode(toks, n);

    // Write in little-endian byte order
    unsigned char bytes[4];
    bytes[0] = mc & 0xFF;
    bytes[1] = (mc >> 8) & 0xFF;
    bytes[2] = (mc >> 16) & 0xFF;
    bytes[3] = (mc >> 24) & 0xFF;

    fwrite(bytes, 4, 1, out);
}

void writeDataValue(FILE *out, char *line)
{
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);

    if (checkIfNegative(buf))
    {
        fprintf(stderr, "Error: data values must be unsigned\n");
        hadError = 1;
        exit(1);
    }

    uint64_t val = convertToNumber(buf);

    if (buf[0] == '-' || (int64_t)val < 0)
    {
        fprintf(stderr, "Error: data values must be unsigned\n");
        hadError = 1;
        exit(1);
    }

    uint32_t v = (uint32_t)val;

    // Write in little-endian byte order
    unsigned char bytes[4];
    bytes[0] = v & 0xFF;
    bytes[1] = (v >> 8) & 0xFF;
    bytes[2] = (v >> 16) & 0xFF;
    bytes[3] = (v >> 24) & 0xFF;

    fwrite(bytes, 4, 1, out);
}

void secondPass(FILE *mid, FILE *out)
{
    inFirst = 0;
    char line[MAX_LINE];
    Section sec = NONE;

    while (fgets(line, sizeof(line), mid))
    {
        cleanLine(line);

        if (line[0] == '\0')
            continue;

        if (line[0] == ' ')
        {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            hadError = 1;
            exit(1);
        }

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
                hadError = 1;
                exit(1);
            }

            if (sec == CODE)
                writeCodeInstr(out, line);
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

    // First, collect all labels
    collectLabels(fIn);
    if (hadError)
    {
        fclose(fIn);
        return 1;
    }

    // Rewind to beginning for actual first pass
    fseek(fIn, 0, SEEK_SET);

    FILE *fMid = fopen(argv[2], "w+");
    if (!fMid)
    {
        fprintf(stderr, "Error: cannot open intermediate file\n");
        fclose(fIn);
        return 1;
    }

    firstPass(fIn, fMid);
    if (hadError)
    {
        fclose(fIn);
        fclose(fMid);
        return 1;
    }
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
    if (hadError)
    {
        fclose(fMid);
        fclose(fOut);
        return 1;
    }
    fclose(fMid);
    fclose(fOut);

    return 0;
}

int main(int argc, char **argv)
{
    int ret = testmain(argc, argv);
    if (hadError)
    {
        remove(argv[2]);
        remove(argv[3]);
    }
    return hadError ? 1 : ret;
}