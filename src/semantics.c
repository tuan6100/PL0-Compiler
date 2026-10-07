#include <stdio.h>
#include <string.h>
#include "semantics.h"
#include "parser.h"

static SymbolTable symbolTable;
static int currentLevel = 0;
static int globalRootVarCount = 0;

void initSymbolTable() {
    symbolTable.count = 0;
    globalRootVarCount = 0;
    currentLevel = 0;
}

void enterObject(char *name, ObjectType type, double value, int size, int isString, int isStatic) {
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
    obj->constIsString = 0;
    obj->constString[0] = '\0';
    obj->level = (isStatic || currentLevel <= 1) ? 1 : currentLevel;
    obj->isStatic = isStatic;
    obj->size = ((type == OBJ_VARIABLE || type == OBJ_PARAMETER) && size > 0) ? size : 0;
    obj->isRuntimeArray = 0;
    obj->lengthAddress = -1;
    obj->initSize = 0;
    obj->isString = (type == OBJ_VARIABLE || type == OBJ_PARAMETER) ? isString : 0;
    obj->isRefParam = 0;
    obj->paramCount = 0;
    obj->totalParamSlots = 0;
    obj->hasReturnValue = 0;
    obj->returnDimCount = 0;
    obj->isImmutable = 0;
    obj->dimCount = 0;
    for (int k = 0; k < MAX_PROC_PARAMS; k++) {
        obj->paramIsRef[k] = 0;
        obj->paramSize[k] = 0;
        obj->paramDimCount[k] = 0;
        for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
            obj->paramDims[k][d] = 0;
        }
    }
    for (int k = 0; k < MAX_ARRAY_DIMS; k++) {
        obj->returnDims[k] = 0;
        obj->dims[k] = 0;
        obj->dimAddr[k] = DIM_ADDR_UNUSED;
    }

    if (type == OBJ_VARIABLE) {
        if (isStatic || currentLevel <= 1) {
            obj->level = 1;
            obj->address = 3 + globalRootVarCount;
            globalRootVarCount += obj->size;
        } else {
            int varCount = 0;
            int sIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
            for (int j = sIdx; j < symbolTable.count - 1; j++) {
                if (symbolTable.symbols[j].type == OBJ_VARIABLE && !symbolTable.symbols[j].isStatic) {
                    varCount += symbolTable.symbols[j].size;
                }
            }
            obj->address = 3 + varCount;
        }
    }
}

void enter(char *name, ObjectType type, double value, int size, int isString) {
    enterObject(name, type, value, size, isString, 0);
}

int getVarCount() {
    if (currentLevel <= 1) {
        return globalRootVarCount;
    }
    int varCount = 0;
    int startIdx = (currentLevel > 0) ? symbolTable.prev_count[currentLevel - 1] : 0;
    for (int i = startIdx; i < symbolTable.count; i++) {
        if (symbolTable.symbols[i].type == OBJ_VARIABLE && !symbolTable.symbols[i].isStatic) {
            varCount += symbolTable.symbols[i].size;
        }
    }
    return varCount;
}

int getCurrentLevel() {
    return currentLevel;
}

Object* lookup(char *name) {
    for (int i = symbolTable.count - 1; i >= 0; i--) {
        if (strcmp(symbolTable.symbols[i].name, name) == 0) {
            return &symbolTable.symbols[i];
        }
    }
    return NULL;
}

void enterBlock() {
    if (currentLevel >= MAX_NESTING_LEVEL) {
        error("too many nested blocks");
    }
    symbolTable.prev_count[currentLevel++] = symbolTable.count;
}

void exitBlock() {
    if (currentLevel > 0) {
        symbolTable.count = symbolTable.prev_count[--currentLevel];
    } else {
        error("no block to exit");
    }
}
