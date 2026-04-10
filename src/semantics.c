#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantics.h"
#include "parser.h"

static SymbolTable symbolTable;
static int currentLevel = 0;

void initSymbolTable(void) {
    symbolTable.count = 0;
}

void enter(char *name, ObjectType type, int value) {
    // Check if the identifier is already declared in the current scope
    int i;
    int startIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
    for (i = startIdx; i < symbolTable.count; i++) {
        if (strcmp(symbolTable.symbols[i].name, name) == 0) {
            char errorMsg[100];
            sprintf(errorMsg, "identifier '%s' already declared in this scope", name);
            error(errorMsg);
        }
    }

    if (symbolTable.count >= MAX_SYMBOL_TABLE_SIZE) {
        error("symbol table overflow");
    }

    Object *obj = &symbolTable.symbols[symbolTable.count++];
    strncpy(obj->name, name, MAX_IDENT_LEN);
    obj->name[MAX_IDENT_LEN] = '\0';
    obj->type = type;
    obj->value = value;
    obj->level = currentLevel;

    if (type == OBJ_VARIABLE) {
        // Find how many variables are already in this scope to assign address
        int varCount = 0;
        int sIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
        for (int j = sIdx; j < symbolTable.count - 1; j++) {
            if (symbolTable.symbols[j].type == OBJ_VARIABLE) {
                varCount++;
            }
        }
        obj->address = 3 + varCount; // 3 is for static link, dynamic link, return address
    }
}

int getVarCount(void) {
    int varCount = 0;
    int startIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
    for (int i = startIdx; i < symbolTable.count; i++) {
        if (symbolTable.symbols[i].type == OBJ_VARIABLE) {
            varCount++;
        }
    }
    return varCount;
}

int getCurrentLevel(void) {
    return currentLevel;
}

Object* lookup(char *name) {
    // Search from inner scope to outer scope
    for (int i = symbolTable.count - 1; i >= 0; i--) {
        if (strcmp(symbolTable.symbols[i].name, name) == 0) {
            return &symbolTable.symbols[i];
        }
    }
    return NULL;
}

void enterBlock(void) {
    if (currentLevel >= MAX_NESTING_LEVEL) {
        error("too many nested blocks");
    }
    symbolTable.prev_count[currentLevel++] = symbolTable.count;
}

void exitBlock(void) {
    if (currentLevel > 0) {
        symbolTable.count = symbolTable.prev_count[--currentLevel];
    } else {
        error("no block to exit");
    }
}
