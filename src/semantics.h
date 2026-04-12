#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "scanner.h"

typedef enum {
    OBJ_CONSTANT,
    OBJ_VARIABLE,
    OBJ_PARAMETER,
    OBJ_PROCEDURE
} ObjectType;

#define MAX_PROC_PARAMS 16

typedef struct {
    char name[MAX_IDENT_LEN + 1];
    ObjectType type;
    double value; // For numeric constants
    int constIsString; // 1 when constant value is a string literal
    char constString[MAX_STRING_LEN + 1]; // String constant payload
    int level; // For variables and procedures
    int address; // For code generation (future)
    int size; // Number of stack cells used by this symbol (variables only)
    int isString; // 1 for STRING declarations, 0 otherwise
    int isRefParam; // 1 when this symbol is a VAR parameter
    int paramCount; // Procedure parameter count
    int paramIsRef[MAX_PROC_PARAMS]; // Procedure parameter passing mode
    int paramSize[MAX_PROC_PARAMS]; // Formal parameter size (1 for scalar, >1 for array)
    int hasReturnValue; // 1 if procedure contains RETURN with a value
} Object;

#define MAX_SYMBOL_TABLE_SIZE 100
#define MAX_NESTING_LEVEL 10

typedef struct {
    Object symbols[MAX_SYMBOL_TABLE_SIZE];
    int count;
    int prev_count[MAX_NESTING_LEVEL]; // To restore count on block exit
} SymbolTable;

void initSymbolTable(void);
void enter(char *name, ObjectType type, double value, int size, int isString);
Object* lookup(char *name);
void enterBlock(void);
void exitBlock(void);
int getVarCount(void);
int getCurrentLevel(void);

#endif
