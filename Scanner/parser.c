#include <stdio.h>
#include <stdlib.h>
#include "scanner.h"
#include "parser.h"

// Các biến toàn cục từ scanner.c
extern TokenType Token;
extern int       Num;
extern char      Id[MAX_IDENT_LEN + 1];

// ─── Tiện ích ────────────────────────────────────────────────────────────────

void nextToken(void) {
    Token = getToken();
    printf(" %s", TabToken[Token]);
    if(Token == TK_IDENT) printf("(%s) \n", Id);
    else if(Token == TK_NUMBER) printf("(%d) \n", Num);
    else printf("\n");
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
        nextToken();
    } else if (Token == TK_IDENT) {
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
        nextToken();
        factor();
    }
}

// expression = ['+' | '-'] term { ('+' | '-') term }
void expression(void) {
    if (Token == SB_PLUS || Token == SB_MINUS) {
        nextToken();                      // dấu đơn ngôi
    }
    term();
    while (Token == SB_PLUS || Token == SB_MINUS) {
        nextToken();
        term();
    }
}

void condition(void) {
    if (Token == KW_ODD) {
        nextToken();
        expression();
    } else {
        expression();
        if (Token == SB_EQU || Token == SB_NEQ ||
            Token == SB_LSS || Token == SB_LEQ ||
            Token == SB_GTR || Token == SB_GEQ) {
            nextToken();
            expression();
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
        nextToken();
        expect(SB_ASSIGN);
        expression();

    } else if (Token == KW_CALL) {
        // Gọi thủ tục: CALL IDENT
        nextToken();
        if (Token != TK_IDENT) error("statement: expected identifier after CALL");
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
        expect(KW_THEN);
        statement();
        if (Token == KW_ELSE) {
            nextToken();
            statement();
        }

    } else if (Token == KW_WHILE) {
        // Vòng lặp: WHILE condition DO statement
        nextToken();
        condition();
        expect(KW_DO);
        statement();

    } else if (Token == KW_FOR) {
        // Vòng lặp: FOR IDENT ':=' expression TO expression DO statement
        nextToken();
        if (Token != TK_IDENT) error("statement: expected identifier after FOR");
        nextToken();
        expect(SB_ASSIGN);
        expression();
        expect(KW_TO);
        expression();
        expect(KW_DO);
        statement();

    }
    // Câu lệnh rỗng: không làm gì
}

// ─── Phân tích khối ──────────────────────────────────────────────────────────

// block = [ CONST ident '=' number { ',' ident '=' number } ';' ]
//         [ VAR ident { ',' ident } ';' ]
//         { PROCEDURE ident ';' block ';' }
//         statement
void block(void) {
    // Khai báo hằng: CONST ident = number { , ident = number } ;
    if (Token == KW_CONST) {
        nextToken();
        do {
            if (Token != TK_IDENT) error("block: expected identifier in CONST");
            nextToken();
            expect(SB_EQU);
            if (Token != TK_NUMBER) error("block: expected number in CONST");
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
            nextToken();
            if (Token == SB_COMMA) nextToken(); else break;
        } while (1);
        expect(SB_SEMICOLON);
    }

    // Khai báo thủ tục: PROCEDURE ident ; block ;
    while (Token == KW_PROCEDURE) {
        nextToken();
        if (Token != TK_IDENT) error("block: expected identifier after PROCEDURE");
        nextToken();
        expect(SB_SEMICOLON);
        block();
        expect(SB_SEMICOLON);
    }

    statement();
}

// ─── Phân tích chương trình ──────────────────────────────────────────────────

// program = PROGRAM ident ';' block '.'
void program(void) {
    expect(KW_PROGRAM);
    if (Token != TK_IDENT) error("program: expected program name");
    nextToken();
    expect(SB_SEMICOLON);
    block();
    expect(SB_PERIOD);

    if (Token != TK_NONE) {
        error("program: unexpected token after '.'");
    }
    printf("Parsing completed successfully.\n");
}