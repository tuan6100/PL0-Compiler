#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "scanner.h"

#include "parser.h"

const Keyword keywords[KEYWORDS_COUNT] = {
	{"BEGIN", KW_BEGIN},
	{"CALL", KW_CALL},
	{"CONST", KW_CONST},
	{"DO", KW_DO},
  {"ELSE", KW_ELSE},
	{"END", KW_END},
  {"FOR", KW_FOR},
	 {"IF", KW_IF},
   {"ODD",KW_ODD},
	{"PROCEDURE", KW_PROCEDURE},
  {"PROGRAM", KW_PROGRAM},
  {"THEN", KW_THEN},
  {"TO", KW_TO},
  {"VAR", KW_VAR},
	{"WHILE", KW_WHILE}
};

const char TabToken[][11] = {	"NONE", "IDENT", "NUMBER",
		"BEGIN", "CALL", "CONST", "DO",  "ELSE", "END", "FOR", "IF",
		"ODD", "PROCEDURE", "PROGRAM", "THEN", "TO", "VAR", "WHILE",

		"PLUS", "MINUS", "TIMES", "SLASH", "EQU", "NEQ", "LSS",
		"LEQ", "GTR", "GEQ", "PERCENT", "LPARENT", "RPARENT",
		"LBRACK", "RBRACK", "PERIOD", "COMMA", "SEMICOLON", "ASSIGN"
	};


TokenType Token;
int		  Num;
char	  Id[MAX_IDENT_LEN + 1];

FILE * f;
int ch;

TokenType checkKeyword(char * str){
	for(int i = 0; i < KEYWORDS_COUNT; i++) {
		if(strcmp(str, keywords[i].string) == 0) {
			return keywords[i].Token;
		}
	}
	return TK_IDENT;
}

int getCh() {
  int c = fgetc(f);
  if (c == EOF) {
	return EOF;
  }
  return toupper(c);
}

TokenType getToken() {
	//TODO
	while(ch==' ' || ch=='\n' || ch=='\t' || ch=='\r') ch = getCh(); //dau phan cach
	if (ch == EOF) {
		return TK_NONE;
	}
	if(isalpha(ch) || ch == '_') {		//bat dau la mot chu cai hoac dau gach duoi
		Id[0] = ch;
		int i = 0;
		ch = getCh();
		while(isalpha(ch) || isdigit(ch) || ch == '_') {
			if (i < MAX_IDENT_LEN - 1) {
				i++;
				Id[i] = ch;
			}
			ch = getCh();
		}
		Id[i+1] = '\0';
		return checkKeyword(Id);
	} else if( isdigit(ch)) {  //bat dau la mot chu so
		Num = 0;
		int length = 0;
		while(ch != EOF && isdigit(ch)) {
			length++;
			Num = Num * 10 + (ch - '0');
			ch = getCh();
		}
		if (length > MAX_NUMBER_LEN) {
			printf(" Number is too large\n");
			return TK_NONE;
		}
		return TK_NUMBER;
	} else if (ch == ':') {
		ch = getCh();
		if(ch == '='){
			ch = getCh();
			return SB_ASSIGN;
		}
		return TK_NONE;
	} else {
		switch(ch){
		case '+': ch = getCh(); return SB_PLUS;
		case '-': ch = getCh(); return SB_MINUS;
		case '*': ch = getCh(); return SB_TIMES;
		case '/': ch = getCh(); return SB_SLASH;
		case '=': ch = getCh(); return SB_EQU;
		case '<':
			ch = getCh();
			if (ch == '=') {
				ch = getCh();
				return SB_LEQ;
			} else if (ch == '>') {
				ch = getCh();
				return SB_NEQ;
			}
			return SB_LSS;
		case '>':
			ch = getCh();
			if (ch == '=') {
				ch = getCh();
				return SB_GEQ;
			}
			return SB_GTR;
		case '%': ch = getCh(); return SB_PERCENT;
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

}

void compile(char * filename) {
	if((f = fopen(filename, "rt")) == NULL) {
		printf("File %s not found\n", filename);
		return;
	}
	ch = ' ';
	nextToken();
	program();
	fclose(f);
}