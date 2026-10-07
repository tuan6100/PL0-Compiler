#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include "codegen.h"
#include "parser.h"
#include "scanner.h"
#include "semantics.h"

Instruction code[MAX_CODE_SIZE];
int cx = 0;

#define STACK_SIZE 5000
double stack[STACK_SIZE];
static int allocLenAt[STACK_SIZE];

#define MAX_STRING_LITERALS 128
static char stringLiterals[MAX_STRING_LITERALS][MAX_STRING_LEN + 1];
static int stringLiteralCount = 0;
static double lastReturnDims[MAX_ARRAY_DIMS];
static int lastReturnDimCount = 0;
static int lastReturnBaseAddr = -1;

static int stackIndexFromValue(double v, const char *context) {
    int idx = (int)v;
    if ((double)idx != v) {
        error(context);
    }
    return idx;
}

static void appendToStringAt(int dstAddr, const char *src) {
    if (dstAddr < 0 || dstAddr >= STACK_SIZE) {
        error("string destination out of stack bounds");
    }
    int end = dstAddr;
    while (end < STACK_SIZE && stack[end] != 0) {
        end++;
    }
    if (end >= STACK_SIZE) {
        error("unterminated string in stack");
    }
    int i = 0;
    while (src[i] != '\0' && end < STACK_SIZE - 1) {
        stack[end++] = (unsigned char)src[i++];
    }
    stack[end] = 0;
}

static void appendStringFromStack(int dstAddr, int srcAddr) {
    if (srcAddr < 0 || srcAddr >= STACK_SIZE) {
        error("string source out of stack bounds");
    }
    char temp[MAX_STRING_LEN + 1];
    int i = 0;
    while (srcAddr + i < STACK_SIZE && i < MAX_STRING_LEN && stack[srcAddr + i] != 0) {
        temp[i] = (char)stack[srcAddr + i];
        i++;
    }
    temp[i] = '\0';
    appendToStringAt(dstAddr, temp);
}

static void printArrayElements(int baseAddr, int currentDim, int dimCount, const int *dims, int *offset) {
    putchar('[');
    int extent = dims[currentDim];
    if (currentDim == dimCount - 1) {
        for (int i = 0; i < extent; i++) {
            if (i > 0) printf(", ");
            int addr = baseAddr + (*offset)++;
            if (addr >= 0 && addr < STACK_SIZE) {
                printf("%g", stack[addr]);
            } else {
                printf("0");
            }
        }
    } else {
        for (int i = 0; i < extent; i++) {
            if (i > 0) printf(", ");
            printArrayElements(baseAddr, currentDim + 1, dimCount, dims, offset);
        }
    }
    putchar(']');
}

static void appendArrayElementsToString(int dstAddr, int baseAddr, int currentDim, int dimCount, const int *dims, int *offset) {
    appendToStringAt(dstAddr, "[");
    int extent = dims[currentDim];
    if (currentDim == dimCount - 1) {
        for (int i = 0; i < extent; i++) {
            if (i > 0) appendToStringAt(dstAddr, ", ");
            int addr = baseAddr + (*offset)++;
            char temp[32];
            if (addr >= 0 && addr < STACK_SIZE) {
                sprintf(temp, "%g", stack[addr]);
            } else {
                strcpy(temp, "0");
            }
            appendToStringAt(dstAddr, temp);
        }
    } else {
        for (int i = 0; i < extent; i++) {
            if (i > 0) appendToStringAt(dstAddr, ", ");
            appendArrayElementsToString(dstAddr, baseAddr, currentDim + 1, dimCount, dims, offset);
        }
    }
    appendToStringAt(dstAddr, "]");
}

int addStringLiteral(const char *literal) {
    if (stringLiteralCount >= MAX_STRING_LITERALS) {
        error("too many string literals");
    }
    strncpy(stringLiterals[stringLiteralCount], literal, MAX_STRING_LEN);
    stringLiterals[stringLiteralCount][MAX_STRING_LEN] = '\0';
    return stringLiteralCount++;
}

void emit(OpCode op, int l, double a) {
    if (cx >= MAX_CODE_SIZE) {
        error("code generation overflow");
    }
    code[cx].op = op;
    code[cx].l = l;
    code[cx].a = a;
    cx++;
}

static const char *opNames[] = {
    "LIT", "OPR", "LOD", "STO", "CAL", "INT", "JMP", "JPC",
    "LDA", "LDI", "STI", "RDI", "WRI", "WRS", "WRL", "STS",
    "WNL", "SCLR", "CATL", "CATV", "CATI", "DUP", "ALC",
    "RETV", "LEN", "POP", "LRD", "CHK", "WRA", "CATA"
};
#define OP_COUNT ((int)(sizeof(opNames) / sizeof(opNames[0])))

const char *getOpCodeName(OpCode op) {
    if ((int)op >= 0 && (int)op < OP_COUNT) {
        return opNames[op];
    }
    return "UNKNOWN";
}

OpCode getOpCodeByName(const char *name) {
    for (int i = 0; i < OP_COUNT; i++) {
        if (strcmp(name, opNames[i]) == 0) {
            return (OpCode)i;
        }
    }
    if (strcmp(name, "SCL") == 0) return SCLR;
    if (strcmp(name, "CTL") == 0) return CATL;
    if (strcmp(name, "CTV") == 0) return CATV;
    if (strcmp(name, "CTI") == 0) return CATI;
    if (strcmp(name, "CTA") == 0) return CATA;
    if (strcmp(name, "RTV") == 0) return RETV;
    return (OpCode)-1;
}

void listCode() {
    if (stringLiteralCount > 0) {
        for (int i = 0; i < stringLiteralCount; i++) {
            printf("[%2d] \"%s\"\n", i, stringLiterals[i]);
        }
    }
    for (int i = 0; i < cx; i++) {
        printf("%3d %-4s %2d %g\n", i, getOpCodeName(code[i].op), code[i].l, code[i].a);
    }
}

void resetCodeGen() {
    cx = 0;
    stringLiteralCount = 0;
    memset(code, 0, sizeof(code));
    memset(stringLiterals, 0, sizeof(stringLiterals));
    memset(stack, 0, sizeof(stack));
    memset(allocLenAt, 0, sizeof(allocLenAt));
    memset(lastReturnDims, 0, sizeof(lastReturnDims));
    lastReturnDimCount = 0;
    lastReturnBaseAddr = -1;
}

static void writeEscapedString(FILE *fp, const char *s) {
    fputc('"', fp);
    for (int i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            fputs("\\\"", fp);
        } else if (c == '\\') {
            fputs("\\\\", fp);
        } else if (c == '\n') {
            fputs("\\n", fp);
        } else if (c == '\r') {
            fputs("\\r", fp);
        } else if (c == '\t') {
            fputs("\\t", fp);
        } else if (c < 32 || c >= 127) {
            fprintf(fp, "\\x%02X", c);
        } else {
            fputc(c, fp);
        }
    }
    fputc('"', fp);
}

static int readEscapedString(FILE *fp, char *dst, size_t maxLen) {
    int ch = fgetc(fp);
    while (ch == ' ' || ch == '\t') ch = fgetc(fp);
    if (ch != '"') return 0;
    size_t len = 0;
    while ((ch = fgetc(fp)) != EOF && ch != '"' && ch != '\n' && ch != '\r') {
        if (ch == '\\') {
            int next = fgetc(fp);
            if (next == 'n') ch = '\n';
            else if (next == 'r') ch = '\r';
            else if (next == 't') ch = '\t';
            else if (next == '"') ch = '"';
            else if (next == '\\') ch = '\\';
            else if (next == 'x') {
                int h1 = fgetc(fp);
                int h2 = fgetc(fp);
                char hex[3] = {(char)h1, (char)h2, '\0'};
                ch = (int)strtol(hex, NULL, 16);
            } else {
                ch = next;
            }
        }
        if (len + 1 < maxLen) {
            dst[len++] = (char)ch;
        }
    }
    dst[len] = '\0';
    return (ch == '"');
}

#define PCODE_BIN_MAGIC "PL0B"
#define PCODE_BIN_VERSION 1
#define PCODE_TXT_MAGIC "PL0_PCODE_TEXT v1"

int savePCodeBinary(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file %s for writing\n", filename);
        return -1;
    }
    if (fwrite(PCODE_BIN_MAGIC, 1, 4, fp) != 4) { fclose(fp); return -1; }
    uint8_t version = PCODE_BIN_VERSION;
    uint8_t flags = 0;
    fwrite(&version, 1, 1, fp);
    fwrite(&flags, 1, 1, fp);

    uint32_t strCount = (uint32_t)stringLiteralCount;
    fwrite(&strCount, sizeof(uint32_t), 1, fp);
    for (int i = 0; i < stringLiteralCount; i++) {
        uint32_t len = (uint32_t)strlen(stringLiterals[i]);
        fwrite(&len, sizeof(uint32_t), 1, fp);
        if (len > 0) {
            fwrite(stringLiterals[i], 1, len, fp);
        }
    }

    uint32_t codeCount = (uint32_t)cx;
    fwrite(&codeCount, sizeof(uint32_t), 1, fp);
    for (int i = 0; i < cx; i++) {
        uint8_t op = (uint8_t)code[i].op;
        int32_t l = (int32_t)code[i].l;
        double a = code[i].a;
        fwrite(&op, 1, 1, fp);
        fwrite(&l, sizeof(int32_t), 1, fp);
        fwrite(&a, sizeof(double), 1, fp);
    }

    fclose(fp);
    return 0;
}

int savePCodeText(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file %s for writing\n", filename);
        return -1;
    }
    fprintf(fp, "%s\n\n", PCODE_TXT_MAGIC);

    fprintf(fp, "STRINGS %d\n", stringLiteralCount);
    for (int i = 0; i < stringLiteralCount; i++) {
        fprintf(fp, "%d ", i);
        writeEscapedString(fp, stringLiterals[i]);
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");

    fprintf(fp, "CODE %d\n", cx);
    for (int i = 0; i < cx; i++) {
        fprintf(fp, "%d %s %d %.*g\n", i, getOpCodeName(code[i].op), code[i].l, 17, code[i].a);
    }

    fclose(fp);
    return 0;
}

int savePCode(const char *filename, PCodeFormat format) {
    if (format == PCODE_FMT_TEXT) {
        return savePCodeText(filename);
    }
    return savePCodeBinary(filename);
}

int loadPCode(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open pcode file %s for reading\n", filename);
        return -1;
    }

    char magic[16];
    memset(magic, 0, sizeof(magic));
    size_t nread = fread(magic, 1, 4, fp);
    if (nread < 4) {
        fprintf(stderr, "Error: %s is not a valid P-Code file (too short)\n", filename);
        fclose(fp);
        return -1;
    }

    resetCodeGen();

    if (memcmp(magic, PCODE_BIN_MAGIC, 4) == 0) {
        uint8_t version = 0, flags = 0;
        if (fread(&version, 1, 1, fp) != 1 || fread(&flags, 1, 1, fp) != 1) {
            fprintf(stderr, "Error: corrupted binary header in %s\n", filename);
            fclose(fp);
            return -1;
        }
        if (version != PCODE_BIN_VERSION) {
            fprintf(stderr, "Error: unsupported binary P-Code version %d in %s\n", version, filename);
            fclose(fp);
            return -1;
        }
        uint32_t strCount = 0;
        if (fread(&strCount, sizeof(uint32_t), 1, fp) != 1 || strCount > MAX_STRING_LITERALS) {
            fprintf(stderr, "Error: invalid string count in %s\n", filename);
            fclose(fp);
            return -1;
        }
        stringLiteralCount = (int)strCount;
        for (int i = 0; i < stringLiteralCount; i++) {
            uint32_t len = 0;
            if (fread(&len, sizeof(uint32_t), 1, fp) != 1 || len > MAX_STRING_LEN) {
                fprintf(stderr, "Error: invalid string length in %s\n", filename);
                fclose(fp);
                return -1;
            }
            if (len > 0) {
                if (fread(stringLiterals[i], 1, len, fp) != len) {
                    fprintf(stderr, "Error: failed reading string data in %s\n", filename);
                    fclose(fp);
                    return -1;
                }
            }
            stringLiterals[i][len] = '\0';
        }

        uint32_t codeCount = 0;
        if (fread(&codeCount, sizeof(uint32_t), 1, fp) != 1 || codeCount > MAX_CODE_SIZE) {
            fprintf(stderr, "Error: invalid instruction count in %s\n", filename);
            fclose(fp);
            return -1;
        }
        cx = (int)codeCount;
        for (int i = 0; i < cx; i++) {
            uint8_t op = 0;
            int32_t l = 0;
            double a = 0;
            if (fread(&op, 1, 1, fp) != 1 ||
                fread(&l, sizeof(int32_t), 1, fp) != 1 ||
                fread(&a, sizeof(double), 1, fp) != 1) {
                fprintf(stderr, "Error: failed reading instruction %d in %s\n", i, filename);
                fclose(fp);
                return -1;
            }
            code[i].op = (OpCode)op;
            code[i].l = (int)l;
            code[i].a = a;
        }
        fclose(fp);
        return 0;
    }

    rewind(fp);
    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Error: empty P-Code file %s\n", filename);
        fclose(fp);
        return -1;
    }
    char *p = line + strlen(line) - 1;
    while (p >= line && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')) {
        *p = '\0';
        p--;
    }
    if (strncmp(line, "PL0_PCODE_TEXT", 14) != 0 && strncmp(line, "# PL0", 5) != 0) {
        fprintf(stderr, "Error: %s is not a recognized PL/0 P-Code format\n", filename);
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\r' || *s == '\n' || *s == '\0') continue;

        if (strncmp(s, "STRINGS", 7) == 0) {
            int strCount = 0;
            if (sscanf(s + 7, "%d", &strCount) == 1) {
                for (int i = 0; i < strCount && i < MAX_STRING_LITERALS; i++) {
                    int idx = 0;
                    if (fscanf(fp, "%d", &idx) != 1) break;
                    char strBuf[MAX_STRING_LEN + 1];
                    if (!readEscapedString(fp, strBuf, sizeof(strBuf))) {
                        fprintf(stderr, "Error: malformed string literal %d in %s\n", idx, filename);
                        fclose(fp);
                        return -1;
                    }
                    if (idx >= 0 && idx < MAX_STRING_LITERALS) {
                        snprintf(stringLiterals[idx], sizeof(stringLiterals[idx]), "%s", strBuf);
                        if (idx >= stringLiteralCount) {
                            stringLiteralCount = idx + 1;
                        }
                    }
                }
            }
        } else if (strncmp(s, "CODE", 4) == 0) {
            int codeCount = 0;
            if (sscanf(s + 4, "%d", &codeCount) == 1) {
                for (int i = 0; i < codeCount && i < MAX_CODE_SIZE; i++) {
                    int idx = 0;
                    char opStr[32];
                    int l = 0;
                    double a = 0;
                    if (fscanf(fp, "%d %31s %d %lf", &idx, opStr, &l, &a) != 4) {
                        fprintf(stderr, "Error: malformed instruction line at index %d in %s\n", i, filename);
                        fclose(fp);
                        return -1;
                    }
                    OpCode op = getOpCodeByName(opStr);
                    if ((int)op < 0) {
                        fprintf(stderr, "Error: unknown opcode '%s' in %s\n", opStr, filename);
                        fclose(fp);
                        return -1;
                    }
                    code[i].op = op;
                    code[i].l = l;
                    code[i].a = a;
                    cx = i + 1;
                }
            }
        }
    }

    fclose(fp);
    return 0;
}

void optimizeCode() {
    for (int i = 0; i < cx - 2; i++) {
        if (code[i].op == LIT && code[i+1].op == LIT && code[i+2].op == OPR) {
            double val1 = code[i].a;
            double val2 = code[i+1].a;
            int op = (int)code[i+2].a;
            double result = 0.0;
            int foldable = 1;
            switch(op) {
                case 2:
                    result = val1 + val2;
                    break;
                case 3:
                    result = val1 - val2;
                    break;
                case 4:
                    result = val1 * val2;
                    break;
                case 5:
                    if (val2 != 0) {
                        result = val1 / val2;
                    } else {
                        foldable = 0;
                        break;
                    }
                    break;
                case 14:
                    if (val2 != 0) {
                        result = (double)((int)val1 % (int)val2);
                    } else {
                        foldable = 0;
                        break;
                    }
                    break;
                case 23:
                    if (val2 != 0) {
                        result = floor(val1 / val2);
                    } else {
                        foldable = 0;
                        break;
                    }
                    break;
                default:
                    foldable = 0;
                    break;
            }
            if (foldable) {
                code[i].a = result;
                for (int j = i + 1; j < cx - 2; j++) {
                    code[j] = code[j + 2];
                }
                cx -= 2;
                for (int j = 0; j < cx; j++) {
                    if (code[j].op == JMP || code[j].op == JPC || code[j].op == CAL) {
                        if (code[j].a > i + 2) {
                            code[j].a -= 2;
                        } else if (code[j].a > i) {
                            code[j].a = i;
                        }
                    }
                }
                i--;
            }
        }
    }
}

int base(int l, int b) {
    int bl = b;
    while (l > 0) {
        bl = stackIndexFromValue(stack[bl], "invalid static link in stack frame");
        l--;
    }
    return bl;
}

void interpret() {
    int p = 0; // Program counter
    int b = 1; // Base pointer
    int t = 0; // Top of stack

    memset(stack, 0, sizeof(stack));
    memset(allocLenAt, 0, sizeof(allocLenAt));
    memset(lastReturnDims, 0, sizeof(lastReturnDims));
    lastReturnDimCount = 0;
    lastReturnBaseAddr = -1;

    stack[1] = 0; // Static link
    stack[2] = 0; // Dynamic link
    stack[3] = 0; // Return address

    do {
        Instruction i = code[p++];
        switch (i.op) {
            case LIT:
                t++;
                stack[t] = i.a;
                break;

            case OPR:
                switch ((int)i.a) {
                    case 0: { // return
                        int paramSlots = i.l;
                        int retAddr = stackIndexFromValue(stack[b + 2], "invalid return address");
                        int dynLink = stackIndexFromValue(stack[b + 1], "invalid dynamic link");
                        t = b - paramSlots - 1;
                        p = retAddr;
                        b = dynLink;
                        break;
                    }
                    case 1: // negate
                        stack[t] = -stack[t];
                        break;
                    case 2: // +
                        t--;
                        stack[t] += stack[t+1];
                        break;
                    case 3: // -
                        t--;
                        stack[t] -= stack[t+1];
                        break;
                    case 4: // *
                        t--;
                        stack[t] *= stack[t+1];
                        break;
                    case 5: // /
                        t--;
                        stack[t] /= stack[t+1];
                        break;
                    case 6: // odd
                        stack[t] = fmod(stack[t], 2.0) != 0.0;
                        break;
                    case 7: // ==
                        t--;
                        stack[t] = stack[t] == stack[t+1];
                        break;
                    case 8: // !=
                        t--;
                        stack[t] = stack[t] != stack[t+1];
                        break;
                    case 9: // <
                        t--;
                        stack[t] = stack[t] < stack[t+1];
                        break;
                    case 10: // >=
                        t--;
                        stack[t] = stack[t] >= stack[t+1];
                        break;
                    case 11: // >
                        t--;
                        stack[t] = stack[t] >  stack[t+1];
                        break;
                    case 12: // <=
                        t--;
                        stack[t] = stack[t] <= stack[t+1];
                        break;
                    case 13: { // bitwise OR
                        int rhs = stackIndexFromValue(stack[t], "bitwise OR requires integer operands");
                        int lhs = stackIndexFromValue(stack[t - 1], "bitwise OR requires integer operands");
                        t--;
                        stack[t] = (double)(lhs | rhs);
                        break;
                    }
                    case 14: { // modulo
                        int rhs = stackIndexFromValue(stack[t], "modulo requires integer operands");
                        int lhs = stackIndexFromValue(stack[t - 1], "modulo requires integer operands");
                        if (rhs == 0) {
                            error("modulo by zero");
                        }
                        t--;
                        stack[t] = (double)(lhs % rhs);
                        break;
                    }
                    case 15: { // bitwise AND
                        int rhs = stackIndexFromValue(stack[t], "bitwise AND requires integer operands");
                        int lhs = stackIndexFromValue(stack[t - 1], "bitwise AND requires integer operands");
                        t--;
                        stack[t] = (double)(lhs & rhs);
                        break;
                    }
                    case 16: { // bitwise XOR
                        int rhs = stackIndexFromValue(stack[t], "bitwise XOR requires integer operands");
                        int lhs = stackIndexFromValue(stack[t - 1], "bitwise XOR requires integer operands");
                        t--;
                        stack[t] = (double)(lhs ^ rhs);
                        break;
                    }
                    case 17: { // bitwise NOT
                        int value = stackIndexFromValue(stack[t], "bitwise NOT requires integer operand");
                        stack[t] = (double)(~value);
                        break;
                    }
                    case 18: { // shift left
                        int rhs = stackIndexFromValue(stack[t], "shift-left requires integer operands");
                        int lhs = stackIndexFromValue(stack[t - 1], "shift-left requires integer operands");
                        if (rhs < 0) {
                            error("shift-left count must be non-negative");
                        }
                        t--;
                        stack[t] = (double)(lhs << rhs);
                        break;
                    }
                    case 19: { // shift right
                        int rhs = stackIndexFromValue(stack[t], "shift-right requires integer operands");
                        int lhs = stackIndexFromValue(stack[t - 1], "shift-right requires integer operands");
                        if (rhs < 0) {
                            error("shift-right count must be non-negative");
                        }
                        t--;
                        stack[t] = (double)(lhs >> rhs);
                        break;
                    }
                    case 20: // logical AND
                        t--;
                        stack[t] = (stack[t] != 0.0 && stack[t + 1] != 0.0);
                        break;
                    case 21: // logical OR
                        t--;
                        stack[t] = stack[t] != 0.0 || stack[t + 1] != 0.0;
                        break;
                    case 22: // logical NOT
                        stack[t] = stack[t] == 0.0;
                        break;
                    case 23: { // floor division (//)
                        double rhs = stack[t];
                        double lhs = stack[t - 1];
                        if (rhs == 0.0) {
                            error("division by zero");
                        }
                        t--;
                        stack[t] = floor(lhs / rhs);
                        break;
                    }
                }

                break;

            case LOD: t++; stack[t] = stack[base(i.l, b) + (int)i.a]; break;
            case STO: stack[base(i.l, b) + (int)i.a] = stack[t];
                 t--;
                 break;
            case LDA:
                t++;
                stack[t] = (double)(base(i.l, b) + (int)i.a);
                break;
            case LDI:
                stack[t] = stack[stackIndexFromValue(stack[t], "indirect load requires integer address")];
                break;
            case STI:
                stack[stackIndexFromValue(stack[t - 1], "indirect store requires integer address")] = stack[t];
                t -= 2;
                break;
            case RDI: {
                double value;
                if (scanf("%lf", &value) != 1) {
                    error("READ failed: expected numeric input");
                }
                stack[stackIndexFromValue(stack[t], "READ destination requires integer address")] = value;
                t--;
                break;
            }
            case WRI:
                printf("%g", stack[t]);
                t--;
                break;
            case WRS: {
                int addr = base(i.l, b) + (int)i.a;
                while (addr < STACK_SIZE && stack[addr] != 0) {
                    putchar((char)stack[addr]);
                    addr++;
                }
                break;
            }
            case WRL:
                if ((int)i.a < 0 || (int)i.a >= stringLiteralCount) {
                    error("invalid string literal reference");
                }
                printf("%s", stringLiterals[(int)i.a]);
                break;
            case STS: {
                if ((int)i.a < 0 || (int)i.a >= stringLiteralCount) {
                    error("invalid string literal reference");
                }
                const char *src = stringLiterals[(int)i.a];
                int addr = stackIndexFromValue(stack[t--], "STS destination requires integer address");
                int k = 0;
                while (k < MAX_STRING_LEN && src[k] != '\0') {
                    stack[addr + k] = (unsigned char)src[k];
                    k++;
                }
                stack[addr + k] = 0;
                break;
            }
            case WNL:
                putchar('\n');
                break;
            case SCLR: {
                int addr = stackIndexFromValue(stack[t--], "SCLR destination requires integer address");
                if (addr < 0 || addr >= STACK_SIZE) {
                    error("SCLR address out of bounds");
                }
                stack[addr] = 0;
                break;
            }
            case CATL: {
                int addr = stackIndexFromValue(stack[t--], "CATL destination requires integer address");
                if ((int)i.a < 0 || (int)i.a >= stringLiteralCount) {
                    error("invalid string literal reference");
                }
                appendToStringAt(addr, stringLiterals[(int)i.a]);
                break;
            }
            case CATV: {
                int srcAddr = stackIndexFromValue(stack[t--], "CATV source requires integer address");
                int dstAddr = stackIndexFromValue(stack[t--], "CATV destination requires integer address");
                appendStringFromStack(dstAddr, srcAddr);
                break;
            }
            case CATI: {
                double value = stack[t--];
                int dstAddr = stackIndexFromValue(stack[t--], "CATI destination requires integer address");
                char temp[32];
                sprintf(temp, "%g", value);
                appendToStringAt(dstAddr, temp);
                break;
            }
            case DUP:
                t++;
                stack[t] = stack[t - 1];
                break;
            case ALC: {
                int len = stackIndexFromValue(stack[t], "VLA length must be an integer");
                t--;
                if (len <= 0) {
                    error("VLA length must be positive");
                }
                int baseAddr = t + 1;
                if (baseAddr + len >= STACK_SIZE) {
                    error("VLA allocation exceeds stack capacity");
                }
                for (int k = 0; k < len; k++) {
                    stack[baseAddr + k] = 0;
                }
                allocLenAt[baseAddr] = len;
                t += len;
                t++;
                stack[t] = (double)baseAddr;
                break;
            }
            case RETV: {
                int returnDimCount = (int)i.a;
                if (returnDimCount < 0 || returnDimCount > MAX_ARRAY_DIMS) {
                    error("invalid RETV descriptor arity");
                }
                double retValue;
                if (returnDimCount > 0) {
                    if (t - returnDimCount < 0) {
                        error("RETV descriptor stack underflow");
                    }
                    int baseIdx = t - returnDimCount;
                    retValue = stack[baseIdx];
                    lastReturnBaseAddr = stackIndexFromValue(retValue, "array return base must be integer address");
                    lastReturnDimCount = returnDimCount;
                    for (int d = 0; d < returnDimCount; d++) {
                        lastReturnDims[d] = stack[baseIdx + 1 + d];
                    }
                    if (returnDimCount > 1 && lastReturnDims[0] <= 0) {
                        int tail = 1;
                        int knownTail = 1;
                        for (int d = 1; d < returnDimCount; d++) {
                            int dim = stackIndexFromValue(lastReturnDims[d], "array return dimension must be integer");
                            if (dim <= 0) {
                                knownTail = 0;
                                break;
                            }
                            tail *= dim;
                        }
                        if (knownTail && tail > 0) {
                            int baseAddr = stackIndexFromValue(retValue, "array return base must be integer address");
                            if (baseAddr >= 0 && baseAddr < STACK_SIZE && allocLenAt[baseAddr] > 0) {
                                lastReturnDims[0] = allocLenAt[baseAddr] / tail;
                            }
                        }
                    }
                    for (int d = returnDimCount; d < MAX_ARRAY_DIMS; d++) {
                        lastReturnDims[d] = 0;
                    }
                    t = baseIdx - 1;
                } else {
                    retValue = stack[t--];
                    lastReturnDimCount = 0;
                    lastReturnBaseAddr = -1;
                    for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                        lastReturnDims[d] = 0;
                    }
                }
                int paramSlots = (int)i.l;
                int retAddr = stackIndexFromValue(stack[b + 2], "invalid return address");
                int dynLink = stackIndexFromValue(stack[b + 1], "invalid dynamic link");
                t = b - paramSlots - 1;
                p = retAddr;
                b = dynLink;
                t++;
                stack[t] = retValue;
                break;
            }
            case LEN: {
                int addr = stackIndexFromValue(stack[t], "LEN source requires integer address");
                if (addr < 0 || addr >= STACK_SIZE) {
                    error("LEN address out of bounds");
                }
                int len = 0;
                while (addr + len < STACK_SIZE && stack[addr + len] != 0) {
                    len++;
                }
                stack[t] = len;
                break;
            }
            case POP:
                if (t <= 0) {
                    error("POP on empty stack");
                }
                t--;
                break;
            case LRD: {
                int d = (int)i.a;
                if (d < 0 || d >= MAX_ARRAY_DIMS) {
                    error("LRD index out of range");
                }
                if (d >= lastReturnDimCount) {
                    t++;
                    stack[t] = 0;
                } else {
                    if (d == 0 && lastReturnDims[0] <= 0 && lastReturnDimCount > 1 && lastReturnBaseAddr >= 0 && lastReturnBaseAddr < STACK_SIZE && allocLenAt[lastReturnBaseAddr] > 0) {
                        int tail = 1;
                        int knownTail = 1;
                        for (int td = 1; td < lastReturnDimCount; td++) {
                            int dim = stackIndexFromValue(lastReturnDims[td], "LRD dimension must be integer");
                            if (dim <= 0) {
                                knownTail = 0;
                                break;
                            }
                            tail *= dim;
                        }
                        if (knownTail && tail > 0) {
                            lastReturnDims[0] = allocLenAt[lastReturnBaseAddr] / tail;
                        }
                    }
                    t++;
                    stack[t] = lastReturnDims[d];
                }
                break;
            }
            case CHK: {
                int bound = stackIndexFromValue(stack[t--], "array bound must be integer");
                int idx = stackIndexFromValue(stack[t], "array index must be integer");
                if (idx < 0 || (bound > 0 && idx >= bound)) {
                    char errBuf[120];
                    if (bound > 0) {
                        snprintf(errBuf, sizeof(errBuf), "array index %d out of bounds (0..%d)", idx, bound - 1);
                    } else {
                        snprintf(errBuf, sizeof(errBuf), "array index %d out of bounds", idx);
                    }
                    error(errBuf);
                }
                break;
            }
            case WRA: {
                int rank = (int)i.l;
                if (rank <= 0) rank = 1;
                int dims[MAX_ARRAY_DIMS];
                for (int d = rank - 1; d >= 0; d--) {
                    dims[d] = stackIndexFromValue(stack[t--], "array dimension must be integer");
                }
                int baseAddr = stackIndexFromValue(stack[t--], "array base must be integer address");
                if (rank == 1 && dims[0] <= 0 && baseAddr >= 0 && baseAddr < STACK_SIZE && allocLenAt[baseAddr] > 0) {
                    dims[0] = allocLenAt[baseAddr];
                }
                if (dims[0] < 0) dims[0] = 0;
                int offset = 0;
                printArrayElements(baseAddr, 0, rank, dims, &offset);
                break;
            }
            case CATA: {
                int rank = (int)i.l;
                if (rank <= 0) rank = 1;
                int dims[MAX_ARRAY_DIMS];
                for (int d = rank - 1; d >= 0; d--) {
                    dims[d] = stackIndexFromValue(stack[t--], "array dimension must be integer");
                }
                int baseAddr = stackIndexFromValue(stack[t--], "array base must be integer address");
                int dstAddr = stackIndexFromValue(stack[t--], "CATA destination requires integer address");
                if (rank == 1 && dims[0] <= 0 && baseAddr >= 0 && baseAddr < STACK_SIZE && allocLenAt[baseAddr] > 0) {
                    dims[0] = allocLenAt[baseAddr];
                }
                if (dims[0] < 0) dims[0] = 0;
                int offset = 0;
                appendArrayElementsToString(dstAddr, baseAddr, 0, rank, dims, &offset);
                break;
            }
            case CAL:
                stack[t + 1] = base(i.l, b);
                stack[t + 2] = b;
                stack[t + 3] = p;
                b = t + 1;
                p = (int)i.a;
                break;
            case INT:
                t += (int)i.a;
                break;
            case JMP:
                p = (int)i.a;
                break;
            case JPC:
                if (stack[t] == 0) {
                    p = (int)i.a;
                }
                t--;
                break;
        }
    } while (p != 0);
}
