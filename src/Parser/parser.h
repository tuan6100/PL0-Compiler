#ifndef PL0_PARSER_H
#define PL0_PARSER_H
#include "../Scanner/scanner.h"

extern TokenType Token;

void parse(TokenType token);
void error (const char msg[]);//Báo lỗi
void factor();//phân tích nhân tử
void term();//phân tích số hạng
void expression(); // phân tích biểu thức
void condition(); // phân tích điều kiện
void statement(); // phân tích câu lệnh
void block(); // phân tích các khối câu lệnh
void program(); //Phân tích chương trình

#endif //PL0_PARSER_H