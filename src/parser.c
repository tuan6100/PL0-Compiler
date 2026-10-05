#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "scanner.h"
#include "parser.h"
#include "semantics.h"
#include "codegen.h"

extern TokenType token;
extern double    Num;
extern char      Id[MAX_IDENT_LEN + 1];
extern char      StringLiteral[MAX_STRING_LEN + 1];
extern int       TokenLine;
extern int       TokenColumn;
extern char      currentSourceFile[260];

static int pendingParamCount = 0;
static int pendingParamIsRef[MAX_PROC_PARAMS];
static int pendingParamSize[MAX_PROC_PARAMS];
static int pendingParamDimCount[MAX_PROC_PARAMS];
static int pendingParamDims[MAX_PROC_PARAMS][MAX_ARRAY_DIMS];
static int pendingParamSlotCount[MAX_PROC_PARAMS];
static char pendingParamName[MAX_PROC_PARAMS][MAX_IDENT_LEN + 1];
static int pendingParamLevel = -1;
static Object *currentProcedure = NULL;

#define MAX_INIT_EXPR_CODE 256
#define MAX_ARRAY_INIT_VALUES 512

typedef enum {
    VAR_INIT_SCALAR_EXPR,
    VAR_INIT_ARRAY_LITERAL
} VarInitKind;

typedef struct {
    VarInitKind kind;
    Object *target;
    Instruction exprCode[MAX_INIT_EXPR_CODE];
    int exprCount;
    double arrayValues[MAX_ARRAY_INIT_VALUES];
    int arrayCount;
} PendingVarInit;

typedef struct {
    Object *target;
    Instruction dimExprCode[MAX_ARRAY_DIMS][MAX_INIT_EXPR_CODE];
    int dimExprCount[MAX_ARRAY_DIMS];
    int dimCount;
} PendingRuntimeArrayInit;

static PendingVarInit pendingInitFrames[MAX_NESTING_LEVEL][MAX_SYMBOL_TABLE_SIZE];
static PendingRuntimeArrayInit pendingRuntimeArrayInitFrames[MAX_NESTING_LEVEL][MAX_SYMBOL_TABLE_SIZE];

static const char *interpExprPtr;

static int tokenStartsStringValueExpr(void);
static void emitAppendStringTerm(const Object *target);
static void emitSizeOfObjectValue(const Object *obj);
static void parseProcedureCallArguments(const Object *proc);
static void formatCurrentTokenDetail(char *buf, size_t bufSize);
static double parseVarInitializerValue(void);
static double parseConstExpression(void);
static Object *parseSizeOfTargetObject(int constMode);
static void parseInitExpression(Instruction *buf, int *count, int maxCount);
static void emitLoadObjectAddress(const Object *obj);
static void parseConstArrayLiteralValues(double *values, int *count, int maxCount);
static void parseVarArrayLiteralValues(double *values, int *count, int maxCount);
static int expectedArrayDims(const Object *obj);
static int isArrayLikeObject(const Object *obj);
static int emitIndexedAddress(const Object *obj, int requireIndex, int requireFullIndex, const char *context);
static void emitLoadArrayDim(const Object *obj, int dimIndex);

static int sizeofIndexDepth = 0;

static void initEmit(Instruction *buf, int *count, int maxCount, OpCode op, int l, double a) {
    if (*count >= maxCount) {
        error("initializer expression is too complex");
    }
    buf[*count].op = op;
    buf[*count].l = l;
    buf[*count].a = a;
    (*count)++;
}

static void initEmitLoadObjectValue(Instruction *buf, int *count, int maxCount, const Object *obj) {
    int l = getCurrentLevel() - obj->level;
    if (obj->isRefParam) {
        initEmit(buf, count, maxCount, LOD, l, obj->address);
        initEmit(buf, count, maxCount, LDI, 0, 0);
    } else {
        initEmit(buf, count, maxCount, LOD, l, obj->address);
    }
}

static void initEmitLoadObjectAddress(Instruction *buf, int *count, int maxCount, const Object *obj) {
    int l = getCurrentLevel() - obj->level;
    if (obj->isRefParam || (obj->type == OBJ_PARAMETER && obj->dimCount > 0)) {
        initEmit(buf, count, maxCount, LOD, l, obj->address);
    } else {
        initEmit(buf, count, maxCount, LDA, l, obj->address);
    }
}

typedef struct {
    TokenType token;
    double Num;
    int TokenLine;
    int TokenColumn;
    char Id[MAX_IDENT_LEN + 1];
    char StringLiteral[MAX_STRING_LEN + 1];
} SavedToken;

static SavedToken lookaheadToken;
static int hasLookahead = 0;

static TokenType peekNextToken(void) {
    if (!hasLookahead) {
        lookaheadToken.token = getToken();
        lookaheadToken.Num = Num;
        lookaheadToken.TokenLine = TokenLine;
        lookaheadToken.TokenColumn = TokenColumn;
        strcpy(lookaheadToken.Id, Id);
        strcpy(lookaheadToken.StringLiteral, StringLiteral);
        hasLookahead = 1;
    }
    return lookaheadToken.token;
}

void nextToken(void) {
    if (hasLookahead) {
        token = lookaheadToken.token;
        Num = lookaheadToken.Num;
        TokenLine = lookaheadToken.TokenLine;
        TokenColumn = lookaheadToken.TokenColumn;
        strcpy(Id, lookaheadToken.Id);
        strcpy(StringLiteral, lookaheadToken.StringLiteral);
        hasLookahead = 0;
    } else {
        token = getToken();
    }
    // printf(" %s", TabToken[Token]);
    // if(Token == TK_IDENT) printf("(%s) \n", Id);
    // else if(Token == TK_NUMBER) printf("(%g) \n", Num);
    // else printf("\n");
}

void error(const char msg[]) {
    char tokenDetail[320];
    formatCurrentTokenDetail(tokenDetail, sizeof(tokenDetail));
    printf("Error at %s: %d:%d: %s%s\n",
           currentSourceFile,
           TokenLine,
           TokenColumn,
           msg,
           tokenDetail);
    exit(1);
}

static void expect(TokenType expected) {
    if (token != expected) {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected '%s' but got '%s'", TabToken[expected], TabToken[token]);
        error(msg);
    }
    nextToken();
}

static void formatCurrentTokenDetail(char *buf, size_t bufSize) {
    if (bufSize == 0) {
        return;
    }
    buf[0] = '\0';
    switch (token) {
        case TK_IDENT:
            snprintf(buf, bufSize, " near identifier '%s'", Id);
            break;
        case TK_NUMBER:
            snprintf(buf, bufSize, " near number %g", Num);
            break;
        case TK_STRING:
            snprintf(buf, bufSize, " near string \"%s\"", StringLiteral);
            break;
        case TK_NONE:
            snprintf(buf, bufSize, " near end of file");
            break;
        default:
            snprintf(buf, bufSize, " near token '%s'", TabToken[token]);
            break;
    }
}

static double parseVarInitializerValue(void) {
    int sign = 1;
    if (token == SB_PLUS || token == SB_MINUS) {
        if (token == SB_MINUS) {
            sign = -1;
        }
        nextToken();
    }

    if (token == TK_NUMBER) {
        double value = Num;
        nextToken();
        return sign * value;
    }

    if (token == KW_NULL) {
        nextToken();
        return 0;
    }

    if (token == TK_IDENT) {
        Object *obj = lookup(Id);
        if (obj == NULL) {
            char msg[120];
            sprintf(msg, "VAR initializer undeclared identifier: %s", Id);
            error(msg);
        }
        if (obj->type != OBJ_CONSTANT || obj->constIsString) {
            error("VAR initializer expects numeric literal or numeric CONST");
        }
        double value = obj->value;
        nextToken();
        return sign * value;
    }

    error("VAR initializer expects numeric literal or numeric CONST");
    return 0;
}

static double parseConstFactor(void) {
    if (token == KW_SIZEOF) {
        nextToken();
        Object *obj = parseSizeOfTargetObject(1);
        int dims = expectedArrayDims(obj);
        int used = sizeofIndexDepth;
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                if (used > 0) {
                    error("SIZEOF: STRING constant does not support index");
                }
                return (double)strlen(obj->constString);
            }
            if (used > 0) {
                error("SIZEOF: scalar constant does not support index");
            }
            return 1;
        }
        if (obj->type == OBJ_VARIABLE || obj->type == OBJ_PARAMETER) {
            if (obj->isString) {
                if (used > 0) {
                    error("SIZEOF: STRING variable does not support index");
                }
                return obj->size > 0 ? obj->size : 1;
            }
            if (used > dims) {
                error("SIZEOF: too many indices");
            }
            if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
                error("SIZEOF in CONST expression requires compile-time sized symbol");
            }
            if (dims > 0) {
                if (obj->dimCount > 0) {
                    int rem = 1;
                    int knownRem = 1;
                    for (int d = used; d < obj->dimCount; d++) {
                        if (obj->dims[d] <= 0) {
                            knownRem = 0;
                            break;
                        }
                        rem *= obj->dims[d];
                    }
                    if (!knownRem) {
                        rem = 1;
                    }
                    return rem;
                }
                return used == 0 ? (double)obj->size : 1;
            }
            return 1;
        }
        error("SIZEOF: procedure is not supported");
        return 0;
    }

    if (token == TK_NUMBER) {
        double value = Num;
        nextToken();
        return value;
    }

    if (token == KW_NULL) {
        nextToken();
        return 0;
    }

    if (token == TK_IDENT) {
        Object *obj = lookup(Id);
        char identName[MAX_IDENT_LEN + 1];
        strcpy(identName, Id);
        if (obj == NULL) {
            char msg[120];
            sprintf(msg, "CONST initializer undeclared identifier: %s", identName);
            error(msg);
        }
        nextToken();
        if (obj->type == OBJ_PROCEDURE) {
            if (token == SB_LPARENT) {
                error("CONST initializer cannot call procedure");
            }
            error("CONST initializer requires numeric constant expression");
        }
        if (obj->type != OBJ_CONSTANT || obj->constIsString) {
            error("CONST initializer requires numeric constant expression");
        }
        return obj->value;
    }

    if (token == SB_LPARENT) {
        nextToken();
        double value = parseConstExpression();
        expect(SB_RPARENT);
        return value;
    }

    error("CONST initializer expects number, SIZEOF, identifier, or '(' expression ')' ");
    return 0;
}

static double parseConstTerm(void) {
    double value = parseConstFactor();
    while (token == SB_TIMES || token == SB_SLASH || token == SB_FLOORDIV || token == SB_PERCENT) {
        TokenType op = token;
        nextToken();
        double rhs = parseConstFactor();
        if (op == SB_TIMES) {
            value *= rhs;
        } else if (op == SB_SLASH) {
            if (rhs == 0) {
                error("CONST initializer division by zero");
            }
            value /= rhs;
        } else if (op == SB_FLOORDIV) {
            if (rhs == 0) {
                error("CONST initializer division by zero");
            }
            value = floor(value / rhs);
        } else {
            if (rhs == 0) {
                error("CONST initializer modulo by zero");
            }
            value = (double)((int)value % (int)rhs);
        }
    }
    return value;
}

static double parseConstExpression(void) {
    TokenType prefix = TK_NONE;
    if (token == SB_PLUS || token == SB_MINUS) {
        prefix = token;
        nextToken();
    }

    double value = parseConstTerm();
    if (prefix == SB_MINUS) {
        value = -value;
    }

    while (token == SB_PLUS || token == SB_MINUS) {
        TokenType op = token;
        nextToken();
        double rhs = parseConstTerm();
        value = (op == SB_PLUS) ? (value + rhs) : (value - rhs);
    }
    return value;
}

// Parse nested numeric array literals like [1, [2, 3], 4] into a flat row-major list.
static void parseConstArrayLiteralValues(double *values, int *count, int maxCount) {
    expect(SB_LBRACK);
    if (token != SB_RBRACK) {
        while (1) {
            if (token == SB_LBRACK) {
                parseConstArrayLiteralValues(values, count, maxCount);
            } else {
                if (*count >= maxCount) {
                    error("array initializer is too long");
                }
                values[(*count)++] = parseConstExpression();
            }
            if (token == SB_COMMA) {
                nextToken();
                continue;
            }
            break;
        }
    }
    expect(SB_RBRACK);
}

static void parseVarArrayLiteralValues(double *values, int *count, int maxCount) {
    expect(SB_LBRACK);
    if (token != SB_RBRACK) {
        while (1) {
            if (token == SB_LBRACK) {
                parseVarArrayLiteralValues(values, count, maxCount);
            } else {
                if (*count >= maxCount) {
                    error("array initializer is too long");
                }
                values[(*count)++] = parseVarInitializerValue();
            }
            if (token == SB_COMMA) {
                nextToken();
                continue;
            }
            break;
        }
    }
    expect(SB_RBRACK);
}

static Object *parseSizeOfTargetObject(int constMode) {
    sizeofIndexDepth = 0;
    expect(SB_LPARENT);
    if (token != TK_IDENT) {
        error("SIZEOF: expected identifier");
    }
    Object *obj = lookup(Id);
    if (obj == NULL) {
        char msg[120];
        sprintf(msg, "SIZEOF: undeclared identifier %s", Id);
        error(msg);
    }
    nextToken();

    while (token == SB_LBRACK) {
        nextToken();
        if (constMode) {
            (void)parseConstExpression();
        } else {
            expression();
            emit(POP, 0, 0);
        }
        expect(SB_RBRACK);
        sizeofIndexDepth++;
    }

    expect(SB_RPARENT);
    return obj;
}

static void parseInitFactor(Instruction *buf, int *count, int maxCount) {
    if (token == TK_NUMBER) {
        initEmit(buf, count, maxCount, LIT, 0, Num);
        nextToken();
        return;
    }

    if (token == KW_NULL) {
        initEmit(buf, count, maxCount, LIT, 0, 0);
        nextToken();
        return;
    }

    if (token == KW_SIZEOF) {
        nextToken();
        Object *obj = parseSizeOfTargetObject(0);
        int dims = expectedArrayDims(obj);
        int used = sizeofIndexDepth;
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                if (used > 0) {
                    error("SIZEOF initializer: STRING constant does not support index");
                }
                initEmit(buf, count, maxCount, LIT, 0, (double)strlen(obj->constString));
            } else {
                if (used > 0) {
                    error("SIZEOF initializer: scalar constant does not support index");
                }
                initEmit(buf, count, maxCount, LIT, 0, 1);
            }
        } else if (obj->type == OBJ_VARIABLE || obj->type == OBJ_PARAMETER) {
            if (obj->isString) {
                if (used > 0) {
                    error("SIZEOF initializer: STRING variable does not support index");
                }
                initEmit(buf, count, maxCount, LIT, 0, obj->size > 0 ? obj->size : 1);
            } else if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
                if (used > dims) {
                    error("SIZEOF initializer: too many indices");
                }
                if (obj->dimCount > 1) {
                    int emitted = 0;
                    for (int d = used; d < obj->dimCount; d++) {
                        if (obj->dims[d] > 0) {
                            initEmit(buf, count, maxCount, LIT, 0, obj->dims[d]);
                        } else if (obj->dimAddr[d] != DIM_ADDR_UNUSED) {
                            initEmit(buf, count, maxCount, LOD, getCurrentLevel() - obj->level, obj->dimAddr[d]);
                        } else if (d == 0 && obj->lengthAddress >= 0) {
                            initEmit(buf, count, maxCount, LOD, getCurrentLevel() - obj->level, obj->lengthAddress);
                        } else {
                            error("SIZEOF initializer: array dimension is not available");
                        }
                        if (emitted) {
                            initEmit(buf, count, maxCount, OPR, 0, 4);
                        }
                        emitted = 1;
                    }
                    if (!emitted) {
                        initEmit(buf, count, maxCount, LIT, 0, 1);
                    }
                } else if (used == 0) {
                    initEmit(buf, count, maxCount, LOD, getCurrentLevel() - obj->level, obj->lengthAddress);
                } else {
                    initEmit(buf, count, maxCount, LIT, 0, 1);
                }
            } else if (dims > 0) {
                if (used > dims) {
                    error("SIZEOF initializer: too many indices");
                }
                if (obj->dimCount > 0) {
                    int rem = 1;
                    int knownRem = 1;
                    int emitted = 0;
                    for (int d = used; d < obj->dimCount; d++) {
                        if (obj->dims[d] <= 0) {
                            knownRem = 0;
                            if (obj->dimAddr[d] != DIM_ADDR_UNUSED) {
                                initEmit(buf, count, maxCount, LOD, getCurrentLevel() - obj->level, obj->dimAddr[d]);
                                if (emitted) {
                                    initEmit(buf, count, maxCount, OPR, 0, 4);
                                }
                                emitted = 1;
                                continue;
                            }
                            break;
                        }
                        rem *= obj->dims[d];
                        if (!knownRem) {
                            initEmit(buf, count, maxCount, LIT, 0, obj->dims[d]);
                            if (emitted) {
                                initEmit(buf, count, maxCount, OPR, 0, 4);
                            }
                            emitted = 1;
                        }
                    }
                    if (knownRem) {
                        initEmit(buf, count, maxCount, LIT, 0, rem);
                    } else if (!emitted) {
                        initEmit(buf, count, maxCount, LIT, 0, 1);
                    }
                } else {
                    initEmit(buf, count, maxCount, LIT, 0, (used == 0) ? obj->size : 1);
                }
            } else {
                initEmit(buf, count, maxCount, LIT, 0, 1);
            }
        } else {
            error("SIZEOF initializer: procedure is not supported");
        }
        return;
    }

    if (token == TK_IDENT) {
        Object *obj = lookup(Id);
        if (obj == NULL) {
            char msg[120];
            sprintf(msg, "initializer undeclared identifier: %s", Id);
            error(msg);
        }

        if (obj->type == OBJ_PROCEDURE) {
            nextToken();
            parseProcedureCallArguments(obj);
            if (!obj->hasReturnValue) {
                error("initializer procedure does not return a value");
            }
            initEmit(buf, count, maxCount, CAL, getCurrentLevel() - obj->level, obj->address);
            return;
        }

        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                error("initializer does not support STRING constant");
            }
            initEmit(buf, count, maxCount, LIT, 0, obj->value);
            nextToken();
            return;
        }

        if (obj->isString) {
            error("initializer does not support STRING variable");
        }

        nextToken();
        if (token == SB_LBRACK) {
            if (!isArrayLikeObject(obj)) {
                error("initializer indexed access requires array variable");
            }
            initEmitLoadObjectAddress(buf, count, maxCount, obj);
            int used = 0;
            int dims = expectedArrayDims(obj);
            while (token == SB_LBRACK) {
                nextToken();
                parseInitExpression(buf, count, maxCount);
                expect(SB_RBRACK);
                used++;
                if ((obj->isRuntimeArray || obj->lengthAddress >= 0) && obj->dimCount <= 1 && used > 1) {
                    error("runtime array supports one dimension only");
                }
                if (dims > 0 && used > dims) {
                    error("initializer has too many array indices");
                }
                if (obj->dimCount > 0 && used < obj->dimCount) {
                    int stride = 1;
                    int knownStride = 1;
                    for (int d = used; d < obj->dimCount; d++) {
                        if (obj->dims[d] <= 0) {
                            knownStride = 0;
                            break;
                        }
                        stride *= obj->dims[d];
                    }
                    if (knownStride) {
                        initEmit(buf, count, maxCount, LIT, 0, stride);
                        initEmit(buf, count, maxCount, OPR, 0, 4);
                    }
                }
                initEmit(buf, count, maxCount, OPR, 0, 2);
            }
            if (dims > 0 && used != dims) {
                error("initializer array variable requires full index");
            }
            initEmit(buf, count, maxCount, LDI, 0, 0);
        } else {
            if (isArrayLikeObject(obj)) {
                error("initializer array variable requires an index");
            }
            initEmitLoadObjectValue(buf, count, maxCount, obj);
        }
        return;
    }

    if (token == SB_LPARENT) {
        nextToken();
        parseInitExpression(buf, count, maxCount);
        expect(SB_RPARENT);
        return;
    }

    error("initializer: expected number, identifier, or '(' ");
}

static void parseInitTerm(Instruction *buf, int *count, int maxCount) {
    parseInitFactor(buf, count, maxCount);
    while (token == SB_TIMES || token == SB_SLASH || token == SB_FLOORDIV || token == SB_PERCENT) {
        TokenType op = token;
        nextToken();
        parseInitFactor(buf, count, maxCount);
        initEmit(buf, count, maxCount, OPR, 0,
                 op == SB_TIMES ? 4 : (op == SB_SLASH ? 5 : (op == SB_FLOORDIV ? 23 : 14)));
    }
}

static void parseInitExpression(Instruction *buf, int *count, int maxCount) {
    TokenType prefix = TK_NONE;
    if (token == SB_PLUS || token == SB_MINUS) {
        prefix = token;
        nextToken();
    }
    parseInitTerm(buf, count, maxCount);
    if (prefix == SB_MINUS) {
        initEmit(buf, count, maxCount, OPR, 0, 1);
    }
    while (token == SB_PLUS || token == SB_MINUS) {
        TokenType op = token;
        nextToken();
        parseInitTerm(buf, count, maxCount);
        initEmit(buf, count, maxCount, OPR, 0, op == SB_PLUS ? 2 : 3);
    }
}

static int isAssignableObject(const Object *obj) {
    if (obj->type == OBJ_PARAMETER) {
        return 1;
    }
    if (obj->type == OBJ_VARIABLE) {
        return !obj->isImmutable;
    }
    return 0;
}

static int expectedArrayDims(const Object *obj) {
    if (obj == NULL || obj->isString) {
        return 0;
    }
    if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
        return (obj->dimCount > 0) ? obj->dimCount : 1;
    }
    if (obj->dimCount > 0) {
        return obj->dimCount;
    }
    return (obj->size > 1) ? 1 : 0;
}

static int isArrayLikeObject(const Object *obj) {
    return expectedArrayDims(obj) > 0;
}

static void emitLoadArrayDim(const Object *obj, int dimIndex) {
    if (obj == NULL || dimIndex < 0 || dimIndex >= MAX_ARRAY_DIMS) {
        error("invalid array dimension access");
    }
    if (obj->dims[dimIndex] > 0) {
        emit(LIT, 0, obj->dims[dimIndex]);
        return;
    }
    if (obj->dimAddr[dimIndex] != DIM_ADDR_UNUSED) {
        emit(LOD, getCurrentLevel() - obj->level, obj->dimAddr[dimIndex]);
        return;
    }
    if (obj->isRuntimeArray && dimIndex == 0 && obj->lengthAddress >= 0) {
        emit(LOD, getCurrentLevel() - obj->level, obj->lengthAddress);
        return;
    }
    if (dimIndex == 0 && obj->size > 0) {
        emit(LIT, 0, obj->size);
        return;
    }
    emit(LIT, 0, 0);
}

static void emitPrintArray(const Object *obj) {
    int rank = expectedArrayDims(obj);
    if (rank <= 0) rank = 1;
    emitLoadObjectAddress(obj);
    for (int d = 0; d < rank; d++) {
        emitLoadArrayDim(obj, d);
    }
    emit(WRA, rank, 0);
}

static void emitAppendArrayToString(const Object *target, const Object *obj) {
    emitLoadObjectAddress(target);
    emitLoadObjectAddress(obj);
    int rank = expectedArrayDims(obj);
    if (rank <= 0) rank = 1;
    for (int d = 0; d < rank; d++) {
        emitLoadArrayDim(obj, d);
    }
    emit(CATA, rank, 0);
}

// Parses one or more [expr] suffixes and emits flattened row-major addressing.
static int emitIndexedAddress(const Object *obj, int requireIndex, int requireFullIndex, const char *context) {
    int dims = expectedArrayDims(obj);
    int used = 0;
    int i;

    emitLoadObjectAddress(obj);
    while (token == SB_LBRACK) {
        nextToken();
        expression();
        expect(SB_RBRACK);
        used++;

        if ((obj->isRuntimeArray || obj->lengthAddress >= 0) && obj->dimCount <= 1 && used > 1) {
            error("runtime array supports one dimension only");
        }
        if (dims > 0 && used > dims) {
            char msg[160];
            snprintf(msg, sizeof(msg), "%s: too many indices", context);
            error(msg);
        }

        // Check index bounds for the current dimension (used - 1)
        emitLoadArrayDim(obj, used - 1);
        emit(CHK, 0, 0);

        if (obj->dimCount > 0 && used < obj->dimCount) {
            for (i = used; i < obj->dimCount; i++) {
                emitLoadArrayDim(obj, i);
                emit(OPR, 0, 4);
            }
        }
        emit(OPR, 0, 2);
    }

    if (requireIndex && used == 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s requires an index", context);
        error(msg);
    }
    if (requireFullIndex && used > 0 && dims > 0 && used != dims) {
        char msg[192];
        snprintf(msg, sizeof(msg), "%s requires %d indices", context, dims);
        error(msg);
    }
    return used;
}

static void emitLoadObjectValue(const Object *obj) {
    int l = getCurrentLevel() - obj->level;
    if (obj->isRefParam) {
        emit(LOD, l, obj->address);
        emit(LDI, 0, 0);
    } else {
        emit(LOD, l, obj->address);
    }
}

static void emitLoadObjectAddress(const Object *obj) {
    int l = getCurrentLevel() - obj->level;
    if (obj->isRefParam || (obj->type == OBJ_PARAMETER && obj->dimCount > 0)) {
        emit(LOD, l, obj->address);
    } else if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
        emit(LOD, l, obj->address);
    } else {
        emit(LDA, l, obj->address);
    }
}

static void emitStoreObjectValue(const Object *obj) {
    int l = getCurrentLevel() - obj->level;
    if (obj->isRefParam) {
        emit(LOD, l, obj->address);
        emit(STI, 0, 0);
    } else {
        emit(STO, l, obj->address);
    }
}

static void emitSizeOfObjectValue(const Object *obj) {
    if (obj == NULL) {
        error("SIZEOF: invalid symbol");
    }
    if (obj->type == OBJ_CONSTANT) {
        if (obj->constIsString) {
            emit(LIT, 0, (int)strlen(obj->constString));
        } else {
            emit(LIT, 0, 1);
        }
        return;
    }
    if (obj->type == OBJ_VARIABLE || obj->type == OBJ_PARAMETER) {
        int dims = expectedArrayDims(obj);
        int used = sizeofIndexDepth;
        if (obj->isString) {
            if (used > 0) {
                error("SIZEOF: STRING variable does not support index");
            }
            emitLoadObjectAddress(obj);
            emit(LEN, 0, 0);
        } else if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
            if (used > dims) {
                error("SIZEOF: too many indices");
            }
            if (obj->dimCount > 1) {
                int emitted = 0;
                for (int d = used; d < obj->dimCount; d++) {
                    emitLoadArrayDim(obj, d);
                    if (emitted) {
                        emit(OPR, 0, 4);
                    }
                    emitted = 1;
                }
                if (!emitted) {
                    emit(LIT, 0, 1);
                }
            } else if (used == 0) {
                emit(LOD, getCurrentLevel() - obj->level, obj->lengthAddress);
            } else {
                emit(LIT, 0, 1);
            }
        } else if (dims > 0) {
            if (used > dims) {
                error("SIZEOF: too many indices");
            }
            if (obj->dimCount > 0) {
                int emitted = 0;
                for (int d = used; d < obj->dimCount; d++) {
                    emitLoadArrayDim(obj, d);
                    if (emitted) {
                        emit(OPR, 0, 4);
                    }
                    emitted = 1;
                }
                if (!emitted) {
                    emit(LIT, 0, 1);
                }
            } else {
                emit(LIT, 0, (used == 0) ? obj->size : 1);
            }
        } else {
            emit(LIT, 0, 1);
        }
        return;
    }
    error("SIZEOF: procedure is not supported");
}

static void parseProcedureCallArguments(const Object *proc) {
    int argCount = 0;
    if (token == SB_LPARENT) {
        nextToken();
        if (token != SB_RPARENT) {
            while (1) {
                if (argCount >= proc->paramCount) {
                    error("Too many arguments in CALL");
                }
                if (proc->paramDimCount[argCount] > 0 || proc->paramSize[argCount] > 1) {
                    if (token != TK_IDENT) {
                        error("Array parameter requires identifier argument");
                    }
                    Object *arg = lookup(Id);
                    if (arg == NULL) {
                        error("Array parameter requires array value argument");
                    }

                    int formalRank = proc->paramDimCount[argCount];
                    int actualRank = 0;
                    int actualFromProc = 0;
                    int actualDimVals[MAX_ARRAY_DIMS];
                    for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                        actualDimVals[d] = 0;
                    }

                    if (arg->type == OBJ_PROCEDURE) {
                        actualFromProc = 1;
                        nextToken();
                        parseProcedureCallArguments(arg);
                        if (!arg->hasReturnValue || arg->returnDimCount <= 0) {
                            error("Array parameter requires procedure returning array value");
                        }
                        emit(CAL, getCurrentLevel() - arg->level, arg->address);
                        actualRank = arg->returnDimCount;
                        for (int d = 0; d < actualRank && d < MAX_ARRAY_DIMS; d++) {
                            actualDimVals[d] = arg->returnDims[d];
                        }
                    } else {
                        if (!isArrayLikeObject(arg) || arg->isString) {
                            error("Array parameter requires array variable argument");
                        }
                        nextToken();
                        if (token == SB_LBRACK) {
                            error("Array parameter requires whole array argument, not indexed element");
                        }
                        emitLoadObjectAddress(arg);
                        actualRank = expectedArrayDims(arg);
                        for (int d = 0; d < actualRank && d < MAX_ARRAY_DIMS; d++) {
                            int actualDim = (d < arg->dimCount) ? arg->dims[d] : 0;
                            if (actualDim <= 0 && d == 0 && arg->dimCount > 1 && arg->size > 1) {
                                int tail = 1;
                                int knownTail = 1;
                                for (int td = 1; td < arg->dimCount; td++) {
                                    if (arg->dims[td] <= 0) {
                                        knownTail = 0;
                                        break;
                                    }
                                    tail *= arg->dims[td];
                                }
                                if (knownTail && tail > 0) {
                                    actualDim = arg->size / tail;
                                }
                            }
                            actualDimVals[d] = actualDim;
                        }
                    }

                    if (formalRank <= 0) {
                        formalRank = actualRank;
                    }
                    if (formalRank <= 0) {
                        error("Array parameter rank is not available");
                    }
                    for (int d = 0; d < formalRank; d++) {
                        int formalDim = (d < MAX_ARRAY_DIMS) ? proc->paramDims[argCount][d] : 0;
                        int actualDim = (d < MAX_ARRAY_DIMS) ? actualDimVals[d] : 0;
                        if (formalDim > 0) {
                            if (actualDim > 0 && actualDim != formalDim) {
                                error("Array argument dimension mismatch");
                            }
                            emit(LIT, 0, formalDim);
                        } else if (actualDim > 0) {
                            emit(LIT, 0, actualDim);
                        } else if (actualFromProc) {
                            emit(LRD, 0, d);
                        } else if (arg != NULL && isArrayLikeObject(arg)) {
                            emitLoadArrayDim(arg, d);
                        } else {
                            emit(LIT, 0, 0);
                        }
                    }
                } else if (proc->paramIsRef[argCount]) {
                    if (token != TK_IDENT) {
                        error("VAR parameter requires assignable identifier argument");
                    }
                    Object *arg = lookup(Id);
                    if (arg == NULL || !isAssignableObject(arg) || arg->isString) {
                        error("VAR parameter requires variable argument");
                    }
                    nextToken();

                    int indexedArg = 0;
                    if (token == SB_LBRACK) {
                        indexedArg = 1;
                        if (!isArrayLikeObject(arg)) {
                            error("Indexed VAR argument requires array variable");
                        }
                        (void)emitIndexedAddress(arg, 1, 1, "Indexed VAR argument");
                    }

                    if (!indexedArg && isArrayLikeObject(arg)) {
                        error("Scalar VAR parameter requires scalar variable or indexed array element");
                    }
                    if (!indexedArg) {
                        emitLoadObjectAddress(arg);
                    }
                } else {
                    if (token == TK_STRING) {
                        emit(LIT, 0, addStringLiteral(StringLiteral));
                        nextToken();
                    } else if (token == TK_IDENT) {
                        Object *argObj = lookup(Id);
                        if (argObj != NULL && argObj->type == OBJ_CONSTANT && argObj->constIsString) {
                            emit(LIT, 0, addStringLiteral(argObj->constString));
                            nextToken();
                        } else if (argObj != NULL && (argObj->type == OBJ_VARIABLE || argObj->type == OBJ_PARAMETER) && argObj->isString) {
                            emitLoadObjectAddress(argObj);
                            nextToken();
                        } else {
                            expression();
                        }
                    } else {
                        expression();
                    }
                }
                argCount++;
                if (token == SB_COMMA) {
                    nextToken();
                    continue;
                }
                break;
            }
        }
        expect(SB_RPARENT);
    }

    if (argCount != proc->paramCount) {
        error("Argument count mismatch in CALL");
    }
}

static void interpSkipSpaces(void) {
    while (*interpExprPtr != '\0' && isspace((unsigned char)*interpExprPtr)) {
        interpExprPtr++;
    }
}

static int interpAccept(char c) {
    interpSkipSpaces();
    if (*interpExprPtr == c) {
        interpExprPtr++;
        return 1;
    }
    return 0;
}

static void interpParseExpression(void);

static double interpParseNumberLiteral(void) {
    char buf[64];
    int n = 0;

    while (isdigit((unsigned char)*interpExprPtr)) {
        if (n < (int)sizeof(buf) - 1) {
            buf[n++] = *interpExprPtr;
        }
        interpExprPtr++;
    }

    if (*interpExprPtr == '.' && isdigit((unsigned char)interpExprPtr[1])) {
        if (n < (int)sizeof(buf) - 1) {
            buf[n++] = *interpExprPtr;
        }
        interpExprPtr++;
        while (isdigit((unsigned char)*interpExprPtr)) {
            if (n < (int)sizeof(buf) - 1) {
                buf[n++] = *interpExprPtr;
            }
            interpExprPtr++;
        }
    }

    buf[n] = '\0';
    return strtod(buf, NULL);
}

static void interpParseProcedureCallArgs(const Object *proc) {
    int argCount = 0;
    if (!interpAccept('(')) {
        error("interpolation: procedure call expects '('");
    }

    if (!interpAccept(')')) {
        while (1) {
            if (argCount >= proc->paramCount) {
                error("interpolation: too many procedure arguments");
            }
            if (proc->paramIsRef[argCount]) {
                error("interpolation: VAR/reference parameter is not supported");
            }
            if (proc->paramSize[argCount] > 1) {
                error("interpolation: array parameter is not supported");
            }

            interpParseExpression();
            argCount++;

            if (interpAccept(',')) {
                continue;
            }
            if (interpAccept(')')) {
                break;
            }
            error("interpolation: expected ',' or ')' in procedure call");
        }
    }

    if (argCount != proc->paramCount) {
        error("interpolation: argument count mismatch in procedure call");
    }
}

static void interpParseFactor(void) {
    interpSkipSpaces();
    if (*interpExprPtr == '\0') {
        error("interpolation: unexpected end of expression");
    }

    if (*interpExprPtr == '(') {
        interpExprPtr++;
        interpParseExpression();
        if (!interpAccept(')')) {
            error("interpolation: expected ')' ");
        }
        return;
    }

    if (isdigit((unsigned char)*interpExprPtr)) {
        emit(LIT, 0, interpParseNumberLiteral());
        return;
    }

    if (isalpha((unsigned char)*interpExprPtr) || *interpExprPtr == '_') {
        char name[MAX_IDENT_LEN + 1];
        int n = 0;
        while (isalpha((unsigned char)*interpExprPtr) || isdigit((unsigned char)*interpExprPtr) || *interpExprPtr == '_') {
            if (n < MAX_IDENT_LEN) {
                name[n++] = (char)toupper((unsigned char)*interpExprPtr);
            }
            interpExprPtr++;
        }
        name[n] = '\0';

        if (strcmp(name, "NULL") == 0) {
            emit(LIT, 0, 0);
            return;
        }

        if (strcmp(name, "SIZEOF") == 0) {
            if (!interpAccept('(')) {
                error("interpolation: SIZEOF expects '(' ");
            }
            interpSkipSpaces();
            if (!(isalpha((unsigned char)*interpExprPtr) || *interpExprPtr == '_')) {
                error("interpolation: SIZEOF expects identifier");
            }
            char targetName[MAX_IDENT_LEN + 1];
            int m = 0;
            while (isalpha((unsigned char)*interpExprPtr) || isdigit((unsigned char)*interpExprPtr) || *interpExprPtr == '_') {
                if (m < MAX_IDENT_LEN) {
                    targetName[m++] = (char)toupper((unsigned char)*interpExprPtr);
                }
                interpExprPtr++;
            }
            targetName[m] = '\0';
            int savedSizeofDepth = sizeofIndexDepth;
            sizeofIndexDepth = 0;
            while (interpAccept('[')) {
                interpParseExpression();
                if (!interpAccept(']')) {
                    error("interpolation: SIZEOF missing ']' ");
                }
                emit(POP, 0, 0);
                sizeofIndexDepth++;
            }
            if (!interpAccept(')')) {
                error("interpolation: SIZEOF missing ')' ");
            }
            Object *targetObj = lookup(targetName);
            if (targetObj == NULL) {
                char msg[120];
                sprintf(msg, "interpolation: SIZEOF undeclared identifier %s", targetName);
                error(msg);
            }
            emitSizeOfObjectValue(targetObj);
            sizeofIndexDepth = savedSizeofDepth;
            return;
        }

        Object *obj = lookup(name);
        if (obj == NULL) {
            char msg[120];
            sprintf(msg, "Undeclared identifier in interpolation: %s", name);
            error(msg);
        }

        interpSkipSpaces();
        if (obj->type == OBJ_PROCEDURE) {
            if (*interpExprPtr != '(') {
                error("interpolation: procedure has no printable value");
            }
            interpParseProcedureCallArgs(obj);
            if (!obj->hasReturnValue) {
                error("interpolation: procedure does not return a value");
            }
            emit(CAL, getCurrentLevel() - obj->level, obj->address);
            return;
        }

        if (*interpExprPtr == '[') {
            if (obj->type == OBJ_CONSTANT || obj->isString || !isArrayLikeObject(obj)) {
                error("interpolation: indexed access requires numeric array variable");
            }
            emitLoadObjectAddress(obj);
            int used = 0;
            int dims = expectedArrayDims(obj);
            while (interpAccept('[')) {
                interpParseExpression();
                if (!interpAccept(']')) {
                    error("interpolation: expected ']' ");
                }
                used++;
                if ((obj->isRuntimeArray || obj->lengthAddress >= 0) && obj->dimCount <= 1 && used > 1) {
                    error("interpolation: runtime array supports one dimension only");
                }
                if (dims > 0 && used > dims) {
                    error("interpolation: too many array indices");
                }
                emitLoadArrayDim(obj, used - 1);
                emit(CHK, 0, 0);
                if (!(obj->isRuntimeArray || obj->lengthAddress >= 0) && obj->dimCount > 0 && used < obj->dimCount) {
                    int stride = 1;
                    int knownStride = 1;
                    for (int d = used; d < obj->dimCount; d++) {
                        if (obj->dims[d] <= 0) {
                            knownStride = 0;
                            break;
                        }
                        stride *= obj->dims[d];
                    }
                    if (knownStride) {
                        emit(LIT, 0, stride);
                        emit(OPR, 0, 4);
                    }
                }
                emit(OPR, 0, 2);
            }
            if (dims > 0 && used != dims) {
                error("interpolation: array requires full index");
            }
            emit(LDI, 0, 0);
            return;
        }
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                error("interpolation arithmetic does not support STRING constants");
            }
            emit(LIT, 0, obj->value);
            return;
        }
        if (obj->isString) {
            error("interpolation arithmetic does not support STRING variables");
        }
        if (isArrayLikeObject(obj)) {
            error("interpolation arithmetic requires scalar variable");
        }
        emitLoadObjectValue(obj);
        return;
    }

    error("interpolation: invalid factor");
}

static void interpParseTerm(void) {
    interpParseFactor();
    while (1) {
        interpSkipSpaces();
        if (*interpExprPtr == '*') {
            interpExprPtr++;
            interpParseFactor();
            emit(OPR, 0, 4);
        } else if (*interpExprPtr == '/' && *(interpExprPtr + 1) == '/') {
            interpExprPtr += 2;
            interpParseFactor();
            emit(OPR, 0, 23);
        } else if (*interpExprPtr == '/') {
            interpExprPtr++;
            interpParseFactor();
            emit(OPR, 0, 5);
        } else if (*interpExprPtr == '%') {
            interpExprPtr++;
            interpParseFactor();
            emit(OPR, 0, 14);
        } else {
            break;
        }
    }
}

static void interpParseExpression(void) {
    int unaryMinus = 0;
    interpSkipSpaces();
    if (*interpExprPtr == '+' || *interpExprPtr == '-') {
        unaryMinus = (*interpExprPtr == '-');
        interpExprPtr++;
    }

    interpParseTerm();
    if (unaryMinus) {
        emit(OPR, 0, 1);
    }

    while (1) {
        interpSkipSpaces();
        if (*interpExprPtr == '+') {
            interpExprPtr++;
            interpParseTerm();
            emit(OPR, 0, 2);
        } else if (*interpExprPtr == '-') {
            interpExprPtr++;
            interpParseTerm();
            emit(OPR, 0, 3);
        } else {
            break;
        }
    }
}

static void emitInterpolatedBracedValue(const char *text, int len) {
    char expr[MAX_STRING_LEN + 1];
    if (len <= 0) {
        error("interpolation: empty ${} expression");
    }
    if (len > MAX_STRING_LEN) {
        len = MAX_STRING_LEN;
    }
    memcpy(expr, text, (size_t)len);
    expr[len] = '\0';

    // Fast path: ${IDENT} can print STRING or numeric symbol directly.
    {
        int i = 0;
        while (isspace((unsigned char)expr[i])) i++;
        int start = i;
        if (isalpha((unsigned char)expr[i]) || expr[i] == '_') {
            char name[MAX_IDENT_LEN + 1];
            int n = 0;
            while (isalpha((unsigned char)expr[i]) || isdigit((unsigned char)expr[i]) || expr[i] == '_') {
                if (n < MAX_IDENT_LEN) {
                    name[n++] = (char)toupper((unsigned char)expr[i]);
                }
                i++;
            }
            while (isspace((unsigned char)expr[i])) i++;
            if (expr[i] == '\0' && n > 0) {
                name[n] = '\0';
                Object *obj = lookup(name);
                if (obj == NULL) {
                    char msg[120];
                    sprintf(msg, "Undeclared placeholder: %s", name);
                    error(msg);
                }
                if (obj->type == OBJ_PROCEDURE) {
                    error("interpolation: procedure has no printable value");
                }
                if (obj->type == OBJ_CONSTANT) {
                    if (obj->constIsString) {
                        emit(WRL, 0, addStringLiteral(obj->constString));
                    } else if (isArrayLikeObject(obj)) {
                        emitPrintArray(obj);
                    } else {
                        emit(LIT, 0, obj->value);
                        emit(WRI, 0, 0);
                    }
                    return;
                }
                if (obj->isString) {
                    emit(WRS, getCurrentLevel() - obj->level, obj->address);
                    return;
                }
                if (isArrayLikeObject(obj)) {
                    emitPrintArray(obj);
                    return;
                }
                emitLoadObjectValue(obj);
                emit(WRI, 0, 0);
                return;
            }
        }
        (void)start;
    }

    // General numeric expression path.
    interpExprPtr = expr;
    interpParseExpression();
    interpSkipSpaces();
    if (*interpExprPtr != '\0') {
        error("interpolation: invalid trailing tokens");
    }
    emit(WRI, 0, 0);
}

static void emitInterpolatedString(const char *literal);

static void emitWriteAtom(void) {
    if (token == TK_STRING) {
        emitInterpolatedString(StringLiteral);
        nextToken();
        return;
    }
    if (token == TK_IDENT) {
        Object *obj = lookup(Id);
        if (obj != NULL) {
            if (obj->type == OBJ_CONSTANT && obj->constIsString) {
                emit(WRL, 0, addStringLiteral(obj->constString));
                nextToken();
                return;
            }
            if (isAssignableObject(obj) && obj->isString) {
                nextToken();
                if (token == SB_LBRACK) {
                    error("WRITE/WRITELN of STRING variable does not take index");
                }
                emit(WRS, getCurrentLevel() - obj->level, obj->address);
                return;
            }
            if (isArrayLikeObject(obj)) {
                nextToken();
                if (token != SB_LBRACK) {
                    emitPrintArray(obj);
                    return;
                }
                (void)emitIndexedAddress(obj, 1, 1, "Indexed WRITE");
                emit(LDI, 0, 0);
                emit(WRI, 0, 0);
                return;
            }
        }
    }
    expression();
    emit(WRI, 0, 0);
}

void factor(void) {
    if (token == TK_NUMBER) {
        emit(LIT, 0, Num);
        nextToken();
    } else if (token == KW_NULL) {
        emit(LIT, 0, 0);
        nextToken();
    } else if (token == KW_SIZEOF) {
        nextToken();
        Object *obj = parseSizeOfTargetObject(0);
        emitSizeOfObjectValue(obj);
    } else if (token == TK_IDENT) {
        Object* obj = lookup(Id);
        if (obj == NULL) {
            char msg[100];
            sprintf(msg, "Undeclared identifier: %s", Id);
            error(msg);
        }
        if (obj->type == OBJ_PROCEDURE) {
            nextToken();
            parseProcedureCallArguments(obj);
            if (!obj->hasReturnValue) {
                error("Procedure does not return a value");
            }
            emit(CAL, getCurrentLevel() - obj->level, obj->address);
            return;
        }
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                error("Cannot use STRING constant in numeric expression");
            }
            emit(LIT, 0, obj->value);
            nextToken();
        } else if (obj->isString) {
            error("Cannot use STRING variable in numeric expression");
        } else {
            nextToken();
            if (token == SB_INC) {
                if (isArrayLikeObject(obj)) {
                    error("Postfix increment requires scalar variable");
                }
                // Postfix form returns old value, then stores incremented value.
                emitLoadObjectValue(obj);
                emitLoadObjectValue(obj);
                emit(LIT, 0, 1);
                emit(OPR, 0, 2);
                emitStoreObjectValue(obj);
                nextToken();
                return;
            }
            if (token == SB_LBRACK) {
                if (!isArrayLikeObject(obj)) {
                    error("Indexed access is only valid for arrays");
                }
                (void)emitIndexedAddress(obj, 1, 1, "array access");
                emit(LDI, 0, 0);
                return;
            }
            if (isArrayLikeObject(obj)) {
                // Array in expression context evaluates to its base address (reference semantics).
                emitLoadObjectAddress(obj);
                return;
            }
            emitLoadObjectValue(obj);
        }
    } else if (token == SB_LPARENT) {
        nextToken();
        expression();
        expect(SB_RPARENT);
    } else {
        error("factor: expected number, identifier, or '('");
    }
}

static void unaryExpr(void) {
    if (token == SB_INC) {
        nextToken();
        if (token != TK_IDENT) {
            error("Prefix increment expects identifier");
        }
        Object *obj = lookup(Id);
        if (obj == NULL || !isAssignableObject(obj)) {
            error("Prefix increment requires variable");
        }
        if (obj->isString || isArrayLikeObject(obj)) {
            error("Prefix increment requires scalar numeric variable");
        }
        nextToken();
        emitLoadObjectValue(obj);
        emit(LIT, 0, 1);
        emit(OPR, 0, 2);
        emitStoreObjectValue(obj);
        emitLoadObjectValue(obj);
        return;
    }
    if (token == SB_PLUS) {
        nextToken();
        unaryExpr();
        return;
    }
    if (token == SB_MINUS) {
        nextToken();
        unaryExpr();
        emit(OPR, 0, 1);
        return;
    }
    if (token == SB_BITNOT) {
        nextToken();
        unaryExpr();
        emit(OPR, 0, 17);
        return;
    }
    factor();
}

// term = unaryExpr { ('*' | '/' | '//' | '%') unaryExpr }
void term(void) {
    unaryExpr();
    while (token == SB_TIMES || token == SB_SLASH || token == SB_FLOORDIV || token == SB_PERCENT) {
        TokenType op = token;
        nextToken();
        unaryExpr();
        if (op == SB_TIMES) emit(OPR, 0, 4);
        else if (op == SB_SLASH) emit(OPR, 0, 5);
        else if (op == SB_FLOORDIV) emit(OPR, 0, 23);
        else emit(OPR, 0, 14);
    }
}

static void additiveExpr(void) {
    term();
    while (token == SB_PLUS || token == SB_MINUS) {
        TokenType op = token;
        nextToken();
        term();
        emit(OPR, 0, (op == SB_PLUS) ? 2 : 3);
    }
}

static void shiftExpr(void) {
    additiveExpr();
    while (token == SB_SHL || token == SB_SHR) {
        TokenType op = token;
        nextToken();
        additiveExpr();
        emit(OPR, 0, (op == SB_SHL) ? 18 : 19);
    }
}

static void bitwiseAndExpr(void) {
    shiftExpr();
    while (token == SB_BITAND) {
        nextToken();
        shiftExpr();
        emit(OPR, 0, 15);
    }
}

static void bitwiseXorExpr(void) {
    bitwiseAndExpr();
    while (token == SB_BITXOR) {
        nextToken();
        bitwiseAndExpr();
        emit(OPR, 0, 16);
    }
}

// expression now includes bitwise operators with C-like precedence.
void expression(void) {
    bitwiseXorExpr();
    while (token == SB_BITOR) {
        nextToken();
        bitwiseXorExpr();
        emit(OPR, 0, 13);
    }
}

static void conditionFactor(void) {
    if (token == KW_NOT) {
        nextToken();
        conditionFactor();
        emit(OPR, 0, 22);
        return;
    }
    if (token == SB_LPARENT) {
        nextToken();
        condition();
        expect(SB_RPARENT);
        return;
    }
    if (token == KW_ODD) {
        nextToken();
        expression();
        emit(OPR, 0, 6);
        return;
    }

    expression();
    if (token == SB_EQU || token == SB_NEQ ||
        token == SB_LSS || token == SB_LEQ ||
        token == SB_GTR || token == SB_GEQ) {
        TokenType op = token;
        nextToken();
        expression();
        switch (op) {
            case SB_EQU: emit(OPR, 0, 7); break;
            case SB_NEQ: emit(OPR, 0, 8); break;
            case SB_LSS: emit(OPR, 0, 9); break;
            case SB_GEQ: emit(OPR, 0, 10); break;
            case SB_GTR: emit(OPR, 0, 11); break;
            case SB_LEQ: emit(OPR, 0, 12); break;
            default: break;
        }
        return;
    }

    // Treat a non-zero numeric expression as true.
    emit(LIT, 0, 0);
    emit(OPR, 0, 8);
}

static void conditionTerm(void) {
    conditionFactor();
    while (token == KW_AND) {
        nextToken();
        conditionFactor();
        emit(OPR, 0, 20);
    }
}

void condition(void) {
    conditionTerm();
    while (token == KW_OR) {
        nextToken();
        conditionTerm();
        emit(OPR, 0, 21);
    }
}

static void emitStringChunk(const char *chunk, int len) {
    if (len <= 0) {
        return;
    }
    char temp[MAX_STRING_LEN + 1];
    if (len > MAX_STRING_LEN) {
        len = MAX_STRING_LEN;
    }
    memcpy(temp, chunk, (size_t)len);
    temp[len] = '\0';
    emit(WRL, 0, addStringLiteral(temp));
}

static void emitInterpolatedString(const char *literal) {
    int i = 0;
    int start = 0;

    while (literal[i] != '\0') {
        if (literal[i] != '$') {
            i++;
            continue;
        }

        emitStringChunk(literal + start, i - start);

        i++;
        if (literal[i] == '{') {
            int exprStart = ++i;
            while (literal[i] != '\0' && literal[i] != '}') {
                i++;
            }
            if (literal[i] != '}') {
                error("interpolation: missing '}'");
            }
            emitInterpolatedBracedValue(literal + exprStart, i - exprStart);
            i++;
            start = i;
            continue;
        }
        if (!(isalpha((unsigned char)literal[i]) || literal[i] == '_')) {
            emitStringChunk("$", 1);
            start = i;
            continue;
        }

        char name[MAX_IDENT_LEN + 1];
        int n = 0;
        while (isalpha((unsigned char)literal[i]) || isdigit((unsigned char)literal[i]) || literal[i] == '_') {
            if (n < MAX_IDENT_LEN) {
                name[n++] = (char)toupper((unsigned char)literal[i]);
            }
            i++;
        }
        name[n] = '\0';

        Object *obj = lookup(name);
        if (obj == NULL) {
            char msg[120];
            sprintf(msg, "Undeclared placeholder: %s", name);
            error(msg);
        }
        if (obj->type == OBJ_PROCEDURE) {
            error("interpolation: procedure has no printable value");
        }
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                emit(WRL, 0, addStringLiteral(obj->constString));
            } else if (isArrayLikeObject(obj)) {
                emitPrintArray(obj);
            } else {
                emit(LIT, 0, obj->value);
                emit(WRI, 0, 0);
            }
        } else {
            if (obj->isString) {
                emit(WRS, getCurrentLevel() - obj->level, obj->address);
            } else if (isArrayLikeObject(obj)) {
                emitPrintArray(obj);
            } else {
                emitLoadObjectValue(obj);
                emit(WRI, 0, 0);
            }
        }

        start = i;
    }

    emitStringChunk(literal + start, i - start);
}

// ─── Phân tích câu lệnh ──────────────────────────────────────────────────────

// statement = IDENT ':=' expression
//           | CALL IDENT
//           | BEGIN statement { ';' statement } END
//           | IF condition THEN statement [ ELSE statement ]
//           | WHILE condition DO statement
//           | FOR IDENT ':=' expression TO expression DO statement
//           | (rỗng)
void statement(void) {
    if (token == SB_INC) {
        nextToken();
        if (token != TK_IDENT) {
            error("Prefix increment expects identifier");
        }
        Object *obj = lookup(Id);
        if (obj == NULL || !isAssignableObject(obj)) {
            error("Prefix increment requires variable");
        }
        if (obj->isString || isArrayLikeObject(obj)) {
            error("Prefix increment requires scalar numeric variable");
        }
        nextToken();
        emitLoadObjectValue(obj);
        emit(LIT, 0, 1);
        emit(OPR, 0, 2);
        emitStoreObjectValue(obj);
        return;

    } else if (token == TK_IDENT) {
        // Allow implicit procedure call syntax: IDENT(...)
        Object* obj = lookup(Id);
        if (obj == NULL) {
            char msg[100];
            sprintf(msg, "Undeclared identifier: %s", Id);
            error(msg);
        }
        if (obj->type == OBJ_PROCEDURE) {
            nextToken();
            parseProcedureCallArguments(obj);
            emit(CAL, getCurrentLevel() - obj->level, obj->address);
            if (obj->hasReturnValue) {
                emit(POP, 0, 0);
            }
            return;
        }

        // Gán: IDENT ':=' expression
        if (!isAssignableObject(obj)) {
            error("Cannot assign to non-variable");
        }
        nextToken();

        if (token == SB_INC) {
            if (obj->isString || isArrayLikeObject(obj)) {
                error("Increment requires scalar numeric variable");
            }
            nextToken();
            emitLoadObjectValue(obj);
            emit(LIT, 0, 1);
            emit(OPR, 0, 2);
            emitStoreObjectValue(obj);
            return;
        }

        int isIndexed = 0;
        if (token == SB_LBRACK) {
            if (!isArrayLikeObject(obj)) {
                error("Indexed assignment requires an array variable");
            }
            (void)emitIndexedAddress(obj, 1, 1, "Indexed assignment");
            isIndexed = 1;
        }

        TokenType assignOp = token;
        if (assignOp != SB_ASSIGN &&
            assignOp != SB_ADD_ASSIGN && assignOp != SB_SUB_ASSIGN &&
            assignOp != SB_MUL_ASSIGN && assignOp != SB_DIV_ASSIGN &&
            assignOp != SB_MOD_ASSIGN) {
            error("assignment: expected ':=' or compound assignment operator");
        }
        nextToken();

        if (assignOp == SB_ASSIGN && !isIndexed && (obj->isString || tokenStartsStringValueExpr() || isArrayLikeObject(obj))) {
            if (isIndexed) {
                error("Cannot assign string expression to indexed storage");
            }
            if (obj->size <= 1) {
                error("String assignment requires variable declared with size, e.g. VAR S[32]");
            }
            obj->isString = 1;

            emitLoadObjectAddress(obj);
            emit(SCLR, 0, 0);
            emitAppendStringTerm(obj);
            while (token == SB_PLUS) {
                nextToken();
                emitAppendStringTerm(obj);
            }
        } else {
            if (obj->isString && isIndexed) {
                error("Cannot assign numeric value to indexed STRING storage");
            }

            if (assignOp != SB_ASSIGN) {
                int oprCode = 0;
                if (obj->isString) {
                    error("Compound assignment is not supported for STRING values");
                }
                if (!isIndexed && isArrayLikeObject(obj)) {
                    error("Compound assignment requires indexed array element");
                }
                switch (assignOp) {
                    case SB_ADD_ASSIGN: oprCode = 2; break;
                    case SB_SUB_ASSIGN: oprCode = 3; break;
                    case SB_MUL_ASSIGN: oprCode = 4; break;
                    case SB_DIV_ASSIGN: oprCode = 5; break;
                    case SB_MOD_ASSIGN: oprCode = 14; break;
                    default: break;
                }

                if (isIndexed) {
                    emit(DUP, 0, 0);
                    emit(LDI, 0, 0);
                    expression();
                    emit(OPR, 0, oprCode);
                    emit(STI, 0, 0);
                } else {
                    emitLoadObjectValue(obj);
                    expression();
                    emit(OPR, 0, oprCode);
                    emitStoreObjectValue(obj);
                }
                return;
            }

            if (isIndexed) {
                expression();
                emit(STI, 0, 0);
            } else {
                if (isArrayLikeObject(obj)) {
                    error("Array assignment requires an index");
                }
                if (obj->isRefParam) {
                    emitLoadObjectAddress(obj);
                    expression();
                    emit(STI, 0, 0);
                } else {
                    expression();
                    emitStoreObjectValue(obj);
                }
            }
        }
    } else if (token == KW_READ) {
        nextToken();
        expect(SB_LPARENT);
        if (token != TK_IDENT) {
            error("CALL READ: expected identifier");
        }
        Object *obj = lookup(Id);
        if (obj == NULL || !isAssignableObject(obj)) {
            error("CALL READ: variable required");
        }
        if (obj->isString) {
            error("CALL READ currently supports only integer variables");
        }
        nextToken();
        if (token == SB_LBRACK) {
            if (!isArrayLikeObject(obj)) {
                error("CALL READ: indexed target requires array variable");
            }
            emitIndexedAddress(obj, 1, 1, "CALL READ indexed target");
        } else {
            if (isArrayLikeObject(obj)) {
                error("CALL READ: array variable requires an index");
            }
            emitLoadObjectAddress(obj);
        }
        expect(SB_RPARENT);
        emit(RDI, 0, 0);
    } else if (token == KW_WRITE || token == KW_WRITELN) {
        int withNewline = token == KW_WRITELN;
        nextToken();
        expect(SB_LPARENT);
        if (token != SB_RPARENT) {
            emitWriteAtom();
            while (token == SB_COMMA || token == SB_PLUS) {
                nextToken();
                emitWriteAtom();
            }
        }
        expect(SB_RPARENT);
        if (withNewline) {
            emit(WNL, 0, 0);
        }
    } else if (token == TK_IDENT) {
        Object* proc = lookup(Id);
        if (proc == NULL) {
            char msg[100];
            sprintf(msg, "Undeclared identifier: %s", Id);
            error(msg);
        }
        if (proc->type != OBJ_PROCEDURE) {
            error("Cannot CALL a non-procedure");
        }
        nextToken();
        parseProcedureCallArguments(proc);
        emit(CAL, getCurrentLevel() - proc->level, proc->address);
        if (proc->hasReturnValue) {
            emit(POP, 0, 0);
        }
    } else if (token == KW_RETURN) {
        if (currentProcedure == NULL) {
            error("RETURN is only valid inside a procedure");
        }
        nextToken();
        if (token == SB_SEMICOLON || token == KW_END) {
            emit(OPR, 0, 0);
        } else {
            if (token == TK_IDENT) {
                Object *retObj = lookup(Id);
                if (retObj != NULL && (retObj->type == OBJ_VARIABLE || retObj->type == OBJ_PARAMETER) && isArrayLikeObject(retObj)) {
                    int retDims = expectedArrayDims(retObj);
                    nextToken();
                    if (token == SB_LBRACK) {
                        error("RETURN array requires whole array value, not indexed element");
                    }
                    emitLoadObjectAddress(retObj);
                    for (int d = 0; d < retDims; d++) {
                        emitLoadArrayDim(retObj, d);
                        currentProcedure->returnDims[d] = (d < retObj->dimCount) ? retObj->dims[d] : 0;
                    }
                    for (int d = retDims; d < MAX_ARRAY_DIMS; d++) {
                        currentProcedure->returnDims[d] = 0;
                    }
                    currentProcedure->returnDimCount = retDims;
                    currentProcedure->hasReturnValue = 1;
                    emit(RETV, 0, retDims);
                    return;
                }
                if (retObj != NULL && retObj->type == OBJ_PROCEDURE) {
                    nextToken();
                    parseProcedureCallArguments(retObj);
                    if (!retObj->hasReturnValue) {
                        error("RETURN procedure does not return a value");
                    }
                    emit(CAL, getCurrentLevel() - retObj->level, retObj->address);
                    if (retObj->returnDimCount > 0) {
                        for (int d = 0; d < retObj->returnDimCount; d++) {
                            emit(LRD, 0, d);
                            currentProcedure->returnDims[d] = retObj->returnDims[d];
                        }
                        for (int d = retObj->returnDimCount; d < MAX_ARRAY_DIMS; d++) {
                            currentProcedure->returnDims[d] = 0;
                        }
                        currentProcedure->returnDimCount = retObj->returnDimCount;
                        currentProcedure->hasReturnValue = 1;
                        emit(RETV, 0, retObj->returnDimCount);
                    } else {
                        currentProcedure->returnDimCount = 0;
                        currentProcedure->hasReturnValue = 1;
                        emit(RETV, 0, 0);
                    }
                    return;
                }
            }
            expression();
            currentProcedure->returnDimCount = 0;
            for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                currentProcedure->returnDims[d] = 0;
            }
            emit(RETV, 0, 0);
            currentProcedure->hasReturnValue = 1;
        }

    } else if (token == KW_BEGIN) {
        nextToken();
        statement();
        while (token == SB_SEMICOLON) {
            nextToken();
            statement();
        }
        expect(KW_END);

    } else if (token == KW_IF) {
        nextToken();
        condition();
        int cx1 = cx;
        emit(JPC, 0, 0);
        expect(KW_THEN);
        statement();
        if (token == SB_SEMICOLON && peekNextToken() == KW_ELSE) {
            nextToken();
        }
        if (token == KW_ELSE) {
            nextToken();
            int cx2 = cx;
            emit(JMP, 0, 0);
            code[cx1].a = cx;
            statement();
            code[cx2].a = cx;
        } else {
            code[cx1].a = cx;
        }

    } else if (token == KW_WHILE) {
        int cx1 = cx;
        nextToken();
        condition();
        int cx2 = cx;
        emit(JPC, 0, 0);
        expect(KW_DO);
        statement();
        emit(JMP, 0, cx1);
        code[cx2].a = cx;

    } else if (token == KW_FOR) {
        nextToken();
        if (token != TK_IDENT) error("statement: expected identifier after FOR");
        Object* obj = lookup(Id);
        if (obj == NULL || obj->type != OBJ_VARIABLE || obj->isString || isArrayLikeObject(obj)) {
            error("FOR: scalar integer variable required");
        }
        nextToken();
        expect(SB_ASSIGN);
        expression();
        emit(STO, getCurrentLevel() - obj->level, obj->address);

        int isDownTo = 0;
        if (token == KW_TO) {
            isDownTo = 0;
            nextToken();
        } else if (token == KW_DOWNTO) {
            isDownTo = 1;
            nextToken();
        } else {
            error("FOR: expected TO or DOWNTO");
        }

        Instruction boundExpr[MAX_INIT_EXPR_CODE];
        int boundExprCount = 0;
        parseInitExpression(boundExpr, &boundExprCount, MAX_INIT_EXPR_CODE);

        Instruction stepExpr[MAX_INIT_EXPR_CODE];
        int stepExprCount = 0;
        if (token == KW_STEP) {
            nextToken();
            parseInitExpression(stepExpr, &stepExprCount, MAX_INIT_EXPR_CODE);
        } else {
            initEmit(stepExpr, &stepExprCount, MAX_INIT_EXPR_CODE, LIT, 0, 1);
        }

        int cx1 = cx;
        emit(LOD, getCurrentLevel() - obj->level, obj->address);
        for (int i = 0; i < boundExprCount; i++) {
            emit(boundExpr[i].op, boundExpr[i].l, boundExpr[i].a);
        }
        emit(OPR, 0, isDownTo ? 10 : 12); // >= or <=
        int cx2 = cx;
        emit(JPC, 0, 0);
        expect(KW_DO);
        statement();

        emit(LOD, getCurrentLevel() - obj->level, obj->address);
        for (int i = 0; i < stepExprCount; i++) {
            emit(stepExpr[i].op, stepExpr[i].l, stepExpr[i].a);
        }
        emit(OPR, 0, isDownTo ? 3 : 2); // - or +
        emit(STO, getCurrentLevel() - obj->level, obj->address);
        emit(JMP, 0, cx1);
        code[cx2].a = cx;
    }
}

void block(void) {
    enterBlock();
    int frameIdx = getCurrentLevel() - 1;
    int tx0 = cx;
    emit(JMP, 0, 0);
    PendingVarInit *pendingInit = pendingInitFrames[frameIdx];
    int pendingInitCount = 0;
    PendingRuntimeArrayInit *pendingRuntimeArrayInit = pendingRuntimeArrayInitFrames[frameIdx];
    int pendingRuntimeArrayInitCount = 0;

    if (pendingParamCount > 0 && getCurrentLevel() == pendingParamLevel) {
        int totalParamSlots = 0;
        for (int i = 0; i < pendingParamCount; i++) {
            totalParamSlots += pendingParamSlotCount[i];
        }
        int slotCursor = -totalParamSlots;
        for (int i = 0; i < pendingParamCount; i++) {
            enter(pendingParamName[i], OBJ_PARAMETER, 0, pendingParamSize[i], 0);
            Object *param = lookup(pendingParamName[i]);
            param->address = slotCursor;
            if (!pendingParamIsRef[i] && pendingParamDimCount[i] > 0) {
                param->size = 1;
            } else {
                param->size = pendingParamSize[i];
            }
            param->isRefParam = pendingParamIsRef[i];
            param->dimCount = pendingParamDimCount[i];
            for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                param->dims[d] = pendingParamDims[i][d];
                param->dimAddr[d] = DIM_ADDR_UNUSED;
                if (pendingParamDims[i][d] == 0 && d < pendingParamDimCount[i]) {
                    param->dimAddr[d] = slotCursor + 1 + d;
                }
            }
            slotCursor += pendingParamSlotCount[i];
        }
        pendingParamCount = 0;
        pendingParamLevel = -1;
    }

    while (token == KW_CONST || token == KW_VAR || token == KW_PROCEDURE) {
        if (token == KW_CONST) {
            nextToken();
            do {
                if (token != TK_IDENT) error("block: expected identifier in CONST");
                char name[MAX_IDENT_LEN + 1];
                strcpy(name, Id);
                nextToken();

                int dims[MAX_ARRAY_DIMS];
                int dimCount = 0;
                int totalSize = 1;
                for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                    dims[d] = 0;
                }

                while (token == SB_LBRACK) {
                    nextToken();
                    if (dimCount >= MAX_ARRAY_DIMS) {
                        error("block: too many array dimensions");
                    }
                    double dimValue = parseConstExpression();
                    if (dimValue <= 0 || dimValue != (double)((int)dimValue)) {
                        error("block: array size must be a positive integer expression");
                    }
                    dims[dimCount] = (int)dimValue;
                    totalSize *= dims[dimCount];
                    dimCount++;
                    expect(SB_RBRACK);
                }

                if (token != SB_EQU && token != SB_ASSIGN) {
                    error("block: expected '=' or ':=' in CONST");
                }
                nextToken();

                if (dimCount > 0) {
                    if (token != SB_LBRACK) {
                        error("immutable array initializer must use bracket list");
                    }
                    if (pendingInitCount >= MAX_SYMBOL_TABLE_SIZE) {
                        error("too many variable initializers in block");
                    }

                    enter(name, OBJ_VARIABLE, 0, totalSize, 0);
                    Object *arrObj = lookup(name);
                    arrObj->isImmutable = 1;
                    arrObj->dimCount = dimCount;
                    for (int d = 0; d < dimCount; d++) {
                        arrObj->dims[d] = dims[d];
                    }

                    pendingInit[pendingInitCount].target = arrObj;
                    pendingInit[pendingInitCount].kind = VAR_INIT_ARRAY_LITERAL;
                    pendingInit[pendingInitCount].exprCount = 0;
                    pendingInit[pendingInitCount].arrayCount = 0;

                    parseConstArrayLiteralValues(pendingInit[pendingInitCount].arrayValues,
                                                &pendingInit[pendingInitCount].arrayCount,
                                                MAX_ARRAY_INIT_VALUES);
                    if (pendingInit[pendingInitCount].arrayCount != totalSize) {
                        error("immutable array initializer size mismatch");
                    }
                    arrObj->initSize = pendingInit[pendingInitCount].arrayCount;
                    pendingInitCount++;

                } else if (token == TK_STRING) {
                    enter(name, OBJ_CONSTANT, 0, 0, 0);
                    Object *obj = lookup(name);
                    obj->constIsString = 1;
                    strncpy(obj->constString, StringLiteral, MAX_STRING_LEN);
                    obj->constString[MAX_STRING_LEN] = '\0';
                    nextToken();
                } else {
                    if (getCurrentLevel() > 1) {
                        if (pendingInitCount >= MAX_SYMBOL_TABLE_SIZE) {
                            error("too many variable initializers in block");
                        }
                        enter(name, OBJ_VARIABLE, 0, 1, 0);
                        Object *obj = lookup(name);
                        obj->isImmutable = 1;
                        pendingInit[pendingInitCount].target = obj;
                        pendingInit[pendingInitCount].kind = VAR_INIT_SCALAR_EXPR;
                        pendingInit[pendingInitCount].exprCount = 0;
                        pendingInit[pendingInitCount].arrayCount = 0;
                        parseInitExpression(pendingInit[pendingInitCount].exprCode,
                                            &pendingInit[pendingInitCount].exprCount,
                                            MAX_INIT_EXPR_CODE);
                        pendingInitCount++;
                    } else {
                        double value = parseConstExpression();
                        enter(name, OBJ_CONSTANT, value, 0, 0);
                    }
                }
                if (token == SB_COMMA) nextToken(); else break;
            } while (1);
            expect(SB_SEMICOLON);
            continue;
        }

        if (token == KW_VAR) {
            nextToken();
            do {
                if (token != TK_IDENT) error("block: expected identifier in VAR");
                char varName[MAX_IDENT_LEN + 1];
                strcpy(varName, Id);
                nextToken();
                int size = 1;
                int isRuntimeArray = 0;
                int dimCount = 0;
                int dims[MAX_ARRAY_DIMS];
                for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                    dims[d] = 0;
                }
                Instruction runtimeDimExpr[MAX_ARRAY_DIMS][MAX_INIT_EXPR_CODE];
                int runtimeDimExprCount[MAX_ARRAY_DIMS];
                int runtimeDimCount = 0;
                for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                    runtimeDimExprCount[d] = 0;
                }
                if (token == SB_LBRACK) {
                    nextToken();
                    if (token == TK_NUMBER) {
                        double sizeValue = Num;
                        nextToken();
                        if (token == SB_RBRACK) {
                            if (sizeValue <= 0 || sizeValue != (double)((int)sizeValue)) {
                                error("block: array size must be a positive integer expression");
                            }
                            size = (int)sizeValue;
                            dims[dimCount++] = size;
                        } else {
                            isRuntimeArray = 1;
                            runtimeDimExprCount[0] = 0;
                            initEmit(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE, LIT, 0, sizeValue);
                            runtimeDimCount = 1;
                            while (token != SB_RBRACK) {
                                if (token == TK_NONE) {
                                    error("block: missing ']' in array declaration");
                                }
                                if (token == SB_PLUS || token == SB_MINUS || token == SB_TIMES || token == SB_SLASH || token == SB_FLOORDIV || token == SB_PERCENT) {
                                    TokenType op = token;
                                    nextToken();
                                    parseInitFactor(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE);
                                    initEmit(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE, OPR, 0,
                                             op == SB_PLUS ? 2 : (op == SB_MINUS ? 3 : (op == SB_TIMES ? 4 : (op == SB_SLASH ? 5 : (op == SB_FLOORDIV ? 23 : 14)))));
                                } else {
                                    error("block: invalid runtime array size expression");
                                }
                            }
                        }
                    } else {
                        if (token == TK_IDENT) {
                            Object *sizeObj = lookup(Id);
                            if (sizeObj != NULL && sizeObj->type == OBJ_CONSTANT && !sizeObj->constIsString) {
                                char constName[MAX_IDENT_LEN + 1];
                                strcpy(constName, Id);
                                nextToken();
                                if (token == SB_RBRACK) {
                                    double sizeValue = sizeObj->value;
                                    if (sizeValue <= 0 || sizeValue != (double)((int)sizeValue)) {
                                        error("block: array size must be a positive integer expression");
                                    }
                                    size = (int)sizeValue;
                                    dims[dimCount++] = size;
                                } else {
                                    isRuntimeArray = 1;
                                    runtimeDimExprCount[0] = 0;
                                    initEmit(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE, LIT, 0, sizeObj->value);
                                    runtimeDimCount = 1;
                                    while (token != SB_RBRACK) {
                                        if (token == TK_NONE) {
                                            error("block: missing ']' in array declaration");
                                        }
                                        if (token == SB_PLUS || token == SB_MINUS || token == SB_TIMES || token == SB_SLASH || token == SB_FLOORDIV || token == SB_PERCENT) {
                                            TokenType op = token;
                                            nextToken();
                                            parseInitFactor(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE);
                                            initEmit(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE, OPR, 0,
                                                     op == SB_PLUS ? 2 : (op == SB_MINUS ? 3 : (op == SB_TIMES ? 4 : (op == SB_SLASH ? 5 : (op == SB_FLOORDIV ? 23 : 14)))));
                                        } else {
                                            error("block: invalid runtime array size expression");
                                        }
                                    }
                                }
                            } else {
                                isRuntimeArray = 1;
                                parseInitExpression(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE);
                                runtimeDimCount = 1;
                                if (token != SB_RBRACK) {
                                    error("block: invalid runtime array size expression");
                                }
                            }
                        } else {
                            isRuntimeArray = 1;
                            parseInitExpression(runtimeDimExpr[0], &runtimeDimExprCount[0], MAX_INIT_EXPR_CODE);
                            runtimeDimCount = 1;
                            if (token != SB_RBRACK) {
                                error("block: invalid runtime array size expression");
                            }
                        }
                    }
                    expect(SB_RBRACK);

                    while (token == SB_LBRACK) {
                        if (dimCount >= MAX_ARRAY_DIMS) {
                            error("block: too many array dimensions");
                        }
                        nextToken();
                        if (isRuntimeArray) {
                            if (runtimeDimCount >= MAX_ARRAY_DIMS) {
                                error("block: too many runtime array dimensions");
                            }
                            parseInitExpression(runtimeDimExpr[runtimeDimCount], &runtimeDimExprCount[runtimeDimCount], MAX_INIT_EXPR_CODE);
                            runtimeDimCount++;
                        } else {
                            double dimValue = parseConstExpression();
                            if (dimValue <= 0 || dimValue != (double)((int)dimValue)) {
                                error("block: array size must be a positive integer expression");
                            }
                            dims[dimCount++] = (int)dimValue;
                            size *= (int)dimValue;
                        }
                        expect(SB_RBRACK);
                    }
                }
                enter(varName, OBJ_VARIABLE, 0, isRuntimeArray ? (1 + runtimeDimCount) : size, 0);
                Object *declObj = lookup(varName);
                if (isRuntimeArray) {
                    declObj->isRuntimeArray = 1;
                    declObj->lengthAddress = declObj->address + 1;
                    declObj->dimCount = runtimeDimCount;
                    for (int d = 0; d < runtimeDimCount; d++) {
                        declObj->dimAddr[d] = declObj->address + 1 + d;
                        declObj->dims[d] = 0;
                    }
                    if (pendingRuntimeArrayInitCount >= MAX_SYMBOL_TABLE_SIZE) {
                        error("too many runtime array declarations in block");
                    }
                    pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].target = declObj;
                    pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].dimCount = runtimeDimCount;
                    for (int d = 0; d < runtimeDimCount; d++) {
                        pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].dimExprCount[d] = runtimeDimExprCount[d];
                        for (int s = 0; s < runtimeDimExprCount[d]; s++) {
                            pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].dimExprCode[d][s] = runtimeDimExpr[d][s];
                        }
                    }
                    pendingRuntimeArrayInitCount++;
                } else if (dimCount > 0) {
                    declObj->dimCount = dimCount;
                    for (int d = 0; d < dimCount; d++) {
                        declObj->dims[d] = dims[d];
                    }
                    if (declObj->dimCount > 1 && declObj->dims[0] <= 0 && size > 0) {
                        int tail = 1;
                        int knownTail = 1;
                        for (int d = 1; d < declObj->dimCount; d++) {
                            if (declObj->dims[d] <= 0) {
                                knownTail = 0;
                                break;
                            }
                            tail *= declObj->dims[d];
                        }
                        if (knownTail && tail > 0) {
                            declObj->dims[0] = size / tail;
                        }
                    }
                }

                if (token == SB_EQU || token == SB_ASSIGN) {
                    nextToken();
                    if (pendingInitCount >= MAX_SYMBOL_TABLE_SIZE) {
                        error("too many variable initializers in block");
                    }

                    pendingInit[pendingInitCount].target = lookup(varName);
                    pendingInit[pendingInitCount].exprCount = 0;
                    pendingInit[pendingInitCount].arrayCount = 0;

                    if (isRuntimeArray) {
                        error("Runtime-sized arrays do not support initializer lists");
                    } else if (size > 1) {
                        if (token != SB_LBRACK) {
                            error("Array initializer must use bracket list, e.g. VAR A[4] := [1,2]");
                        }
                        pendingInit[pendingInitCount].kind = VAR_INIT_ARRAY_LITERAL;
                        parseVarArrayLiteralValues(pendingInit[pendingInitCount].arrayValues,
                                                   &pendingInit[pendingInitCount].arrayCount,
                                                   MAX_ARRAY_INIT_VALUES);
                        if (pendingInit[pendingInitCount].arrayCount > size) {
                            error("array initializer has more elements than declared size");
                        }
                        if (pendingInit[pendingInitCount].target != NULL) {
                            pendingInit[pendingInitCount].target->initSize = pendingInit[pendingInitCount].arrayCount;
                        }
                    } else {
                        pendingInit[pendingInitCount].kind = VAR_INIT_SCALAR_EXPR;
                        parseInitExpression(pendingInit[pendingInitCount].exprCode,
                                            &pendingInit[pendingInitCount].exprCount,
                                            MAX_INIT_EXPR_CODE);
                    }

                    pendingInitCount++;
                }

                if (token == SB_COMMA) nextToken(); else break;
            } while (1);
            expect(SB_SEMICOLON);
            continue;
        }

        // PROCEDURE declaration
        nextToken();
        if (token != TK_IDENT) error("block: expected identifier after PROCEDURE");
        char procName[MAX_IDENT_LEN + 1];
        strcpy(procName, Id);
        enter(procName, OBJ_PROCEDURE, 0, 0, 0);
        Object* obj = lookup(procName);
        nextToken();

        pendingParamCount = 0;
        if (token == SB_LPARENT) {
            nextToken();
            if (token != SB_RPARENT) {
                while (1) {
                    int isRef = 0;
                    if (token == KW_VAR) {
                        isRef = 1;
                        nextToken();
                    }
                    if (token != TK_IDENT) {
                        error("procedure parameter: expected identifier");
                    }
                    if (pendingParamCount >= MAX_PROC_PARAMS) {
                        error("too many procedure parameters");
                    }
                    int paramSize = 1;
                    int paramDimCount = 0;
                    int paramDims[MAX_ARRAY_DIMS];
                    for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                        paramDims[d] = 0;
                    }
                    strcpy(pendingParamName[pendingParamCount], Id);
                    nextToken();
                    if (token == SB_LBRACK) {
                        int totalSize = 1;
                        int hasUnsizedDim = 0;
                        while (token == SB_LBRACK) {
                            if (paramDimCount >= MAX_ARRAY_DIMS) {
                                error("too many array dimensions in parameter");
                            }
                            nextToken();
                            if (token == SB_RBRACK) {
                                hasUnsizedDim = 1;
                                paramDims[paramDimCount++] = 0;
                                nextToken();
                            } else {
                                double sizeValue = parseConstExpression();
                                if (sizeValue <= 0 || sizeValue != (double)((int)sizeValue)) {
                                    error("procedure parameter array size must be a positive integer expression");
                                }
                                paramDims[paramDimCount++] = (int)sizeValue;
                                totalSize *= (int)sizeValue;
                                expect(SB_RBRACK);
                            }
                        }
                        // Keep unsized form as generic array marker for argument validation.
                        paramSize = hasUnsizedDim ? 2 : totalSize;
                    }
                    pendingParamIsRef[pendingParamCount] = isRef;
                    pendingParamSize[pendingParamCount] = paramSize;
                    pendingParamDimCount[pendingParamCount] = paramDimCount;
                    pendingParamSlotCount[pendingParamCount] = (paramDimCount > 0) ? (1 + paramDimCount) : 1;
                    for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                        pendingParamDims[pendingParamCount][d] = (d < paramDimCount) ? paramDims[d] : 0;
                    }
                    obj->paramIsRef[pendingParamCount] = isRef;
                    obj->paramSize[pendingParamCount] = paramSize;
                    obj->paramDimCount[pendingParamCount] = paramDimCount;
                    for (int d = 0; d < MAX_ARRAY_DIMS; d++) {
                        obj->paramDims[pendingParamCount][d] = (d < paramDimCount) ? paramDims[d] : 0;
                    }
                    pendingParamCount++;

                    if (token == SB_COMMA || token == SB_SEMICOLON) {
                        nextToken();
                        continue;
                    }
                    break;
                }
            }
            expect(SB_RPARENT);
        }
        obj->paramCount = pendingParamCount;
        pendingParamLevel = getCurrentLevel() + 1;

        expect(SB_SEMICOLON);
        int procEntry = cx;
        // Publish entry point early so recursive calls inside this block resolve correctly.
        obj->address = procEntry;
        Object *savedProcedure = currentProcedure;
        currentProcedure = obj;
        block();
        currentProcedure = savedProcedure;
        expect(SB_SEMICOLON);
    }

    code[tx0].a = cx;
    emit(INT, 0, getVarCount() + 3);

    // Initialize scalar immutable/runtime values first (may feed VLA dimensions).
    for (int i = 0; i < pendingInitCount; i++) {
        if (pendingInit[i].kind == VAR_INIT_SCALAR_EXPR) {
            for (int k = 0; k < pendingInit[i].exprCount; k++) {
                emit(pendingInit[i].exprCode[k].op,
                     pendingInit[i].exprCode[k].l,
                     pendingInit[i].exprCode[k].a);
            }
            emitStoreObjectValue(pendingInit[i].target);
        }
    }

    // Allocate runtime arrays once dependent scalars are initialized.
    for (int i = 0; i < pendingRuntimeArrayInitCount; i++) {
        Object *arrObj = pendingRuntimeArrayInit[i].target;
        int l = getCurrentLevel() - arrObj->level;
        for (int d = 0; d < pendingRuntimeArrayInit[i].dimCount; d++) {
            for (int k = 0; k < pendingRuntimeArrayInit[i].dimExprCount[d]; k++) {
                emit(pendingRuntimeArrayInit[i].dimExprCode[d][k].op,
                     pendingRuntimeArrayInit[i].dimExprCode[d][k].l,
                     pendingRuntimeArrayInit[i].dimExprCode[d][k].a);
            }
            emit(DUP, 0, 0);
            emit(STO, l, arrObj->dimAddr[d]);
            if (d > 0) {
                emit(OPR, 0, 4);
            }
        }
        emit(ALC, 0, 0);
        emit(STO, l, arrObj->address);
    }

    // Apply static array literal initializers after storage exists.
    for (int i = 0; i < pendingInitCount; i++) {
        if (pendingInit[i].kind == VAR_INIT_ARRAY_LITERAL) {
            for (int k = 0; k < pendingInit[i].arrayCount; k++) {
                emitLoadObjectAddress(pendingInit[i].target);
                emit(LIT, 0, k);
                emit(OPR, 0, 2);
                emit(LIT, 0, pendingInit[i].arrayValues[k]);
                emit(STI, 0, 0);
            }
        }
    }
    statement();
    emit(OPR, 0, 0); // Return
    exitBlock();
}

void program(void) {
    hasLookahead = 0;
    initSymbolTable();
    cx = 0;
    // expect(KW_PROGRAM);
    if (token == KW_PROGRAM) {
        nextToken();
        if (token != TK_IDENT) {
            error("program: expected program name");
        }
    }
    nextToken();
    expect(SB_SEMICOLON);
    block();
    if (token == SB_PERIOD) {
        nextToken();
    }
    if (token != TK_NONE) {
        error("program: unexpected token after '.'");
    }
    optimizeCode();
    // listCode();
    interpret();
}

static int tokenStartsStringValueExpr(void) {
    if (token == TK_STRING) {
        return 1;
    }
    if (token == TK_IDENT) {
        Object *obj = lookup(Id);
        return obj != NULL && ((obj->type == OBJ_CONSTANT && obj->constIsString) || obj->isString);
    }
    return 0;
}

static void emitAppendStringTerm(const Object *target) {
    if (token == TK_STRING) {
        emitLoadObjectAddress(target);
        emit(CATL, 0, addStringLiteral(StringLiteral));
        nextToken();
        return;
    }

    if (token == TK_NUMBER) {
        emitLoadObjectAddress(target);
        emit(LIT, 0, Num);
        emit(CATI, 0, 0);
        nextToken();
        return;
    }

    if (token == SB_LPARENT) {
        emitLoadObjectAddress(target);
        nextToken();
        expression();
        expect(SB_RPARENT);
        emit(CATI, 0, 0);
        return;
    }

    if (token != TK_IDENT) {
        error("string expression: expected string or numeric term");
    }

    Object *obj = lookup(Id);
    if (obj == NULL) {
        char msg[100];
        sprintf(msg, "Undeclared identifier: %s", Id);
        error(msg);
    }
    if (obj->type == OBJ_PROCEDURE) {
        error("string expression: procedure has no value");
    }

    if (obj->type == OBJ_CONSTANT) {
        emitLoadObjectAddress(target);
        if (obj->constIsString) {
            emit(CATL, 0, addStringLiteral(obj->constString));
        } else {
            emit(LIT, 0, obj->value);
            emit(CATI, 0, 0);
        }
        nextToken();
        return;
    }

    nextToken();
    if (obj->isString) {
        if (token == SB_LBRACK) {
            error("string expression: STRING variable does not take index");
        }
        emitLoadObjectAddress(target);
        emitLoadObjectAddress(obj);
        emit(CATV, 0, 0);
        return;
    }

    if (token == SB_LBRACK) {
        if (!isArrayLikeObject(obj)) {
            error("string expression: indexed access requires array variable");
        }
        emitLoadObjectAddress(target);
        (void)emitIndexedAddress(obj, 1, 1, "string expression indexed access");
        emit(LDI, 0, 0);
        emit(CATI, 0, 0);
    } else {
        if (isArrayLikeObject(obj)) {
            emitAppendArrayToString(target, obj);
        } else {
            emitLoadObjectAddress(target);
            emitLoadObjectValue(obj);
            emit(CATI, 0, 0);
        }
    }
}
