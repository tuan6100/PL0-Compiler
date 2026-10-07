# Extended PL/0 Programing Language

## Overview
A simple compiler and runtime environment for the [PL/0 language](https://en.wikipedia.org/wiki/PL/0) written in C11.

This implementation extends standard Wirth's PL/0 with modern programming language capabilities including multidimensional arrays, first-class strings, string interpolation, compound operators, bitwise logic, procedure return values, reference parameters, and runtime safety checks.

## Language Features

### 1. Multi-Dimensional & Dynamic Arrays 
- **Fixed-size and Multi-dimensional Declarations**:
  ```pascal
  VAR arr[5] := [2, 4, 3, 5, 1];
  CONST mat[3][3] = [[1, 2, 3],
                     [4, 5, 6],
                     [7, 8, 9]];
  ```
- **Variable-Length / Runtime-Sized Arrays (VLA)**:
  ```pascal
  VAR temp[right + 1];
  ```
- **Runtime Array Bounds Checking**:
  - Automatically verifies $0 \le \text{index} < \text{bound}$ for each dimension.
  - Generates clear runtime errors upon out-of-bounds access (e.g. `array index 5 out of bounds (0..4)`).
- **First-Class Array Printing**:
  - Directly print whole arrays via `WRITE(arr)`, `WRITELN(arr)`, or interpolated `$arr` / `${arr}`.
  - Multidimensional arrays format as nested bracketed structures (e.g., `[[1, 2], [3, 4]]`).

### 2. Strings & String Interpolation
- **String Variables and Literals**:
  ```pascal
  CONST greeting = "Hello";
  VAR str[255];
  ```
- **String Concatenation** with `+` across strings, numbers, and arrays:
  ```pascal
  str := num + " " + str1 + str2;
  ```
- **Variable and Expression Interpolation**:
  ```pascal
  WRITELN("Circle radius $r has area: ${calculateArea(r)}");
  WRITELN("Array contents: $arr");
  ```

### 3. Rich Operators & Expressions
- **Arithmetic**: `+`, `-`, `*`, `/`, `//` (floor division), `%` (modulo)
- **Bitwise**: `&` (AND), `|` (OR), `^` (XOR), `~` (NOT), `<<` (SHL), `>>` (SHR)
- **Relational**: `=`, `<>` (not equal), `<`, `<=`, `>`, `>=`
- **Logical**: `AND`, `OR`, `NOT`
- **Assignments**: Standard `:=`, compound assignments (`+=`, `-=`, `*=`, `/=`, `%=`), and postfix increment (`++`)
- **Built-in Functions**:
  - `SIZEOF(x)`: Compile-time or runtime byte/element count (e.g., `SIZEOF(mat) / SIZEOF(mat[0])`).
  - `ODD x`: Tests if integer is odd.
  - `NULL`: Null pointer/value literal.

### 4. Control Flow, Functions & Procedures
- **Conditionals**: `IF condition THEN statement [ELSE statement]`
- **Loops**:
  - `WHILE condition DO statement`
  - `FOR var := start (TO | DOWNTO) end [STEP step] DO statement`
- **Functions & Procedures** (Pascal-style distinction):
  - `FUNCTION name(params); ... RETURN expr; END;`: Returns a scalar or array value; can be used in expressions, initializers, or interpolation.
  - `PROCEDURE name(params); ... END;`: Subroutine that does not return a value. Bare `RETURN;` is allowed for early exit. Attempting to return a value or use a `PROCEDURE` in an expression produces a compile-time error.
  - Value parameters, reference parameters (`VAR param`), and array parameters (`arr[]`, `VAR arr[]`, `arr[][]`).
- **Invocation & `CALL` Keyword**:
  - Both explicit `CALL name(args);` and direct `name(args);` syntax are supported for backward compatibility and clean scripting style.
  - Functions called as statements automatically discard their return value.

### 5. Static Variables & Constants
Static variables and constants persist their state across multiple procedure invocations while retaining local procedure scope visibility:
- **Prefix Syntax** (applies `STATIC` to all items in the declaration clause):
  ```pascal
  STATIC VAR counter = 0, total = 100;
  STATIC CONST limit = 50;
  ```
- **Suffix / Item Syntax** (applies `STATIC` to a specific variable or constant):
  ```pascal
  VAR x = 0 STATIC;
  VAR count STATIC;
  VAR arr[10] STATIC;
  CONST max = 0 STATIC;
  ```

## Building and Running
### 1. Direct GCC Compilation
```bash
gcc -Wall -O2 -o pl0 src/*.c -lm
```

### 2. With CMake
```bash
cmake -B build
cmake --build build
```

## Compiler CLI & P-Code Virtual Machine

PL/0 can compile source code directly to executable three-address bytecode (`.pcode` files) in either **compact binary** format (like Java `.class` bytecode) or **plain text** format, and execute them on the PL/0 Virtual Machine.

### Quick Usage Examples

#### 1. Compile & Execute Directly
```bash
./pl0 program.pl0
```

#### 2. Compile to `.pcode` Bytecode File (Compile-Only)
```bash
# Compile to binary bytecode (default format -> program.pcode)
./pl0 -c program.pl0

# Compile to binary bytecode with custom output path
./pl0 -c -b -o build/program.pcode program.pl0

# Compile to human-readable plain text P-Code
./pl0 -c -t -o program_text.pcode program.pl0
```

#### 3. Execute `.pcode` Bytecode Files
```bash
# Using -r / --run flag
./pl0 -r program.pcode

# Using 'run' subcommand
./pl0 run program.pcode

# Direct file execution
./pl0 program.pcode
```

#### 4. Disassemble / List P-Code Instructions
```bash
# Disassemble an existing .pcode file
./pl0 -l program.pcode
./pl0 disasm program.pcode

# Compile and print disassembled bytecode
./pl0 -l program.pl0
```

### Command-Line Options Reference

| Option | Description |
| :--- | :--- |
| `[file.pl0]` | Compile and execute source file directly |
| `[file.pcode]` | Execute precompiled bytecode file directly |
| `-c`, `--compile` | Compile only; write `.pcode` file without running |
| `-o <file>` | Specify output `.pcode` file path (defaults to `<source>.pcode`) |
| `-b`, `--binary`, `-format binary` | Generate compact binary bytecode (default) |
| `-t`, `--text`, `-format text` | Generate human-readable plain text P-Code |
| `-r`, `-e`, `--run`, `--exec <file>` | Execute `.pcode` bytecode file in the VM |
| `-l`, `-d`, `--list`, `--disasm` | Disassemble and print instructions and string table |
| `-h`, `--help` | Display CLI help message |

### `.pcode` File Formats

#### Binary Format (`.pcode`)
- **Magic Header**: `PL0B` (4 bytes: `0x50, 0x4C, 0x30, 0x42`)
- **Version & Flags**: Version `1` (`uint8_t`), Flags (`uint8_t`)
- **String Pool**: Count (`uint32_t`), followed by length-prefixed UTF-8 string literals
- **Instructions**: Count (`uint32_t`), followed by `[OpCode: uint8_t, Level: int32_t, Operand: double 8-byte IEEE-754]`

#### Plain Text Format (`.pcode`)
- **Magic Header**: `PL0_PCODE_TEXT v1`
- **String Pool Section**: `STRINGS <count>` followed by `<index> "<escaped_string>"`
- **Code Section**: `CODE <count>` followed by `<index> <OPCODE> <level> <operand>`