#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#define MAX_LINE 512
#define MAX_TOK 8
#define MAX_TOK_LEN 64
#define MAX_LBL 512
#define CODE_START 0x1000

int err = 0;  // error flag

// instruction types
typedef enum {
    R, I, BR, MOV, PRIV, NO_OP, OTHER
} InstrType;

// instruction info
typedef struct {
    char name[16];
    uint32_t op;
    InstrType type;
    int nops;
} InstrInfo;

// instruction table
InstrInfo instrs[] = {
    {"and", 0x00, R, 3}, {"or", 0x01, R, 3}, {"xor", 0x02, R, 3}, 
    {"not", 0x03, OTHER, 2}, {"shftr", 0x04, R, 3}, {"shftri", 0x05, I, 2}, 
    {"shftl", 0x06, R, 3}, {"shftli", 0x07, I, 2}, {"br", 0x08, BR, 1}, 
    {"brr", 0x09, BR, 1}, {"brnz", 0x0b, OTHER, 2}, {"call", 0x0c, BR, 1}, 
    {"return", 0x0d, NO_OP, 0}, {"brgt", 0x0e, R, 3}, {"priv", 0x0f, PRIV, 4}, 
    {"mov", 0x10, MOV, 2}, {"addf", 0x14, R, 3}, {"subf", 0x15, R, 3}, 
    {"mulf", 0x16, R, 3}, {"divf", 0x17, R, 3}, {"add", 0x18, R, 3}, 
    {"addi", 0x19, I, 2}, {"sub", 0x1a, R, 3}, {"subi", 0x1b, I, 2}, 
    {"mul", 0x1c, R, 3}, {"div", 0x1d, R, 3}
};
int ninstr = sizeof(instrs) / sizeof(InstrInfo);

// section types
typedef enum { NONE, CODE, DATA } Sec;

// label storage
typedef struct {
    char name[256];
    uint64_t addr;
} Lbl;

Lbl lbls[MAX_LBL];
int nlbl = 0;

// add label with dup check
void addLbl(const char *n, uint64_t a) {
    for (int i = 0; i < nlbl; i++) {
        if (strcmp(lbls[i].name, n) == 0) {
            fprintf(stderr, "Error: duplicate label '%s'\n", n);
            err = 1;
            return;
        }
    }
    if (nlbl >= MAX_LBL) {
        fprintf(stderr, "Error: too many labels\n");
        err = 1;
        return;
    }
    if (strlen(n) > 255) {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        err = 1;
        return;
    }
    strcpy(lbls[nlbl].name, n);
    lbls[nlbl].addr = a;
    nlbl++;
}

// find label address
uint64_t findLbl(const char *n) {
    for (int i = 0; i < nlbl; i++) {
        if (strcmp(lbls[i].name, n) == 0)
            return lbls[i].addr;
    }
    fprintf(stderr, "Error: undefined label '%s'\n", n);
    err = 1;
    return 0;
}

// strip comments and newlines
void clean(char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == ';') {
            s[i] = '\0';
            break;
        }
    }
    int len = strlen(s);
    if (len > 0 && s[len - 1] == '\n')
        s[len - 1] = '\0';
}

// tokenize: remove parens/commas, split on space/tab
int tokenize(char *s, char t[MAX_TOK][MAX_TOK_LEN]) {
    int n = 0;
    // replace punctuation with spaces
    for (int i = 0; s[i]; i++) {
        if (s[i] == ',' || s[i] == '(' || s[i] == ')')
            s[i] = ' ';
    }
    char *p = strtok(s, " \t");
    while (p && n < MAX_TOK) {
        strcpy(t[n++], p);
        p = strtok(NULL, " \t");
    }
    return n;
}

// parse register, check validity
int getReg(const char *s) {
    if (!s || s[0] != 'r') {
        fprintf(stderr, "Error: invalid register '%s'\n", s);
        err = 1;
        return -1;
    }
    // check leading zeros
    if (s[1] == '0' && s[2] != '\0') {
        fprintf(stderr, "Error: invalid register '%s' (leading zeros not allowed)\n", s);
        err = 1;
        return -1;
    }
    char *end;
    long r = strtol(s + 1, &end, 10);
    if (*end != '\0' || r < 0 || r > 31) {
        fprintf(stderr, "Error: invalid register '%s'\n", s);
        err = 1;
        return -1;
    }
    return (int)r;
}

// check if negative
int isNeg(const char *s) {
    return s[0] == '-';
}

// convert string to number, handle labels
uint64_t toNum(const char *s) {
    if (!s) {
        fprintf(stderr, "Error: NULL literal\n");
        err = 1;
        return 0;
    }
    // label reference
    if (s[0] == ':')
        return findLbl(&s[1]);
    
    // strip leading zeros for decimal
    const char *p = s;
    if (s[0] == '0' && s[1] != 'x' && s[1] != 'X' && s[1] != '\0') {
        while (*p == '0' && *(p + 1) != '\0')
            p++;
    }
    
    char *end = NULL;
    uint64_t val = strtoull(p, &end, 10);
    if (end == p || *end != '\0') {
        fprintf(stderr, "Error: invalid numeric literal '%s'\n", s);
        err = 1;
        return 0;
    }
    return val;
}

// write ld macro expansion
void writeLd(FILE *f, int rd, uint64_t v) {
    // split into 12-bit chunks
    uint32_t p[6];
    p[0] = (v >> 52) & 0xFFF;
    p[1] = (v >> 40) & 0xFFF;
    p[2] = (v >> 28) & 0xFFF;
    p[3] = (v >> 16) & 0xFFF;
    p[4] = (v >> 4) & 0xFFF;
    p[5] = v & 0xF;
    
    fprintf(f, "\txor r%d, r%d, r%d\n", rd, rd, rd);
    fprintf(f, "\taddi r%d, %u\n", rd, p[0]);
    for (int i = 1; i < 5; i++) {
        fprintf(f, "\tshftli r%d, 12\n", rd);
        fprintf(f, "\taddi r%d, %u\n", rd, p[i]);
    }
    fprintf(f, "\tshftli r%d, 4\n", rd);
    fprintf(f, "\taddi r%d, %u\n", rd, p[5]);
}

// check macro arg count
void chkMacro(const char *n, int exp, int act) {
    if (act - 1 != exp) {
        fprintf(stderr, "Error: macro '%s' expects %d argument(s), got %d\n", n, exp, act - 1);
        err = 1;
    }
}

// try expand macro, return 1 if matched
int tryMacro(FILE *f, char t[MAX_TOK][MAX_TOK_LEN], int n, uint64_t *a) {
    char *nm = t[0];
    
    if (strcmp(nm, "halt") == 0) {
        chkMacro("halt", 0, n);
        if (err) return 1;
        fprintf(f, "\tpriv r0, r0, r0, 0\n");
        *a += 4;
        return 1;
    }
    if (strcmp(nm, "in") == 0) {
        chkMacro("in", 2, n);
        if (err) return 1;
        fprintf(f, "\tpriv %s, %s, r0, 3\n", t[1], t[2]);
        *a += 4;
        return 1;
    }
    if (strcmp(nm, "out") == 0) {
        chkMacro("out", 2, n);
        if (err) return 1;
        fprintf(f, "\tpriv %s, %s, r0, 4\n", t[1], t[2]);
        *a += 4;
        return 1;
    }
    if (strcmp(nm, "clr") == 0) {
        chkMacro("clr", 1, n);
        if (err) return 1;
        fprintf(f, "\txor %s, %s, %s\n", t[1], t[1], t[1]);
        *a += 4;
        return 1;
    }
    if (strcmp(nm, "push") == 0) {
        chkMacro("push", 1, n);
        if (err) return 1;
        getReg(t[1]);
        if (err) return 1;
        fprintf(f, "\tmov (r31)(-8), %s\n", t[1]);
        fprintf(f, "\tsubi r31, 8\n");
        *a += 8;
        return 1;
    }
    if (strcmp(nm, "pop") == 0) {
        chkMacro("pop", 1, n);
        if (err) return 1;
        getReg(t[1]);
        if (err) return 1;
        fprintf(f, "\tmov %s, (r31)(0)\n", t[1]);
        fprintf(f, "\taddi r31, 8\n");
        *a += 8;
        return 1;
    }
    if (strcmp(nm, "ld") == 0) {
        chkMacro("ld", 2, n);
        if (err) return 1;
        int r = getReg(t[1]);
        if (err) return 1;
        if (t[2][0] != ':' && isNeg(t[2])) {
            fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
            err = 1;
            return 1;
        }
        uint64_t v = toNum(t[2]);
        if (err) return 1;
        writeLd(f, r, v);
        *a += 52;
        return 1;
    }
    return 0;
}

// print instruction to intermediate file
void printInstr(FILE *f, char t[MAX_TOK][MAX_TOK_LEN], int n) {
    // mov has special formatting
    if (strcmp(t[0], "mov") == 0) {
        fprintf(f, "\t%s ", t[0]);
        if (n == 3) {
            fprintf(f, "%s, %s\n", t[1], t[2]);
        } else if (n == 4) {
            int t1reg = (t[1][0] == 'r');
            int t2num = (t[2][0] == '-' || isdigit(t[2][0]) || t[2][0] == ':');
            if (t1reg && t2num) {
                fprintf(f, "(%s)(%s), %s\n", t[1], t[2], t[3]);
            } else {
                uint64_t v = (t[3][0] == ':') ? findLbl(&t[3][1]) : strtoull(t[3], NULL, 10);
                fprintf(f, "%s, (%s)(%lld)\n", t[1], t[2], (long long)(int64_t)v);
            }
        } else if (n == 5) {
            uint64_t v1 = (t[2][0] == ':') ? findLbl(&t[2][1]) : strtoull(t[2], NULL, 10);
            uint64_t v2 = (t[4][0] == ':') ? findLbl(&t[4][1]) : strtoull(t[4], NULL, 10);
            fprintf(f, "(%s)(%lld), (%s)(%lld)\n", t[1], (long long)(int64_t)v1, t[3], (long long)(int64_t)v2);
        } else {
            fprintf(stderr, "Error: unexpected mov token count %d\n", n);
            err = 1;
        }
        return;
    }
    
    // non-mov instructions
    fprintf(f, "\t%s", t[0]);
    for (int i = 1; i < n; i++) {
        if (t[i][0] == ':') {
            // label ref
            fprintf(f, "%s%llu", (i == 1 ? " " : ", "), (unsigned long long)findLbl(&t[i][1]));
        } else {
            // check if number
            int neg = (t[i][0] == '-');
            const char *ns = neg ? &t[i][1] : &t[i][0];
            if (isdigit(ns[0])) {
                // parse and print to strip leading zeros
                if (neg) {
                    fprintf(f, "%s%lld", (i == 1 ? " " : ", "), strtoll(t[i], NULL, 0));
                } else {
                    fprintf(f, "%s%llu", (i == 1 ? " " : ", "), strtoull(t[i], NULL, 10));
                }
            } else {
                // register or other
                fprintf(f, "%s%s", (i == 1 ? " " : ", "), t[i]);
            }
        }
    }
    fprintf(f, "\n");
}

// handle tab-indented line
void handleLine(FILE *f, char *ln, Sec sec, uint64_t *a) {
    char buf[MAX_LINE];
    strcpy(buf, &ln[1]);
    
    // skip inline labels
    if (buf[0] == ':')
        return;
    
    char orig[MAX_LINE];
    strcpy(orig, buf);
    
    char t[MAX_TOK][MAX_TOK_LEN];
    int n = tokenize(buf, t);
    if (n == 0)
        return;
    
    if (sec == CODE) {
        if (!tryMacro(f, t, n, a)) {
            printInstr(f, t, n);
            *a += 4;
        }
    } else if (sec == DATA) {
        uint64_t v = toNum(orig);
        fprintf(f, "\t%llu\n", (unsigned long long)v);
        *a += 4;
    }
}

// collect all labels
void collectLbls(FILE *in) {
    char ln[MAX_LINE];
    Sec sec = NONE;
    uint64_t addr = CODE_START;
    
    while (fgets(ln, sizeof(ln), in)) {
        clean(ln);
        if (ln[0] == '\0') continue;
        
        // directives
        if (ln[0] == '.') {
            if (strcmp(ln, ".code") == 0) {
                if (sec == NONE) addr = CODE_START;
                sec = CODE;
            } else if (strcmp(ln, ".data") == 0) {
                sec = DATA;
            } else {
                fprintf(stderr, "Error: invalid directive '%s'\n", ln);
                err = 1;
                return;
            }
            continue;
        }
        
        // labels
        if (ln[0] == ':') {
            if (strchr(ln, '\t')) {
                fprintf(stderr, "Error: label must be alone on its line\n");
                err = 1;
                return;
            }
            const char *nm = &ln[1];
            if (*nm == '\0') {
                fprintf(stderr, "Error: empty label name\n");
                err = 1;
                return;
            }
            for (const char *p = nm; *p; p++) {
                if (!isalnum((unsigned char)*p) && *p != '_') {
                    fprintf(stderr, "Error: invalid label name '%s'\n", ln);
                    err = 1;
                    return;
                }
            }
            addLbl(nm, addr);
            if (err) return;
            continue;
        }
        
        // instructions/data
        if (ln[0] == '\t') {
            if (ln[1] == ':') continue;
            
            char buf[MAX_LINE];
            strcpy(buf, &ln[1]);
            char t[MAX_TOK][MAX_TOK_LEN];
            int n = tokenize(buf, t);
            if (n == 0) continue;
            
            if (sec == CODE) {
                char *nm = t[0];
                if (strcmp(nm, "halt") == 0 || strcmp(nm, "in") == 0 ||
                    strcmp(nm, "out") == 0 || strcmp(nm, "clr") == 0) {
                    addr += 4;
                } else if (strcmp(nm, "push") == 0 || strcmp(nm, "pop") == 0) {
                    addr += 8;
                } else if (strcmp(nm, "ld") == 0) {
                    addr += 52;
                } else {
                    addr += 4;
                }
            } else if (sec == DATA) {
                addr += 4;
            }
            continue;
        }
        
        if (ln[0] == ' ') {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
        } else {
            fprintf(stderr, "Error: invalid line format\n");
        }
        err = 1;
        return;
    }
}

// validate all instructions (no output)
void validate(FILE *in) {
    char ln[MAX_LINE];
    Sec sec = NONE;
    
    while (fgets(ln, sizeof(ln), in)) {
        clean(ln);
        if (ln[0] == '\0') continue;
        if (ln[0] == ' ') {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            err = 1;
            return;
        }
        
        // directives
        if (ln[0] == '.') {
            if (strcmp(ln, ".code") == 0) {
                sec = CODE;
            } else if (strcmp(ln, ".data") == 0) {
                sec = DATA;
            } else {
                fprintf(stderr, "Error: invalid directive '%s'\n", ln);
                err = 1;
                return;
            }
            continue;
        }
        
        if (ln[0] == ':') continue;
        
        // instructions/data
        if (ln[0] == '\t') {
            if (ln[1] == ':') continue;
            
            char buf[MAX_LINE];
            strcpy(buf, &ln[1]);
            
            char orig[MAX_LINE];
            strcpy(orig, buf);
            
            // check balanced parens
            int pc = 0;
            for (const char *p = buf; *p; p++) {
                if (*p == '(') pc++;
                else if (*p == ')') pc--;
                if (pc < 0) {
                    fprintf(stderr, "Error: unmatched closing parenthesis\n");
                    err = 1;
                    return;
                }
            }
            if (pc != 0) {
                fprintf(stderr, "Error: unmatched opening parenthesis\n");
                err = 1;
                return;
            }
            
            char t[MAX_TOK][MAX_TOK_LEN];
            int n = tokenize(buf, t);
            if (n == 0) continue;
            
            if (sec == CODE) {
                // validate macros
                if (strcmp(t[0], "halt") == 0) {
                    chkMacro("halt", 0, n);
                    if (err) return;
                } else if (strcmp(t[0], "in") == 0) {
                    chkMacro("in", 2, n);
                    if (err) return;
                    getReg(t[1]);
                    if (err) return;
                    getReg(t[2]);
                    if (err) return;
                } else if (strcmp(t[0], "out") == 0) {
                    chkMacro("out", 2, n);
                    if (err) return;
                    getReg(t[1]);
                    if (err) return;
                    getReg(t[2]);
                    if (err) return;
                } else if (strcmp(t[0], "clr") == 0) {
                    chkMacro("clr", 1, n);
                    if (err) return;
                    getReg(t[1]);
                    if (err) return;
                } else if (strcmp(t[0], "push") == 0) {
                    chkMacro("push", 1, n);
                    if (err) return;
                    getReg(t[1]);
                    if (err) return;
                } else if (strcmp(t[0], "pop") == 0) {
                    chkMacro("pop", 1, n);
                    if (err) return;
                    getReg(t[1]);
                    if (err) return;
                } else if (strcmp(t[0], "ld") == 0) {
                    chkMacro("ld", 2, n);
                    if (err) return;
                    getReg(t[1]);
                    if (err) return;
                    if (t[2][0] != ':' && isNeg(t[2])) {
                        fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
                        err = 1;
                        return;
                    }
                    if (t[2][0] != ':') {
                        errno = 0;
                        char *end = NULL;
                        unsigned long long v = strtoull(t[2], &end, 10);
                        if (errno == ERANGE) {
                            fprintf(stderr, "Error: ld literal %s overflows 64-bit unsigned integer\n", t[2]);
                            err = 1;
                            return;
                        }
                        if (end == t[2] || *end != '\0') {
                            fprintf(stderr, "Error: invalid numeric literal '%s'\n", t[2]);
                            err = 1;
                            return;
                        }
                        (void)v;
                    } else {
                        toNum(t[2]);
                        if (err) return;
                    }
                } else {
                    // validate regular instructions
                    int found = 0;
                    InstrType type = R;
                    int exp = 0;
                    for (int i = 0; i < ninstr; i++) {
                        if (strcmp(instrs[i].name, t[0]) == 0) {
                            type = instrs[i].type;
                            exp = instrs[i].nops;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "Error: invalid instruction '%s'\n", t[0]);
                        err = 1;
                        return;
                    }
                    
                    int ops = n - 1;
                    if (type == MOV) {
                        if (ops < 2 || ops > 3) {
                            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", ops);
                            err = 1;
                            return;
                        }
                    } else if (ops != exp) {
                        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n", t[0], exp, ops);
                        err = 1;
                        return;
                    }
                    
                    // validate based on type
                    if (type == R) {
                        getReg(t[1]);
                        if (err) return;
                        getReg(t[2]);
                        if (err) return;
                        getReg(t[3]);
                        if (err) return;
                    } else if (type == I) {
                        getReg(t[1]);
                        if (err) return;
                        int uns = (strcmp(t[0], "addi") == 0 || strcmp(t[0], "subi") == 0 ||
                                   strcmp(t[0], "shftli") == 0 || strcmp(t[0], "shftri") == 0);
                        if (t[2][0] == ':') {
                            fprintf(stderr, "Error: label not allowed as immediate\n");
                            err = 1;
                            return;
                        }
                        if (uns && isNeg(t[2])) {
                            fprintf(stderr, "Error: unsigned instruction cannot have negative literal\n");
                            err = 1;
                            return;
                        }
                        uint64_t v = toNum(t[2]);
                        if (err) return;
                        if (uns && v > 4095) {
                            fprintf(stderr, "Error: literal %llu out of range for '%s' (max 4095)\n", (unsigned long long)v, t[0]);
                            err = 1;
                            return;
                        } else if (!uns) {
                            int64_t sv = (int64_t)v;
                            if (sv < -2048 || sv > 2047) {
                                fprintf(stderr, "Error: literal %lld out of range for '%s' (range -2048 to 2047)\n", (long long)sv, t[0]);
                                err = 1;
                                return;
                            }
                        }
                    } else if (type == OTHER) {
                        getReg(t[1]);
                        if (err) return;
                        getReg(t[2]);
                        if (err) return;
                    } else if (type == BR) {
                        if (strcmp(t[0], "brr") == 0 && t[1][0] != 'r') {
                            uint64_t v = toNum(t[1]);
                            if (err) return;
                            if (t[1][0] != ':') {
                                int64_t sv = (int64_t)v;
                                if (sv < -2048 || sv > 2047) {
                                    fprintf(stderr, "Error: literal %lld out of range for 'brr' (range -2048 to 2047)\n", (long long)sv);
                                    err = 1;
                                    return;
                                }
                            }
                        } else {
                            getReg(t[1]);
                            if (err) return;
                        }
                    } else if (type == PRIV) {
                        getReg(t[1]);
                        if (err) return;
                        getReg(t[2]);
                        if (err) return;
                        getReg(t[3]);
                        if (err) return;
                        if (t[4][0] == ':') {
                            fprintf(stderr, "Error: label not allowed as immediate\n");
                            err = 1;
                            return;
                        }
                        uint64_t v = toNum(t[4]);
                        if (err) return;
                        if (v > 4095) {
                            fprintf(stderr, "Error: literal %llu out of range for 'priv' (max 4095)\n", (unsigned long long)v);
                            err = 1;
                            return;
                        }
                        if (v != 0 && v != 3 && v != 4) {
                            fprintf(stderr, "Error: invalid priv code %llu (valid codes: 0, 3, 4)\n", (unsigned long long)v);
                            err = 1;
                            return;
                        }
                    } else if (type == MOV) {
                        if (n < 3 || n > 5) {
                            fprintf(stderr, "Error: invalid mov format\n");
                            err = 1;
                            return;
                        }
                        // check for invalid 3-operand mov without parens
                        if (n == 4 && strchr(orig, '(') == NULL) {
                            fprintf(stderr, "Error: mov instruction does not support 3 operands without memory addressing\n");
                            err = 1;
                            return;
                        }
                        
                        if (n == 5) {
                            getReg(t[1]);
                            if (err) return;
                            uint64_t v1 = toNum(t[2]);
                            if (err) return;
                            getReg(t[3]);
                            if (err) return;
                            uint64_t v2 = toNum(t[4]);
                            if (err) return;
                            if (t[2][0] != ':') {
                                int64_t sv = (int64_t)v1;
                                if (sv < -2048 || sv > 2047) {
                                    fprintf(stderr, "Error: literal %lld out of range for 'mov' (range -2048 to 2047)\n", (long long)sv);
                                    err = 1;
                                    return;
                                }
                            }
                            if (t[4][0] != ':') {
                                int64_t sv = (int64_t)v2;
                                if (sv < -2048 || sv > 2047) {
                                    fprintf(stderr, "Error: literal %lld out of range for 'mov' (range -2048 to 2047)\n", (long long)sv);
                                    err = 1;
                                    return;
                                }
                            }
                        } else if (n == 3 && t[1][0] == 'r' && t[2][0] == 'r') {
                            getReg(t[1]);
                            if (err) return;
                            getReg(t[2]);
                            if (err) return;
                        } else if (n == 3 && t[1][0] == 'r' && t[2][0] != 'r') {
                            getReg(t[1]);
                            if (err) return;
                            if (t[2][0] != ':' && isNeg(t[2])) {
                                fprintf(stderr, "Error: unsigned literal cannot be negative\n");
                                err = 1;
                                return;
                            }
                            uint64_t v = toNum(t[2]);
                            if (err) return;
                            if (t[2][0] != ':' && v > 4095) {
                                fprintf(stderr, "Error: literal %llu out of range for 'mov' (max 4095)\n", (unsigned long long)v);
                                err = 1;
                                return;
                            }
                        } else if (n == 4 && t[1][0] == 'r' && t[2][0] == 'r' && t[3][0] != 'r') {
                            getReg(t[1]);
                            if (err) return;
                            getReg(t[2]);
                            if (err) return;
                            uint64_t v = toNum(t[3]);
                            if (err) return;
                            if (t[3][0] != ':') {
                                int64_t sv = (int64_t)v;
                                if (sv < -2048 || sv > 2047) {
                                    fprintf(stderr, "Error: literal %lld out of range for 'mov' (range -2048 to 2047)\n", (long long)sv);
                                    err = 1;
                                    return;
                                }
                            }
                        } else if (n == 4 && t[1][0] == 'r' && t[2][0] != 'r' && t[3][0] == 'r') {
                            getReg(t[1]);
                            if (err) return;
                            uint64_t v = toNum(t[2]);
                            if (err) return;
                            getReg(t[3]);
                            if (err) return;
                            if (t[2][0] != ':') {
                                int64_t sv = (int64_t)v;
                                if (sv < -2048 || sv > 2047) {
                                    fprintf(stderr, "Error: literal %lld out of range for 'mov' (range -2048 to 2047)\n", (long long)sv);
                                    err = 1;
                                    return;
                                }
                            }
                        } else {
                            fprintf(stderr, "Error: invalid mov format\n");
                            err = 1;
                            return;
                        }
                    }
                }
            } else if (sec == DATA) {
                if (isNeg(buf)) {
                    fprintf(stderr, "Error: data values must be unsigned\n");
                    err = 1;
                    return;
                }
                errno = 0;
                char *end;
                unsigned long long v = strtoull(buf, &end, 10);
                if (errno == ERANGE) {
                    fprintf(stderr, "Error: data value '%s' overflows 64-bit unsigned integer\n", buf);
                    err = 1;
                    return;
                }
                if (end == buf || *end != '\0') {
                    fprintf(stderr, "Error: invalid numeric literal '%s'\n", buf);
                    err = 1;
                    return;
                }
                (void)v;
            }
        } else {
            fprintf(stderr, "Error: invalid line format\n");
            err = 1;
            return;
        }
    }
}

// first pass: expand macros, write intermediate
void pass1(FILE *in, FILE *mid) {
    char ln[MAX_LINE];
    Sec sec = NONE, last = NONE;
    uint64_t addr = CODE_START;
    
    while (fgets(ln, sizeof(ln), in)) {
        clean(ln);
        if (ln[0] == '\0') continue;
        if (ln[0] == ' ') {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            err = 1;
            return;
        }
        
        // directives
        if (ln[0] == '.') {
            if (strcmp(ln, ".code") == 0) {
                sec = CODE;
            } else if (strcmp(ln, ".data") == 0) {
                sec = DATA;
            } else {
                fprintf(stderr, "Error: invalid directive '%s'\n", ln);
                err = 1;
                return;
            }
            continue;
        }
        
        if (ln[0] == ':') {
            if (strchr(ln, '\t')) {
                fprintf(stderr, "Error: label must be alone on its line\n");
                err = 1;
                return;
            }
            continue;
        }
        
        // instructions/data
        if (ln[0] == '\t') {
            // write section directive only when needed
            if (sec != last) {
                if (sec == CODE) fprintf(mid, ".code\n");
                else if (sec == DATA) fprintf(mid, ".data\n");
                last = sec;
            }
            handleLine(mid, ln, sec, &addr);
            if (err) return;
        } else {
            fprintf(stderr, "Error: invalid line format\n");
            err = 1;
            return;
        }
    }
}

// check operand count
void chkOps(const char *nm, int exp, int act, InstrType ty) {
    int ops = act - 1;
    if (ty == MOV) {
        if (ops < 2 || ops > 3) {
            fprintf(stderr, "Error: instruction 'mov' expects 2-3 operands, got %d\n", ops);
            err = 1;
        }
        return;
    }
    if (ops != exp) {
        fprintf(stderr, "Error: instruction '%s' expects %d operand(s), got %d\n", nm, exp, ops);
        err = 1;
    }
}

// encode r-type
void encR(char t[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt) {
    *rd = getReg(t[1]);
    if (err) return;
    *rs = getReg(t[2]);
    if (err) return;
    *rt = getReg(t[3]);
}

// encode i-type
void encI(char t[MAX_TOK][MAX_TOK_LEN], const char *nm,
          uint32_t *rd, uint32_t *imm)
{
    *rd = getReg(t[1]);
    if (err) return;

    int uns = (strcmp(nm, "addi") == 0 ||
               strcmp(nm, "subi") == 0 ||
               strcmp(nm, "shftli") == 0 ||
               strcmp(nm, "shftri") == 0);

    if (t[2][0] == ':') {
        fprintf(stderr, "Error: label not allowed as immediate\n");
        err = 1;
        return;
    }

    int64_t sv = (int64_t)toNum(t[2]);
    if (err) return;

    if (uns) {
        if (sv < 0 || sv > 4095) {
            fprintf(stderr, "Error: literal %lld out of range for '%s'\n",
                    (long long)sv, nm);
            err = 1;
            return;
        }
        *imm = (uint32_t)sv;
    } else {
        if (sv < -2048 || sv > 2047) {
            fprintf(stderr, "Error: literal %lld out of range for '%s'\n",
                    (long long)sv, nm);
            err = 1;
            return;
        }
        *imm = (uint32_t)(sv & 0xFFF);   // proper sign encoding
    }
}

// encode branch
void encBr(char t[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *imm) {
    if (strcmp(t[0], "call") == 0) {
        *rd = getReg(t[1]);
        return;
    }
    if (strcmp(t[0], "brr") == 0 && t[1][0] != 'r') {
        uint64_t v = toNum(t[1]);
        if (err) return;
        if (t[1][0] != ':') {
            int64_t sv = (int64_t)v;
            if (sv < -2048 || sv > 2047) {
                fprintf(stderr, "Error: literal %lld out of range for 'brr' (range -2048 to 2047)\n", (long long)sv);
                err = 1;
                return;
            }
        }
        *imm = (uint32_t)(v & 0xFFF);
    } else {
        *rd = getReg(t[1]);
        if (err) return;
        *imm = 0;
    }
}

// encode priv
void encPriv(char t[MAX_TOK][MAX_TOK_LEN], uint32_t *rd, uint32_t *rs, uint32_t *rt, uint32_t *imm) {
    *rd = getReg(t[1]);
    if (err) return;
    *rs = getReg(t[2]);
    if (err) return;
    *rt = getReg(t[3]);
    if (err) return;
    uint64_t v = toNum(t[4]);
    if (err) return;
    if (t[4][0] != ':' && v > 4095) {
        fprintf(stderr, "Error: literal %llu out of range for 'priv' (max 4095)\n", (unsigned long long)v);
        err = 1;
        return;
    }
    *imm = (uint32_t)(v & 0xFFF);
}

// encode mov
void encMov(char t[MAX_TOK][MAX_TOK_LEN], int n, uint32_t *op, uint32_t *rd, uint32_t *rs, uint32_t *imm) {
    if (n < 3 || n > 4) {
        fprintf(stderr, "Error: invalid mov format\n");
        err = 1;
        return;
    }
    if (n == 3 && t[1][0] == 'r' && t[2][0] == 'r') {
        // mov rd, rs
        *op = 0x11;
        *rd = getReg(t[1]);
        if (err) return;
        *rs = getReg(t[2]);
        if (err) return;
        *imm = 0;
    } else if (n == 3 && t[1][0] == 'r' && t[2][0] != 'r') {
        // mov rd, imm
        *op = 0x12;
        *rd = getReg(t[1]);
        if (err) return;
        if (t[2][0] != ':' && isNeg(t[2])) {
            fprintf(stderr, "Error: unsigned literal cannot be negative\n");
            err = 1;
            return;
        }
        uint64_t v = toNum(t[2]);
        if (err) return;
        if (t[2][0] != ':' && v > 4095) {
            fprintf(stderr, "Error: literal %llu out of range for 'mov' (max 4095)\n", (unsigned long long)v);
            err = 1;
            return;
        }
        *imm = (uint32_t)(v & 0xFFF);
    } else if (n == 4 && t[1][0] == 'r' && t[2][0] == 'r') {
        // mov rd, (rs)(imm)
        *op = 0x10;
        *rd = getReg(t[1]);
        if (err) return;
        *rs = getReg(t[2]);
        if (err) return;
        uint64_t v = toNum(t[3]);
        if (err) return;
        if (t[3][0] != ':') {
            int64_t sv = (int64_t)v;
            if (sv < -2048 || sv > 2047) {
                fprintf(stderr, "Error: literal %lld out of range for 'mov' (range -2048 to 2047)\n", (long long)sv);
                err = 1;
                return;
            }
        }
        *imm = (uint32_t)(v & 0xFFF);
    } else if (n == 4 && t[1][0] == 'r' && t[3][0] == 'r') {
        // mov (rd)(imm), rs
        *op = 0x13;
        *rd = getReg(t[1]);
        if (err) return;
        uint64_t v = toNum(t[2]);
        if (err) return;
        *rs = getReg(t[3]);
        if (err) return;
        if (t[2][0] != ':') {
            int64_t sv = (int64_t)v;
            if (sv < -2048 || sv > 2047) {
                fprintf(stderr, "Error: literal %lld out of range for 'mov' (range -2048 to 2047)\n", (long long)sv);
                err = 1;
                return;
            }
        }
        *imm = (uint32_t)(v & 0xFFF);
    } else {
        fprintf(stderr, "Error: invalid mov format\n");
        err = 1;
    }
}

// find instruction info
InstrType findInstr(char *nm, uint32_t *op, int *exp) {
    for (int i = 0; i < ninstr; i++) {
        if (strcmp(instrs[i].name, nm) == 0) {
            *op = instrs[i].op;
            *exp = instrs[i].nops;
            return instrs[i].type;
        }
    }
    fprintf(stderr, "Error: invalid instruction '%s'\n", nm);
    err = 1;
    return R;
}

// assemble 32-bit instruction
uint32_t asm32(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm) {
    uint32_t r = 0;
    r |= (imm & 0xFFF);
    r |= (rt & 0x1F) << 12;
    r |= (rs & 0x1F) << 17;
    r |= (rd & 0x1F) << 22;
    r |= (op & 0x3F) << 27;
    return r;
}

// convert to machine code
uint32_t toMC(char t[MAX_TOK][MAX_TOK_LEN], int n) {
    uint32_t op = 0, rd = 0, rs = 0, rt = 0, imm = 0;
    int exp = 0;
    
    InstrType ty = findInstr(t[0], &op, &exp);
    if (err) return 0;
    chkOps(t[0], exp, n, ty);
    if (err) return 0;
    if  (ty != MOV && n != exp + 1) {
        fprintf(stderr, "Error: wrong number of operands for '%s'\n", t[0]);
        err = 1;
        return 0;
    }
    
    if (ty == R) {
        encR(t, &rd, &rs, &rt);
        if (err) return 0;
    } else if (ty == I) {
        encI(t, t[0], &rd, &imm);
        if (err) return 0;
    } else if (ty == OTHER) {
        rd = getReg(t[1]);
        if (err) return 0;
        rs = getReg(t[2]);
        if (err) return 0;
    } else if (ty == BR) {
        encBr(t, &rd, &imm);
        if (err) return 0;
    } else if (ty == PRIV) {
        encPriv(t, &rd, &rs, &rt, &imm);
        if (err) return 0;
    } else if (ty == MOV) {
        encMov(t, n, &op, &rd, &rs, &imm);
        if (err) return 0;
    }
    
    return asm32(op, rd, rs, rt, imm);
}

// write code instruction to binary
void writeCode(FILE *f, char *ln) {
    char buf[MAX_LINE];
    strcpy(buf, &ln[1]);
    
    char t[MAX_TOK][MAX_TOK_LEN];
    int n = tokenize(buf, t);
    if (n == 0) {
        fprintf(stderr, "Error: empty instruction line\n");
        err = 1;
        return;
    }
    if (n > MAX_TOK) {
        fprintf(stderr, "Error: too many operands\n");
        err = 1;
        return;
    }
    
    uint32_t mc = toMC(t, n);
    if (err) return;
    
    // little-endian
    unsigned char b[4];
    b[0] = mc & 0xFF;
    b[1] = (mc >> 8) & 0xFF;
    b[2] = (mc >> 16) & 0xFF;
    b[3] = (mc >> 24) & 0xFF;
    fwrite(b, 4, 1, f);
}

// write data value to binary
void writeData(FILE *f, char *ln) {
    char buf[MAX_LINE];
    strcpy(buf, &ln[1]);
    
    if (isNeg(buf)) {
        fprintf(stderr, "Error: data values must be unsigned\n");
        err = 1;
        return;
    }
    
    uint64_t val = toNum(buf);
    if (err) return;
    uint32_t v = (uint32_t)val;
    
    // little-endian
    unsigned char b[4];
    b[0] = v & 0xFF;
    b[1] = (v >> 8) & 0xFF;
    b[2] = (v >> 16) & 0xFF;
    b[3] = (v >> 24) & 0xFF;
    fwrite(b, 4, 1, f);
}

// second pass: generate binary
void pass2(FILE *mid, FILE *out) {
    char ln[MAX_LINE];
    Sec sec = NONE;
    
    while (fgets(ln, sizeof(ln), mid)) {
        clean(ln);
        if (ln[0] == '\0') continue;
        if (ln[0] == ' ') {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            err = 1;
            return;
        }
        
        // directives
        if (ln[0] == '.') {
            if (strcmp(ln, ".code") == 0) {
                sec = CODE;
            } else if (strcmp(ln, ".data") == 0) {
                sec = DATA;
            } else {
                fprintf(stderr, "Error: invalid directive '%s'\n", ln);
                err = 1;
                return;
            }
            continue;
        }
        
        if (ln[0] == ':') continue;
        
        // instructions/data
        if (ln[0] == '\t') {
            if (ln[1] == ':') continue;
            if (sec == NONE) {
                fprintf(stderr, "Error: instr/data outside of .code/.data section\n");
                err = 1;
                return;
            }
            if (sec == CODE) {
                writeCode(out, ln);
                if (err) return;
            } else if (sec == DATA) {
                writeData(out, ln);
                if (err) return;
            }
            continue;
        }
        
        fprintf(stderr, "Error: invalid line format\n");
        err = 1;
        return;
    }
}

// main entry
int main(int argc, char **argv) {
    err = 0;
    nlbl = 0;
    
    if (argc != 4) {
        fprintf(stderr, "Error: incorrect number of inputs\n");
        return 1;
    }
    
    FILE *in = fopen(argv[1], "r");
    if (!in) {
        fprintf(stderr, "Error: cannot open input file\n");
        return 1;
    }
    
    // step 1: collect labels
    collectLbls(in);
    if (err) {
        fclose(in);
        return 1;
    }
    fseek(in, 0, SEEK_SET);
    
    // step 2: validate
    validate(in);
    if (err) {
        fclose(in);
        return 1;
    }
    fseek(in, 0, SEEK_SET);
    
    // step 3: first pass
    FILE *mid = fopen(argv[2], "w+");
    if (!mid) {
        fprintf(stderr, "Error: cannot open intermediate file\n");
        fclose(in);
        return 1;
    }
    
    pass1(in, mid);
    if (err) {
        fclose(in);
        fclose(mid);
        remove(argv[2]);
        return 1;
    }
    fclose(in);
    fseek(mid, 0, SEEK_SET);
    
    // step 4: second pass
    FILE *out = fopen(argv[3], "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot open output file\n");
        fclose(mid);
        remove(argv[2]);
        return 1;
    }
    
    pass2(mid, out);
    if (err) {
        fclose(mid);
        fclose(out);
        remove(argv[2]);
        remove(argv[3]);
        return 1;
    }
    
    fclose(mid);
    fclose(out);
    return 0;
}