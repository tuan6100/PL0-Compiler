#ifndef PARSER_H
#define PARSER_H

// Lấy token tiếp theo và lưu vào biến toàn cục Token
void nextToken(void);

// Báo lỗi và dừng chương trình
void error(const char msg[]);

// Phân tích nhân tử: NUMBER | IDENT | '(' expression ')'
void factor(void);

// Phân tích số hạng: factor { ('*' | '/' | '%') factor }
void term(void);

// Phân tích biểu thức: ['+' | '-'] term { ('+' | '-') term }
void expression(void);

// Phân tích điều kiện: ODD expression | expression ('='|'#'|'<'|'<='|'>'|'>=') expression
void condition(void);

// Phân tích câu lệnh
void statement(void);

// Phân tích khối: CONST, VAR, PROCEDURE, statement
void block(void);

// Phân tích chương trình: PROGRAM ident ';' block '.'
void program(void);

#endif