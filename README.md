# Extended PL/0 Programing Language

## Overview
A simple compiler and runtime environment for the [PL/0 language](https://en.wikipedia.org/wiki/PL/0) written in C11.

This implementation extends standard Wirth's PL/0 with modern programming language capabilities including multidimensional arrays, first-class strings, string interpolation, compound operators, bitwise logic, procedure return values, reference parameters, and runtime safety checks.

---

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

### 4. Control Flow & Procedures
- **Conditionals**: `IF condition THEN statement [ELSE statement]`
- **Loops**:
  - `WHILE condition DO statement`
  - `FOR var := start (TO | DOWNTO) end [STEP step] DO statement`
- **Procedures**:
  - Value parameters, reference parameters (followed by `VAR` keyword), and array parameters (`arr[]`, `VAR arr[]`).
  - Return values with `RETURN expression;`.
  - Procedures can be used directly as expressions or function calls.

---

## Building and Running
### 1. With CMake
```bash
cmake -B build
cmake --build build
./target/pl0 <filename.pl0>
```

### 2. With Makefile
```bash
make run <filename.pl0>
```

### 3. Direct GCC Compilation
```bash
gcc -Wall -O2 -o pl0 src/*.c -lm
./pl0 <filename.pl0>
```