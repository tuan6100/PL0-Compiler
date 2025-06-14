#ifndef CODEGEN_H
#define CODEGEN_H

typedef enum {
    LIT, // Load literal value onto stack
    OPR, // Execute arithmetic or logical operation
    LOD, // Load variable value from a given level and address onto stack
    STO, // Store value from top of stack into a given level and address
    CAL, // Call procedure at a given level and entry address
    INT, // Increment stack pointer (allocate space for variables)
    JMP, // Unconditional jump to a target address
    JPC, // Jump to a target address if top of stack is zero (condition false)
    LDA, // Push absolute stack address for a symbol
    LDI, // Load value indirectly from address on top of stack
    STI, // Store value indirectly (address below top, value on top)
    RDI, // Read integer from stdin and store to address on top of stack
    WRI, // Write integer value on top of stack
    WRS, // Write zero-terminated string variable at level/address
    WRL, // Write interned string literal by index
    STS, // Store interned string literal to address on top of stack
    WNL, // Write newline
    SCLR, // Clear zero-terminated string at address on top of stack
    CATL, // Append interned string literal to destination address on top of stack
    CATV, // Append source string address (top) to destination (below top)
    CATI, // Append integer value (top) to destination (below top)
    DUP,  // Duplicate top-of-stack value
    ALC,  // Allocate runtime array cells; consume length and push base address
    RETV, // Return from procedure with value on top of stack
    LEN,  // Compute string length from address on top of stack
    POP,  // Discard top-of-stack value
    LRD,  // Load last returned array dimension by index
    CHK,  // Check array index bounds
    WRA,  // Write array given base address and dimension descriptors
    CATA  // Append array string representation to string destination
} OpCode;

typedef struct {
    OpCode op;
    int l; // Level
    double a; // Address/Value/Operator (LIT uses floating-point payload)
} Instruction;

#define MAX_CODE_SIZE 1000

extern Instruction code[MAX_CODE_SIZE];
extern int cx; // Code index

typedef enum {
    PCODE_FMT_BINARY,
    PCODE_FMT_TEXT
} PCodeFormat;

void emit(OpCode op, int l, double a);
const char *getOpCodeName(OpCode op);
OpCode getOpCodeByName(const char *name);
void listCode();
void optimizeCode();
void interpret();
int addStringLiteral(const char *literal);
void resetCodeGen();
int savePCodeBinary(const char *filename);
int savePCodeText(const char *filename);
int savePCode(const char *filename, PCodeFormat format);
int loadPCode(const char *filename);

#endif
