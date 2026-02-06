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
            return;
        }
    }

    if (numLbls >= MAX_LABELS)
    {
        fprintf(stderr, "Error: too many labels\n");
        hadError = 1;
        return;
    }

    if (strlen(lbl) > 255)
    {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        hadError = 1;
        return;
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
    return 0;
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

int getRegisterNumber(const char *tok)
{
    if (!tok || tok[0] != 'r')
    {
        fprintf(stderr, "Error: invalid register '%s'\n", tok);
        hadError = 1;
        return -1;
    }

    char *end;
    long reg = strtol(tok + 1, &end, 10);

    // Must consume the entire string and be in range
    if (*end != '\0' || reg < 0 || reg > 31)
    {
        fprintf(stderr, "Error: invalid register '%s'\n", tok);
        hadError = 1;
        return -1;
    }

    return (int)reg;
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
        hadError = 1;
        return 0;
    }

    if (lit[0] == ':')
    {
        uint64_t addr = findLabelAddress(&lit[1]);
        return addr;
    }

    char *endptr = NULL;
    uint64_t result = strtoull(lit, &endptr, 0);

    if (endptr == lit || *endptr != '\0')
    {
        fprintf(stderr, "Error: invalid numeric literal '%s'\n", lit);
        hadError = 1;
        return 0;
    }

    return result;
}

// expand ld macro
void writeLdMacro(FILE *out, int rd, uint64_t val)
{
    // We build the value from MSB to LSB using:
    // addi + shftli(12), except final shftli(4)

    uint32_t parts[6];

    parts[0] = (val >> 52) & 0xFFF;
    parts[1] = (val >> 40) & 0xFFF;
    parts[2] = (val >> 28) & 0xFFF;
    parts[3] = (val >> 16) & 0xFFF;
    parts[4] = (val >> 4) & 0xFFF;
    parts[5] = val & 0xF;

    fprintf(out, "\txor r%d, r%d, r%d\n", rd, rd, rd);

    // First 5 chunks
    for (int i = 0; i < 5; i++)
    {
        fprintf(out, "\taddi r%d, %u\n", rd, parts[i]);
        fprintf(out, "\tshftli r%d, %d\n", rd, (i == 4 ? 4 : 12));
    }

    // Final low nibble
    fprintf(out, "\taddi r%d, %u\n", rd, parts[5]);
}

void checkMacroArgumentCount(const char *name, int exp, int act)
{
    int args = act - 1;
    if (args != exp)
    {
        fprintf(stderr, "Error: macro '%s' expects %d argument(s), got %d\n",
                name, exp, args);
        hadError = 1;
    }
}

int tryExpandMacro(FILE *out, char toks[MAX_TOK][MAX_TOK_LEN], int n, uint64_t *addr)
{
    char *name = toks[0];

    if (strcmp(name, "halt") == 0)
    {
        checkMacroArgumentCount("halt", 0, n);
        if (hadError)
            return 1;
        fprintf(out, "\tpriv r0, r0, r0, 0\n");
        *addr += 4;
        return 1;
    }

    if (strcmp(name, "in") == 0)
    {
        checkMacroArgumentCount("in", 2, n);
        if (hadError)
            return 1;
        fprintf(out, "\tpriv %s, %s, r0, 3\n", toks[1], toks[2]);
        *addr += 4;
        return 1;
    }

    if (strcmp(name, "out") == 0)
    {
        checkMacroArgumentCount("out", 2, n);
        if (hadError)
            return 1;
        fprintf(out, "\tpriv %s, %s, r0, 4\n", toks[1], toks[2]);
        *addr += 4;
        return 1;
    }

    if (strcmp(name, "clr") == 0)
    {
        checkMacroArgumentCount("clr", 1, n);
        if (hadError)
            return 1;
        fprintf(out, "\txor %s, %s, %s\n", toks[1], toks[1], toks[1]);
        *addr += 4;
        return 1;
    }

    if (strcmp(name, "push") == 0)
    {
        checkMacroArgumentCount("push", 1, n);
        if (hadError)
            return 1;
        getRegisterNumber(toks[1]);
        if (hadError)
            return 1;
        fprintf(out, "\tmov (r31)(-8), %s\n", toks[1]);
        fprintf(out, "\tsubi r31, 8\n");
        *addr += 8;
        return 1;
    }

    if (strcmp(name, "pop") == 0)
    {
        checkMacroArgumentCount("pop", 1, n);
        if (hadError)
            return 1;
        getRegisterNumber(toks[1]);
        if (hadError)
            return 1;
        fprintf(out, "\tmov %s, (r31)(0)\n", toks[1]);
        fprintf(out, "\taddi r31, 8\n");
        *addr += 8;
        return 1;
    }

    if (strcmp(name, "ld") == 0)
    {
        checkMacroArgumentCount("ld", 2, n);
        if (hadError)
            return 1;

        // Validate register BEFORE checking literal
        int reg = getRegisterNumber(toks[1]);
        if (hadError)
            return 1;

        if (toks[2][0] != ':' && checkIfNegative(toks[2]))
        {
            fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
            hadError = 1;
            return 1;
        }

        uint64_t val = convertToNumber(toks[2]);
        if (hadError)
            return 1;

        if (toks[2][0] != ':' && val > UINT64_MAX)
        {
            fprintf(stderr, "Error: ld literal overflow\n");
            hadError = 1;
            return 1;
        }

        writeLdMacro(out, reg, val);
        *addr += 52; // 13 instructions * 4 bytes
        return 1;
    }

    return 0;
}

void printResolvedInstr(FILE *out, char toks[MAX_TOK][MAX_TOK_LEN], int n)
{
    // Special handling for mov instruction which needs parentheses reconstructed
    if (strcmp(toks[0], "mov") == 0)
    {
        fprintf(out, "\t%s ", toks[0]);

        // Pattern detection based on number of tokens
        if (n == 3)
        {
            // mov rd, rs (2 operands after splitting)
            fprintf(out, "%s, %s\n", toks[1], toks[2]);
        }
        else if (n == 4)
        {
            // Either: mov (rd)(imm), rs OR mov rd, (rs)(imm) OR mov rd, imm
            // Check if token 1 is a register and token 2 is a number (could be negative)
            int tok1_is_reg = (toks[1][0] == 'r');
            int tok2_is_num = (toks[2][0] == '-' || isdigit(toks[2][0]) || toks[2][0] == ':');

            if (tok1_is_reg && tok2_is_num)
            {
                // mov rd, imm (but shouldn't be 4 tokens... this is mov rd, rs, imm which is invalid for output)
                // Actually this is: mov (rd)(imm), rs pattern split into: mov, rd, imm, rs
                fprintf(out, "(%s)(%s), %s\n", toks[1], toks[2], toks[3]);
            }
            else
            {
                // mov rd, (rs)(imm) pattern split into: mov, rd, rs, imm
                uint64_t val = (toks[3][0] == ':') ? findLabelAddress(&toks[3][1]) : strtoull(toks[3], NULL, 0);
                fprintf(out, "%s, (%s)(%lld)\n", toks[1], toks[2], (long long)(int64_t)val);
            }
        }
        else if (n == 5)
        {
            // mov (rd)(imm1), (rs)(imm2) pattern split into: mov, rd, imm1, rs, imm2
            uint64_t val1 = (toks[2][0] == ':') ? findLabelAddress(&toks[2][1]) : strtoull(toks[2], NULL, 0);
            uint64_t val2 = (toks[4][0] == ':') ? findLabelAddress(&toks[4][1]) : strtoull(toks[4], NULL, 0);
            fprintf(out, "(%s)(%lld), (%s)(%lld)\n", toks[1], (long long)(int64_t)val1, toks[3], (long long)(int64_t)val2);
        }
        else
        {
            fprintf(stderr, "Error: unexpected mov token count %d\n", n);
            hadError = 1;
        }
    }
    else
    {
        // Non-mov instructions - handle normally
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
    char buf[MAX_LINE];
    strcpy(buf, &line[1]);

    // labels don't go to intermediate
    if (buf[0] == ':')
    {
        return;
    }

    char orig[MAX_LINE];
    strcpy(orig, buf);

    char toks[MAX_TOK][MAX_TOK_LEN];
    int n = splitIntoTokens(buf, toks);

    if (n == 0)
    {
        return;
    }

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

    while (fgets(line, sizeof(line), in))
    {
        cleanLine(line);

        if (line[0] == '\0')
        {
            continue;
        }

        // Handle directives
        if (line[0] == '.')
        {
            if (strcmp(line, ".code") == 0)
            {
                if (sec == NONE)
                {
                    addr = CODE_START;
                }
                sec = CODE;
                continue;
            }
            else if (strcmp(line, ".data") == 0)
            {
                sec = DATA;
                continue;
            }
            else
            {
                fprintf(stderr, "Error: invalid directive '%s'\n", line);
                hadError = 1;
                return;
            }
        }

        // Handle labels
        if (line[0] == ':')
        {
            if (strchr(line, '\t'))
            {
                fprintf(stderr, "Error: label must be alone on its line\n");
                hadError = 1;
                return;
            }

            // Validate label name - should be alphanumeric or underscore
            const char *lbl_name = &line[1];
            if (*lbl_name == '\0') // Empty label name
            {
                fprintf(stderr, "Error: empty label name\n");
                hadError = 1;
                return;
            }

            for (const char *p = lbl_name; *p; p++)
            {
                if (isspace((unsigned char)*p))
                {
                    fprintf(stderr, "Error: label cannot contain spaces\n");
                    hadError = 1;
                    return;
                }
                if (!isalnum((unsigned char)*p) && *p != '_')
                {
                    fprintf(stderr, "Error: invalid label name '%s'\n", line);
                    hadError = 1;
                    return;
                }
            }

            addLabelToArray(lbl_name, addr);
            if (hadError)
                return;
            continue;
        }

        // Handle instructions/data
        if (line[0] == '\t')
        {
            if (line[1] == ':')
            {
                continue;
            }

            // Estimate address increment
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
                    addr += 52;
                }
                else
                {
                    addr += 4;
                }
            }
            else if (sec == DATA)
            {
                addr += 4;
            }
            continue;
        }

        // If we get here, line format is invalid
        if (line[0] == ' ')
        {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
        }
        else
        {
            fprintf(stderr, "Error: invalid line format\n");
        }
        hadError = 1;
        return;
    }
}

// NEW FUNCTION: Validate all instructions without writing output
void validateAllInstructions(FILE *in)
{
    char line[MAX_LINE];
    Section sec = NONE;

    while (fgets(line, sizeof(line), in))
    {
        cleanLine(line);

        if (line[0] == '\0')
            continue;

        if (line[0] == ' ')
        {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            hadError = 1;
            return;
        }

        if (line[0] == '.')
        {
            if (strcmp(line, ".code") == 0)
            {
                sec = CODE;
                continue;
            }
            else if (strcmp(line, ".data") == 0)
            {
                sec = DATA;
                continue;
            }
            else
            {
                fprintf(stderr, "Error: invalid directive '%s'\n", line);
                hadError = 1;
                return;
            }
        }

        if (line[0] == ':')
            continue;

        if (line[0] == '\t')
        {
            if (line[1] == ':')
                continue;

            char buf[MAX_LINE];
            strcpy(buf, &line[1]);

            char toks[MAX_TOK][MAX_TOK_LEN];
            int n = splitIntoTokens(buf, toks);

            if (n == 0)
                continue;

            if (sec == CODE)
            {
                // Validate macros
                if (strcmp(toks[0], "halt") == 0)
                {
                    checkMacroArgumentCount("halt", 0, n);
                    if (hadError)
                        return;
                }
                else if (strcmp(toks[0], "in") == 0)
                {
                    checkMacroArgumentCount("in", 2, n);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[1]);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[2]);
                    if (hadError)
                        return;
                }
                else if (strcmp(toks[0], "out") == 0)
                {
                    checkMacroArgumentCount("out", 2, n);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[1]);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[2]);
                    if (hadError)
                        return;
                }
                else if (strcmp(toks[0], "clr") == 0)
                {
                    checkMacroArgumentCount("clr", 1, n);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[1]);
                    if (hadError)
                        return;
                }
                else if (strcmp(toks[0], "push") == 0)
                {
                    checkMacroArgumentCount("push", 1, n);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[1]);
                    if (hadError)
                        return;
                }
                else if (strcmp(toks[0], "pop") == 0)
                {
                    checkMacroArgumentCount("pop", 1, n);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[1]);
                    if (hadError)
                        return;
                }
                else if (strcmp(toks[0], "ld") == 0)
                {
                    checkMacroArgumentCount("ld", 2, n);
                    if (hadError)
                        return;
                    getRegisterNumber(toks[1]);
                    if (hadError)
                        return;

                    if (toks[2][0] != ':' && checkIfNegative(toks[2]))
                    {
                        fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
                        hadError = 1;
                        return;
                    }

                    else if (strcmp(toks[0], "ld") == 0)
                    {
                        checkMacroArgumentCount("ld", 2, n);
                        if (hadError)
                            return;

                        int reg = getRegisterNumber(toks[1]);
                        if (hadError)
                            return;

                        if (toks[2][0] != ':' && checkIfNegative(toks[2]))
                        {
                            fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
                            hadError = 1;
                            return;
                        }

                        // NEW: Additional validation
                        if (toks[2][0] != ':')
                        {
                            // Check if it's a valid number format
                            char *endptr = NULL;
                            errno = 0; // Need to add: #include <errno.h> at top
                            uint64_t val = strtoull(toks[2], &endptr, 0);

                            if (errno == ERANGE || (endptr && *endptr != '\0'))
                            {
                                fprintf(stderr, "Error: invalid numeric literal '%s'\n", toks[2]);
                                hadError = 1;
                                return;
                            }
                        }

                        convertToNumber(toks[2]);
                        if (hadError)
                            return;
                    }
                }
                else
                {
                    // Validate regular instructions - find instruction type
                    int found = 0;
                    InstrType type = R;
                    int exp = 0;

                    for (int i = 0; i < tableSize; i++)
                    {
                        if (strcmp(instrTable[i].name, toks[0]) == 0)
                        {
                            type = instrTable[i].type;
                            exp = instrTable[i].numOps;
                            found = 1;
                            break;
                        }
                    }

                    if (!found)
                    {
                        fprintf(stderr, "Error: invalid instruction '%s'\n", toks[0]);
                        hadError = 1;
                        return;
                    }

                    // Check operand count
                    int ops = n - 1;
                    if (type == MOV)
                    {
                        if (ops < 2 || ops > 3)
                        {
                            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", ops);
                            hadError = 1;
                            return;
                        }
                        else if (type == MOV)
                        {
                            if (n < 3 || n > 4)
                            {
                                fprintf(stderr, "Error: invalid mov format\n");
                                hadError = 1;
                                return;
                            }

                            // validate n==3 cases more strictly
                            if (n == 3)
                            {
                                // Must be: mov rd, rs OR mov rd, imm
                                // First operand MUST be a register
                                if (toks[1][0] != 'r')
                                {
                                    fprintf(stderr, "Error: invalid mov format\n");
                                    hadError = 1;
                                    return;
                                }

                                if (toks[2][0] == 'r')
                                {
                                    // mov rd, rs
                                    getRegisterNumber(toks[1]);
                                    if (hadError)
                                        return;
                                    getRegisterNumber(toks[2]);
                                    if (hadError)
                                        return;
                                }
                                else
                                {
                                    // mov rd, imm
                                    getRegisterNumber(toks[1]);
                                    if (hadError)
                                        return;

                                    if (checkIfNegative(toks[2]))
                                    {
                                        fprintf(stderr, "Error: unsigned literal cannot be negative\n");
                                        hadError = 1;
                                        return;
                                    }

                                    uint64_t val = convertToNumber(toks[2]);
                                    if (hadError)
                                        return;

                                    if (toks[2][0] != ':' && val > 4095)
                                    {
                                        fprintf(stderr, "Error: literal out of range\n");
                                        hadError = 1;
                                        return;
                                    }
                                }
                            }
                            else if (n == 4)
                            {
                                // Must be: mov rd, (rs)(imm) OR mov (rd)(imm), rs
                                // Both patterns require first token to be a register
                                if (toks[1][0] != 'r')
                                {
                                    fprintf(stderr, "Error: invalid mov format\n");
                                    hadError = 1;
                                    return;
                                }

                                // Check which pattern based on tokens 2 and 3
                                if (toks[2][0] == 'r' && toks[3][0] != 'r')
                                {
                                    // mov rd, (rs)(imm) pattern
                                    getRegisterNumber(toks[1]);
                                    if (hadError)
                                        return;
                                    getRegisterNumber(toks[2]);
                                    if (hadError)
                                        return;

                                    uint64_t val = convertToNumber(toks[3]);
                                    if (hadError)
                                        return;

                                    if (toks[3][0] != ':')
                                    {
                                        int64_t sval = (int64_t)val;
                                        if (sval < -2048 || sval > 2047)
                                        {
                                            fprintf(stderr, "Error: literal out of range\n");
                                            hadError = 1;
                                            return;
                                        }
                                    }
                                }
                                else if (toks[2][0] != 'r' && toks[3][0] == 'r')
                                {
                                    // mov (rd)(imm), rs pattern
                                    getRegisterNumber(toks[1]);
                                    if (hadError)
                                        return;

                                    uint64_t val = convertToNumber(toks[2]);
                                    if (hadError)
                                        return;

                                    getRegisterNumber(toks[3]);
                                    if (hadError)
                                        return;

                                    if (toks[2][0] != ':')
                                    {
                                        int64_t sval = (int64_t)val;
                                        if (sval < -2048 || sval > 2047)
                                        {
                                            fprintf(stderr, "Error: literal out of range\n");
                                            hadError = 1;
                                            return;
                                        }
                                    }
                                }
                                else
                                {
                                    // Invalid pattern: both registers or both literals
                                    fprintf(stderr, "Error: invalid mov format\n");
                                    hadError = 1;
                                    return;
                                }
                            }
                        }
                    }
                    else if (ops != exp)
                    {
                        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n",
                                toks[0], exp, ops);
                        hadError = 1;
                        return;
                    }

                    // Validate operands based on type
                    if (type == R)
                    {
                        getRegisterNumber(toks[1]);
                        if (hadError)
                            return;
                        getRegisterNumber(toks[2]);
                        if (hadError)
                            return;
                        getRegisterNumber(toks[3]);
                        if (hadError)
                            return;
                    }
                    else if (type == I)
                    {
                        getRegisterNumber(toks[1]);
                        if (hadError)
                            return;

                        int uns = (strcmp(toks[0], "addi") == 0 || strcmp(toks[0], "subi") == 0 ||
                                   strcmp(toks[0], "shftli") == 0 || strcmp(toks[0], "shftri") == 0);

                        if (toks[2][0] == ':')
                        {
                            fprintf(stderr, "Error: label not allowed as immediate\n");
                            hadError = 1;
                            return;
                        }

                        if (uns && checkIfNegative(toks[2]))
                        {
                            fprintf(stderr, "Error: unsigned instruction cannot have negative literal\n");
                            hadError = 1;
                            return;
                        }

                        uint64_t val = convertToNumber(toks[2]);
                        if (hadError)
                            return;

                        if (uns && val > 4095)
                        {
                            fprintf(stderr, "Error: literal out of range\n");
                            hadError = 1;
                            return;
                        }
                        else if (!uns)
                        {
                            int64_t sval = (int64_t)val;
                            if (sval < -2048 || sval > 2047)
                            {
                                fprintf(stderr, "Error: literal out of range\n");
                                hadError = 1;
                                return;
                            }
                        }
                    }
                    else if (type == OTHER)
                    {
                        getRegisterNumber(toks[1]);
                        if (hadError)
                            return;
                        getRegisterNumber(toks[2]);
                        if (hadError)
                            return;
                    }
                    else if (type == BR)
                    {
                        if (strcmp(toks[0], "brr") == 0 && toks[1][0] != 'r')
                        {
                            uint64_t val = convertToNumber(toks[1]);
                            if (hadError)
                                return;

                            if (toks[1][0] != ':')
                            {
                                int64_t sval = (int64_t)val;
                                if (sval < -2048 || sval > 2047)
                                {
                                    fprintf(stderr, "Error: literal out of range\n");
                                    hadError = 1;
                                    return;
                                }
                            }
                        }
                        else
                        {
                            getRegisterNumber(toks[1]);
                            if (hadError)
                                return;
                        }
                    }
                    else if (type == PRIV)
                    {
                        getRegisterNumber(toks[1]);
                        if (hadError)
                            return;
                        getRegisterNumber(toks[2]);
                        if (hadError)
                            return;
                        getRegisterNumber(toks[3]);
                        if (hadError)
                            return;

                        uint64_t val = convertToNumber(toks[4]);
                        if (hadError)
                            return;

                        if (toks[4][0] != ':' && val > 4095)
                        {
                            fprintf(stderr, "Error: literal out of range\n");
                            hadError = 1;
                            return;
                        }
                    }
                    else if (type == MOV)
                    {
                        if (n < 3 || n > 4)
                        {
                            fprintf(stderr, "Error: invalid mov format\n");
                            hadError = 1;
                            return;
                        }

                        if (n == 3 && toks[1][0] == 'r' && toks[2][0] == 'r')
                        {
                            getRegisterNumber(toks[1]);
                            if (hadError)
                                return;
                            getRegisterNumber(toks[2]);
                            if (hadError)
                                return;
                        }
                        else if (n == 3 && toks[1][0] == 'r' && toks[2][0] != 'r')
                        {
                            getRegisterNumber(toks[1]);
                            if (hadError)
                                return;

                            if (toks[2][0] != ':' && checkIfNegative(toks[2]))
                            {
                                fprintf(stderr, "Error: unsigned literal cannot be negative\n");
                                hadError = 1;
                                return;
                            }

                            uint64_t val = convertToNumber(toks[2]);
                            if (hadError)
                                return;

                            if (toks[2][0] != ':' && val > 4095)
                            {
                                fprintf(stderr, "Error: literal out of range\n");
                                hadError = 1;
                                return;
                            }
                        }
                        else if (n == 4 && toks[1][0] == 'r' && toks[2][0] == 'r')
                        {
                            getRegisterNumber(toks[1]);
                            if (hadError)
                                return;
                            getRegisterNumber(toks[2]);
                            if (hadError)
                                return;

                            uint64_t val = convertToNumber(toks[3]);
                            if (hadError)
                                return;

                            if (toks[3][0] != ':')
                            {
                                int64_t sval = (int64_t)val;
                                if (sval < -2048 || sval > 2047)
                                {
                                    fprintf(stderr, "Error: literal out of range\n");
                                    hadError = 1;
                                    return;
                                }
                            }
                        }
                        else if (n == 4 && toks[1][0] == 'r' && toks[3][0] == 'r')
                        {
                            getRegisterNumber(toks[1]);
                            if (hadError)
                                return;

                            uint64_t val = convertToNumber(toks[2]);
                            if (hadError)
                                return;

                            getRegisterNumber(toks[3]);
                            if (hadError)
                                return;

                            if (toks[2][0] != ':')
                            {
                                int64_t sval = (int64_t)val;
                                if (sval < -2048 || sval > 2047)
                                {
                                    fprintf(stderr, "Error: literal out of range\n");
                                    hadError = 1;
                                    return;
                                }
                            }
                        }
                        else
                        {
                            fprintf(stderr, "Error: invalid mov format\n");
                            hadError = 1;
                            return;
                        }
                    }
                }
            }
            else if (sec == DATA)
            {
                if (checkIfNegative(buf))
                {
                    fprintf(stderr, "Error: data values must be unsigned\n");
                    hadError = 1;
                    return;
                }

                char *endptr = NULL;
                errno = 0;
                uint64_t val = strtoull(buf, &endptr, 0);

                if (errno == ERANGE || (endptr && *endptr != '\0') || (endptr == buf))
                {
                    fprintf(stderr, "Error: invalid data value '%s'\n", buf);
                    hadError = 1;
                    return;
                }

                // Data values are stored as 32-bit, so check range
                if (val > UINT32_MAX)
                {
                    fprintf(stderr, "Error: data value out of range\n");
                    hadError = 1;
                    return;
                }
            }
        }
        else
        {
            fprintf(stderr, "Error: invalid line format\n");
            hadError = 1;
            return;
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

        // Check for invalid leading spaces (not tabs)
        if (line[0] == ' ')
        {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            hadError = 1;
            return;
        }

        // Handle directives
        if (line[0] == '.')
        {
            if (strcmp(line, ".code") == 0)
            {
                sec = CODE;
                if (lastWritten != CODE)
                {
                    fprintf(mid, ".code\n");
                    lastWritten = CODE;
                }
                continue;
            }
            else if (strcmp(line, ".data") == 0)
            {
                sec = DATA;
                if (lastWritten != DATA)
                {
                    fprintf(mid, ".data\n");
                    lastWritten = DATA;
                }
                continue;
            }
            else
            {
                fprintf(stderr, "Error: invalid directive '%s'\n", line);
                hadError = 1;
                return;
            }
        }

        // Handle labels
        if (line[0] == ':')
        {
            // Check if there's a tab after the label (label not alone on line)
            if (strchr(line, '\t'))
            {
                fprintf(stderr, "Error: label must be alone on its line\n");
                hadError = 1;
                return;
            }
            // Labels already collected, skip
            continue;
        }

        // Handle instructions/data
        if (line[0] == '\t')
        {
            handleTabLine(mid, line, sec, &addr);
            if (hadError)
                return;
        }
        else
        {
            // Line doesn't start with '.', ':', '\t', or ' ' (space already handled)
            // This is an error - likely a malformed label or instruction
            fprintf(stderr, "Error: invalid line format\n");
            hadError = 1;
            return;
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
            return;
        }
        return;
    }

    if (ops != exp)
    {
        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n",
                name, exp, ops);
        hadError = 1;
        return;
    }
}

void encodeRType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt)
{
    *rd = getRegisterNumber(toks[1]);
    if (hadError)
        return;
    *rs = getRegisterNumber(toks[2]);
    if (hadError)
        return;
    *rt = getRegisterNumber(toks[3]);
}

void encodeIType(char toks[MAX_TOK][MAX_TOK_LEN], const char *instr, uint32_t *rd, uint32_t *imm)
{
    *rd = getRegisterNumber(toks[1]);
    if (hadError)
        return;

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
        return;
    }

    if (uns && toks[2][0] != ':' && checkIfNegative(toks[2]))
    {
        fprintf(stderr, "Error: unsigned instruction cannot have negative literal\n");
        hadError = 1;
        return;
    }

    if (toks[2][0] == ':')
    {
        fprintf(stderr, "Error: label not allowed as immediate\n");
        hadError = 1;
        return;
    }

    uint64_t val = convertToNumber(toks[2]);
    if (hadError)
        return;

    if (toks[2][0] != ':')
    {
        if (uns && val > 4095)
        {
            fprintf(stderr, "Error: literal out of range\n");
            hadError = 1;
            return;
        }
        else if (!uns)
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                hadError = 1;
                return;
            }
        }
    }

    *imm = (uint32_t)(val & 0xFFF);
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
        if (hadError)
            return;

        if (toks[1][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                hadError = 1;
                return;
            }
        }

        *imm = (uint32_t)(val & 0xFFF);
    }
    else
    {
        *rd = getRegisterNumber(toks[1]);
        if (hadError)
            return;
        *imm = 0;
    }
}

void encodePrivType(char toks[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt, uint32_t *imm)
{
    *rd = getRegisterNumber(toks[1]);
    if (hadError)
        return;
    *rs = getRegisterNumber(toks[2]);
    if (hadError)
        return;
    *rt = getRegisterNumber(toks[3]);
    if (hadError)
        return;

    uint64_t val = convertToNumber(toks[4]);
    if (hadError)
        return;

    if (toks[4][0] != ':' && val > 4095)
    {
        fprintf(stderr, "Error: literal out of range\n");
        hadError = 1;
        return;
    }

    *imm = (uint32_t)(val & 0xFFF);
}

void encodeMovType(char toks[MAX_TOK][MAX_TOK_LEN], int n, uint32_t *op, uint32_t *rd, uint32_t *rs, uint32_t *imm)
{
    if (n < 3 || n > 4)
    {
        fprintf(stderr, "Error: invalid mov format\n");
        hadError = 1;
        return;
    }

    if (n == 3 && toks[1][0] == 'r' && toks[2][0] == 'r')
    {
        *op = 0x11;
        *rd = getRegisterNumber(toks[1]);
        if (hadError)
            return;
        *rs = getRegisterNumber(toks[2]);
        if (hadError)
            return;
        *imm = 0;
    }
    else if (n == 3 && toks[1][0] == 'r' && toks[2][0] != 'r')
    {
        *op = 0x12;
        *rd = getRegisterNumber(toks[1]);
        if (hadError)
            return;

        if (toks[2][0] != ':' && checkIfNegative(toks[2]))
        {
            fprintf(stderr, "Error: unsigned literal cannot be negative\n");
            hadError = 1;
            return;
        }

        uint64_t val = convertToNumber(toks[2]);
        if (hadError)
            return;

        if (toks[2][0] != ':' && val > 4095)
        {
            fprintf(stderr, "Error: literal out of range\n");
            hadError = 1;
            return;
        }

        *imm = (uint32_t)(val & 0xFFF);
    }
    else if (n == 4 && toks[1][0] == 'r' && toks[2][0] == 'r')
    {
        *op = 0x10;
        *rd = getRegisterNumber(toks[1]);
        if (hadError)
            return;
        *rs = getRegisterNumber(toks[2]);
        if (hadError)
            return;

        uint64_t val = convertToNumber(toks[3]);
        if (hadError)
            return;

        if (toks[3][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                hadError = 1;
                return;
            }
        }

        *imm = (uint32_t)(val & 0xFFF);
    }
    else if (n == 4 && toks[1][0] == 'r' && toks[3][0] == 'r')
    {
        *op = 0x13;
        *rd = getRegisterNumber(toks[1]);
        if (hadError)
            return;

        uint64_t val = convertToNumber(toks[2]);
        if (hadError)
            return;

        *rs = getRegisterNumber(toks[3]);
        if (hadError)
            return;

        if (toks[2][0] != ':')
        {
            int64_t sval = (int64_t)val;
            if (sval < -2048 || sval > 2047)
            {
                fprintf(stderr, "Error: literal out of range\n");
                hadError = 1;
                return;
            }
        }

        *imm = (uint32_t)(val & 0xFFF);
    }
    else
    {
        fprintf(stderr, "Error: invalid mov format\n");
        hadError = 1;
        return;
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
    return R; // Return dummy value
}

uint32_t assembleInstruction(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm)
{
    uint32_t res = 0;
    res |= (imm & 0xFFF);     // bits 0-11
    res |= (rt & 0x1F) << 12; // bits 12-16 (5 bits)
    res |= (rs & 0x1F) << 17; // bits 17-21 (5 bits)
    res |= (rd & 0x1F) << 22; // bits 22-26 (5 bits)
    res |= (op & 0x3F) << 27; // bits 27-32 (6 bits)
    return res;
}

uint32_t convertToMachineCode(char toks[MAX_TOK][MAX_TOK_LEN], int n)
{
    uint32_t op = 0, rd = 0, rs = 0, rt = 0, imm = 0;
    int exp = 0;

    InstrType type = findInstructionInfo(toks[0], &op, &exp);
    if (hadError)
        return 0;

    checkInstructionOperands(toks[0], exp, n, type);
    if (hadError)
        return 0;

    if (n != exp + 1 && type != MOV)
    {
        fprintf(stderr, "Error: wrong number of operands for '%s'\n", toks[0]);
        hadError = 1;
        return 0;
    }

    if (type == R)
    {
        encodeRType(toks, &rd, &rs, &rt);
        if (hadError)
            return 0;
    }
    else if (type == I)
    {
        encodeIType(toks, toks[0], &rd, &imm);
        if (hadError)
            return 0;
    }
    else if (type == OTHER)
    {
        rd = getRegisterNumber(toks[1]);
        if (hadError)
            return 0;
        rs = getRegisterNumber(toks[2]);
        if (hadError)
            return 0;
    }
    else if (type == BR)
    {
        encodeBranchType(toks, &op, &rd, &imm);
        if (hadError)
            return 0;
    }
    else if (type == PRIV)
    {
        encodePrivType(toks, &rd, &rs, &rt, &imm);
        if (hadError)
            return 0;
    }
    else if (type == MOV)
    {
        encodeMovType(toks, n, &op, &rd, &rs, &imm);
        if (hadError)
            return 0;
    }

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
        return;
    }

    if (n > MAX_TOK)
    {
        fprintf(stderr, "Error: too many operands\n");
        hadError = 1;
        return;
    }

    uint32_t mc = convertToMachineCode(toks, n);
    if (hadError)
        return;

    // Write in little-endian byte order (LSB first)
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
        return;
    }

    uint64_t val = convertToNumber(buf);
    if (hadError)
        return;

    uint32_t v = (uint32_t)val;

    // Write in little-endian byte order (LSB first)
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

        // Check for invalid leading spaces (not tabs)
        if (line[0] == ' ')
        {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            hadError = 1;
            return;
        }

        // Handle directives
        if (line[0] == '.')
        {
            if (strcmp(line, ".code") == 0)
            {
                sec = CODE;
                continue;
            }
            else if (strcmp(line, ".data") == 0)
            {
                sec = DATA;
                continue;
            }
            else
            {
                fprintf(stderr, "Error: invalid directive '%s'\n", line);
                hadError = 1;
                return;
            }
        }

        // Handle labels (skip them - they were already processed)
        if (line[0] == ':')
        {
            continue;
        }

        // Handle instructions/data
        if (line[0] == '\t')
        {
            if (line[1] == ':')
                continue;

            if (sec == NONE)
            {
                fprintf(stderr, "Error: instr/data outside of .code/.data section\n");
                hadError = 1;
                return;
            }

            if (sec == CODE)
            {
                writeCodeInstr(out, line);
                if (hadError)
                    return;
            }
            else if (sec == DATA)
            {
                writeDataValue(out, line);
                if (hadError)
                    return;
            }

            continue;
        }

        // If we get here, line format is invalid
        fprintf(stderr, "Error: invalid line format\n");
        hadError = 1;
        return;
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

    // STEP 1: Collect all labels
    collectLabels(fIn);
    if (hadError)
    {
        fclose(fIn);
        return 1;
    }
    fseek(fIn, 0, SEEK_SET);

    // STEP 2: Validate all instructions (WITHOUT writing output)
    validateAllInstructions(fIn);
    if (hadError)
    {
        fclose(fIn);
        return 1;
    }
    fseek(fIn, 0, SEEK_SET);

    // STEP 3: NOW create intermediate file and write to it
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
        remove(argv[2]);
        return 1;
    }
    fclose(fIn);
    fseek(fMid, 0, SEEK_SET);

    // STEP 4: Create binary output
    FILE *fOut = fopen(argv[3], "wb");
    if (!fOut)
    {
        fprintf(stderr, "Error: cannot open output file\n");
        fclose(fMid);
        remove(argv[2]);
        return 1;
    }

    secondPass(fMid, fOut);
    if (hadError)
    {
        fclose(fMid);
        fclose(fOut);
        remove(argv[2]);
        remove(argv[3]);
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
        if (argc == 4)
        {
            remove(argv[2]);
            remove(argv[3]);
        }
    }
    return hadError ? 1 : ret;
}