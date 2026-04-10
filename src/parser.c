#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scanner.h"
#include "parser.h"
#include "semantics.h"
#include "codegen.h"

// Các biến toàn cục từ scanner.c
extern TokenType Token;
extern int       Num;
extern char      Id[MAX_IDENT_LEN + 1];

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
        } else {
            emit(LOD, getCurrentLevel() - obj->level, obj->address);
        }
        nextToken();
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
        if (obj->type != OBJ_VARIABLE) {
            error("Cannot assign to non-variable");
        }
        nextToken();
        expect(SB_ASSIGN);
        expression();
        emit(STO, getCurrentLevel() - obj->level, obj->address);
        emit(LOD, getCurrentLevel() - obj->level, obj->address);
        emit(OPR, 0, 13); // Print value after assignment

    } else if (Token == KW_CALL) {
        // Gọi thủ tục: CALL IDENT
        nextToken();
        if (Token != TK_IDENT) error("statement: expected identifier after CALL");
        Object* obj = lookup(Id);
        if (obj == NULL) {
            char msg[100];
            sprintf(msg, "Undeclared identifier: %s", Id);
            error(msg);
        }
        if (obj->type != OBJ_PROCEDURE) {
            error("Cannot CALL a non-procedure");
        }
        emit(CAL, getCurrentLevel() - obj->level, obj->address);
        nextToken();

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
        if (obj == NULL || obj->type != OBJ_VARIABLE) error("FOR: variable required");
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
        emit(LOD, getCurrentLevel() - obj->level, obj->address);
        emit(OPR, 0, 13); // Print loop variable after increment
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
            enter(name, OBJ_CONSTANT, Num);
            nextToken();
            if (Token == SB_COMMA) nextToken(); else break;
        } while (1);
        expect(SB_SEMICOLON);
    }

    // Khai báo biến: VAR ident { , ident } ;
    if (Token == KW_VAR) {
        nextToken();
        do {
            if (Token != TK_IDENT) error("block: expected identifier in VAR");
            enter(Id, OBJ_VARIABLE, 0);
            nextToken();
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
        enter(procName, OBJ_PROCEDURE, 0);
        Object* obj = lookup(procName);
        nextToken();
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
    // expect(SB_PERIOD);
    if (Token != TK_NONE) {
        error("program: unexpected token after '.'");
    }
    // listCode();
    optimizeCode();
    // listCode();
    interpret();
}