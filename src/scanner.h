#ifndef SCANNER_H
#define SCANNER_H

#define MAX_NUMBER_LEN  32
#define MAX_IDENT_LEN   10
#define KEYWORDS_COUNT  28
#define MAX_STRING_LEN  255

typedef enum {
	TK_NONE=0, TK_IDENT, TK_NUMBER, TK_STRING,
	KW_BEGIN, KW_CALL, KW_CONST, KW_DO, KW_ELSE, KW_END,
	KW_FOR, KW_IF, KW_ODD, KW_PROCEDURE, KW_PROGRAM,
	KW_READ, KW_RETURN, KW_SIZEOF, KW_STATIC, KW_THEN, KW_TO, KW_VAR, KW_WHILE, KW_WRITE, KW_WRITELN,
	KW_DOWNTO, KW_STEP,
	KW_AND, KW_OR, KW_NOT, KW_NULL,
	SB_PLUS, SB_MINUS, SB_TIMES, SB_SLASH, SB_FLOORDIV, SB_EQU, SB_NEQ,
	SB_LSS, SB_LEQ, SB_GTR, SB_GEQ, SB_PERCENT,
	SB_BITAND, SB_BITOR, SB_BITXOR, SB_BITNOT, SB_SHL, SB_SHR, SB_INC,
	SB_LPARENT, SB_RPARENT, SB_LBRACK, SB_RBRACK,
	SB_PERIOD, SB_COMMA, SB_SEMICOLON, SB_ASSIGN,
	SB_ADD_ASSIGN, SB_SUB_ASSIGN, SB_MUL_ASSIGN, SB_DIV_ASSIGN, SB_MOD_ASSIGN
} TokenType;

typedef struct {
	char string[MAX_IDENT_LEN + 1];
	TokenType Token;
} Keyword;

extern const Keyword keywords[KEYWORDS_COUNT];

extern const char TabToken[][12];

extern double Num;
extern int TokenLine;
extern int TokenColumn;
extern char currentSourceFile[260];

extern char StringLiteral[MAX_STRING_LEN + 1];


TokenType getToken();

int compileSource(const char *filename);
void compile(char *filename);

#endif