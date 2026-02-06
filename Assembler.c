#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_LABELS 512
#define CODE_START 0x1000

int hadError = 0;

typedef struct {
    char name[256];
    uint64_t addr;
} Label;

Label labels[MAX_LABELS];
int numLabels = 0;

// Add a label, checking for duplicates
void addLabel(const char *name, uint64_t addr) {
    for (int i = 0; i < numLabels; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            fprintf(stderr, "Error: duplicate label '%s'\n", name);
            hadError = 1;
            exit(1);
        }
    }
    if (numLabels >= MAX_LABELS) {
        fprintf(stderr, "Error: too many labels\n");
        hadError = 1;
        exit(1);
    }
    if (strlen(name) > 255) {
        fprintf(stderr, "Error: label name too long (max 256 chars)\n");
        hadError = 1;
        exit(1);
    }
    strcpy(labels[numLabels].name, name);
    labels[numLabels].addr = addr;
    numLabels++;
}

// Find label address
uint64_t findLabel(const char *name) {
    for (int i = 0; i < numLabels; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Error: undefined label '%s'\n", name);
    hadError = 1;
    exit(1);
}

// Remove comments and trim
void cleanLine(char *line) {
    // Remove comment
    char *comment = strchr(line, ';');
    if (comment) *comment = '\0';
    
    // Remove newline
    int len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        line[len-1] = '\0';
        len--;
    }
    
    // Remove trailing whitespace
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t')) {
        line[len-1] = '\0';
        len--;
    }
}

// Parse register number
int parseRegister(const char *str) {
    if (!str || str[0] != 'r') {
        fprintf(stderr, "Error: invalid register '%s'\n", str);
        hadError = 1;
        exit(1);
    }
    
    char *end;
    long reg = strtol(str + 1, &end, 10);
    
    if (*end != '\0' || reg < 0 || reg > 31) {
        fprintf(stderr, "Error: invalid register '%s'\n", str);
        hadError = 1;
        exit(1);
    }
    
    return (int)reg;
}

// Parse number or label
uint64_t parseValue(const char *str) {
    if (!str) {
        fprintf(stderr, "Error: NULL literal\n");
        hadError = 1;
        exit(1);
    }
    
    // Label reference
    if (str[0] == ':') {
        return findLabel(str + 1);
    }
    
    // Numeric literal
    char *end;
    uint64_t val = strtoull(str, &end, 0);
    
    if (end == str || *end != '\0') {
        fprintf(stderr, "Error: invalid numeric literal '%s'\n", str);
        hadError = 1;
        exit(1);
    }
    
    return val;
}

// Check if string is negative
int isNegative(const char *str) {
    return str && str[0] == '-';
}

// Assemble 32-bit instruction
uint32_t assemble(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm) {
    uint32_t instr = 0;
    instr |= (imm & 0xFFF);
    instr |= (rt & 0x1F) << 12;
    instr |= (rs & 0x1F) << 17;
    instr |= (rd & 0x1F) << 22;
    instr |= (op & 0x3F) << 27;
    return instr;
}

// Write instruction to binary file
void writeBinary(FILE *f, uint32_t instr) {
    unsigned char bytes[4];
    bytes[0] = instr & 0xFF;
    bytes[1] = (instr >> 8) & 0xFF;
    bytes[2] = (instr >> 16) & 0xFF;
    bytes[3] = (instr >> 24) & 0xFF;
    fwrite(bytes, 4, 1, f);
}

// Split line into tokens (handles commas and parentheses)
int tokenize(char *line, char tokens[][64]) {
    int count = 0;
    
    // Replace punctuation with spaces
    for (int i = 0; line[i]; i++) {
        if (line[i] == ',' || line[i] == '(' || line[i] == ')') {
            line[i] = ' ';
        }
    }
    
    char *tok = strtok(line, " \t");
    while (tok && count < 8) {
        strcpy(tokens[count++], tok);
        tok = strtok(NULL, " \t");
    }
    
    return count;
}

// First pass: collect labels and validate
void firstPass(FILE *in) {
    char line[MAX_LINE];
    uint64_t addr = CODE_START;
    int inCode = 0, inData = 0;
    
    while (fgets(line, sizeof(line), in)) {
        cleanLine(line);
        
        if (line[0] == '\0') continue;
        
        // Directives
        if (line[0] == '.') {
            if (strcmp(line, ".code") == 0) {
                inCode = 1;
                inData = 0;
                continue;
            } else if (strcmp(line, ".data") == 0) {
                inData = 1;
                inCode = 0;
                continue;
            } else {
                fprintf(stderr, "Error: invalid directive '%s'\n", line);
                hadError = 1;
                exit(1);
            }
        }
        
        // Labels
        if (line[0] == ':') {
            if (strchr(line, '\t')) {
                fprintf(stderr, "Error: label must be alone on its line\n");
                hadError = 1;
                exit(1);
            }
            
            const char *name = line + 1;
            if (*name == '\0') {
                fprintf(stderr, "Error: empty label name\n");
                hadError = 1;
                exit(1);
            }
            
            for (const char *p = name; *p; p++) {
                if (!isalnum(*p) && *p != '_') {
                    fprintf(stderr, "Error: invalid label name '%s'\n", line);
                    hadError = 1;
                    exit(1);
                }
            }
            
            addLabel(name, addr);
            continue;
        }
        
        // Instructions/data
        if (line[0] == '\t') {
            if (line[1] == ':') continue;
            
            char buf[MAX_LINE];
            strcpy(buf, line + 1);
            
            char tokens[8][64];
            int n = tokenize(buf, tokens);
            
            if (n == 0) continue;
            
            if (inCode) {
                // Validate and estimate instruction size
                if (strcmp(tokens[0], "ld") == 0) {
                    if (n != 3) {
                        fprintf(stderr, "Error: macro 'ld' expects 2 argument(s), got %d\n", n - 1);
                        hadError = 1;
                        exit(1);
                    }
                    parseRegister(tokens[1]); // Validate register
                    if (isNegative(tokens[2])) {
                        fprintf(stderr, "Error: 'ld' cannot have negative literal\n");
                        hadError = 1;
                        exit(1);
                    }
                    addr += 52; // 13 instructions
                } else if (strcmp(tokens[0], "push") == 0 || strcmp(tokens[0], "pop") == 0) {
                    if (n != 2) {
                        fprintf(stderr, "Error: macro '%s' expects 1 argument(s), got %d\n", tokens[0], n - 1);
                        hadError = 1;
                        exit(1);
                    }
                    parseRegister(tokens[1]); // Validate register
                    addr += 8; // 2 instructions
                } else if (strcmp(tokens[0], "halt") == 0) {
                    if (n != 1) {
                        fprintf(stderr, "Error: macro 'halt' expects 0 argument(s), got %d\n", n - 1);
                        hadError = 1;
                        exit(1);
                    }
                    addr += 4;
                } else if (strcmp(tokens[0], "clr") == 0 || strcmp(tokens[0], "in") == 0 || strcmp(tokens[0], "out") == 0) {
                    int expected = (strcmp(tokens[0], "clr") == 0) ? 1 : 2;
                    if (n != expected + 1) {
                        fprintf(stderr, "Error: macro '%s' expects %d argument(s), got %d\n", tokens[0], expected, n - 1);
                        hadError = 1;
                        exit(1);
                    }
                    // Validate registers
                    for (int i = 1; i < n; i++) {
                        parseRegister(tokens[i]);
                    }
                    addr += 4; // 1 instruction
                } else {
                    addr += 4; // 1 instruction
                }
            } else if (inData) {
                // Validate data value
                if (isNegative(tokens[0])) {
                    fprintf(stderr, "Error: data values must be unsigned\n");
                    hadError = 1;
                    exit(1);
                }
                
                char *end;
                uint64_t val = strtoull(tokens[0], &end, 0);
                if (end == tokens[0] || *end != '\0') {
                    fprintf(stderr, "Error: invalid numeric literal '%s'\n", tokens[0]);
                    hadError = 1;
                    exit(1);
                }
                
                if (val > UINT32_MAX) {
                    fprintf(stderr, "Error: data value out of range\n");
                    hadError = 1;
                    exit(1);
                }
                
                addr += 4;
            }
            continue;
        }
        
        if (line[0] == ' ') {
            fprintf(stderr, "Error: instruction must begin with a tab\n");
            hadError = 1;
            exit(1);
        }
        
        fprintf(stderr, "Error: invalid line format\n");
        hadError = 1;
        exit(1);
    }
}

// Expand ld macro
void expandLd(FILE *mid, int rd, uint64_t val) {
    uint32_t parts[6];
    parts[0] = (val >> 52) & 0xFFF;
    parts[1] = (val >> 40) & 0xFFF;
    parts[2] = (val >> 28) & 0xFFF;
    parts[3] = (val >> 16) & 0xFFF;
    parts[4] = (val >> 4) & 0xFFF;
    parts[5] = val & 0xF;
    
    fprintf(mid, "\txor r%d, r%d, r%d\n", rd, rd, rd);
    
    for (int i = 0; i < 5; i++) {
        fprintf(mid, "\taddi r%d, %u\n", rd, parts[i]);
        fprintf(mid, "\tshftli r%d, %d\n", rd, (i == 4) ? 4 : 12);
    }
    
    fprintf(mid, "\taddi r%d, %u\n", rd, parts[5]);
}

// Second pass: expand macros and resolve labels
void secondPass(FILE *in, FILE *mid) {
    char line[MAX_LINE];
    int inCode = 0, inData = 0;
    int wroteCode = 0, wroteData = 0;
    
    rewind(in);
    
    while (fgets(line, sizeof(line), in)) {
        cleanLine(line);
        
        if (line[0] == '\0') continue;
        
        // Directives
        if (line[0] == '.') {
            if (strcmp(line, ".code") == 0) {
                inCode = 1;
                inData = 0;
                if (!wroteCode) {
                    fprintf(mid, ".code\n");
                    wroteCode = 1;
                }
                continue;
            } else if (strcmp(line, ".data") == 0) {
                inData = 1;
                inCode = 0;
                if (!wroteData) {
                    fprintf(mid, ".data\n");
                    wroteData = 1;
                }
                continue;
            }
        }
        
        // Labels
        if (line[0] == ':') continue;
        
        // Instructions/data
        if (line[0] == '\t') {
            if (line[1] == ':') continue;
            
            char buf[MAX_LINE];
            strcpy(buf, line + 1);
            
            char orig[MAX_LINE];
            strcpy(orig, buf);
            
            char tokens[8][64];
            int n = tokenize(buf, tokens);
            
            if (n == 0) continue;
            
            if (inCode) {
                // Expand macros
                if (strcmp(tokens[0], "halt") == 0) {
                    fprintf(mid, "\tpriv r0, r0, r0, 0\n");
                } else if (strcmp(tokens[0], "clr") == 0) {
                    int rd = parseRegister(tokens[1]);
                    fprintf(mid, "\txor r%d, r%d, r%d\n", rd, rd, rd);
                } else if (strcmp(tokens[0], "in") == 0) {
                    fprintf(mid, "\tpriv %s, %s, r0, 3\n", tokens[1], tokens[2]);
                } else if (strcmp(tokens[0], "out") == 0) {
                    fprintf(mid, "\tpriv %s, %s, r0, 4\n", tokens[1], tokens[2]);
                } else if (strcmp(tokens[0], "push") == 0) {
                    fprintf(mid, "\tmov (r31)(-8), %s\n", tokens[1]);
                    fprintf(mid, "\tsubi r31, 8\n");
                } else if (strcmp(tokens[0], "pop") == 0) {
                    fprintf(mid, "\tmov %s, (r31)(0)\n", tokens[1]);
                    fprintf(mid, "\taddi r31, 8\n");
                } else if (strcmp(tokens[0], "ld") == 0) {
                    int rd = parseRegister(tokens[1]);
                    uint64_t val = parseValue(tokens[2]);
                    expandLd(mid, rd, val);
                } else {
                    // Regular instruction - resolve labels
                    fprintf(mid, "\t%s", tokens[0]);
                    for (int i = 1; i < n; i++) {
                        if (tokens[i][0] == ':') {
                            uint64_t addr = findLabel(tokens[i] + 1);
                            fprintf(mid, "%s%llu", (i == 1) ? " " : ", ", (unsigned long long)addr);
                        } else {
                            fprintf(mid, "%s%s", (i == 1) ? " " : ", ", tokens[i]);
                        }
                    }
                    fprintf(mid, "\n");
                }
            } else if (inData) {
                uint64_t val = parseValue(orig);
                fprintf(mid, "\t%llu\n", (unsigned long long)val);
            }
        }
    }
}

// Third pass: generate binary
void thirdPass(FILE *mid, FILE *out) {
    char line[MAX_LINE];
    int inCode = 0, inData = 0;
    
    rewind(mid);
    
    while (fgets(line, sizeof(line), mid)) {
        cleanLine(line);
        
        if (line[0] == '\0') continue;
        
        if (line[0] == '.') {
            if (strcmp(line, ".code") == 0) {
                inCode = 1;
                inData = 0;
            } else if (strcmp(line, ".data") == 0) {
                inData = 1;
                inCode = 0;
            }
            continue;
        }
        
        if (line[0] == '\t') {
            char buf[MAX_LINE];
            strcpy(buf, line + 1);
            
            char tokens[8][64];
            int n = tokenize(buf, tokens);
            
            if (n == 0) continue;
            
            if (inCode) {
                uint32_t op = 0, rd = 0, rs = 0, rt = 0, imm = 0;
                
                // Parse instruction
                if (strcmp(tokens[0], "and") == 0) {
                    op = 0x00; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "or") == 0) {
                    op = 0x01; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "xor") == 0) {
                    op = 0x02; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "not") == 0) {
                    op = 0x03; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]);
                } else if (strcmp(tokens[0], "shftr") == 0) {
                    op = 0x04; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "shftri") == 0) {
                    op = 0x05; rd = parseRegister(tokens[1]); imm = parseValue(tokens[2]) & 0xFFF;
                } else if (strcmp(tokens[0], "shftl") == 0) {
                    op = 0x06; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "shftli") == 0) {
                    op = 0x07; rd = parseRegister(tokens[1]); imm = parseValue(tokens[2]) & 0xFFF;
                } else if (strcmp(tokens[0], "br") == 0) {
                    op = 0x08; rd = parseRegister(tokens[1]);
                } else if (strcmp(tokens[0], "brr") == 0) {
                    if (tokens[1][0] == 'r') {
                        op = 0x09; rd = parseRegister(tokens[1]);
                    } else {
                        op = 0x0a; imm = parseValue(tokens[1]) & 0xFFF;
                    }
                } else if (strcmp(tokens[0], "brnz") == 0) {
                    op = 0x0b; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]);
                } else if (strcmp(tokens[0], "call") == 0) {
                    op = 0x0c; rd = parseRegister(tokens[1]);
                } else if (strcmp(tokens[0], "return") == 0) {
                    op = 0x0d;
                } else if (strcmp(tokens[0], "brgt") == 0) {
                    op = 0x0e; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "priv") == 0) {
                    op = 0x0f; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); 
                    rt = parseRegister(tokens[3]); imm = parseValue(tokens[4]) & 0xFFF;
                } else if (strcmp(tokens[0], "mov") == 0) {
                    if (n == 3 && tokens[2][0] == 'r') {
                        op = 0x11; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]);
                    } else if (n == 3) {
                        op = 0x12; rd = parseRegister(tokens[1]); imm = parseValue(tokens[2]) & 0xFFF;
                    } else if (n == 4 && tokens[2][0] == 'r') {
                        op = 0x10; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); 
                        imm = parseValue(tokens[3]) & 0xFFF;
                    } else if (n == 4) {
                        op = 0x13; rd = parseRegister(tokens[1]); imm = parseValue(tokens[2]) & 0xFFF;
                        rs = parseRegister(tokens[3]);
                    }
                } else if (strcmp(tokens[0], "addf") == 0) {
                    op = 0x14; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "subf") == 0) {
                    op = 0x15; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "mulf") == 0) {
                    op = 0x16; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "divf") == 0) {
                    op = 0x17; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "add") == 0) {
                    op = 0x18; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "addi") == 0) {
                    op = 0x19; rd = parseRegister(tokens[1]); imm = parseValue(tokens[2]) & 0xFFF;
                } else if (strcmp(tokens[0], "sub") == 0) {
                    op = 0x1a; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "subi") == 0) {
                    op = 0x1b; rd = parseRegister(tokens[1]); imm = parseValue(tokens[2]) & 0xFFF;
                } else if (strcmp(tokens[0], "mul") == 0) {
                    op = 0x1c; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                } else if (strcmp(tokens[0], "div") == 0) {
                    op = 0x1d; rd = parseRegister(tokens[1]); rs = parseRegister(tokens[2]); rt = parseRegister(tokens[3]);
                }
                
                writeBinary(out, assemble(op, rd, rs, rt, imm));
                
            } else if (inData) {
                uint64_t val = parseValue(buf);
                uint32_t v = (uint32_t)val;
                writeBinary(out, v);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Error: incorrect number of inputs\n");
        return 1;
    }
    
    FILE *in = fopen(argv[1], "r");
    if (!in) {
        fprintf(stderr, "Error: cannot open input file\n");
        return 1;
    }
    
    // First pass: collect labels
    firstPass(in);
    if (hadError) {
        fclose(in);
        return 1;
    }
    
    // Second pass: expand macros and resolve labels
    FILE *mid = fopen(argv[2], "w+");
    if (!mid) {
        fprintf(stderr, "Error: cannot open intermediate file\n");
        fclose(in);
        return 1;
    }
    
    secondPass(in, mid);
    fclose(in);
    
    if (hadError) {
        fclose(mid);
        remove(argv[2]);
        return 1;
    }
    
    // Third pass: generate binary
    FILE *out = fopen(argv[3], "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot open output file\n");
        fclose(mid);
        return 1;
    }
    
    thirdPass(mid, out);
    fclose(mid);
    fclose(out);
    
    if (hadError) {
        remove(argv[2]);
        remove(argv[3]);
        return 1;
    }
    
    return 0;
}