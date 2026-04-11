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
static char pendingParamName[MAX_PROC_PARAMS][MAX_IDENT_LEN + 1];
static int pendingParamLevel = -1;

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

// ─── Phân tích biểu thức ─────────────────────────────────────────────────────

// factor = NUMBER | IDENT | '(' expression ')'
void factor(void) {
    if (Token == TK_NUMBER) {
        emit(LIT, 0, Num);
        nextToken();
    } else if (Token == TK_IDENT) {
        Object* obj = lookup(Id);
        if (obj == NULL) {
            char msg[100];
            sprintf(msg, "Undeclared identifier: %s", Id);
            error(msg);
        }
        if (obj->type == OBJ_PROCEDURE) {
            error("Cannot use procedure in expression");
        }
        if (obj->type == OBJ_CONSTANT) {
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
                if (obj->isRefParam) {
                    error("Indexed access on VAR parameter is not supported");
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
            error("Placeholder cannot reference procedure");
        }
        if (obj->type == OBJ_CONSTANT) {
            emit(LIT, 0, obj->value);
            emit(WRI, 0, 0);
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
            if (obj->isRefParam) {
                error("Indexed assignment on VAR parameter is not supported");
            }
            emitLoadObjectAddress(obj);
            nextToken();
            expression();
            expect(SB_RBRACK);
            emit(OPR, 0, 2);
            isIndexed = 1;
        }

        expect(SB_ASSIGN);

        if (obj->isString && !isIndexed) {
            if (Token != TK_STRING) {
                error("STRING assignment requires a string literal");
            }
            emitLoadObjectAddress(obj);
            emit(STS, 0, addStringLiteral(StringLiteral));
            nextToken();
        } else {
            if (Token == TK_STRING && !isIndexed) {
                // Strings now use VAR declarations; require multi-cell storage (e.g. VAR s[32]).
                if (obj->size <= 1) {
                    error("String assignment requires variable declared with size, e.g. VAR S[32]");
                }
                obj->isString = 1;
                emitLoadObjectAddress(obj);
                emit(STS, 0, addStringLiteral(StringLiteral));
                nextToken();
                return;
            }
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
                if (obj->isRefParam) {
                    error("CALL READ with indexed VAR parameter is not supported");
                }
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
                if (Token == TK_STRING) {
                    emitInterpolatedString(StringLiteral);
                    nextToken();
                } else if (Token == TK_IDENT) {
                    Object *obj = lookup(Id);
                    if (obj == NULL) {
                        char msg[100];
                        sprintf(msg, "Undeclared identifier: %s", Id);
                        error(msg);
                    }
                    if (isAssignableObject(obj) && obj->isString) {
                        nextToken();
                        if (Token == SB_LBRACK) {
                            error("CALL WRITE/CALL WRITELN of STRING variable does not take index");
                        }
                        emit(WRS, getCurrentLevel() - obj->level, obj->address);
                    } else {
                        expression();
                        emit(WRI, 0, 0);
                    }
                } else {
                    expression();
                    emit(WRI, 0, 0);
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
                            if (arg == NULL || !isAssignableObject(arg) || arg->size != 1 || arg->isString) {
                                error("VAR parameter requires scalar integer variable");
                            }
                            if (arg->isRefParam) {
                                emit(LOD, getCurrentLevel() - arg->level, arg->address);
                            } else {
                                emit(LDA, getCurrentLevel() - arg->level, arg->address);
                            }
                            nextToken();
                        } else {
                            expression();
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
            emit(CAL, getCurrentLevel() - proc->level, proc->address);
        } else {
            error("CALL: expected procedure name or built-in");
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
            enter(pendingParamName[i], OBJ_PARAMETER, 0, 1, 0);
            Object *param = lookup(pendingParamName[i]);
            param->address = i - pendingParamCount; // arguments are below base pointer
            param->size = 1;
            param->isRefParam = pendingParamIsRef[i];
        }
        pendingParamCount = 0;
        pendingParamLevel = -1;
    }

    // Khai báo hằng: CONST ident = number { , ident = number } ;
    if (Token == KW_CONST) {
        nextToken();
        do {
            if (Token != TK_IDENT) error("block: expected identifier in CONST");
            char name[MAX_IDENT_LEN + 1];
            strcpy(name, Id);
            nextToken();
            expect(SB_EQU);
            if (Token != TK_NUMBER) error("block: expected number in CONST");
            enter(name, OBJ_CONSTANT, Num, 0, 0);
            nextToken();
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
                    while (1) {
                        if (pendingParamCount >= MAX_PROC_PARAMS) {
                            error("too many procedure parameters");
                        }
                        strcpy(pendingParamName[pendingParamCount], Id);
                        pendingParamIsRef[pendingParamCount] = isRef;
                        obj->paramIsRef[pendingParamCount] = isRef;
                        pendingParamCount++;
                        nextToken();
                        if (Token == SB_COMMA) {
                            nextToken();
                            continue;
                        }
                        break;
                    }
                    if (Token == SB_SEMICOLON) {
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
        block();
        obj->address = tx0 + 1; // Start of block code
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