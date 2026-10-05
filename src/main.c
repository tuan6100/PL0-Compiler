#include <stdio.h>
#include "scanner.h"

int	main(int argc, char * argv[]) {
	if(argc == 1) {
		fprintf(stderr, "Missing file name\n");
	} else if(argc == 2) {
		compile(argv[1]);
	} else {
		fprintf(stderr, "Syntax error (SYNTAX: pl0 filename.pl0)\n");
	}
 	return 0;
}
