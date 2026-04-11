#ifndef SCANNER_H
#define SCANNER_H

#define MAX_NUMBER_LEN  6
#define MAX_IDENT_LEN   10
#define KEYWORDS_COUNT  19
#define MAX_STRING_LEN  255

typedef enum {
	TK_NONE=0, TK_IDENT, TK_NUMBER, TK_STRING,
	KW_BEGIN, KW_CALL, KW_CONST, KW_DO, KW_ELSE, KW_END,
	KW_FOR, KW_IF, KW_ODD, KW_PROCEDURE, KW_PROGRAM,
	KW_READ, KW_THEN, KW_TO, KW_VAR, KW_WHILE, KW_WRITE, KW_WRITELN,
	SB_PLUS, SB_MINUS, SB_TIMES, SB_SLASH, SB_EQU, SB_NEQ,
	SB_LSS, SB_LEQ, SB_GTR, SB_GEQ, SB_PERCENT,
	SB_LPARENT, SB_RPARENT, SB_LBRACK, SB_RBRACK,
	SB_PERIOD, SB_COMMA, SB_SEMICOLON, SB_ASSIGN
} TokenType;

typedef struct {
	char string[MAX_IDENT_LEN + 1];
	TokenType Token;
} Keyword;

extern const Keyword keywords[KEYWORDS_COUNT];

extern const char TabToken[][12];

extern char StringLiteral[MAX_STRING_LEN + 1];


TokenType getToken();

void compile(char *filename);

#endif