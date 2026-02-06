#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

// assembler functions
int getRegisterNumber(const char *s);
uint64_t parseNumber(const char *s);
void addLabel(const char *name, int addr);
int findLabel(const char *name);
int parseCodeLine(char *line, int address);
int parseDataLine(char *line, int address);
void cleanLine(char *s);

// helpers for testing
void resetLabels() {
    extern int numLabels;
    numLabels = 0;
}

void test_getRegisterNumber() {
    assert(getRegisterNumber("r0") == 0);
    assert(getRegisterNumber("r15") == 15);
    assert(getRegisterNumber("r31") == 31);
}

void test_parseNumber() {
    assert(parseNumber("123") == 123);
    assert(parseNumber("0") == 0);
    assert(parseNumber("0010") == 10);
    assert(parseNumber("0xFF") == 255);
}

void test_labels() {
    resetLabels();
    addLabel("start", 0x1000);
    addLabel("loop", 0x1004);
    assert(findLabel("start") == 0x1000);
    assert(findLabel("loop") == 0x1004);
    assert(findLabel("nonexistent") == -1);
}

void test_cleanLine() {
    char s[128];
    strcpy(s, "mov r1, r2 ; comment\n");
    cleanLine(s);
    assert(strcmp(s, "mov r1, r2 ; comment") == 0); // cleanLine removes only newline
}

void test_parseCodeLine() {
    char line[128];
    strcpy(line, "add r1, r2, r3");
    int addr = parseCodeLine(line, 0x1000);
    assert(addr > 0); // address advanced
}

void test_parseDataLine() {
    char line[128];
    strcpy(line, "12345");
    int addr = parseDataLine(line, 0x1000);
    assert(addr == 0x1000 + 8); // data increments by 8
}

int main() {
    printf("Running Assembler tests...\n");

    test_getRegisterNumber();
    test_parseNumber();
    test_labels();
    test_cleanLine();
    test_parseCodeLine();
    test_parseDataLine();

    printf("All tests passed!\n");
    return 0;
}
