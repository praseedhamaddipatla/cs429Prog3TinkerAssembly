#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#ifndef TESTING

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
    // valid registers
    assert(getRegisterNumber("r0") == 0);
    assert(getRegisterNumber("r15") == 15);
    assert(getRegisterNumber("r31") == 31);
}

void test_parseNumber() {
    // decimal numbers
    assert(parseNumber("123") == 123);
    assert(parseNumber("0") == 0);

    // hexadecimal numbers
    assert(parseNumber("0xFF") == 255);
    assert(parseNumber("0X10") == 16);

    // large numbers
    assert(parseNumber("18446744073709551615") == 18446744073709551615ULL);

    // label-like input
    assert(parseNumber(":label") == 0); // assembler interprets as label reference

    // edge cases
    assert(parseNumber("000") == 0);
    assert(parseNumber("0x0") == 0);
}

void test_labels() {
    resetLabels();
    addLabel("start", 0x1000);
    addLabel("loop", 0x1004);
    addLabel("_underscore", 0x1010);
    addLabel("END", 0x1020);

    // existing labels
    assert(findLabel("start") == 0x1000);
    assert(findLabel("loop") == 0x1004);
    assert(findLabel("_underscore") == 0x1010);
    assert(findLabel("END") == 0x1020);

    // non-existent labels
    assert(findLabel("nonexistent") == -1);
    assert(findLabel("") == -1);
}

void test_cleanLine() {
    char s[128];

    strcpy(s, "mov r1, r2 ; comment\n");
    cleanLine(s);
    assert(strcmp(s, "mov r1, r2 ; comment") == 0);

    strcpy(s, "add r0, r1\n");
    cleanLine(s);
    assert(strcmp(s, "add r0, r1") == 0);

    strcpy(s, "\n");
    cleanLine(s);
    assert(strcmp(s, "") == 0);

    strcpy(s, "nop");
    cleanLine(s);
    assert(strcmp(s, "nop") == 0);
}

void test_parseCodeLine() {
    char line[128];

    // basic instructions
    strcpy(line, "add r1, r2, r3");
    int addr = parseCodeLine(line, 0x1000);
    assert(addr > 0);

    strcpy(line, "sub r4, r5, r6");
    addr = parseCodeLine(line, 0x1010);
    assert(addr > 0);

    strcpy(line, "mov r7, r8");
    addr = parseCodeLine(line, 0x1020);
    assert(addr > 0);

    strcpy(line, "ld r9, 0xFF");
    addr = parseCodeLine(line, 0x1030);
    assert(addr > 0);

    // invalid instruction
    strcpy(line, "foobar r1, r2");
    addr = parseCodeLine(line, 0x1040);
    assert(addr > 0); // parseCodeLine should handle unknown instructions gracefully

    // macros / edge cases
    strcpy(line, "push r10");
    addr = parseCodeLine(line, 0x1050);
    assert(addr > 0);

    strcpy(line, "pop r11");
    addr = parseCodeLine(line, 0x1060);
    assert(addr > 0);

    strcpy(line, "halt");
    addr = parseCodeLine(line, 0x1070);
    assert(addr > 0);

    strcpy(line, "clr r12");
    addr = parseCodeLine(line, 0x1080);
    assert(addr > 0);

    strcpy(line, "in r13, r14");
    addr = parseCodeLine(line, 0x1090);
    assert(addr > 0);

    strcpy(line, "out r15, r0");
    addr = parseCodeLine(line, 0x10A0);
    assert(addr > 0);
}

void test_parseDataLine() {
    char line[128];

    // single numbers
    strcpy(line, "12345");
    int addr = parseDataLine(line, 0x1000);
    assert(addr == 0x1000 + 8);

    strcpy(line, "0");
    addr = parseDataLine(line, 0x1010);
    assert(addr == 0x1010 + 8);

    strcpy(line, "18446744073709551615");
    addr = parseDataLine(line, 0x1020);
    assert(addr == 0x1020 + 8);

    // label-like input
    strcpy(line, ":start");
    addr = parseDataLine(line, 0x1030);
    assert(addr == 0x1030 + 8);

    // edge cases
    strcpy(line, "00000");
    addr = parseDataLine(line, 0x1040);
    assert(addr == 0x1040 + 8);
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

#endif