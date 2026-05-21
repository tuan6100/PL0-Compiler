#include <stdio.h>

extern void compile(char * );

#ifdef WIN32
#include <windows.h>
#endif

int	main(int argc, char * argv[]) {
#ifdef WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	if(argc == 1)
		compile("test.pl0");
    else if(argc == 2)
    	compile(argv[1]);
	else
		printf("Syntax error (SYNTAX: pl0.exe filename.pl0)\n\n");
 	return 0;
}
