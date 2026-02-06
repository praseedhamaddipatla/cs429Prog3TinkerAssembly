#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>

#define MAX_LINE 512
#define MAX_TOK  8
#define MAX_LBL  512
#define CODE_START 0x1000


typedef struct {
    char name[64];
    uint64_t addr;
} Label;

Label labels[MAX_LBL];
int nlbl = 0;


void cleanLine(char *s) {
    char *c = strchr(s, ';');
    if (c) *c = '\0';

    for (int i = 0; s[i]; i++) {
        if (s[i] == ',' || s[i] == '\n')
            s[i] = ' ';
    }
}

int splitIntoTokens(char *s, char t[MAX_TOK][64]) {
    int n = 0;
    char *tok = strtok(s, " \t");
    while (tok && n < MAX_TOK) {
        strcpy(t[n++], tok);
        tok = strtok(NULL, " \t");
    }
    return n;
}

int getRegisterNumber(const char *s) {
    if (s[0] != 'r') return -1;
    return atoi(s + 1);
}

uint64_t convertToNumber(const char *s) {
    return strtoull(s, NULL, 0);
}


void addLabelToArray(const char *name, uint64_t addr) {
    strcpy(labels[nlbl].name, name);
    labels[nlbl].addr = addr;
    nlbl++;
}

uint64_t findLabelAddress(const char *name) {
    for (int i = 0; i < nlbl; i++) {
        if (!strcmp(labels[i].name, name))
            return labels[i].addr;
    }
    fprintf(stderr, "Unknown label: %s\n", name);
    exit(1);
}

void resetLabels() {
    nlbl = 0;
}


void firstPass(FILE *in) {
    char line[MAX_LINE];
    uint64_t addr = CODE_START;

    while (fgets(line, sizeof(line), in)) {
        cleanLine(line);
        if (strlen(line) == 0) continue;

        char tmp[MAX_LINE];
        strcpy(tmp, line);

        char tok[MAX_TOK][64];
        int n = splitIntoTokens(tmp, tok);
        if (n == 0) continue;

        int len = strlen(tok[0]);
        if (tok[0][len - 1] == ':') {
            tok[0][len - 1] = '\0';
            addLabelToArray(tok[0], addr);
            continue;
        }

        if (!strcmp(tok[0], "ld")) {
            addr += 48;   // 12 instructions
        } else if (!strcmp(tok[0], "push") || !strcmp(tok[0], "pop")) {
            addr += 8;    // 2 instructions
        } else if (!strcmp(tok[0], ".data")) {
            addr += 8;
        } else {
            addr += 4;
        }
    }
}


uint32_t encode(uint32_t op, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t imm) {
    return (op << 27) | (rd << 22) | (rs << 17) | (rt << 12) | (imm & 0xFFF);
}


int tryMacro(FILE *out, char t[MAX_TOK][64], uint64_t *addr) {
    if (!strcmp(t[0], "halt")) {
        fprintf(out, "\tadd r0 r0 r0\n");
        *addr += 4;
        return 1;
    }

    if (!strcmp(t[0], "clr")) {
        fprintf(out, "\txor %s %s %s\n", t[1], t[1], t[1]);
        *addr += 4;
        return 1;
    }

    if (!strcmp(t[0], "ld")) {
        int rd = getRegisterNumber(t[1]);
        uint64_t v = convertToNumber(t[2]);

        fprintf(out, "\txor r%d r%d r%d\n", rd, rd, rd);
        for (int shift = 52; shift >= 4; shift -= 12) {
            fprintf(out, "\taddi r%d %lu\n", rd, (v >> shift) & 0xFFF);
            fprintf(out, "\tshftli r%d 12\n", rd);
        }
        fprintf(out, "\taddi r%d %lu\n", rd, v & 0xF);

        *addr += 48;
        return 1;
    }

    return 0;
}


uint32_t toMachineCode(char t[MAX_TOK][64]) {
    if (!strcmp(t[0], "add"))
        return encode(0x0, getRegisterNumber(t[1]),
                      getRegisterNumber(t[2]),
                      getRegisterNumber(t[3]), 0);

    if (!strcmp(t[0], "addi"))
        return encode(0x1, getRegisterNumber(t[1]), 0, 0,
                      convertToNumber(t[2]));

    if (!strcmp(t[0], "xor"))
        return encode(0x2, getRegisterNumber(t[1]),
                      getRegisterNumber(t[2]),
                      getRegisterNumber(t[3]), 0);

    if (!strcmp(t[0], "shftli"))
        return encode(0x3, getRegisterNumber(t[1]), 0, 0,
                      convertToNumber(t[2]));

    if (!strcmp(t[0], "br"))
        return encode(0x8, findLabelAddress(t[1]) >> 2, 0, 0, 0);

    if (!strcmp(t[0], "brr")) {
        if (t[1][0] == 'r')
            return encode(0x9, getRegisterNumber(t[1]), 0, 0, 0);
        else
            return encode(0xA, 0, 0, 0,
                          findLabelAddress(t[1]) >> 2);
    }

    fprintf(stderr, "Unknown instruction: %s\n", t[0]);
    exit(1);
}

void secondPass(FILE *in, FILE *out) {
    char line[MAX_LINE];
    uint64_t addr = CODE_START;

    while (fgets(line, sizeof(line), in)) {
        cleanLine(line);
        if (strlen(line) == 0) continue;

        char tmp[MAX_LINE];
        strcpy(tmp, line);

        char tok[MAX_TOK][64];
        int n = splitIntoTokens(tmp, tok);
        if (n == 0) continue;

        int len = strlen(tok[0]);
        if (tok[0][len - 1] == ':') continue;

        if (tryMacro(out, tok, &addr))
            continue;

        uint32_t mc = toMachineCode(tok);
        fwrite(&mc, sizeof(mc), 1, out);
        addr += 4;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s input.asm output.bin\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    FILE *out = fopen(argv[2], "wb");
    if (!in || !out) {
        perror("file");
        return 1;
    }

    firstPass(in);
    rewind(in);
    secondPass(in, out);

    fclose(in);
    fclose(out);
    return 0;
}
