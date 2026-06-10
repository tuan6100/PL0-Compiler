#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantic.h"

#include <ctype.h>

static Symbol table[MAX_SYMBOLS];
static int    tableSize = 0;
static int    currentLevel = 0;

static void semanticError(const char *msg) {
    fprintf(stderr, "Semantic Error: %s\n", msg);
    exit(1);
}

void initSymbolTable(void) {
    tableSize    = 0;
    currentLevel = 0;
    addSymbol("READLN",  SYM_PROCEDURE, 0);
    addSymbol("READ",  SYM_PROCEDURE, 0);
    addSymbol("WRITELN", SYM_PROCEDURE, 0);
    addSymbol("WRITE", SYM_PROCEDURE, 0);
}

void enterScope(void) {
    currentLevel++;
}

void leaveScope(void) {
    while (tableSize > 0 && table[tableSize - 1].level == currentLevel)
        tableSize--;
    currentLevel--;
}

int strcmpIgnoreCase(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (toupper((unsigned char)*s1) != toupper((unsigned char)*s2))
            return 1;
        s1++; s2++;
    }
    return *s1 != *s2;
}

void addSymbol(const char *name, SymbolType type, int value) {
    for (int i = tableSize - 1; i >= 0; i--) {
        if (table[i].level < currentLevel) break;
        if (strcmpIgnoreCase(table[i].name, name) == 0) {
            char buf[80];
            sprintf(buf, "'%s' is already declared in this scope", name);
            semanticError(buf);
        }
    }
    if (tableSize >= MAX_SYMBOLS)
        semanticError("Symbol table overflow");

    strncpy(table[tableSize].name, name, 10);
    table[tableSize].name[10] = '\0';
    table[tableSize].type     = type;
    table[tableSize].level    = currentLevel;
    table[tableSize].value    = value;
    tableSize++;
}

Symbol *lookupSymbol(const char *name) {
    for (int i = tableSize - 1; i >= 0; i--) {
        if (strcmpIgnoreCase(table[i].name, name) == 0)
            return &table[i];
    }
    return NULL;
}

void checkDeclared(const char *name) {
    if (lookupSymbol(name) == NULL) {
        char buf[80];
        sprintf(buf, "'%s' is not declared", name);
        semanticError(buf);
    }
}

void checkNotDeclared(const char *name) {
    for (int i = tableSize - 1; i >= 0; i--) {
        if (table[i].level < currentLevel) break;
        if (strcmpIgnoreCase(table[i].name, name) == 0) {
            char buf[80];
            sprintf(buf, "'%s' is already declared in this scope", name);
            semanticError(buf);
        }
    }
}

void checkIsVar(const char *name) {
    Symbol *s = lookupSymbol(name);
    if (s == NULL) {
        char buf[80];
        sprintf(buf, "'%s' is not declared", name);
        semanticError(buf);
    }
    if (s->type != SYM_VAR) {
        char buf[80];
        sprintf(buf, "'%s' is not a variable", name);
        semanticError(buf);
    }
}

void checkIsConst(const char *name) {
    Symbol *s = lookupSymbol(name);
    if (s == NULL) {
        char buf[80];
        sprintf(buf, "'%s' is not declared", name);
        semanticError(buf);
    }
    if (s->type != SYM_CONST) {
        char buf[80];
        sprintf(buf, "'%s' is not a constant", name);
        semanticError(buf);
    }
}

void checkIsProcedure(const char *name) {
    Symbol *s = lookupSymbol(name);
    if (s == NULL) {
        char buf[80];
        sprintf(buf, "'%s' is not declared", name);
        semanticError(buf);
    }
    if (s->type != SYM_PROCEDURE) {
        char buf[80];
        sprintf(buf, "'%s' is not a procedure", name);
        semanticError(buf);
    }
}
