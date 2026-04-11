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
extern int       Num;
extern char      Id[MAX_IDENT_LEN + 1];
extern char      StringLiteral[MAX_STRING_LEN + 1];

static int pendingParamCount = 0;
static int pendingParamIsRef[MAX_PROC_PARAMS];
static int pendingParamSize[MAX_PROC_PARAMS];
static char pendingParamName[MAX_PROC_PARAMS][MAX_IDENT_LEN + 1];
static int pendingParamLevel = -1;
static Object *currentProcedure = NULL;

static const char *interpExprPtr;

static int tokenStartsStringValueExpr(void);
static void emitAppendStringTerm(const Object *target);
static void emitSizeOfObjectValue(const Object *obj);
static void parseProcedureCallArguments(const Object *proc);

// ─── Tiện ích ────────────────────────────────────────────────────────────────

void nextToken(void) {
    Token = getToken();
    // printf(" %s", TabToken[Token]);
    // if(Token == TK_IDENT) printf("(%s) \n", Id);
    // else if(Token == TK_NUMBER) printf("(%d) \n", Num);
    // else printf("\n");
}

void error(const char msg[]) {
    printf("Error: %s\n", msg);
    exit(1);
}

// Kiểm tra token hiện tại có khớp không, nếu có thì đọc token tiếp
static void expect(TokenType expected) {
    if (Token != expected) {
        printf("Error: expected token %d but got %d\n", expected, Token);
        exit(1);
    }
    nextToken();
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
        } else if (obj->size > 1) {
            // Logical array length convention: first cell stores length.
            emitLoadObjectAddress(obj);
            emit(LDI, 0, 0);
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
                        error("VAR parameter requires an identifier argument");
                    }
                    Object *arg = lookup(Id);
                    if (arg == NULL || !isAssignableObject(arg) || arg->isString) {
                        error("VAR parameter requires variable argument");
                    }
                    if (proc->paramSize[argCount] > 1) {
                        if (arg->size <= 1) {
                            error("Array parameter requires array argument");
                        }
                    } else {
                        if (arg->size != 1) {
                            error("Scalar VAR parameter requires scalar argument");
                        }
                    }
                    if (arg->isRefParam) {
                        emit(LOD, getCurrentLevel() - arg->level, arg->address);
                    } else {
                        emit(LDA, getCurrentLevel() - arg->level, arg->address);
                    }
                    nextToken();
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
        int value = 0;
        while (isdigit((unsigned char)*interpExprPtr)) {
            value = value * 10 + (*interpExprPtr - '0');
            interpExprPtr++;
        }
        emit(LIT, 0, value);
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
        if (*interpExprPtr == '[') {
            if (obj->type == OBJ_PROCEDURE) {
                error("interpolation: procedure has no printable value");
            }
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

        if (obj->type == OBJ_PROCEDURE) {
            error("interpolation: procedure has no printable value");
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
            error("Cannot print procedure directly");
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
    } else if (Token == TK_IDENT) {
        if (strcmp(Id, "SIZEOF") == 0) {
            nextToken();
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
            expect(SB_RPARENT);
            emitSizeOfObjectValue(obj);
            return;
        }

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

// term = factor { ('*' | '/' | '%') factor }
void term(void) {
    factor();
    while (Token == SB_TIMES || Token == SB_SLASH || Token == SB_PERCENT) {
        TokenType op = Token;
        nextToken();
        factor();
        if (op == SB_TIMES) emit(OPR, 0, 4);
        else if (op == SB_SLASH) emit(OPR, 0, 5);
        else if (op == SB_PERCENT) {
            // Placeholder for modulo if not supported by P-Code OPR
            // For now let's say OPR 5 is division, we might need a dedicated one for modulo
            // Or just error if not supported.
        }
    }
}

// expression = ['+' | '-'] term { ('+' | '-') term }
void expression(void) {
    TokenType prefixOp = TK_NONE;
    if (Token == SB_PLUS || Token == SB_MINUS) {
        prefixOp = Token;
        nextToken();                      // dấu đơn ngôi
    }
    term();
    if (prefixOp == SB_MINUS) emit(OPR, 0, 1);
    while (Token == SB_PLUS || Token == SB_MINUS) {
        TokenType op = Token;
        nextToken();
        term();
        if (op == SB_PLUS) emit(OPR, 0, 2);
        else emit(OPR, 0, 3);
    }
}

void condition(void) {
    if (Token == KW_ODD) {
        nextToken();
        expression();
        emit(OPR, 0, 6);
    } else {
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
        } else {
            error("condition: expected relational operator");
        }
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
    if (Token == TK_IDENT) {
        // Gán: IDENT ':=' expression
        Object* obj = lookup(Id);
        if (obj == NULL) {
            char msg[100];
            sprintf(msg, "Undeclared identifier: %s", Id);
            error(msg);
        }
        if (!isAssignableObject(obj)) {
            error("Cannot assign to non-variable");
        }
        nextToken();

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

// block = [ CONST ident '=' number { ',' ident '=' number } ';' ]
//         [ VAR ident { ',' ident } ';' ]
//         { PROCEDURE ident ';' block ';' }
//         statement
void block(void) {
    enterBlock();
    int tx0 = cx;
    emit(JMP, 0, 0);

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

    // Khai báo hằng: CONST ident = (number|string) { , ident = (number|string) } ;
    if (Token == KW_CONST) {
        nextToken();
        do {
            if (Token != TK_IDENT) error("block: expected identifier in CONST");
            char name[MAX_IDENT_LEN + 1];
            strcpy(name, Id);
            nextToken();
            expect(SB_EQU);
            if (Token == TK_NUMBER) {
                enter(name, OBJ_CONSTANT, Num, 0, 0);
                nextToken();
            } else if (Token == TK_STRING) {
                enter(name, OBJ_CONSTANT, 0, 0, 0);
                Object *obj = lookup(name);
                obj->constIsString = 1;
                strncpy(obj->constString, StringLiteral, MAX_STRING_LEN);
                obj->constString[MAX_STRING_LEN] = '\0';
                nextToken();
            } else {
                error("block: expected number or string in CONST");
            }
            if (Token == SB_COMMA) nextToken(); else break;
        } while (1);
        expect(SB_SEMICOLON);
    }

    // Khai báo biến: VAR ident [ '[' number ']' ] { , ident [ '[' number ']' ] } ;
    if (Token == KW_VAR) {
        nextToken();
        do {
            if (Token != TK_IDENT) error("block: expected identifier in VAR");
            char varName[MAX_IDENT_LEN + 1];
            strcpy(varName, Id);
            nextToken();
            int size = 1;
            if (Token == SB_LBRACK) {
                nextToken();
                if (Token != TK_NUMBER || Num <= 0) {
                    error("block: array size must be a positive number");
                }
                size = Num;
                nextToken();
                expect(SB_RBRACK);
            }
            enter(varName, OBJ_VARIABLE, 0, size, 0);
            if (Token == SB_COMMA) nextToken(); else break;
        } while (1);
        expect(SB_SEMICOLON);
    }

    // Khai báo thủ tục: PROCEDURE ident ; block ;
    while (Token == KW_PROCEDURE) {
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
                        if (Token != TK_NUMBER || Num <= 0) {
                            error("procedure parameter array size must be a positive number");
                        }
                        paramSize = Num;
                        nextToken();
                        expect(SB_RBRACK);
                        isRef = 1; // Arrays are passed by reference.
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
        Object *savedProcedure = currentProcedure;
        currentProcedure = obj;
        block();
        currentProcedure = savedProcedure;
        obj->address = procEntry;
        expect(SB_SEMICOLON);
    }

    code[tx0].a = cx;
    emit(INT, 0, getVarCount() + 3);
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
    // listCode();
    optimizeCode();
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
