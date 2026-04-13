#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "scanner.h"
#include "parser.h"
#include "semantics.h"
#include "codegen.h"

// Các biến toàn cục từ scanner.c
extern TokenType Token;
extern double    Num;
extern char      Id[MAX_IDENT_LEN + 1];
extern char      StringLiteral[MAX_STRING_LEN + 1];
extern int       TokenLine;
extern int       TokenColumn;
extern char      CurrentSourceFile[260];

static int pendingParamCount = 0;
static int pendingParamIsRef[MAX_PROC_PARAMS];
static int pendingParamSize[MAX_PROC_PARAMS];
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
    Instruction exprCode[MAX_INIT_EXPR_CODE];
    int exprCount;
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
    if (obj->isRefParam) {
        initEmit(buf, count, maxCount, LOD, l, obj->address);
    } else {
        initEmit(buf, count, maxCount, LDA, l, obj->address);
    }
}

// ─── Tiện ích ────────────────────────────────────────────────────────────────

void nextToken(void) {
    Token = getToken();
    // printf(" %s", TabToken[Token]);
    // if(Token == TK_IDENT) printf("(%s) \n", Id);
    // else if(Token == TK_NUMBER) printf("(%g) \n", Num);
    // else printf("\n");
}

void error(const char msg[]) {
    char tokenDetail[320];
    formatCurrentTokenDetail(tokenDetail, sizeof(tokenDetail));
    printf("Error at %s: %d:%d: %s%s\n",
           CurrentSourceFile,
           TokenLine,
           TokenColumn,
           msg,
           tokenDetail);
    exit(1);
}

// Kiểm tra token hiện tại có khớp không, nếu có thì đọc token tiếp
static void expect(TokenType expected) {
    if (Token != expected) {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected '%s' but got '%s'", TabToken[expected], TabToken[Token]);
        error(msg);
    }
    nextToken();
}

static void formatCurrentTokenDetail(char *buf, size_t bufSize) {
    if (bufSize == 0) {
        return;
    }
    buf[0] = '\0';
    switch (Token) {
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
            snprintf(buf, bufSize, " near token '%s'", TabToken[Token]);
            break;
    }
}

static double parseVarInitializerValue(void) {
    int sign = 1;
    if (Token == SB_PLUS || Token == SB_MINUS) {
        if (Token == SB_MINUS) {
            sign = -1;
        }
        nextToken();
    }

    if (Token == TK_NUMBER) {
        double value = Num;
        nextToken();
        return sign * value;
    }

    if (Token == TK_IDENT) {
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
    if (Token == KW_SIZEOF) {
        nextToken();
        Object *obj = parseSizeOfTargetObject(1);
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                return (double)strlen(obj->constString);
            }
            return 1;
        }
        if (obj->type == OBJ_VARIABLE || obj->type == OBJ_PARAMETER) {
            if (obj->isString) {
                return (double)((obj->size > 0) ? obj->size : 1);
            }
            if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
                error("SIZEOF in CONST expression requires compile-time sized symbol");
            }
            if (obj->size > 1) {
                return (double)obj->size;
            }
            return 1;
        }
        error("SIZEOF: procedure is not supported");
        return 0;
    }

    if (Token == TK_NUMBER) {
        double value = Num;
        nextToken();
        return value;
    }

    if (Token == TK_IDENT) {
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
            if (Token == SB_LPARENT) {
                error("CONST initializer cannot call procedure");
            }
            error("CONST initializer requires numeric constant expression");
        }
        if (obj->type != OBJ_CONSTANT || obj->constIsString) {
            error("CONST initializer requires numeric constant expression");
        }
        return obj->value;
    }

    if (Token == SB_LPARENT) {
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
    while (Token == SB_TIMES || Token == SB_SLASH) {
        TokenType op = Token;
        nextToken();
        double rhs = parseConstFactor();
        if (op == SB_TIMES) {
            value *= rhs;
        } else {
            if (rhs == 0) {
                error("CONST initializer division by zero");
            }
            value /= rhs;
        }
    }
    return value;
}

static double parseConstExpression(void) {
    TokenType prefix = TK_NONE;
    if (Token == SB_PLUS || Token == SB_MINUS) {
        prefix = Token;
        nextToken();
    }

    double value = parseConstTerm();
    if (prefix == SB_MINUS) {
        value = -value;
    }

    while (Token == SB_PLUS || Token == SB_MINUS) {
        TokenType op = Token;
        nextToken();
        double rhs = parseConstTerm();
        value = (op == SB_PLUS) ? (value + rhs) : (value - rhs);
    }
    return value;
}

static Object *parseSizeOfTargetObject(int constMode) {
    expect(SB_LPARENT);
    if (Token != TK_IDENT) {
        error("SIZEOF: expected identifier");
    }
    Object *obj = lookup(Id);
    if (obj == NULL) {
        char msg[120];
        sprintf(msg, "SIZEOF: undeclared identifier %s", Id);
        error(msg);
    }
    nextToken();

    if (Token == SB_LBRACK) {
        if (constMode) {
            nextToken();
            (void)parseConstExpression();
            expect(SB_RBRACK);
        } else {
            int depth = 1;
            nextToken();
            while (depth > 0) {
                if (Token == TK_NONE) {
                    error("SIZEOF: missing ']' ");
                }
                if (Token == SB_LBRACK) {
                    depth++;
                } else if (Token == SB_RBRACK) {
                    depth--;
                }
                nextToken();
            }
        }
    }

    expect(SB_RPARENT);
    return obj;
}

static void parseInitFactor(Instruction *buf, int *count, int maxCount) {
    if (Token == TK_NUMBER) {
        initEmit(buf, count, maxCount, LIT, 0, Num);
        nextToken();
        return;
    }

    if (Token == KW_SIZEOF) {
        nextToken();
        expect(SB_LPARENT);
        if (Token != TK_IDENT) {
            error("SIZEOF initializer: expected identifier");
        }
        Object *obj = lookup(Id);
        if (obj == NULL) {
            char msg[120];
            sprintf(msg, "SIZEOF initializer: undeclared identifier %s", Id);
            error(msg);
        }
        nextToken();
        expect(SB_RPARENT);
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                initEmit(buf, count, maxCount, LIT, 0, (double)strlen(obj->constString));
            } else {
                initEmit(buf, count, maxCount, LIT, 0, 1);
            }
        } else if (obj->type == OBJ_VARIABLE || obj->type == OBJ_PARAMETER) {
            if (obj->isString) {
                initEmit(buf, count, maxCount, LIT, 0, obj->size > 0 ? obj->size : 1);
            } else if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
                initEmit(buf, count, maxCount, LOD, getCurrentLevel() - obj->level, obj->lengthAddress);
            } else if (obj->size > 1) {
                initEmit(buf, count, maxCount, LIT, 0, obj->size);
            } else {
                initEmit(buf, count, maxCount, LIT, 0, 1);
            }
        } else {
            error("SIZEOF initializer: procedure is not supported");
        }
        return;
    }

    if (Token == TK_IDENT) {
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
        if (Token == SB_LBRACK) {
            if (obj->size <= 1) {
                error("initializer indexed access requires array variable");
            }
            initEmitLoadObjectAddress(buf, count, maxCount, obj);
            nextToken();
            parseInitExpression(buf, count, maxCount);
            expect(SB_RBRACK);
            initEmit(buf, count, maxCount, OPR, 0, 2);
            initEmit(buf, count, maxCount, LDI, 0, 0);
        } else {
            if (obj->size > 1) {
                error("initializer array variable requires an index");
            }
            initEmitLoadObjectValue(buf, count, maxCount, obj);
        }
        return;
    }

    if (Token == SB_LPARENT) {
        nextToken();
        parseInitExpression(buf, count, maxCount);
        expect(SB_RPARENT);
        return;
    }

    error("initializer: expected number, identifier, or '(' ");
}

static void parseInitTerm(Instruction *buf, int *count, int maxCount) {
    parseInitFactor(buf, count, maxCount);
    while (Token == SB_TIMES || Token == SB_SLASH) {
        TokenType op = Token;
        nextToken();
        parseInitFactor(buf, count, maxCount);
        initEmit(buf, count, maxCount, OPR, 0, op == SB_TIMES ? 4 : 5);
    }
}

static void parseInitExpression(Instruction *buf, int *count, int maxCount) {
    TokenType prefix = TK_NONE;
    if (Token == SB_PLUS || Token == SB_MINUS) {
        prefix = Token;
        nextToken();
    }
    parseInitTerm(buf, count, maxCount);
    if (prefix == SB_MINUS) {
        initEmit(buf, count, maxCount, OPR, 0, 1);
    }
    while (Token == SB_PLUS || Token == SB_MINUS) {
        TokenType op = Token;
        nextToken();
        parseInitTerm(buf, count, maxCount);
        initEmit(buf, count, maxCount, OPR, 0, op == SB_PLUS ? 2 : 3);
    }
}

static int isAssignableObject(const Object *obj) {
    return obj->type == OBJ_VARIABLE || obj->type == OBJ_PARAMETER;
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
    if (obj->isRefParam) {
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
        if (obj->isString) {
            emitLoadObjectAddress(obj);
            emit(LEN, 0, 0);
        } else if (obj->isRuntimeArray || obj->lengthAddress >= 0) {
            emit(LOD, getCurrentLevel() - obj->level, obj->lengthAddress);
        } else if (obj->size > 1) {
            // C-like behavior: SIZEOF(array) returns declared element count.
            emit(LIT, 0, obj->size);
        } else {
            emit(LIT, 0, 1);
        }
        return;
    }
    error("SIZEOF: procedure is not supported");
}

static void parseProcedureCallArguments(const Object *proc) {
    int argCount = 0;
    if (Token == SB_LPARENT) {
        nextToken();
        if (Token != SB_RPARENT) {
            while (1) {
                if (argCount >= proc->paramCount) {
                    error("Too many arguments in CALL");
                }
                if (proc->paramIsRef[argCount]) {
                    if (Token != TK_IDENT) {
                        error("VAR parameter requires assignable identifier argument");
                    }
                    Object *arg = lookup(Id);
                    if (arg == NULL || !isAssignableObject(arg) || arg->isString) {
                        error("VAR parameter requires variable argument");
                    }
                    nextToken();

                    int indexedArg = 0;
                    if (Token == SB_LBRACK) {
                        indexedArg = 1;
                        if (arg->size <= 1) {
                            error("Indexed VAR argument requires array variable");
                        }
                        emitLoadObjectAddress(arg);
                        nextToken();
                        expression();
                        expect(SB_RBRACK);
                        emit(OPR, 0, 2);
                    }

                    if (proc->paramSize[argCount] > 1) {
                        if (indexedArg || arg->size <= 1) {
                            error("Array parameter requires array variable argument");
                        }
                        emitLoadObjectAddress(arg);
                    } else {
                        if (!indexedArg && arg->size != 1) {
                            error("Scalar VAR parameter requires scalar variable or indexed array element");
                        }
                        if (!indexedArg) {
                            emitLoadObjectAddress(arg);
                        }
                    }
                } else {
                    if (proc->paramSize[argCount] > 1) {
                        error("Array parameter must be passed by VAR/reference");
                    }
                    if (Token == TK_STRING) {
                        emit(LIT, 0, addStringLiteral(StringLiteral));
                        nextToken();
                    } else if (Token == TK_IDENT) {
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
                if (Token == SB_COMMA) {
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
            if (obj->type == OBJ_CONSTANT || obj->isString || obj->size <= 1) {
                error("interpolation: indexed access requires numeric array variable");
            }
            emitLoadObjectAddress(obj);
            interpExprPtr++; // consume '['
            interpParseExpression();
            if (!interpAccept(']')) {
                error("interpolation: expected ']' ");
            }
            emit(OPR, 0, 2);
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
        if (obj->size > 1) {
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
        } else if (*interpExprPtr == '/') {
            interpExprPtr++;
            interpParseFactor();
            emit(OPR, 0, 5);
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
                if (obj->size > 1) {
                    error("interpolation: array requires explicit index in expression");
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
    if (Token == TK_STRING) {
        emitInterpolatedString(StringLiteral);
        nextToken();
    } else if (Token == TK_NUMBER) {
        emit(LIT, 0, Num);
        emit(WRI, 0, 0);
        nextToken();
    } else if (Token == TK_IDENT) {
        Object *obj = lookup(Id);
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
            emit(WRI, 0, 0);
            return;
        }
        if (obj->type == OBJ_CONSTANT) {
            if (obj->constIsString) {
                emit(WRL, 0, addStringLiteral(obj->constString));
            } else {
                emit(LIT, 0, obj->value);
                emit(WRI, 0, 0);
            }
            nextToken();
            return;
        }

        nextToken();
        if (Token == SB_INC) {
            if (obj->isString || obj->size != 1) {
                error("Postfix increment in WRITE requires scalar numeric variable");
            }
            // Print old value, then commit increment.
            emitLoadObjectValue(obj);
            emitLoadObjectValue(obj);
            emit(LIT, 0, 1);
            emit(OPR, 0, 2);
            emitStoreObjectValue(obj);
            emit(WRI, 0, 0);
            nextToken();
            return;
        }
        if (isAssignableObject(obj) && obj->isString) {
            if (Token == SB_LBRACK) {
                error("WRITE/WRITELN of STRING variable does not take index");
            }
            emit(WRS, getCurrentLevel() - obj->level, obj->address);
            return;
        }

        if (Token == SB_LBRACK) {
            if (obj->size <= 1) {
                error("Indexed WRITE requires an array variable");
            }
            emitLoadObjectAddress(obj);
            nextToken();
            expression();
            expect(SB_RBRACK);
            emit(OPR, 0, 2);
            emit(LDI, 0, 0);
            emit(WRI, 0, 0);
        } else {
            if (obj->size > 1) {
                error("Array variable requires an index");
            }
            emitLoadObjectValue(obj);
            emit(WRI, 0, 0);
        }
    } else {
        expression();
        emit(WRI, 0, 0);
    }
}

// ─── Phân tích biểu thức ─────────────────────────────────────────────────────

// factor = NUMBER | IDENT | '(' expression ')'
void factor(void) {
    if (Token == TK_NUMBER) {
        emit(LIT, 0, Num);
        nextToken();
    } else if (Token == KW_SIZEOF) {
        nextToken();
        Object *obj = parseSizeOfTargetObject(0);
        emitSizeOfObjectValue(obj);
        return;
    } else if (Token == TK_IDENT) {

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
            if (Token == SB_INC) {
                if (obj->size > 1) {
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
            if (Token == SB_LBRACK) {
                if (obj->size <= 1) {
                    error("Indexed access is only valid for arrays");
                }
                emitLoadObjectAddress(obj);
                nextToken();
                expression();
                expect(SB_RBRACK);
                emit(OPR, 0, 2);
                emit(LDI, 0, 0);
                return;
            }
            if (obj->size > 1) {
                error("Array variable requires an index");
            }
            emitLoadObjectValue(obj);
        }
    } else if (Token == SB_LPARENT) {
        nextToken();                      // ăn '('
        expression();
        expect(SB_RPARENT);              // ăn ')'
    } else {
        error("factor: expected number, identifier, or '('");
    }
}

static void unaryExpr(void) {
    if (Token == SB_INC) {
        nextToken();
        if (Token != TK_IDENT) {
            error("Prefix increment expects identifier");
        }
        Object *obj = lookup(Id);
        if (obj == NULL || !isAssignableObject(obj)) {
            error("Prefix increment requires variable");
        }
        if (obj->isString || obj->size != 1) {
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
    if (Token == SB_PLUS) {
        nextToken();
        unaryExpr();
        return;
    }
    if (Token == SB_MINUS) {
        nextToken();
        unaryExpr();
        emit(OPR, 0, 1);
        return;
    }
    if (Token == SB_BITNOT) {
        nextToken();
        unaryExpr();
        emit(OPR, 0, 17);
        return;
    }
    factor();
}

// term = unaryExpr { ('*' | '/' | '%') unaryExpr }
void term(void) {
    unaryExpr();
    while (Token == SB_TIMES || Token == SB_SLASH || Token == SB_PERCENT) {
        TokenType op = Token;
        nextToken();
        unaryExpr();
        if (op == SB_TIMES) emit(OPR, 0, 4);
        else if (op == SB_SLASH) emit(OPR, 0, 5);
        else emit(OPR, 0, 14);
    }
}

static void additiveExpr(void) {
    term();
    while (Token == SB_PLUS || Token == SB_MINUS) {
        TokenType op = Token;
        nextToken();
        term();
        emit(OPR, 0, (op == SB_PLUS) ? 2 : 3);
    }
}

static void shiftExpr(void) {
    additiveExpr();
    while (Token == SB_SHL || Token == SB_SHR) {
        TokenType op = Token;
        nextToken();
        additiveExpr();
        emit(OPR, 0, (op == SB_SHL) ? 18 : 19);
    }
}

static void bitwiseAndExpr(void) {
    shiftExpr();
    while (Token == SB_BITAND) {
        nextToken();
        shiftExpr();
        emit(OPR, 0, 15);
    }
}

static void bitwiseXorExpr(void) {
    bitwiseAndExpr();
    while (Token == SB_BITXOR) {
        nextToken();
        bitwiseAndExpr();
        emit(OPR, 0, 16);
    }
}

// expression now includes bitwise operators with C-like precedence.
void expression(void) {
    bitwiseXorExpr();
    while (Token == SB_BITOR) {
        nextToken();
        bitwiseXorExpr();
        emit(OPR, 0, 13);
    }
}

static void conditionFactor(void) {
    if (Token == KW_NOT) {
        nextToken();
        conditionFactor();
        emit(OPR, 0, 22);
        return;
    }
    if (Token == SB_LPARENT) {
        nextToken();
        condition();
        expect(SB_RPARENT);
        return;
    }
    if (Token == KW_ODD) {
        nextToken();
        expression();
        emit(OPR, 0, 6);
        return;
    }

    expression();
    if (Token == SB_EQU || Token == SB_NEQ ||
        Token == SB_LSS || Token == SB_LEQ ||
        Token == SB_GTR || Token == SB_GEQ) {
        TokenType op = Token;
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
    while (Token == KW_AND) {
        nextToken();
        conditionFactor();
        emit(OPR, 0, 20);
    }
}

void condition(void) {
    conditionTerm();
    while (Token == KW_OR) {
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
            } else {
                emit(LIT, 0, obj->value);
                emit(WRI, 0, 0);
            }
        } else {
            if (obj->isString) {
                emit(WRS, getCurrentLevel() - obj->level, obj->address);
            } else {
                if (obj->size > 1) {
                    error("Array placeholder requires explicit index in expression");
                }
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
    if (Token == SB_INC) {
        nextToken();
        if (Token != TK_IDENT) {
            error("Prefix increment expects identifier");
        }
        Object *obj = lookup(Id);
        if (obj == NULL || !isAssignableObject(obj)) {
            error("Prefix increment requires variable");
        }
        if (obj->isString || obj->size != 1) {
            error("Prefix increment requires scalar numeric variable");
        }
        nextToken();
        emitLoadObjectValue(obj);
        emit(LIT, 0, 1);
        emit(OPR, 0, 2);
        emitStoreObjectValue(obj);
        return;

    } else if (Token == TK_IDENT) {
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
            return;
        }

        // Gán: IDENT ':=' expression
        if (!isAssignableObject(obj)) {
            error("Cannot assign to non-variable");
        }
        nextToken();

        if (Token == SB_INC) {
            if (obj->isString || obj->size != 1) {
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
        if (Token == SB_LBRACK) {
            if (obj->size <= 1) {
                error("Indexed assignment requires an array variable");
            }
            emitLoadObjectAddress(obj);
            nextToken();
            expression();
            expect(SB_RBRACK);
            emit(OPR, 0, 2);
            isIndexed = 1;
        }

        expect(SB_ASSIGN);

        if (!isIndexed && (obj->isString || tokenStartsStringValueExpr() || obj->size > 1)) {
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
            while (Token == SB_PLUS) {
                nextToken();
                emitAppendStringTerm(obj);
            }
        } else {
            if (obj->isString && isIndexed) {
                error("Cannot assign numeric value to indexed STRING storage");
            }
            if (isIndexed) {
                expression();
                emit(STI, 0, 0);
            } else {
                if (obj->size > 1) {
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

    } else if (Token == KW_READ || Token == KW_WRITE || Token == KW_WRITELN) {
        error("Use CALL before READ/READLN/WRITE/WRITELN");

    } else if (Token == KW_CALL) {
        // CALL can invoke built-ins (READ/WRITE/WRITELN) and user procedures.
        nextToken();
        if (Token == KW_READ) {
            nextToken();
            expect(SB_LPARENT);
            if (Token != TK_IDENT) {
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
            if (Token == SB_LBRACK) {
                emitLoadObjectAddress(obj);
                nextToken();
                expression();
                expect(SB_RBRACK);
                emit(OPR, 0, 2);
            } else {
                emitLoadObjectAddress(obj);
            }
            expect(SB_RPARENT);
            emit(RDI, 0, 0);

        } else if (Token == KW_WRITE || Token == KW_WRITELN) {
            int withNewline = (Token == KW_WRITELN);
            nextToken();
            expect(SB_LPARENT);
            if (Token != SB_RPARENT) {
                emitWriteAtom();
                while (Token == SB_PLUS) {
                    nextToken();
                    emitWriteAtom();
                }
            }
            expect(SB_RPARENT);
            if (withNewline) {
                emit(WNL, 0, 0);
            }

        } else if (Token == TK_IDENT) {
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
        } else {
            error("CALL: expected procedure name or built-in");
        }

    } else if (Token == KW_RETURN) {
        if (currentProcedure == NULL) {
            error("RETURN is only valid inside a procedure");
        }
        nextToken();
        if (Token == SB_SEMICOLON || Token == KW_END) {
            emit(OPR, 0, 0);
        } else {
            expression();
            emit(RETV, 0, 0);
            currentProcedure->hasReturnValue = 1;
        }

    } else if (Token == KW_BEGIN) {
        // Khối: BEGIN statement { ';' statement } END
        nextToken();
        statement();
        while (Token == SB_SEMICOLON) {
            nextToken();
            statement();
        }
        expect(KW_END);

    } else if (Token == KW_IF) {
        // Rẽ nhánh: IF condition THEN statement [ ELSE statement ]
        nextToken();
        condition();
        int cx1 = cx;
        emit(JPC, 0, 0);
        expect(KW_THEN);
        statement();
        if (Token == KW_ELSE) {
            nextToken();
            int cx2 = cx;
            emit(JMP, 0, 0);
            code[cx1].a = cx;
            statement();
            code[cx2].a = cx;
        } else {
            code[cx1].a = cx;
        }

    } else if (Token == KW_WHILE) {
        // Vòng lặp: WHILE condition DO statement
        int cx1 = cx;
        nextToken();
        condition();
        int cx2 = cx;
        emit(JPC, 0, 0);
        expect(KW_DO);
        statement();
        emit(JMP, 0, cx1);
        code[cx2].a = cx;

    } else if (Token == KW_FOR) {
        // Vòng lặp: FOR IDENT ':=' expression TO expression DO statement
        // Tương đương: i := expr1; while i <= expr2 do begin statement; i := i + 1; end
        nextToken();
        if (Token != TK_IDENT) error("statement: expected identifier after FOR");
        Object* obj = lookup(Id);
        if (obj == NULL || obj->type != OBJ_VARIABLE || obj->isString || obj->size != 1) {
            error("FOR: scalar integer variable required");
        }
        nextToken();
        expect(SB_ASSIGN);
        expression();
        emit(STO, getCurrentLevel() - obj->level, obj->address);
        expect(KW_TO);
        int cx1 = cx;
        emit(LOD, getCurrentLevel() - obj->level, obj->address);
        expression();
        emit(OPR, 0, 12); // <=
        int cx2 = cx;
        emit(JPC, 0, 0);
        expect(KW_DO);
        statement();
        // Increment i
        emit(LOD, getCurrentLevel() - obj->level, obj->address);
        emit(LIT, 0, 1);
        emit(OPR, 0, 2); // +
        emit(STO, getCurrentLevel() - obj->level, obj->address);
        emit(JMP, 0, cx1);
        code[cx2].a = cx;
    }
}

// ─── Phân tích khối ──────────────────────────────────────────────────────────

// block = { CONST-section | VAR-section | PROCEDURE-section }
//         statement
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
        for (int i = 0; i < pendingParamCount; i++) {
            enter(pendingParamName[i], OBJ_PARAMETER, 0, pendingParamSize[i], 0);
            Object *param = lookup(pendingParamName[i]);
            param->address = i - pendingParamCount; // arguments are below base pointer
            param->size = pendingParamSize[i];
            param->isRefParam = pendingParamIsRef[i];
        }
        pendingParamCount = 0;
        pendingParamLevel = -1;
    }

    // Declarations can be mixed in any order before statements.
    while (Token == KW_CONST || Token == KW_VAR || Token == KW_PROCEDURE) {
        if (Token == KW_CONST) {
            nextToken();
            do {
                if (Token != TK_IDENT) error("block: expected identifier in CONST");
                char name[MAX_IDENT_LEN + 1];
                strcpy(name, Id);
                nextToken();
                if (Token != SB_EQU && Token != SB_ASSIGN) {
                    error("block: expected '=' or ':=' in CONST");
                }
                nextToken();
                if (Token == TK_STRING) {
                    enter(name, OBJ_CONSTANT, 0, 0, 0);
                    Object *obj = lookup(name);
                    obj->constIsString = 1;
                    strncpy(obj->constString, StringLiteral, MAX_STRING_LEN);
                    obj->constString[MAX_STRING_LEN] = '\0';
                    nextToken();
                } else {
                    double value = parseConstExpression();
                    enter(name, OBJ_CONSTANT, value, 0, 0);
                }
                if (Token == SB_COMMA) nextToken(); else break;
            } while (1);
            expect(SB_SEMICOLON);
            continue;
        }

        if (Token == KW_VAR) {
            nextToken();
            do {
                if (Token != TK_IDENT) error("block: expected identifier in VAR");
                char varName[MAX_IDENT_LEN + 1];
                strcpy(varName, Id);
                nextToken();
                int size = 1;
                int isRuntimeArray = 0;
                Instruction runtimeSizeExpr[MAX_INIT_EXPR_CODE];
                int runtimeSizeExprCount = 0;
                if (Token == SB_LBRACK) {
                    nextToken();
                    if (Token == TK_NUMBER) {
                        double sizeValue = Num;
                        nextToken();
                        if (Token == SB_RBRACK) {
                            if (sizeValue <= 0 || sizeValue != (double)((int)sizeValue)) {
                                error("block: array size must be a positive integer expression");
                            }
                            size = (int)sizeValue;
                        } else {
                            isRuntimeArray = 1;
                            runtimeSizeExprCount = 0;
                            initEmit(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE, LIT, 0, sizeValue);
                            while (Token != SB_RBRACK) {
                                if (Token == TK_NONE) {
                                    error("block: missing ']' in array declaration");
                                }
                                if (Token == SB_PLUS || Token == SB_MINUS || Token == SB_TIMES || Token == SB_SLASH) {
                                    TokenType op = Token;
                                    nextToken();
                                    parseInitFactor(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE);
                                    initEmit(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE, OPR, 0,
                                             op == SB_PLUS ? 2 : (op == SB_MINUS ? 3 : (op == SB_TIMES ? 4 : 5)));
                                } else {
                                    error("block: invalid runtime array size expression");
                                }
                            }
                        }
                    } else {
                        if (Token == TK_IDENT) {
                            Object *sizeObj = lookup(Id);
                            if (sizeObj != NULL && sizeObj->type == OBJ_CONSTANT && !sizeObj->constIsString) {
                                char constName[MAX_IDENT_LEN + 1];
                                strcpy(constName, Id);
                                nextToken();
                                if (Token == SB_RBRACK) {
                                    double sizeValue = sizeObj->value;
                                    if (sizeValue <= 0 || sizeValue != (double)((int)sizeValue)) {
                                        error("block: array size must be a positive integer expression");
                                    }
                                    size = (int)sizeValue;
                                } else {
                                    isRuntimeArray = 1;
                                    runtimeSizeExprCount = 0;
                                    initEmit(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE, LIT, 0, sizeObj->value);
                                    while (Token != SB_RBRACK) {
                                        if (Token == TK_NONE) {
                                            error("block: missing ']' in array declaration");
                                        }
                                        if (Token == SB_PLUS || Token == SB_MINUS || Token == SB_TIMES || Token == SB_SLASH) {
                                            TokenType op = Token;
                                            nextToken();
                                            parseInitFactor(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE);
                                            initEmit(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE, OPR, 0,
                                                     op == SB_PLUS ? 2 : (op == SB_MINUS ? 3 : (op == SB_TIMES ? 4 : 5)));
                                        } else {
                                            error("block: invalid runtime array size expression");
                                        }
                                    }
                                }
                            } else {
                                isRuntimeArray = 1;
                                parseInitExpression(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE);
                                if (Token != SB_RBRACK) {
                                    error("block: invalid runtime array size expression");
                                }
                            }
                        } else {
                            isRuntimeArray = 1;
                            parseInitExpression(runtimeSizeExpr, &runtimeSizeExprCount, MAX_INIT_EXPR_CODE);
                            if (Token != SB_RBRACK) {
                                error("block: invalid runtime array size expression");
                            }
                        }
                    }
                    expect(SB_RBRACK);
                }
                enter(varName, OBJ_VARIABLE, 0, isRuntimeArray ? 2 : size, 0);
                Object *declObj = lookup(varName);
                if (isRuntimeArray) {
                    declObj->isRuntimeArray = 1;
                    declObj->lengthAddress = declObj->address + 1;
                    if (pendingRuntimeArrayInitCount >= MAX_SYMBOL_TABLE_SIZE) {
                        error("too many runtime array declarations in block");
                    }
                    pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].target = declObj;
                    pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].exprCount = runtimeSizeExprCount;
                    for (int s = 0; s < runtimeSizeExprCount; s++) {
                        pendingRuntimeArrayInit[pendingRuntimeArrayInitCount].exprCode[s] = runtimeSizeExpr[s];
                    }
                    pendingRuntimeArrayInitCount++;
                }

                if (Token == SB_EQU || Token == SB_ASSIGN) {
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
                        if (Token != SB_LBRACK) {
                            error("Array initializer must use bracket list, e.g. VAR A[4] := [1,2]");
                        }
                        pendingInit[pendingInitCount].kind = VAR_INIT_ARRAY_LITERAL;
                        nextToken();
                        if (Token != SB_RBRACK) {
                            while (1) {
                                if (pendingInit[pendingInitCount].arrayCount >= MAX_ARRAY_INIT_VALUES) {
                                    error("array initializer is too long");
                                }
                                pendingInit[pendingInitCount].arrayValues[pendingInit[pendingInitCount].arrayCount++] = parseVarInitializerValue();
                                if (Token == SB_COMMA) {
                                    nextToken();
                                    continue;
                                }
                                break;
                            }
                        }
                        expect(SB_RBRACK);
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

                if (Token == SB_COMMA) nextToken(); else break;
            } while (1);
            expect(SB_SEMICOLON);
            continue;
        }

        // PROCEDURE declaration
        nextToken();
        if (Token != TK_IDENT) error("block: expected identifier after PROCEDURE");
        char procName[MAX_IDENT_LEN + 1];
        strcpy(procName, Id);
        enter(procName, OBJ_PROCEDURE, 0, 0, 0);
        Object* obj = lookup(procName);
        nextToken();

        pendingParamCount = 0;
        if (Token == SB_LPARENT) {
            nextToken();
            if (Token != SB_RPARENT) {
                while (1) {
                    int isRef = 0;
                    if (Token == KW_VAR) {
                        isRef = 1;
                        nextToken();
                    }
                    if (Token != TK_IDENT) {
                        error("procedure parameter: expected identifier");
                    }
                    if (pendingParamCount >= MAX_PROC_PARAMS) {
                        error("too many procedure parameters");
                    }
                    int paramSize = 1;
                    strcpy(pendingParamName[pendingParamCount], Id);
                    nextToken();
                    if (Token == SB_LBRACK) {
                        nextToken();
                        if (Token == SB_RBRACK) {
                            // Unsized formal array parameter: VAR ARR[]
                            paramSize = 2;
                            nextToken();
                        } else {
                            double sizeValue = parseConstExpression();
                            if (sizeValue <= 0 || sizeValue != (double)((int)sizeValue)) {
                                error("procedure parameter array size must be a positive integer expression");
                            }
                            paramSize = (int)sizeValue;
                            expect(SB_RBRACK);
                        }
                        isRef = 1;
                    }
                    pendingParamIsRef[pendingParamCount] = isRef;
                    pendingParamSize[pendingParamCount] = paramSize;
                    obj->paramIsRef[pendingParamCount] = isRef;
                    obj->paramSize[pendingParamCount] = paramSize;
                    pendingParamCount++;

                    if (Token == SB_COMMA || Token == SB_SEMICOLON) {
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

    for (int i = 0; i < pendingRuntimeArrayInitCount; i++) {
        Object *arrObj = pendingRuntimeArrayInit[i].target;
        int l = getCurrentLevel() - arrObj->level;
        for (int k = 0; k < pendingRuntimeArrayInit[i].exprCount; k++) {
            emit(pendingRuntimeArrayInit[i].exprCode[k].op,
                 pendingRuntimeArrayInit[i].exprCode[k].l,
                 pendingRuntimeArrayInit[i].exprCode[k].a);
        }
        emit(DUP, 0, 0);
        emit(STO, l, arrObj->lengthAddress);
        emit(ALC, 0, 0);
        emit(STO, l, arrObj->address);
    }

    for (int i = 0; i < pendingInitCount; i++) {
        if (pendingInit[i].kind == VAR_INIT_SCALAR_EXPR) {
            for (int k = 0; k < pendingInit[i].exprCount; k++) {
                emit(pendingInit[i].exprCode[k].op,
                     pendingInit[i].exprCode[k].l,
                     pendingInit[i].exprCode[k].a);
            }
            emitStoreObjectValue(pendingInit[i].target);
        } else {
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

// ─── Phân tích chương trình ──────────────────────────────────────────────────

// program = PROGRAM ident ';' block '.'
void program(void) {
    initSymbolTable();
    cx = 0;
    expect(KW_PROGRAM);
    if (Token != TK_IDENT) error("program: expected program name");
    nextToken();
    expect(SB_SEMICOLON);
    block();
    if (Token == SB_PERIOD) {
        nextToken();
    }
    if (Token != TK_NONE) {
        error("program: unexpected token after '.'");
    }
    // The current optimizer does not retarget jumps after folding,
    // which can corrupt loop/branch control flow. Keep execution unoptimized.
    // optimizeCode();
    // listCode();
    interpret();
}

static int tokenStartsStringValueExpr(void) {
    if (Token == TK_STRING) {
        return 1;
    }
    if (Token == TK_IDENT) {
        Object *obj = lookup(Id);
        return (obj != NULL && ((obj->type == OBJ_CONSTANT && obj->constIsString) || obj->isString));
    }
    return 0;
}

static void emitAppendStringTerm(const Object *target) {
    if (Token == TK_STRING) {
        emitLoadObjectAddress(target);
        emit(CATL, 0, addStringLiteral(StringLiteral));
        nextToken();
        return;
    }

    if (Token == TK_NUMBER) {
        emitLoadObjectAddress(target);
        emit(LIT, 0, Num);
        emit(CATI, 0, 0);
        nextToken();
        return;
    }

    if (Token == SB_LPARENT) {
        emitLoadObjectAddress(target);
        nextToken();
        expression();
        expect(SB_RPARENT);
        emit(CATI, 0, 0);
        return;
    }

    if (Token != TK_IDENT) {
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
        if (Token == SB_LBRACK) {
            error("string expression: STRING variable does not take index");
        }
        emitLoadObjectAddress(target);
        emitLoadObjectAddress(obj);
        emit(CATV, 0, 0);
        return;
    }

    emitLoadObjectAddress(target);
    if (Token == SB_LBRACK) {
        if (obj->size <= 1) {
            error("string expression: indexed access requires array variable");
        }
        emitLoadObjectAddress(obj);
        nextToken();
        expression();
        expect(SB_RBRACK);
        emit(OPR, 0, 2);
        emit(LDI, 0, 0);
        emit(CATI, 0, 0);
    } else {
        if (obj->size > 1) {
            error("string expression: array variable requires an index");
        }
        emitLoadObjectValue(obj);
        emit(CATI, 0, 0);
    }
}
