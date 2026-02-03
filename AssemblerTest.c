#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef TESTING

int testmain(int argc, char **argv);

// add this to reset state between tests
extern int labelCount;
void reset() {
    labelCount = 0;
}

// helper to compare two binary files
bool compareFiles(char *file1, char *file2) {
    FILE *f1 = fopen(file1, "rb");
    FILE *f2 = fopen(file2, "rb");

    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return false;
    }

    int ch1, ch2;
    bool match = true;
    int bytePos = 0;

    // loop through every byte until the end of one file
    do {
        ch1 = fgetc(f1);
        ch2 = fgetc(f2);
        if (ch1 != ch2) {
            match = false;
            printf("    Mismatch at byte %d: got 0x%02x, expected 0x%02x\n", 
                   bytePos, (unsigned char)ch1, (unsigned char)ch2);
            break;
        }
        bytePos++;
    } while (ch1 != EOF && ch2 != EOF);

    // check if files have different lengths
    if (match && (ch1 != EOF || ch2 != EOF)) {
        match = false;
        printf("    Files have different lengths\n");
    }

    fclose(f1);
    fclose(f2);
    return match;
}

// helper to print hexdump of a file
void printHexdump(char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("    Cannot open %s for hexdump\n", filename);
        return;
    }

    printf("    Hexdump of %s:\n", filename);
    unsigned char buffer[16];
    size_t bytesRead;
    size_t offset = 0;

    while ((bytesRead = fread(buffer, 1, 16, f)) > 0) {
        // print offset
        printf("    %08zx  ", offset);

        // print hex values
        for (size_t i = 0; i < 16; i++) {
            if (i < bytesRead) {
                printf("%02x ", buffer[i]);
            } else {
                printf("   ");
            }
            if (i == 7) printf(" ");
        }

        printf(" |");

        // print ascii representation
        for (size_t i = 0; i < bytesRead; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                printf("%c", buffer[i]);
            } else {
                printf(".");
            }
        }

        printf("|\n");
        offset += bytesRead;
    }

    fclose(f);
}

void testHelper(char *args[], int argc, char *expectedFile, char *testName) {
    printf("Running: %s\n", testName);
    
    // reset assembler state before each test
    reset();
    
    int result = testmain(argc, args);
    
    if (result != 0) {
        printf("FAIL: %s (assembler returned error code %d)\n\n", testName, result);
        return;
    }

    // if no expected file, just show output
    if (expectedFile == NULL) {
        printf("PASS: %s (no comparison file)\n", testName);
        printHexdump(args[3]);
        printf("\n");
        return;
    }

    if (compareFiles(args[3], expectedFile)) {
        printf("PASS: %s\n\n", testName);
    } else {
        printf("FAIL: %s\n", testName);
        printf("    Binary files %s and %s do not match.\n", args[3], expectedFile);
        printf("\n");
        printf("    Expected output:\n");
        printHexdump(expectedFile);
        printf("\n");
        printf("    Actual output:\n");
        printHexdump(args[3]);
        printf("\n");
    }
}

void simpleCommands() {
    char *a1[] = {
        "assembler", 
        "input.tk", 
        "interm.tk", 
        "output.tko"
    };
    
    // just show output, don't compare yet
    testHelper(a1, 4, NULL, "Simple Commands");
}

// test with just basic instructions
void testBasicArithmetic() {
    // create a simple test file
    FILE *f = fopen("test_basic.tk", "w");
    fprintf(f, ".code\n");
    fprintf(f, "\tadd r1, r2, r3\n");
    fprintf(f, "\tsub r4, r5, r6\n");
    fprintf(f, "\thalt\n");
    fclose(f);
    
    char *args[] = {
        "assembler",
        "test_basic.tk",
        "test_basic_interm.tk",
        "test_basic.tko"
    };
    
    testHelper(args, 4, NULL, "Basic Arithmetic");
    
    // show the intermediate file
    printf("Intermediate file:\n");
    FILE *interm = fopen("test_basic_interm.tk", "r");
    if (interm) {
        char line[256];
        while (fgets(line, sizeof(line), interm)) {
            printf("    %s", line);
        }
        fclose(interm);
    }
    printf("\n");
}

// test macros
void testMacros() {
    FILE *f = fopen("test_macros.tk", "w");
    fprintf(f, ".code\n");
    fprintf(f, "\tclr r0\n");
    fprintf(f, "\tpush r1\n");
    fprintf(f, "\tpop r2\n");
    fprintf(f, "\thalt\n");
    fclose(f);
    
    char *args[] = {
        "assembler",
        "test_macros.tk",
        "test_macros_interm.tk",
        "test_macros.tko"
    };
    
    testHelper(args, 4, NULL, "Macros Test");
    
    printf("Intermediate file:\n");
    FILE *interm = fopen("test_macros_interm.tk", "r");
    if (interm) {
        char line[256];
        while (fgets(line, sizeof(line), interm)) {
            printf("    %s", line);
        }
        fclose(interm);
    }
    printf("\n");
}

// test labels and ld macro
void testLabelsAndLD() {
    FILE *f = fopen("test_labels.tk", "w");
    fprintf(f, ".data\n");
    fprintf(f, "\t:mydata\n");
    fprintf(f, "\t42\n");
    fprintf(f, ".code\n");
    fprintf(f, ":start\n");
    fprintf(f, "\tld r1, :mydata\n");
    fprintf(f, "\thalt\n");
    fclose(f);
    
    char *args[] = {
        "assembler",
        "test_labels.tk",
        "test_labels_interm.tk",
        "test_labels.tko"
    };
    
    testHelper(args, 4, NULL, "Labels and LD Test");
    
    printf("Intermediate file:\n");
    FILE *interm = fopen("test_labels_interm.tk", "r");
    if (interm) {
        char line[256];
        while (fgets(line, sizeof(line), interm)) {
            printf("    %s", line);
        }
        fclose(interm);
    }
    printf("\n");
}

// test error handling
void testErrorHandling() {
    printf("Testing error handling:\n\n");
    
    // test 1: invalid register
    reset();
    FILE *f = fopen("test_error1.tk", "w");
    fprintf(f, ".code\n");
    fprintf(f, "\tadd r99, r1, r2\n");
    fclose(f);
    
    char *args1[] = {"assembler", "test_error1.tk", "err1_interm.tk", "err1.tko"};
    printf("Test: Invalid register (should fail)\n");
    int result = testmain(4, args1);
    if (result != 0) {
        printf("PASS: Correctly rejected invalid register\n\n");
    } else {
        printf("FAIL: Should have rejected invalid register\n\n");
    }
    
    // test 2: wrong number of arguments
    reset();
    f = fopen("test_error2.tk", "w");
    fprintf(f, ".code\n");
    fprintf(f, "\tadd r1, r2\n");
    fclose(f);
    
    char *args2[] = {"assembler", "test_error2.tk", "err2_interm.tk", "err2.tko"};
    printf("Test: Wrong argument count (should fail)\n");
    result = testmain(4, args2);
    if (result != 0) {
        printf("PASS: Correctly rejected wrong argument count\n\n");
    } else {
        printf("FAIL: Should have rejected wrong argument count\n\n");
    }
    
    // test 3: negative literal on unsigned instruction
    reset();
    f = fopen("test_error3.tk", "w");
    fprintf(f, ".code\n");
    fprintf(f, "\taddi r1, -5\n");
    fclose(f);
    
    char *args3[] = {"assembler", "test_error3.tk", "err3_interm.tk", "err3.tko"};
    printf("Test: Negative literal on unsigned instruction (should fail)\n");
    result = testmain(4, args3);
    if (result != 0) {
        printf("PASS: Correctly rejected negative literal\n\n");
    } else {
        printf("FAIL: Should have rejected negative literal\n\n");
    }
}

int main() {
    printf("========================================\n");
    printf("TINKER ASSEMBLER TEST SUITE\n");
    printf("========================================\n\n");
    
    simpleCommands();
    testBasicArithmetic();
    testMacros();
    testLabelsAndLD();
    testErrorHandling();
    
    printf("========================================\n");
    printf("All tests finished.\n");
    printf("========================================\n");
    return 0;
}

#endif