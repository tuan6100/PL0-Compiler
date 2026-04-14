#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "scanner.h"

#include "parser.h"

const Keyword keywords[KEYWORDS_COUNT] = {
	{"AND", KW_AND},
	{"BEGIN", KW_BEGIN},
	{"CALL", KW_CALL},
	{"CONST", KW_CONST},
	{"DO", KW_DO},
		  {"DOWNTO", KW_DOWNTO},
  {"ELSE", KW_ELSE},
	{"END", KW_END},
  {"FOR", KW_FOR},
	 {"IF", KW_IF},
  {"NOT", KW_NOT},
  {"NULL", KW_NULL},
   {"ODD",KW_ODD},
  {"OR", KW_OR},
	{"PROCEDURE", KW_PROCEDURE},
  {"PROGRAM", KW_PROGRAM},
  {"READ", KW_READ},
  {"READLN", KW_READ},
  {"RETURN", KW_RETURN},
  {"SIZEOF", KW_SIZEOF},
  {"STEP", KW_STEP},
  {"THEN", KW_THEN},
  {"TO", KW_TO},
  {"VAR", KW_VAR},
	{"WHILE", KW_WHILE},
	{"WRITE", KW_WRITE},
	{"WRITELN", KW_WRITELN}
};

const char TabToken[][12] = {	"NONE", "IDENT", "NUMBER", "STRING",
		"BEGIN", "CALL", "CONST", "DO",  "ELSE", "END", "FOR", "IF",
		"ODD", "PROCEDURE", "PROGRAM", "READ", "RETURN", "SIZEOF", "THEN", "TO", "VAR", "WHILE", "WRITE", "WRITELN",
		"DOWNTO", "STEP",
		"AND", "OR", "NOT", "NULL",

		"PLUS", "MINUS", "TIMES", "SLASH", "EQU", "NEQ", "LSS",
		"LEQ", "GTR", "GEQ", "PERCENT", "BITAND", "BITOR", "BITXOR", "BITNOT", "SHL", "SHR", "INC", "LPARENT", "RPARENT",
		"LBRACK", "RBRACK", "PERIOD", "COMMA", "SEMICOLON", "ASSIGN",
		"ADD_ASSIGN", "SUB_ASSIGN", "MUL_ASSIGN", "DIV_ASSIGN", "MOD_ASSIGN"
	};


TokenType Token;
double    Num;
int       TokenLine = 1;
int       TokenColumn = 1;
char      CurrentSourceFile[260] = "<input>";
char	  Id[MAX_IDENT_LEN + 1];
char      StringLiteral[MAX_STRING_LEN + 1];

FILE * f;
int ch;
static int curLine = 1;
static int curColumn = 0;
static int prevLine = 1;
static int prevColumn = 0;

TokenType checkKeyword(char * str){
	for(int i = 0; i < KEYWORDS_COUNT; i++) {
		if(strcmp(str, keywords[i].string) == 0) {
			return keywords[i].Token;
		}
	}
	return TK_IDENT;
}

int getCh() {
	prevLine = curLine;
	prevColumn = curColumn;
	int c = fgetc(f);
	if (c == '\n') {
		curLine++;
		curColumn = 0;
	} else if (c != EOF) {
		curColumn++;
	}
	return c;
}

static void unreadCh(int c) {
	if (c == EOF) {
		return;
	}
	ungetc(c, f);
	curLine = prevLine;
	curColumn = prevColumn;
}

TokenType getToken() {
	//TODO
	while(ch==' ' || ch=='\n' || ch=='\t' || ch=='\r') ch = getCh(); //dau phan cach
	if (ch == EOF) {
		return TK_NONE;
	}
	TokenLine = curLine;
	TokenColumn = curColumn;
	if(isalpha(ch) || ch == '_') {		//bat dau la mot chu cai hoac dau gach duoi
		Id[0] = (char)toupper(ch);
		int i = 0;
		ch = getCh();
		while(isalpha(ch) || isdigit(ch) || ch == '_') {
			if (i < MAX_IDENT_LEN - 1) {
				i++;
				Id[i] = (char)toupper(ch);
			}
			ch = getCh();
		}
		Id[i+1] = '\0';
		return checkKeyword(Id);
	}
	if (ch == '"') {
		int i = 0;
		ch = getCh();
		while (ch != EOF && ch != '"') {
			if (i < MAX_STRING_LEN) {
				StringLiteral[i++] = (char)ch;
			}
			ch = getCh();
		}
		StringLiteral[i] = '\0';
		if (ch != '"') {
			printf("Error at %s: %d:%d: Unterminated string literal\n", CurrentSourceFile, TokenLine, TokenColumn);
			return TK_NONE;
		}
		ch = getCh();
		return TK_STRING;
	}
	if (isdigit(ch)) {  // bat dau la mot chu so (int/float)
		char numberBuf[MAX_NUMBER_LEN + 1];
		int length = 0;
		while (ch != EOF && isdigit(ch)) {
			if (length < MAX_NUMBER_LEN) {
				numberBuf[length++] = (char)ch;
			}
			ch = getCh();
		}

		if (ch == '.') {
			int peek = getCh();
			if (peek != EOF && isdigit(peek)) {
				if (length < MAX_NUMBER_LEN) {
					numberBuf[length++] = '.';
				}
				ch = peek;
				while (ch != EOF && isdigit(ch)) {
					if (length < MAX_NUMBER_LEN) {
						numberBuf[length++] = (char)ch;
					}
					ch = getCh();
				}
			} else {
				unreadCh(peek);
			}
		}

		if (length > MAX_NUMBER_LEN) {
			printf("Error at %s:%d:%d: Number is too large\n", CurrentSourceFile, TokenLine, TokenColumn);
			return TK_NONE;
		}

		numberBuf[length] = '\0';
		Num = strtod(numberBuf, NULL);
		return TK_NUMBER;
	}
	if (ch == ':') {
		ch = getCh();
		if(ch == '='){
			ch = getCh();
			return SB_ASSIGN;
		}
		return TK_NONE;
	}
	switch(ch){
	case '+':
		ch = getCh();
		if (ch == '+') {
			ch = getCh();
			return SB_INC;
		}
		if (ch == '=') {
			ch = getCh();
			return SB_ADD_ASSIGN;
		}
		return SB_PLUS;
	case '-':
		ch = getCh();
		if (ch == '=') {
			ch = getCh();
			return SB_SUB_ASSIGN;
		}
		return SB_MINUS;
	case '*':
		ch = getCh();
		if (ch == '=') {
			ch = getCh();
			return SB_MUL_ASSIGN;
		}
		return SB_TIMES;
	case '/':
		ch = getCh();
		if (ch == '=') {
			ch = getCh();
			return SB_DIV_ASSIGN;
		}
		return SB_SLASH;
	case '=': ch = getCh(); return SB_EQU;
	case '!': ch = getCh(); return KW_NOT;
	case '<':
		ch = getCh();
		if (ch == '=') {
			ch = getCh();
			return SB_LEQ;
		} else if (ch == '>') {
			ch = getCh();
			return SB_NEQ;
		} else if (ch == '<') {
			ch = getCh();
			return SB_SHL;
		}
		return SB_LSS;
	case '>':
		ch = getCh();
		if (ch == '=') {
			ch = getCh();
			return SB_GEQ;
		} else if (ch == '>') {
			ch = getCh();
			return SB_SHR;
		}
		return SB_GTR;
	case '%':
		ch = getCh();
		if (ch == '=') {
			ch = getCh();
			return SB_MOD_ASSIGN;
		}
		return SB_PERCENT;
	case '&': ch = getCh(); return SB_BITAND;
	case '|': ch = getCh(); return SB_BITOR;
	case '^': ch = getCh(); return SB_BITXOR;
	case '~': ch = getCh(); return SB_BITNOT;
	case '(': ch = getCh(); return SB_LPARENT;
	case ')': ch = getCh(); return SB_RPARENT;
	case '[': ch = getCh(); return SB_LBRACK;
	case ']': ch = getCh(); return SB_RBRACK;
	case '.': ch = getCh(); return SB_PERIOD;
	case ',': ch = getCh(); return SB_COMMA;
	case ';': ch = getCh(); return SB_SEMICOLON;
	default:
		ch = getCh();
		return TK_NONE;
	}
}

void compile(char * filename) {
	if((f = fopen(filename, "rt")) == NULL) {
		printf("File %s not found\n", filename);
		return;
	}
	strncpy(CurrentSourceFile, filename, sizeof(CurrentSourceFile) - 1);
	CurrentSourceFile[sizeof(CurrentSourceFile) - 1] = '\0';
	curLine = 1;
	curColumn = 0;
	prevLine = 1;
	prevColumn = 0;
	TokenLine = 1;
	TokenColumn = 1;
	ch = ' ';
	nextToken();
	program();
	fclose(f);
}