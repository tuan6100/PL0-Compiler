#include <stdio.h>
#include <string.h>
#include "codegen.h"
#include "parser.h"
#include "scanner.h"

Instruction code[MAX_CODE_SIZE];
int cx = 0;

#define MAX_STRING_LITERALS 128
static char stringLiterals[MAX_STRING_LITERALS][MAX_STRING_LEN + 1];
static int stringLiteralCount = 0;

int addStringLiteral(const char *literal) {
    if (stringLiteralCount >= MAX_STRING_LITERALS) {
        error("too many string literals");
    }
    strncpy(stringLiterals[stringLiteralCount], literal, MAX_STRING_LEN);
    stringLiterals[stringLiteralCount][MAX_STRING_LEN] = '\0';
    return stringLiteralCount++;
}

void emit(OpCode op, int l, int a) {
    if (cx >= MAX_CODE_SIZE) {
        error("code generation overflow");
    }
    code[cx].op = op;
    code[cx].l = l;
    code[cx].a = a;
    cx++;
}

void listCode(void) {
    printf("--- Code Generation ---\n");
    for (int i = 0; i < cx; i++) {
        printf("%3d %-3s %d %d\n", i,
            code[i].op == LIT ? "LIT" :
                code[i].op == OPR ? "OPR" :
                code[i].op == LOD ? "LOD" :
                code[i].op == STO ? "STO" :
                code[i].op == CAL ? "CAL" :
                code[i].op == INT ? "INT" :
                code[i].op == JMP ? "JMP" :
                code[i].op == JPC ? "JPC" :
                code[i].op == LDA ? "LDA" :
                code[i].op == LDI ? "LDI" :
                code[i].op == STI ? "STI" :
                code[i].op == RDI ? "RDI" :
                code[i].op == WRI ? "WRI" :
                code[i].op == WRS ? "WRS" :
                code[i].op == WRL ? "WRL" :
                code[i].op == STS ? "STS" : "WNL",
            code[i].l, code[i].a);
    }
}

// Simple optimization: Constant Folding and basic dead code elimination can be added here
void optimizeCode(void) {
    for (int i = 0; i < cx - 2; i++) {
        if (code[i].op == LIT && code[i+1].op == LIT && code[i+2].op == OPR) {
            int val1 = code[i].a;
            int val2 = code[i+1].a;
            int op = code[i+2].a;
            int result = 0;
            int foldable = 1;
            switch(op) {
                case 2: result = val1 + val2; break; // +
                case 3: result = val1 - val2; break; // -
                case 4: result = val1 * val2; break; // *
                case 5: if (val2 != 0) result = val1 / val2; else foldable = 0; break; // /
                default: foldable = 0; break;
            }
            if (foldable) {
                code[i].a = result;
                // Move following instructions up by 2
                for (int j = i + 1; j < cx - 2; j++) {
                    code[j] = code[j + 2];
                }
                cx -= 2;
                i--; // Check again
            }
        }
    }
}

#define STACK_SIZE 500
int stack[STACK_SIZE];

int base(int l, int b) {
    int bl = b;
    while (l > 0) {
        bl = stack[bl];
        l--;
    }
    return bl;
}

void interpret(void) {
    int p = 0; // Program counter
    int b = 1; // Base pointer
    int t = 0; // Top of stack

    stack[1] = 0; // Static link
    stack[2] = 0; // Dynamic link
    stack[3] = 0; // Return address

    do {
        Instruction i = code[p++];
        switch (i.op) {
            case LIT: t++; stack[t] = i.a; break;
            case OPR:
                switch (i.a) {
                    case 0: // return
                        t = b - 1;
                        p = stack[t + 3];
                        b = stack[t + 2];
                        break;
                    case 1: stack[t] = -stack[t]; break; // negate
                    case 2: t--; stack[t] += stack[t+1]; break; // +
                    case 3: t--; stack[t] -= stack[t+1]; break; // -
                    case 4: t--; stack[t] *= stack[t+1]; break; // *
                    case 5: t--; stack[t] /= stack[t+1]; break; // /
                    case 6: stack[t] = (stack[t] % 2 != 0); break; // odd
                    case 7: t--; stack[t] = (stack[t] == stack[t+1]); break; // ==
                    case 8: t--; stack[t] = (stack[t] != stack[t+1]); break; // !=
                    case 9: t--; stack[t] = (stack[t] <  stack[t+1]); break; // <
                    case 10: t--; stack[t] = (stack[t] >= stack[t+1]); break; // >=
                    case 11: t--; stack[t] = (stack[t] >  stack[t+1]); break; // >
                    case 12: t--; stack[t] = (stack[t] <= stack[t+1]); break; // <=
                    case 13: // print
                        printf("%d\n", stack[t]);
                        t--;
                        break;
                }
                break;
            case LOD: t++; stack[t] = stack[base(i.l, b) + i.a]; break;
            case STO: stack[base(i.l, b) + i.a] = stack[t]; 
                 t--; 
                 break;
            case LDA:
                t++;
                stack[t] = base(i.l, b) + i.a;
                break;
            case LDI:
                stack[t] = stack[stack[t]];
                break;
            case STI:
                stack[stack[t - 1]] = stack[t];
                t -= 2;
                break;
            case RDI: {
                int value;
                if (scanf("%d", &value) != 1) {
                    error("READ failed: expected integer input");
                }
                stack[stack[t]] = value;
                t--;
                break;
            }
            case WRI:
                printf("%d", stack[t]);
                t--;
                break;
            case WRS: {
                int addr = base(i.l, b) + i.a;
                while (addr < STACK_SIZE && stack[addr] != 0) {
                    putchar((char)stack[addr]);
                    addr++;
                }
                break;
            }
            case WRL:
                if (i.a < 0 || i.a >= stringLiteralCount) {
                    error("invalid string literal reference");
                }
                printf("%s", stringLiterals[i.a]);
                break;
            case STS: {
                if (i.a < 0 || i.a >= stringLiteralCount) {
                    error("invalid string literal reference");
                }
                const char *src = stringLiterals[i.a];
                int addr = stack[t--];
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
            case CAL:
                stack[t + 1] = base(i.l, b);
                stack[t + 2] = b;
                stack[t + 3] = p;
                b = t + 1;
                p = i.a;
                break;
            case INT: t += i.a; break;
            case JMP: p = i.a; break;
            case JPC: if (stack[t] == 0) p = i.a; t--; break;
        }
    } while (p != 0);
}
