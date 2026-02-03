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

typedef enum { 
    R,      // rd, rs, rt
    I,      // rd, imm
    BR,     // br variants
    MOV,    // special handling for mov
    PRIV,   // rd, rs, rt, imm
    NO_OP,  // return, etc
    OTHER     // rd, rs
} InstrType;

typedef struct {
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
    {"brnz", 0x0b, OTHER, 2}, {"call", 0x0c, R, 3},
    {"return", 0x0d, NO_OP, 0}, {"brgt", 0x0e, R, 3},
    {"priv", 0x0f, PRIV, 4}, {"mov", 0x10, MOV, 2},
    {"addf", 0x14, R, 3}, {"subf", 0x15, R, 3},
    {"mulf", 0x16, R, 3}, {"divf", 0x17, R, 3},
    {"add", 0x18, R, 3}, {"addi", 0x19, I, 2},
    {"sub", 0x1a, R, 3}, {"subi", 0x1b, I, 2},
    {"mul", 0x1c, R, 3}, {"div",  0x1d, R, 3}
};

int tableSize = sizeof(instrTable) / sizeof(InstrInfo);

typedef enum { NONE, CODE, DATA } Section;

typedef struct {
    char name[256];
    uint64_t addr;
} Label;

Label labels[MAX_LABELS];
int labelCount = 0;

// add labels, check for duplicates
void addLabel(const char *name, uint64_t addr) {
    int i;
    for (i = 0; i < labelCount; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            fprintf(stderr, "Error: duplicate label");
            exit(1);
        }
    }
    // prevent overflow of label array
    if (labelCount < MAX_LABELS) {
        strcpy(labels[labelCount].name, name);
        labels[labelCount].addr = addr;
        labelCount++;
    }
}

uint64_t lookupLabel(const char *name) {
    int i;
    for (i = 0; i < labelCount; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Error: undefined label\n");
    exit(1);
}

// remove comments and trailing whitespace
void trim(char *s) {
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] == ';') {
            s[i] = '\0';
            break;
        }
    }
    // strip newline
    int len = strlen(s);
    if (len > 0 && s[len-1] == '\n') s[len-1] = '\0';
}

int tokenize(char *line, char tok[MAX_TOK][MAX_TOK_LEN]) {
    int n = 0;
    // replace punctuation with spaces
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == ',' || line[i] == '(' || line[i] == ')') {
            line[i] = ' ';
        }
    }

    char *p = strtok(line, " \t");
    while (p != NULL && n < MAX_TOK) {
        strcpy(tok[n], p);
        n++;
        p = strtok(NULL, " \t");
    }
    return n;
}

int parseReg(const char *s) {
    if (s[0] != 'r') {
        fprintf(stderr, "Error: invalid register\n");
        exit(1);
    }
    // skip 'r' and get the number
    int r = atoi(&s[1]);
    if (r < 0 || r > 31) {
        fprintf(stderr, "Error: register out of range\n");
        exit(1);
    }
    return r;
}

uint64_t parseLiteral(const char *s) {
    if (s[0] == ':') {
        return lookupLabel(&s[1]);
    }
    return strtoull(s, NULL, 0); 
}

// LD is macro -> break into smaller instructions
void expandld(FILE *out, int rd, uint64_t val) {
    fprintf(out, "\txor r%d, r%d, r%d\n", rd, rd, rd);
    
    // break the 64-bit value into 12-bit chunks for addi
    for (int i = 52; i >= 0; i -= 12) {
        uint64_t chunk = (val >> i) & 0xFFF;
        fprintf(out, "\taddi r%d, %llu\n", rd, (unsigned long long)chunk);
        if (i > 0) {
            fprintf(out, "\tshftli r%d, 12\n", rd);
        }
    }
}

void pass1(FILE *in, FILE *out) {
    char line[MAX_LINE];
    Section sec = NONE;
    uint64_t pc = CODE_START;

    while (fgets(line, sizeof(line), in)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '\n') continue;

        if (strcmp(line, ".code") == 0) {
            sec = CODE;
            fprintf(out, ".code\n");
            continue;
        }
        if (strcmp(line, ".data") == 0) {
            sec = DATA;
            fprintf(out, ".data\n");
            continue;
        }

        // label definition
        if (line[0] == ':') {
            addLabel(&line[1], pc);
            continue;
        }

        // instruction or data
        if (line[0] == '\t') {
            char buf[MAX_LINE];
            strcpy(buf, &line[1]);
            char tok[MAX_TOK][MAX_TOK_LEN];
            int n = tokenize(buf, tok);
            if (n == 0) continue;

            if (sec == CODE) {
                // check for macros first
                if (strcmp(tok[0], "halt") == 0) {
                    fprintf(out, "\tpriv r0, r0, r0, 0\n");
                    pc += 4;
                } else if (strcmp(tok[0], "in") == 0) {
                    fprintf(out, "\tpriv %s, %s, r0, 3\n", tok[1], tok[2]);
                    pc += 4;
                } else if (strcmp(tok[0], "out") == 0) {
                    fprintf(out, "\tpriv %s, %s, r0, 4\n", tok[1], tok[2]);
                    pc += 4;
                } else if (strcmp(tok[0], "clr") == 0) {
                    fprintf(out, "\txor %s, %s, %s\n", tok[1], tok[1], tok[1]);
                    pc += 4;
                } else if (strcmp(tok[0], "push") == 0) {
                    fprintf(out, "\tsubi r31, 8\n");
                    fprintf(out, "\tmov (r31)(0), %s\n", tok[1]);
                    pc += 8;
                } else if (strcmp(tok[0], "pop") == 0) {
                    fprintf(out, "\tmov %s, (r31)(0)\n", tok[1]);
                    fprintf(out, "\taddi r31, 8\n");
                    pc += 8;
                } else if (strcmp(tok[0], "ld") == 0) {
                    uint64_t val = parseLiteral(tok[2]);
                    expandld(out, parseReg(tok[1]), val);
                    pc += (4 * 11); 
                } else {
                    // regular instruction
                    fprintf(out, "\t%s\n", &line[1]);
                    pc += 4;
                }
            } else if (sec == DATA) {
                fprintf(out, "\t%s\n", &line[1]);
                pc += 8; // data is 8 bytes
            }
        }
    }
}

uint32_t encode(char tok[MAX_TOK][MAX_TOK_LEN], int n) {
    uint32_t op = 0, rd = 0, rs = 0, rt = 0, imm = 0;
    InstrType type = NO_OP;
    int tableIndex = -1;

    // find loc of instruciton in table
    for (int i = 0; i < tableSize; i++) {
        if (strcmp(tok[0], instrTable[i].name) == 0) {
            op = instrTable[i].opcode;
            type = instrTable[i].type;
            tableIndex = i;
            break;
        }
    }

    if (tableIndex == -1) { 
        fprintf(stderr, "Error: invalid instruction\n"); 
        exit(1); 
    }

    switch (type) {
        case R:
            rd = parseReg(tok[1]);
            rs = parseReg(tok[2]);
            rt = parseReg(tok[3]);
            break;

        case I:
            rd = parseReg(tok[1]);
            imm = (uint32_t)parseLiteral(tok[2]);
            break;

        case OTHER:
            rd = parseReg(tok[1]);
            rs = parseReg(tok[2]);
            break;

        case BR:
            // changes opcodes based on input
            if (strcmp(tok[0], "brr") == 0 && tok[1][0] != 'r') {
                op = 0x0a; 
                imm = (uint32_t)parseLiteral(tok[1]);
            } else {
                rd = parseReg(tok[1]);
            }
            break;

        case PRIV:
            rd = parseReg(tok[1]);
            rs = parseReg(tok[2]);
            rt = parseReg(tok[3]);
            imm = (uint32_t)parseLiteral(tok[4]);
            break;

        case MOV:
            // MOV formats
            if (tok[1][0] == 'r' && tok[2][0] == 'r') { 
                op = 0x11; rd = parseReg(tok[1]); rs = parseReg(tok[2]); 
            } else if (tok[1][0] == 'r' && n == 3) { 
                op = 0x12; rd = parseReg(tok[1]); imm = (uint32_t)parseLiteral(tok[2]); 
            } else if (tok[1][0] == 'r') { 
                op = 0x10; rd = parseReg(tok[1]); rs = parseReg(tok[2]); imm = (uint32_t)parseLiteral(tok[3]); 
            } else { 
                op = 0x13; rd = parseReg(tok[1]); rs = parseReg(tok[2]); imm = (uint32_t)parseLiteral(tok[3]); 
            }
            break;

        case NO_OP:
            // if return, nothing to parse
            break;

        default:
            fprintf(stderr, "Error: invalid instruction\n");
            exit(1);
    }

    // combine everything into one 32-bit word using shifts
    uint32_t res = 0;
    res |= (op << 26);
    res |= ((rd & 0x1F) << 21);
    res |= ((rs & 0x1F) << 16);
    res |= ((rt & 0x1F) << 11);
    res |= (imm & 0x7FF);
    return res;
}

void pass2(FILE *in, FILE *out) {
    char line[MAX_LINE];
    Section sec = NONE;

    while (fgets(line, sizeof(line), in)) {
        trim(line);
        if (line[0] == '\0') continue;

        if (strcmp(line, ".code") == 0) { sec = CODE; continue; }
        if (strcmp(line, ".data") == 0) { sec = DATA; continue; }

        if (line[0] == '\t') {
            if (sec == CODE) {
                char buf[MAX_LINE];
                strcpy(buf, &line[1]);
                char tok[MAX_TOK][MAX_TOK_LEN];
                int n = tokenize(buf, tok);
                uint32_t instr = encode(tok, n);
                fwrite(&instr, 4, 1, out);
            } else if (sec == DATA) {
                uint64_t val = parseLiteral(&line[1]);
                fwrite(&val, 8, 1, out);
            }
        }
    }
}

int testmain(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Error: invalid number of args\n");
        return 1;
    }

    FILE *f_in = fopen(argv[1], "r");
    FILE *f_mid = fopen(argv[2], "w+");
    FILE *f_out = fopen(argv[3], "wb");

    if (!f_in || !f_mid || !f_out) {
        printf("Error: problem opening files\n");
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

int main(int argc, char **argv){
    return testmain(argc, argv);
}