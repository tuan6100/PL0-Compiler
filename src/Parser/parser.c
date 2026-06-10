#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "../Scanner/scanner.h"
#include "../Semantic/semantic.h"

void parse(TokenType token) {
    initSymbolTable();
    Token = token;
    program();
}

void error(const char msg[]) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void factor() {
    if (Token == TK_IDENT) {
        checkDeclared(Id);
        Token = getToken();
        if (Token == SB_LBRACK) {
            Token = getToken();
            expression();
            if (Token == SB_RBRACK)
                Token = getToken();
            else
                error("Thiếu dấu ]");
        }
    } else if (Token == TK_NUMBER)
        Token = getToken();
    else if (Token == SB_LPARENT) {
        Token = getToken();
        expression();
        if (Token == SB_RPARENT)
            Token = getToken();
        else
            error("Thiếu dấu )");
    } else {
        error("factor: syntax error");
    }
}

void term() {
    factor();
    while (Token == SB_TIMES || Token == SB_SLASH || Token == SB_PERCENT) {
        Token = getToken();
        factor();
    }
}

void expression() {
    if (Token == SB_MINUS)
        Token = getToken();
    term();
    while (Token == SB_PLUS || Token == SB_MINUS) {
        Token = getToken();
        term();
    }
}

void condition() {
    if (Token == KW_ODD) {
        Token = getToken();
        expression();
    } else {
        expression();
        if (Token == SB_EQU || Token == SB_NEQ || Token == SB_LSS ||
            Token == SB_LEQ || Token == SB_GTR || Token == SB_GEQ) {
            Token = getToken();
            expression();
            } else {
                error("Thiếu toán tử so sánh");
            }
    }
}

void statement() {
    if (Token == TK_IDENT) {
        checkIsVar(Id);
        Token = getToken();
        if (Token == SB_LBRACK) {
            Token = getToken();
            expression();
            if (Token == SB_RBRACK)
                Token = getToken();
            else
                error("Thiếu dấu ]");
        }
        if (Token == SB_ASSIGN) {
            Token = getToken();
            expression();
        } else
            error("Thiếu toán tử gán");
    } else if (Token == KW_CALL) {
        Token = getToken();
        if (Token == TK_IDENT) {
            checkIsProcedure(Id);
            Token = getToken();
            if (Token == SB_LPARENT) {
                Token = getToken();
                expression();
                while (Token == SB_COMMA) {
                    Token = getToken();
                    expression();
                }
                if (Token == SB_RPARENT)
                    Token = getToken();
                else
                    error("Thiếu dấu )");
            }
        } else
            error("Thiếu tên thủ tục/hàm");
    } else if (Token == KW_BEGIN) {
        Token = getToken();
        statement();
        while (Token == SB_SEMICOLON) {
            Token = getToken();
            statement();
        }
        if (Token == KW_END)
            Token = getToken();
        else
            error("Thiếu từ khóa END");
    } else if (Token == KW_IF) {
        Token = getToken();
        condition();
        if (Token == KW_THEN) {
            Token = getToken();
            statement();
            if (Token == SB_SEMICOLON)
                Token = getToken();
            if (Token == KW_ELSE) {
                Token = getToken();
                statement();
            }
        } else
            error("Thiếu từ khóa THEN");
    } else if (Token == KW_WHILE) {
        Token = getToken();
        condition();
        if (Token == KW_DO) {
            Token = getToken();
            statement();
        } else
            error("Thiếu từ khóa DO");
    } else if (Token == KW_FOR) {
        Token = getToken();
        if (Token == TK_IDENT) {
            Token = getToken();
            if (Token == SB_ASSIGN) {
                Token = getToken();
                expression();
                if (Token == KW_TO) {
                    Token = getToken();
                    expression();
                    if (Token == KW_DO) {
                        Token = getToken();
                        statement();
                    } else
                        error("Thiếu từ khóa DO");
                } else
                    error("Thiếu từ khóa TO");
            } else
                error("Thiếu toán tử gán");
        } else
            error("Thiếu tên biến lặp");
    }
}

void block(void) {
    if (Token == KW_CONST) {
        Token = getToken();
        if (Token == TK_IDENT) {
            char constName[11];
            strncpy(constName, Id, 10); constName[10] = '\0';
            Token = getToken();
            if (Token == SB_EQU) {
                Token = getToken();
                if (Token == TK_NUMBER) {
                    addSymbol(constName, SYM_CONST, Num);
                    Token = getToken();
                } else
                    error("Thiếu giá trị hằng số");
            } else
                error("Thiếu dấu =");
        } else
            error("Thiếu tên hằng số");
        while (Token == SB_COMMA) {
            Token = getToken();
            if (Token == TK_IDENT) {
                char constName2[11];
                strncpy(constName2, Id, 10); constName2[10] = '\0';
                Token = getToken();
                if (Token == SB_EQU) {
                    Token = getToken();
                    if (Token == TK_NUMBER) {
                        addSymbol(constName2, SYM_CONST, Num);
                        Token = getToken();
                    } else
                        error("Thiếu giá trị hằng số");
                } else
                    error("Thiếu dấu =");
            } else
                error("Thiếu tên hằng số");
        }
        if (Token == SB_SEMICOLON)
            Token = getToken();
        else
            error("Thiếu dấu ;");
    }

    if (Token == KW_VAR) {
        Token = getToken();
        if (Token == TK_IDENT) {
            addSymbol(Id, SYM_VAR, 0);
            Token = getToken();
            if (Token == SB_LBRACK) {
                Token = getToken();
                if (Token == TK_NUMBER) {
                    Token = getToken();
                    if (Token == SB_RBRACK)
                        Token = getToken();
                    else
                        error("Thiếu dấu ]");
                } else
                    error("Thiếu kích thước mảng");
            }
        } else
            error("Thiếu tên biến");
        while (Token == SB_COMMA) {
            Token = getToken();
            if (Token == TK_IDENT) {
                addSymbol(Id, SYM_VAR, 0);
                Token = getToken();
                if (Token == SB_LBRACK) {
                    Token = getToken();
                    if (Token == TK_NUMBER) {
                        Token = getToken();
                        if (Token == SB_RBRACK)
                            Token = getToken();
                        else
                            error("Thiếu dấu ]");
                    } else
                        error("Thiếu kích thước mảng");
                }
            } else
                error("Thiếu tên biến");
        }
        if (Token == SB_SEMICOLON)
            Token = getToken();
        else
            error("Thiếu dấu ;");
    }

    if (Token == KW_PROCEDURE) {
        Token = getToken();
        if (Token == TK_IDENT) {
            addSymbol(Id, SYM_PROCEDURE, 0);
            Token = getToken();
            enterScope();
            if (Token == SB_LPARENT) {
                Token = getToken();
                if (Token == KW_VAR)
                    Token = getToken();
                if (Token == TK_IDENT) {
                    addSymbol(Id, SYM_VAR, 0);
                    Token = getToken();
                }
                while (Token == SB_SEMICOLON) {
                    Token = getToken();
                    if (Token == KW_VAR)
                        Token = getToken();
                    if (Token == TK_IDENT) {
                        addSymbol(Id, SYM_VAR, 0);
                        Token = getToken();
                    }
                }
                if (Token == SB_RPARENT)
                    Token = getToken();
                else
                    error("Thiếu dấu )");
            }
            if (Token == SB_SEMICOLON) {
                Token = getToken();
                block();
                if (Token == SB_SEMICOLON)
                    Token = getToken();
                else
                    error("Thiếu dấu ;");
            } else
                error("Thiếu dấu ;");
            leaveScope();
        }
    }

    if (Token == KW_BEGIN) {
        Token = getToken();
        statement();
        while (Token == SB_SEMICOLON) {
            Token = getToken();
            statement();
        }
        if (Token == KW_END)
            Token = getToken();
        else
            error("Thiếu từ khóa END");
    }
}

void program() {
    if (Token == KW_PROGRAM){
        Token = getToken();
        if (Token == TK_IDENT){
            Token = getToken();
            if(Token == SB_SEMICOLON){
                Token = getToken();
                block();
                if(Token == SB_PERIOD)
                    printf("Thành công");
                else
                    error("Thiếu dấu .");
            } else
                error("Thiếu dấu chấm phẩy");
        } else
            error("Thiếu tên chương trình");
    } else
        error("Thiếu từ khóa Program");
}