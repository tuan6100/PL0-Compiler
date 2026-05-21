#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "scanner.h"

#include "../Parser/parser.h"

Keyword Keywords[KEYWORDS_COUNT] = {
	{"BEGIN", KW_BEGIN}, {"CALL", KW_CALL}, {"CONST", KW_CONST},
	{"DO", KW_DO}, {"ELSE", KW_ELSE}, {"END", KW_END}, {"FOR", KW_FOR},
	{"IF", KW_IF}, {"ODD", KW_ODD}, {"PROCEDURE", KW_PROCEDURE},
	{"PROGRAM", KW_PROGRAM}, {"THEN", KW_THEN}, {"TO", KW_TO},
	{"VAR", KW_VAR}, {"WHILE", KW_WHILE}
};

char TabToken[][10] = {
	"NONE", "IDENT", "NUMBER",
	"BEGIN", "CALL", "CONST", "DO", "ELSE", "END", "FOR", "IF",
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
		if(strcmp(str, Keywords[i].string) == 0) {
			return Keywords[i].Token;
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
	while(ch==' ' || ch=='\n' || ch=='\t' || ch=='\r') ch = getCh(); //dau phan cach
	if (ch == EOF) {
		return TK_NONE;
	}
	if(isalpha(ch)) {		//bat dau la mot chu cai
		Id[0] = (char)ch;
		int i = 0;
		ch = getCh();
		while(isalpha(ch) || isdigit(ch)) {
			i++;
			Id[i] = (char)ch;
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
		return TK_NUMBER;		//chu y canh bao: so qua lon
	} else if (ch == ':') {   //doan nhan tu vung :=
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
			}
			if (ch == '>') {
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
	Token = getToken();
	parse(Token);
	fclose(f);
}