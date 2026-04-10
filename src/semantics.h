#ifndef SEMANTICS_H
#define SEMANTICS_H

#include "scanner.h"

typedef enum {
    OBJ_CONSTANT,
    OBJ_VARIABLE,
    OBJ_PROCEDURE
} ObjectType;

typedef struct {
    char name[MAX_IDENT_LEN + 1];
    ObjectType type;
    int value; // For constants
    int level; // For variables and procedures
    int address; // For code generation (future)
} Object;

#define MAX_SYMBOL_TABLE_SIZE 100
#define MAX_NESTING_LEVEL 10

typedef struct {
    Object symbols[MAX_SYMBOL_TABLE_SIZE];
    int count;
    int prev_count[MAX_NESTING_LEVEL]; // To restore count on block exit
} SymbolTable;

void initSymbolTable(void);
void enter(char *name, ObjectType type, int value);
Object* lookup(char *name);
void enterBlock(void);
void exitBlock(void);
int getVarCount(void);
int getCurrentLevel(void);

#endif
