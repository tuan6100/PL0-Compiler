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

void enter(char *name, ObjectType type, int value, int size, int isString) {
    // Check if the identifier is already declared in the current scope
    int startIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
    for (int i = startIdx; i < symbolTable.count; i++) {
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
    obj->size = (type == OBJ_VARIABLE && size > 0) ? size : 0;
    obj->isString = (type == OBJ_VARIABLE) ? isString : 0;
    obj->isRefParam = 0;
    obj->paramCount = 0;
    for (int k = 0; k < MAX_PROC_PARAMS; k++) {
        obj->paramIsRef[k] = 0;
    }

    if (type == OBJ_VARIABLE) {
        // Compute current scope stack offset for this variable/array/string.
        int varCount = 0;
        int sIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
        for (int j = sIdx; j < symbolTable.count - 1; j++) {
            if (symbolTable.symbols[j].type == OBJ_VARIABLE) {
                varCount += symbolTable.symbols[j].size;
            }
        }
        obj->address = 3 + varCount; // static link, dynamic link, return address
    }
}

int getVarCount(void) {
    int varCount = 0;
    int startIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
    for (int i = startIdx; i < symbolTable.count; i++) {
        if (symbolTable.symbols[i].type == OBJ_VARIABLE) {
            varCount += symbolTable.symbols[i].size;
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
