#include <stdio.h>
#include "scanner.h"

int	main(int argc, char * argv[]) {
	if(argc == 1)  compile("test.pl0");
    else if(argc == 2)  compile(argv[1]); 
	else printf("Syntax error (SYNTAX: pl0.exe filename.pl0)\n\n");
 	return 0;
}
