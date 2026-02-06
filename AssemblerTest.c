#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

int getReg(const char *s);                 
uint64_t toNum(const char *s);                 
void addLbl(const char *name, uint64_t addr);
uint64_t findLbl(const char *name);           
int tokenize(char *line, char tokens[8][64]);  
int tryMacro(FILE *f, char tokens[8][64], int n, uint64_t *addr);  
uint32_t toMC(char tokens[8][64], int n);     
void clean(char *line);

// helpers for testing
void resetLabels() {
    extern int nlbl;  // use assembler's label counter
    nlbl = 0;
}

void test_getReg() {
    assert(getReg("r0") == 0);
    assert(getReg("r15") == 15);
    assert(getReg("r31") == 31);
}

void test_toNum() {
    assert(toNum("123") == 123);
    assert(toNum("0") == 0);
    assert(toNum("0010") == 10);
}

void test_labels() {
    resetLabels();
    addLbl("start", 0x1000);
    addLbl("loop", 0x1004);
    assert(findLbl("start") == 0x1000);
    assert(findLbl("loop") == 0x1004);
}

void test_tokenize() {
    char t[8][64];
    int n = tokenize("mov r1, r2", t);
    assert(n == 3);
    assert(strcmp(t[0], "mov") == 0);
    assert(strcmp(t[1], "r1") == 0);
    assert(strcmp(t[2], "r2") == 0);
}

void test_tryMacro() {
    FILE *f = tmpfile();
    char t[8][64];
    uint64_t addr = 0;

    strcpy(t[0], "halt");
    int matched = tryMacro(f, t, 1, &addr);
    assert(matched);
    assert(addr == 4);

    strcpy(t[0], "clr");
    strcpy(t[1], "r1");
    matched = tryMacro(f, t, 2, &addr);
    assert(matched);
    assert(addr == 8);

    fclose(f);
}

void test_toMC() {
    char t[8][64];
    strcpy(t[0], "add");
    strcpy(t[1], "r1");
    strcpy(t[2], "r2");
    strcpy(t[3], "r3");
    uint32_t mc = toMC(t, 4);
    assert(mc != 0);

    strcpy(t[0], "addi");
    strcpy(t[1], "r1");
    strcpy(t[2], "10");
    mc = toMC(t, 3);
    assert(mc != 0);
}

void test_clean() {
    char s[128];
    strcpy(s, "mov r1, r2 ; comment\n");
    clean(s);
    assert(strcmp(s, "mov r1, r2 ") == 0);
}

int main() {
    printf("Running Assembler tests...\n");

    test_getReg();
    test_toNum();
    test_labels();
    test_tokenize();
    test_tryMacro();
    test_toMC();
    test_clean();

    printf("All tests passed!\n");
    return 0;
}
