#ifndef PL0_SEMANTIC_H
#define PL0_SEMANTIC_H

#define MAX_SYMBOLS 100

// Symbol types in PL/0
typedef enum {
    SYM_CONST,
    SYM_VAR,
    SYM_PROCEDURE
} SymbolType;

// A single entry in the symbol table
typedef struct {
    char name[11];
    SymbolType type;
    int level;
    int value;
} Symbol;

void initSymbolTable(void);
void enterScope(void);
void leaveScope(void);
void addSymbol(const char *name, SymbolType type, int value);
Symbol *lookupSymbol(const char *name);
void checkDeclared(const char *name);
void checkNotDeclared(const char *name);
void checkIsVar(const char *name);
void checkIsConst(const char *name);
void checkIsProcedure(const char *name);

#endif //PL0_SEMANTIC_H
