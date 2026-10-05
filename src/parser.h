#ifndef PARSER_H
#define PARSER_H

void nextToken();

void error(const char msg[]);

void factor();

void term();

void expression();

void condition();

void statement();

void block();

void program();

#endif