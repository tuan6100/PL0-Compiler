#include "codegen.h"
#include "parser.h"
#include "scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void printHelper(const char *progName) {
  printf("PL/0 Compiler & Bytecode Virtual Machine\n\n");
  printf("Usage:\n");
  printf("  %s [options] <source.pl0>             Compile and run PL/0 source "
         "file\n",
         progName);
  printf("  %s -c [options] <source.pl0>          Compile to .pcode without "
         "executing\n",
         progName);
  printf("  %s -r <file.pcode>                    Execute a .pcode bytecode "
         "file\n",
         progName);
  printf("  %s run <file.pcode>                   Execute a .pcode bytecode "
         "file\n",
         progName);
  printf("  %s <file.pcode>                       Execute a .pcode bytecode "
         "file directly\n",
         progName);
  printf("  %s -l <file.pl0|file.pcode>           Disassemble / list "
         "instructions\n",
         progName);
  printf("  %s disasm <file.pl0|file.pcode>       Disassemble / list "
         "instructions\n\n",
         progName);
  printf("Options:\n");
  printf("  -c, --compile                         Compile only (do not "
         "execute)\n");
  printf("  -o <file>                             Specify output .pcode "
         "filename (default: <source>.pcode)\n");
  printf("  -t, --text, -format text              Save .pcode in "
         "human-readable plain text format\n");
  printf("  -b, --binary, --bin, -format binary   Save .pcode in compact "
         "binary format (default)\n");
  printf("  -r, -e, --run, --exec <file.pcode>    Execute the given .pcode "
         "file\n");
  printf("  -l, -d, --list, --disasm              Disassemble/list generated "
         "or loaded instructions\n");
  printf("  -h, --help                            Display this help message\n");
}

static void getDefaultPCodeName(const char *srcName, char *dst,
                                size_t dstSize) {
  strncpy(dst, srcName, dstSize - 1);
  dst[dstSize - 1] = '\0';
  char *dot = strrchr(dst, '.');
  if (dot != NULL) {
    *dot = '\0';
  }
  strncat(dst, ".pcode", dstSize - strlen(dst) - 1);
}

static int isPCodeFile(const char *filename) {
  const char *ext = strrchr(filename, '.');
  if (ext != NULL && strcmp(ext, ".pcode") == 0) {
    return 1;
  }
  FILE *fp = fopen(filename, "rb");
  if (fp) {
    char buf[16];
    memset(buf, 0, sizeof(buf));
    size_t n = fread(buf, 1, 14, fp);
    fclose(fp);
    if (n >= 4 && memcmp(buf, "PL0B", 4) == 0)
      return 1;
    if (n >= 14 && memcmp(buf, "PL0_PCODE_TEXT", 14) == 0)
      return 1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printHelper(argv[0]);
    return 1;
  }

  const char *inputFile = NULL;
  const char *outputPCode = NULL;
  const char *runPCodeFile = NULL;
  int compileOnly = 0;
  int disasm = 0;
  PCodeFormat format = PCODE_FMT_BINARY;
  int formatSpecified = 0;
  int startIdx = 1;

  if (strcmp(argv[1], "run") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Error: 'run' command requires a .pcode file path\n");
      return 1;
    }
    runPCodeFile = argv[2];
    startIdx = 3;
  } else if (strcmp(argv[1], "disasm") == 0 || strcmp(argv[1], "list") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Error: '%s' command requires a file path\n", argv[1]);
      return 1;
    }
    disasm = 1;
    inputFile = argv[2];
    startIdx = 3;
  } else if (strcmp(argv[1], "compile") == 0) {
    compileOnly = 1;
    startIdx = 2;
  } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
             strcmp(argv[1], "--help") == 0) {
    printHelper("pl0");
    return 0;
  }

  for (int i = startIdx; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printHelper("pl0");
      return 0;
    } else if (strcmp(argv[i], "-c") == 0 ||
               strcmp(argv[i], "--compile") == 0) {
      compileOnly = 1;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -o option requires an output filename\n");
        return 1;
      }
      outputPCode = argv[++i];
    } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--text") == 0) {
      format = PCODE_FMT_TEXT;
      formatSpecified = 1;
    } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--binary") == 0 ||
               strcmp(argv[i], "--bin") == 0) {
      format = PCODE_FMT_BINARY;
      formatSpecified = 1;
    } else if (strcmp(argv[i], "-format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -format option requires 'text' or 'binary'\n");
        return 1;
      }
      const char *fmtArg = argv[++i];
      if (strcmp(fmtArg, "text") == 0 || strcmp(fmtArg, "txt") == 0) {
        format = PCODE_FMT_TEXT;
      } else if (strcmp(fmtArg, "binary") == 0 || strcmp(fmtArg, "bin") == 0) {
        format = PCODE_FMT_BINARY;
      } else {
        fprintf(stderr,
                "Error: invalid format '%s', expected 'text' or 'binary'\n",
                fmtArg);
        return 1;
      }
      formatSpecified = 1;
    } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-e") == 0 ||
               strcmp(argv[i], "--run") == 0 ||
               strcmp(argv[i], "--exec") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s option requires a .pcode filename\n",
                argv[i]);
        return 1;
      }
      runPCodeFile = argv[++i];
    } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "-d") == 0 ||
               strcmp(argv[i], "--list") == 0 ||
               strcmp(argv[i], "--disasm") == 0) {
      disasm = 1;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Error: unrecognized option '%s'\n", argv[i]);
      printHelper("pl0");
      return 1;
    } else {
      if (inputFile == NULL) {
        inputFile = argv[i];
      } else {
        fprintf(stderr, "Error: multiple input files specified ('%s', '%s')\n",
                inputFile, argv[i]);
        return 1;
      }
    }
  }

  if (runPCodeFile != NULL) {
    if (loadPCode(runPCodeFile) != 0) {
      return 1;
    }
    if (disasm) {
      listCode();
    } else {
      interpret();
    }
    return 0;
  }

  if (inputFile == NULL) {
    fprintf(stderr, "Error: no input file specified\n");
    printHelper("pl0");
    return 1;
  }

  if (isPCodeFile(inputFile)) {
    if (loadPCode(inputFile) != 0) {
      return 1;
    }
    if (disasm) {
      listCode();
    } else {
      interpret();
    }
    return 0;
  }

  if (compileSource(inputFile) != 0) {
    return 1;
  }

  if (disasm) {
    listCode();
  }

  if (compileOnly || outputPCode != NULL || formatSpecified) {
    char defaultOut[300];
    if (outputPCode == NULL) {
      getDefaultPCodeName(inputFile, defaultOut, sizeof(defaultOut));
      outputPCode = defaultOut;
    }
    if (savePCode(outputPCode, format) != 0) {
      return 1;
    }
    if (!compileOnly && !disasm) {
      interpret();
    }
  } else if (!disasm) {
    interpret();
  }

  return 0;
}
