#ifndef PL0_SCANNER_H
#define PL0_SCANNER_H
#define	MAX_NUMBER_LEN   6
#define MAX_IDENT_LEN   10
#define KEYWORDS_COUNT  15

typedef enum {	TK_NONE=0, TK_IDENT, TK_NUMBER,
		KW_BEGIN, KW_CALL, KW_CONST, KW_DO,  KW_ELSE, KW_END, KW_FOR, KW_IF, KW_ODD,
		KW_PROCEDURE, KW_PROGRAM, KW_THEN, KW_TO, KW_VAR, KW_WHILE,
		
		SB_PLUS, SB_MINUS, SB_TIMES, SB_SLASH, SB_EQU, SB_NEQ, SB_LSS, SB_LEQ,SB_GTR, SB_GEQ,
		SB_PERCENT, SB_LPARENT, SB_RPARENT, SB_LBRACK, SB_RBRACK, SB_PERIOD, SB_COMMA, 
		SB_SEMICOLON, SB_ASSIGN
} TokenType;

typedef struct {
	char string[MAX_IDENT_LEN + 1];
	TokenType Token;
} Keyword;

extern Keyword Keywords[KEYWORDS_COUNT];

extern char TabToken[][10];

TokenType getToken();

#endif
