# Stack Machine ISA: A Stack-Oriented Virtual Machine in C

**Course:** CSE360 – Computer Architecture (Section 2)
**Mini Project #8:** *Stack machine ISA: design a stack machine whose instruction set is stack oriented (no registers!) and supports basic integer arithmetic and logical operations only.*

---

## Overview

This project implements a complete **stack-based virtual machine (VM)** together with a **two-pass assembler**, all in a single C source file (`stack-machine.c`).

- The instruction set is **purely stack oriented**. There are no general-purpose registers. Operands are taken from, and results are pushed back to, an operand stack.
- Programs are written in a small assembly language (with labels and comments), assembled into **bytecode**, and then executed by the VM.
- The ISA supports integer arithmetic, bitwise/logical operations, comparisons, control flow, subroutine calls, word-addressed memory access, and basic I/O.

The built-in demo program computes **factorial(5) recursively** to show that calls, returns, branching, and stack manipulation all work together.

## Features

- 29 stack-oriented instructions (arithmetic, logic, comparison, control flow, memory, I/O)
- Two-pass assembler with **label support** (forward and backward references)
- Case-insensitive mnemonics, `;` comments, and `label: instruction` on the same line
- Decimal and hexadecimal (`0x...`) immediates
- Separate **operand stack** and **call stack**
- Runtime error detection: stack overflow/underflow, division by zero, out-of-bounds jumps and memory accesses, unknown opcodes, truncated immediates
- Little-endian 32-bit immediates in the bytecode

## Machine Architecture

| Component | Description | Size |
|---|---|---|
| Operand stack | Holds 32-bit signed integers | 65,536 entries (`1<<16`) |
| Call stack | Holds return addresses for `CALL`/`RET` | 4,096 entries (`1<<12`) |
| Data memory | Word-addressed linear memory for `LOAD`/`STORE` | 65,536 words (`1<<16`) |
| Instruction pointer (`ip`) | Byte index into the bytecode | – |
| Stack pointer (`sp`) | Next free slot on the operand stack | – |

All data values are **32-bit signed integers** (`int32_t`).

### Bytecode format

Each instruction is one opcode byte. Instructions that take an operand (`PUSH`, `JMP`, `JZ`, `JNZ`, `CALL`, `LOAD`, `STORE`) are followed by a **4-byte little-endian immediate**, so they occupy 5 bytes. All other instructions are 1 byte.

## Instruction Set

In the "Stack effect" column, the rightmost item is the top of the stack. `b` is the value on top and `a` is the one beneath it.

### Stack manipulation

| Mnemonic | Opcode | Stack effect | Description |
|---|---|---|---|
| `HALT` | `0x00` | – | Stop execution |
| `PUSH imm` | `0x01` | `→ imm` | Push a 32-bit immediate |
| `POP` | `0x02` | `a →` | Discard the top value |
| `DUP` | `0x03` | `a → a a` | Duplicate the top value |
| `SWAP` | `0x04` | `a b → b a` | Swap the top two values |

### Arithmetic

| Mnemonic | Opcode | Stack effect | Description |
|---|---|---|---|
| `ADD` | `0x10` | `a b → a+b` | Addition |
| `SUB` | `0x11` | `a b → a-b` | Subtraction |
| `MUL` | `0x12` | `a b → a*b` | Multiplication |
| `DIV` | `0x13` | `a b → a/b` | Integer division (error if `b == 0`) |
| `MOD` | `0x14` | `a b → a%b` | Remainder (error if `b == 0`) |
| `NEG` | `0x15` | `a → -a` | Negation |

### Logical / bitwise

| Mnemonic | Opcode | Stack effect | Description |
|---|---|---|---|
| `AND` | `0x20` | `a b → a&b` | Bitwise AND |
| `OR` | `0x21` | `a b → a\|b` | Bitwise OR |
| `XOR` | `0x22` | `a b → a^b` | Bitwise XOR |
| `NOT` | `0x23` | `a → ~a` | Bitwise NOT |
| `SHL` | `0x24` | `v s → v<<s` | Shift left (shift amount masked to 5 bits) |
| `SHR` | `0x25` | `v s → v>>s` | Logical shift right (shift amount masked to 5 bits) |

### Comparison

Each comparison pushes `1` for true and `0` for false.

| Mnemonic | Opcode | Stack effect | Description |
|---|---|---|---|
| `EQ` | `0x30` | `a b → (a==b)` | Equal |
| `LT` | `0x31` | `a b → (a<b)` | Less than |
| `GT` | `0x32` | `a b → (a>b)` | Greater than |

### Control flow

| Mnemonic | Opcode | Stack effect | Description |
|---|---|---|---|
| `JMP addr` | `0x40` | – | Unconditional jump to a label or address |
| `JZ addr` | `0x41` | `a →` | Jump if `a == 0` |
| `JNZ addr` | `0x42` | `a →` | Jump if `a != 0` |
| `CALL addr` | `0x43` | – | Push the return address on the call stack and jump |
| `RET` | `0x44` | – | Return to the caller (ends execution if the call stack is empty) |

### Memory and I/O

| Mnemonic | Opcode | Stack effect | Description |
|---|---|---|---|
| `LOAD addr` | `0x50` | `→ mem[addr]` | Push the word at `addr` |
| `STORE addr` | `0x51` | `a →` | Pop `a` and store it at `mem[addr]` |
| `IN` | `0x60` | `→ v` | Read an integer from standard input and push it |
| `OUT` | `0x61` | `a →` | Pop `a` and print it followed by a newline |

## Assembly Language Syntax

```asm
; a comment
label:  INSTRUCTION operand   ; label and instruction may share a line
        PUSH 0x10             ; hexadecimal immediate
        JMP  label            ; labels can be used as jump/call targets
```

- One instruction per line
- Comments start with `;`
- Labels must start with a letter or underscore and contain only letters, digits, and underscores
- Mnemonics are case-insensitive

## Demo Program: Recursive Factorial

The program embedded in `main()` (as `program_lines[]`) computes `5!`. It pushes the argument, calls `main`, prints the result, and halts.

```asm
    PUSH 5
    CALL main
    OUT
    HALT
main: CALL fact
      RET

fact: DUP
      PUSH 1
      LT
      JNZ base_case
      DUP
      PUSH 1
      EQ
      JNZ base_case
      DUP
      PUSH 1
      SUB
      CALL fact
      MUL
      RET
base_case: POP
           PUSH 1
           RET
```

**How it works:** `fact` duplicates `n` and checks whether `n < 1` or `n == 1`. If so, it jumps to `base_case`, which discards `n` and returns `1`. Otherwise it computes `n * fact(n-1)` by duplicating `n`, subtracting 1, recursing, and multiplying the result by the saved `n`.

## Requirements

- A C compiler with C11 support (GCC or Clang)
- A POSIX-like environment (Linux, macOS, or WSL on Windows). The code uses `strdup` and `strcasecmp`, which are POSIX functions.

## Build

```bash
gcc -std=gnu11 -O2 -Wall -o stackvm stack-machine.c
```

> **Note:** Use `-std=gnu11` rather than `-std=c11`. Strict C11 mode hides the POSIX functions `strdup` and `strcasecmp`, which the assembler needs.

## Run

```bash
./stackvm
```

### Sample output

```text
120

--- VM final SP = 0 ---
--- Sample memory (first 8 words) ---
[0] = 0
[1] = 0
[2] = 0
[3] = 0
[4] = 0
[5] = 0
[6] = 0
[7] = 0
```

The first line, `120`, is `5!` printed by the `OUT` instruction. After `HALT`, the program prints the final stack pointer (0 means the stack is empty) and the first 8 words of data memory, which are all zero because the demo never uses `STORE`.

## Running Your Own Program

Edit the `program_lines[]` array near the bottom of `stack-machine.c`. It is a `NULL`-terminated array of assembly lines. For example, to add two numbers read from input:

```c
const char *program_lines[] = {
    "    IN",
    "    IN",
    "    ADD",
    "    OUT",
    "    HALT",
    NULL
};
```

Rebuild with the command above and run `./stackvm`, then type two integers (for example `3 4`) to see `7`.

## Error Handling

The assembler and VM report errors to `stderr`:

| Stage | Error |
|---|---|
| Assembler | Invalid label name, unknown opcode, missing immediate/address, invalid immediate, unknown label |
| VM | Stack overflow/underflow, `DUP`/`SWAP` on too few elements, division by zero, jump/call target out of bounds, call stack overflow, load/store out of bounds, truncated immediate, unknown opcode, failed input read |

## Project Structure

```
.
├── stack-machine.c   # assembler + VM + demo program (single file)
└── README.md
```

### Main components in `stack-machine.c`

| Section | Purpose |
|---|---|
| Opcode `enum` | Numeric encoding of every instruction |
| `ByteBuf` | Growable byte buffer for assembled code |
| `LabelTable` | Label name to address table used by the assembler |
| `assemble_lines()` | Two-pass assembler: pass 1 computes label addresses, pass 2 emits bytecode |
| `VM`, `vm_init()`, `vm_run()` | Virtual machine state and the fetch-decode-execute loop |
| `main()` | Assembles and runs the demo program, then prints the final VM state |

## Design Notes and Limitations

- Because there are no registers, every operation works on the operand stack, so the instruction encoding is compact and needs no register specifiers.
- Arithmetic (`ADD`, `SUB`, `MUL`) is computed in 64 bits and truncated to 32 bits, so overflow wraps around.
- `SHR` is a logical (unsigned) shift, not an arithmetic shift.
- The `LOAD`/`STORE` operand is a fixed address, so there is no indirect addressing.
- The program is embedded in the source file and there is no command-line assembly file loader.

## Author

**Name:** _Your Name_
**Student ID:** _Your ID_
**Course:** CSE360 – Computer Architecture, Section 2
**University:** East West University
