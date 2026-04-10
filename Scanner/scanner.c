#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "scanner.h"

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
	if(isalpha(ch)) {		//bat dau la mot chu cai
		Id[0] = ch;
		int i = 0;
		ch = getCh();
		while(isalpha(ch) || isdigit(ch)) {
			i++;
			Id[i] = ch;
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
		case '#': ch = getCh(); return SB_NEQ;
		case '<':
			ch = getCh();
			if(ch == '='){
				ch = getCh();
				return SB_LEQ;
			}
			return SB_LSS;
		case '>':
			ch = getCh();
			if(ch == '='){
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
	do {
		Token = getToken();
		printf(" %s", TabToken[Token]);
		if(Token == TK_IDENT) printf("(%s) \n", Id);
		else if(Token == TK_NUMBER) printf("(%d) \n", Num);
		else printf("\n");
	} while(Token != TK_NONE);
	fclose(f);
}