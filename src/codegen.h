#ifndef CODEGEN_H
#define CODEGEN_H

#include "semantics.h"

typedef enum {
    LIT, // Load literal value onto stack
    OPR, // Execute arithmetic or logical operation
    LOD, // Load variable value from a given level and address onto stack
    STO, // Store value from top of stack into a given level and address
    CAL, // Call procedure at a given level and entry address
    INT, // Increment stack pointer (allocate space for variables)
    JMP, // Unconditional jump to a target address
    JPC  // Jump to a target address if top of stack is zero (condition false)
} OpCode;

typedef struct {
    OpCode op;
    int l; // Level
    int a; // Address/Value/Operator
} Instruction;

#define MAX_CODE_SIZE 1000

extern Instruction code[MAX_CODE_SIZE];
extern int cx; // Code index

void emit(OpCode op, int l, int a);
void listCode(void);
void optimizeCode(void);
void interpret(void);

#endif
