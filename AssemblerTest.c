#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef TESTING

// external functions from assembler
extern int getRegisterNumber(const char *reg);
extern uint64_t convertToNumber(const char *lit);
extern void addLabelToArray(const char *lbl, uint64_t addr);
extern uint64_t findLabelAddress(const char *lbl);
extern int numLbls;
extern void cleanLine(char *line);
extern int splitIntoTokens(char *line, char toks[][64]);
extern void firstPass(FILE *in, FILE *mid);
extern void secondPass(FILE *mid, FILE *out);

int testmain(int argc, char **argv);

void resetLabels()
{
    numLbls = 0;
}

void test_parseReg()
{
    printf("\n parseReg \n");

    int r = getRegisterNumber("r0");
    if (r == 0)
        printf("PASS: r0 = 0\n");
    else
        printf("FAIL: r0\n");

    r = getRegisterNumber("r31");
    if (r == 31)
        printf("PASS: r31 = 31\n");
    else
        printf("FAIL: r31\n");
}

void test_parseLit()
{
    printf("\n parseLit \n");

    uint64_t v = convertToNumber("42");
    if (v == 42)
        printf("PASS: 42\n");
    else
        printf("FAIL: 42\n");

    v = convertToNumber("0x10");
    if (v == 16)
        printf("PASS: 0x10 = 16\n");
    else
        printf("FAIL: 0x10\n");
}

void test_lbls()
{
    printf("\n labels \n");
    resetLabels();

    addLabelToArray("start", 0x1000);
    uint64_t a = findLabelAddress("start");
    if (a == 0x1000)
        printf("PASS: start at 0x1000\n");
    else
        printf("FAIL: lbl lookup\n");

    resetLabels();
}

void test_tok()
{
    printf("\n tokenize \n");

    char buf[256];
    strcpy(buf, "add r1, r2, r3");

    char toks[8][64];
    int n = splitIntoTokens(buf, toks);

    if (n == 4 && strcmp(toks[0], "add") == 0)
        printf("PASS: add instr\n");
    else
        printf("FAIL: tok (got %d)\n", n);

    strcpy(buf, "mov (r31)(0), r4");
    n = splitIntoTokens(buf, toks);

    if (n == 4)
        printf("PASS: mov instr\n");
    else
        printf("FAIL: mov tok\n");
}

void test_trim()
{
    printf("\n trim \n");

    char buf[256];
    strcpy(buf, "add r1, r2, r3 ; comment\n");
    cleanLine(buf);

    if (!strchr(buf, ';'))
        printf("PASS: rm comment\n");
    else
        printf("FAIL: trim\n");

    strcpy(buf, "halt\n");
    cleanLine(buf);

    if (!strchr(buf, '\n'))
        printf("PASS: rm newline\n");
    else
        printf("FAIL: newline\n");
}

void test_basic()
{
    printf("\n basic prog \n");
    resetLabels();

    FILE *f = fopen("t1.tk", "w");
    fprintf(f, ".code\n\tadd r1, r2, r3\n\thalt\n");
    fclose(f);

    char *args[] = {"asm", "t1.tk", "t1_mid.tk", "t1.tko"};
    int res = testmain(4, args);

    if (res == 0)
    {
        printf("PASS: assembled\n");

        FILE *out = fopen("t1.tko", "rb");
        if (out)
        {
            fseek(out, 0, SEEK_END);
            long sz = ftell(out);
            if (sz == 8)
                printf("PASS: size = %ld bytes\n", sz);
            else
                printf("FAIL: size = %ld\n", sz);
            fclose(out);
        }
    }
    else
    {
        printf("FAIL: asm failed\n");
    }

    resetLabels();
}

void test_macros()
{
    printf("\n macros \n");
    resetLabels();

    FILE *f = fopen("t2.tk", "w");
    fprintf(f, ".code\n\tclr r0\n\thalt\n");
    fclose(f);

    char *args[] = {"asm", "t2.tk", "t2_mid.tk", "t2.tko"};
    int res = testmain(4, args);

    if (res == 0)
    {
        printf("PASS: macro prog\n");

        FILE *mid = fopen("t2_mid.tk", "r");
        if (mid)
        {
            char line[256];
            int hasXor = 0, hasPriv = 0;

            while (fgets(line, sizeof(line), mid))
            {
                if (strstr(line, "xor r0, r0, r0"))
                    hasXor = 1;
                if (strstr(line, "priv"))
                    hasPriv = 1;
            }

            if (hasXor && hasPriv)
                printf("PASS: expanded\n");
            else
                printf("FAIL: not expanded\n");
            fclose(mid);
        }
    }
    else
    {
        printf("FAIL: macro failed\n");
    }

    resetLabels();
}

void test_lblProg()
{
    printf("\n labels in prog \n");
    resetLabels();

    FILE *f = fopen("t3.tk", "w");
    fprintf(f, ".code\n:start\n\tadd r1, r2, r3\n:loop\n\tsub r1, r1, r2\n\thalt\n");
    fclose(f);

    char *args[] = {"asm", "t3.tk", "t3_mid.tk", "t3.tko"};
    int res = testmain(4, args);

    if (res == 0)
        printf("PASS: w/ labels\n");
    else
        printf("FAIL: labels\n");

    resetLabels();
}

void test_data()
{
    printf("\n data section \n");
    resetLabels();

    FILE *f = fopen("t4.tk", "w");
    fprintf(f, ".data\n\t:mydata\n\t100\n.code\n\tadd r1, r2, r3\n\thalt\n");
    fclose(f);

    char *args[] = {"asm", "t4.tk", "t4_mid.tk", "t4.tko"};
    int res = testmain(4, args);

    if (res == 0)
    {
        printf("PASS: data prog\n");

        FILE *out = fopen("t4.tko", "rb");
        if (out)
        {
            uint64_t val;
            fread(&val, 8, 1, out);
            if (val == 100)
                printf("PASS: data = 100\n");
            else
                printf("FAIL: data = %llu\n", (unsigned long long)val);
            fclose(out);
        }
    }
    else
    {
        printf("FAIL: data failed\n");
    }

    resetLabels();
}

void test_errors()
{
    printf("\n error checking \n");

    resetLabels();
    FILE *f = fopen("e1.tk", "w");
    fprintf(f, ".code\n\tadd r99, r1, r2\n");
    fclose(f);

    char *args1[] = {"asm", "e1.tk", "e1.tk", "e1.tko"};
    if (testmain(4, args1) != 0)
        printf("PASS: bad reg\n");
    else
        printf("FAIL: should reject r99\n");

    resetLabels();
    f = fopen("e2.tk", "w");
    fprintf(f, ".code\n\tadd r1, r2\n");
    fclose(f);

    char *args2[] = {"asm", "e2.tk", "e2.tk", "e2.tko"};
    if (testmain(4, args2) != 0)
        printf("PASS: bad args\n");
    else
        printf("FAIL: should reject wrong args\n");

    resetLabels();
}

void test_p1_macros()
{
    printf("\n pass1: macros \n");
    resetLabels();

    FILE *f = fopen("p1.tk", "w");
    fprintf(f, ".code\n\tclr r0\n\tpush r1\n\tpop r2\n\thalt\n");
    fclose(f);

    FILE *in = fopen("p1.tk", "r");
    FILE *out = fopen("p1_out.tk", "w");
    firstPass(in, out);
    fclose(in);
    fclose(out);

    FILE *mid = fopen("p1_out.tk", "r");
    if (!mid)
    {
        printf("FAIL: no output\n");
        return;
    }

    char line[256];
    int xor = 0, subi = 0, mov = 0, addi = 0, priv = 0, macro = 0;

    while (fgets(line, sizeof(line), mid))
    {
        if (strstr(line, "xor"))
            xor = 1;
        if (strstr(line, "subi"))
            subi = 1;
        if (strstr(line, "mov"))
            mov = 1;
        if (strstr(line, "addi"))
            addi = 1;
        if (strstr(line, "priv"))
            priv = 1;
        if (strstr(line, "clr") || strstr(line, "push") ||
            strstr(line, "pop") || strstr(line, "halt"))
            macro = 1;
    }
    fclose(mid);

    if (xor && subi && mov && addi && priv && !macro)
        printf("PASS: all macros expanded\n");
    else
        printf("FAIL: macro expansion\n");

    resetLabels();
}

void test_p1_labels()
{
    printf("\n pass1: labels \n");
    resetLabels();

    FILE *f = fopen("p1_lbl.tk", "w");
    fprintf(f, ".data\n\t:mydata\n\t42\n.code\n:start\n\tld r1, :mydata\n\thalt\n");
    fclose(f);

    FILE *in = fopen("p1_lbl.tk", "r");
    FILE *out = fopen("p1_lbl_out.tk", "w");
    firstPass(in, out);
    fclose(in);
    fclose(out);

    FILE *mid = fopen("p1_lbl_out.tk", "r");
    if (!mid)
    {
        printf("FAIL: no output\n");
        return;
    }

    char line[256];
    int ld = 0, addi = 0;

    while (fgets(line, sizeof(line), mid))
    {
        if (strstr(line, "ld"))
            ld = 1;
        if (strstr(line, "addi") && strstr(line, "r1"))
            addi = 1;
    }
    fclose(mid);

    if (!ld && addi)
        printf("PASS: labels resolved\n");
    else
        printf("FAIL: lbl resolution\n");

    resetLabels();
}

void test_p2_binary()
{
    printf("\n pass2: binary \n");
    resetLabels();

    FILE *f = fopen("p2.tk", "w");
    fprintf(f, ".code\n\tadd r1, r2, r3\n\tsub r4, r5, r6\n");
    fclose(f);

    FILE *in = fopen("p2.tk", "r");
    FILE *out = fopen("p2.tko", "wb");
    secondPass(in, out);
    fclose(in);
    fclose(out);

    FILE *bin = fopen("p2.tko", "rb");
    if (!bin)
    {
        printf("FAIL: no output\n");
        return;
    }

    fseek(bin, 0, SEEK_END);
    long sz = ftell(bin);
    fseek(bin, 0, SEEK_SET);

    if (sz == 8)
    {
        printf("PASS: size = %ld\n", sz);

        uint32_t i1, i2;
        fread(&i1, 4, 1, bin);
        fread(&i2, 4, 1, bin);

        uint32_t op1 = (i1 >> 26) & 0x3F;
        if (op1 == 0x18)
            printf("PASS: add opcode\n");
        else
            printf("FAIL: add op = 0x%x\n", op1);

        uint32_t op2 = (i2 >> 26) & 0x3F;
        if (op2 == 0x1a)
            printf("PASS: sub opcode\n");
        else
            printf("FAIL: sub op = 0x%x\n", op2);
    }
    else
    {
        printf("FAIL: sz = %ld\n", sz);
    }

    fclose(bin);
    resetLabels();
}

void test_p2_data()
{
    printf("\n pass2: data \n");
    resetLabels();

    FILE *f = fopen("p2_d.tk", "w");
    fprintf(f, ".data\n\t100\n\t200\n.code\n\tadd r1, r2, r3\n");
    fclose(f);

    FILE *in = fopen("p2_d.tk", "r");
    FILE *out = fopen("p2_d.tko", "wb");
    secondPass(in, out);
    fclose(in);
    fclose(out);

    FILE *bin = fopen("p2_d.tko", "rb");
    if (bin)
    {
        uint64_t d1, d2;
        fread(&d1, 8, 1, bin);
        fread(&d2, 8, 1, bin);

        if (d1 == 100 && d2 == 200)
            printf("PASS: data vals\n");
        else
            printf("FAIL: data\n");

        fclose(bin);
    }
    else
    {
        printf("FAIL: no output\n");
    }

    resetLabels();
}

void test_integration()
{
    printf("\n p1 + p2 \n");
    resetLabels();

    FILE *f = fopen("int.tk", "w");
    fprintf(f, ".data\n\t:myval\n\t999\n.code\n:start\n\tclr r0\n\tadd r1, r2, r3\n\thalt\n");
    fclose(f);

    FILE *in = fopen("int.tk", "r");
    FILE *mid = fopen("int_mid.tk", "w");
    firstPass(in, mid);
    fclose(in);
    fclose(mid);

    mid = fopen("int_mid.tk", "r");
    FILE *out = fopen("int.tko", "wb");
    secondPass(mid, out);
    fclose(mid);
    fclose(out);

    FILE *bin = fopen("int.tko", "rb");
    if (bin)
    {
        fseek(bin, 0, SEEK_END);
        long sz = ftell(bin);

        if (sz == 20)
            printf("PASS: full pipeline\n");
        else
            printf("FAIL: sz = %ld (expected 20)\n", sz);

        fclose(bin);
    }
    else
    {
        printf("FAIL: no output\n");
    }

    resetLabels();
}

int main()
{
    printf("TESTS\n");
    
    // basic funcs
    test_parseReg();
    test_parseLit();
    test_lbls();
    test_tok();
    test_trim();

    // pass tests
    test_p1_macros();
    test_p1_labels();
    test_p2_binary();
    test_p2_data();
    test_integration();

    // full progs
    test_basic();
    test_macros();
    test_lblProg();
    test_data();
    test_errors();

    printf("\n DONE\n");
    return 0;
}

#endif