// stackvm.c
// Simple stack-oriented virtual machine with assembler in C.
// Compile: gcc -std=c11 -O2 -o stackvm stackvm.c
// Run: ./stackvm

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define STACK_MAX  (1<<16)
#define CALLSTACK_MAX (1<<12)
#define MEM_WORDS  (1<<16)  // simple linear memory for LOAD/STORE (word-addressed)

typedef int32_t i32;
typedef uint32_t u32;
typedef uint8_t u8;

// Opcodes
enum {
    OP_HALT = 0x00,
    OP_PUSH = 0x01,
    OP_POP  = 0x02,
    OP_DUP  = 0x03,
    OP_SWAP = 0x04,
    OP_ADD  = 0x10,
    OP_SUB  = 0x11,
    OP_MUL  = 0x12,
    OP_DIV  = 0x13,
    OP_MOD  = 0x14,
    OP_NEG  = 0x15,
    OP_AND  = 0x20,
    OP_OR   = 0x21,
    OP_XOR  = 0x22,
    OP_NOT  = 0x23,
    OP_SHL  = 0x24,
    OP_SHR  = 0x25,
    OP_EQ   = 0x30,
    OP_LT   = 0x31,
    OP_GT   = 0x32,
    OP_JMP  = 0x40,
    OP_JZ   = 0x41,
    OP_JNZ  = 0x42,
    OP_CALL = 0x43,
    OP_RET  = 0x44,
    OP_LOAD = 0x50,
    OP_STORE= 0x51,
    OP_IN   = 0x60,
    OP_OUT  = 0x61
};

// Dynamic byte buffer for assembled code
typedef struct {
    u8 *data;
    size_t len;
    size_t cap;
} ByteBuf;

static void buf_init(ByteBuf *b) {
    b->data = NULL; b->len = 0; b->cap = 0;
}
static void buf_free(ByteBuf *b) {
    free(b->data); b->data = NULL; b->len = b->cap = 0;
}
static void buf_push_u8(ByteBuf *b, u8 v) {
    if (b->len + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->data = realloc(b->data, b->cap);
    }
    b->data[b->len++] = v;
}
static void buf_push_i32_le(ByteBuf *b, i32 v) {
    u32 u = (u32)v;
    buf_push_u8(b, (u & 0xFF));
    buf_push_u8(b, ((u >> 8) & 0xFF));
    buf_push_u8(b, ((u >> 16) & 0xFF));
    buf_push_u8(b, ((u >> 24) & 0xFF));
}

// Simple label table (linear)
typedef struct {
    char **names;
    int *addrs;
    size_t n, cap;
} LabelTable;

static void labels_init(LabelTable *t) { t->names = NULL; t->addrs = NULL; t->n = t->cap = 0; }
static void labels_free(LabelTable *t) {
    for (size_t i=0;i<t->n;i++) free(t->names[i]);
    free(t->names); free(t->addrs);
    t->names = NULL; t->addrs = NULL; t->n = t->cap = 0;
}
static int labels_find(LabelTable *t, const char *name) {
    for (size_t i=0;i<t->n;i++) if (strcmp(t->names[i], name)==0) return (int)i;
    return -1;
}
static void labels_add(LabelTable *t, const char *name, int addr) {
    if (labels_find(t, name) != -1) return; // ignore duplicates here (assembler will catch)
    if (t->n + 1 > t->cap) {
        t->cap = t->cap ? t->cap * 2 : 64;
        t->names = realloc(t->names, t->cap * sizeof(char*));
        t->addrs = realloc(t->addrs, t->cap * sizeof(int));
    }
    t->names[t->n] = strdup(name);
    t->addrs[t->n] = addr;
    t->n++;
}
static int labels_get_addr(LabelTable *t, const char *name, int *out_addr) {
    int idx = labels_find(t, name);
    if (idx == -1) return 0;
    *out_addr = t->addrs[idx];
    return 1;
}

// Trim functions
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) { *end = 0; end--; }
    return s;
}

// Tokenize single line into words, returns malloced array (caller frees)
static char **tokenize_line(const char *line, int *out_count) {
    // naive split by whitespace
    char *copy = strdup(line);
    char *p = copy;
    int cap = 8, n = 0;
    char **arr = malloc(cap * sizeof(char*));
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *start = p;
        if (*p == '"') { // quoted string (not used here but kept)
            p++;
            start = p;
            while (*p && *p != '"') p++;
            if (*p == '"') { *p = 0; p++; }
        } else {
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) { *p = 0; p++; }
        }
        if (n + 1 > cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char*)); }
        arr[n++] = strdup(start);
    }
    free(copy);
    *out_count = n;
    return arr;
}
static void free_tokens(char **toks, int n) {
    for (int i=0;i<n;i++) free(toks[i]);
    free(toks);
}

// Check if string is identifier (simple)
static int is_identifier(const char *s) {
    if (!s || !*s) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0]=='_' )) return 0;
    for (const char *p=s+1; *p; ++p)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    return 1;
}

// Parse integer with base/0x support
static int parse_int32(const char *s, i32 *out) {
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s) return 0;
    *out = (i32)v;
    return 1;
}

// Assembler: two-pass over array of lines (null-terminated list)
static unsigned char* assemble_lines(const char **lines, size_t *out_len) {
    // First pass: detect labels and compute addresses
    LabelTable labels; labels_init(&labels);
    size_t pc = 0;
    for (size_t li=0; lines[li] != NULL; ++li) {
        char linebuf[1024];
        strncpy(linebuf, lines[li], sizeof(linebuf)-1); linebuf[sizeof(linebuf)-1] = 0;
        // strip comment
        char *semi = strchr(linebuf, ';');
        if (semi) *semi = 0;
        char *line = trim(linebuf);
        if (!*line) continue;
        // handle labels possibly on same line: label: instr...
        while (1) {
            char *col = strchr(line, ':');
            if (!col) break;
            // label found
            char lblname[256]; memset(lblname,0,sizeof(lblname));
            int len = (int)(col - line);
            if (len >= (int)sizeof(lblname)) len = sizeof(lblname)-1;
            strncpy(lblname, line, len); lblname[len]=0;
            char *t = trim(lblname);
            if (!is_identifier(t)) {
                fprintf(stderr, "Invalid label name '%s'\n", t);
                labels_free(&labels); return NULL;
            }
            labels_add(&labels, t, (int)pc);
            // move past label:
            line = trim(col+1);
            if (!*line) break;
        }
        if (!*line) continue;
        int ntoks; char **toks = tokenize_line(line, &ntoks);
        if (ntoks <= 0) { free_tokens(toks, ntoks); continue; }
        char *op = toks[0];
        // opcode size
        pc += 1; // opcode byte
        if (strcasecmp(op, "PUSH")==0) pc += 4;
        else if (strcasecmp(op, "JMP")==0 || strcasecmp(op,"JZ")==0 || strcasecmp(op,"JNZ")==0 ||
                 strcasecmp(op,"CALL")==0 || strcasecmp(op,"LOAD")==0 || strcasecmp(op,"STORE")==0)
            pc += 4;
        // else no immediate
        free_tokens(toks, ntoks);
    }

    // Second pass: emit bytes
    ByteBuf out; buf_init(&out);
    for (size_t li=0; lines[li] != NULL; ++li) {
        char linebuf[1024];
        strncpy(linebuf, lines[li], sizeof(linebuf)-1); linebuf[sizeof(linebuf)-1] = 0;
        char *semi = strchr(linebuf, ';');
        if (semi) *semi = 0;
        char *line = trim(linebuf);
        if (!*line) continue;
        // strip labels
        while (1) {
            char *col = strchr(line, ':');
            if (!col) break;
            line = trim(col+1);
            if (!*line) break;
        }
        if (!*line) continue;
        int ntoks; char **toks = tokenize_line(line, &ntoks);
        if (ntoks <= 0) { free_tokens(toks, ntoks); continue; }
        char *op = toks[0];
        // map opcode string to numeric code
        u8 code = 0xFF;
        #define OPCASE(s,c) if (strcasecmp(op,s)==0) code = c; else
        OPCASE("HALT", OP_HALT)
        OPCASE("PUSH", OP_PUSH)
        OPCASE("POP",  OP_POP)
        OPCASE("DUP",  OP_DUP)
        OPCASE("SWAP", OP_SWAP)
        OPCASE("ADD",  OP_ADD)
        OPCASE("SUB",  OP_SUB)
        OPCASE("MUL",  OP_MUL)
        OPCASE("DIV",  OP_DIV)
        OPCASE("MOD",  OP_MOD)
        OPCASE("NEG",  OP_NEG)
        OPCASE("AND",  OP_AND)
        OPCASE("OR",   OP_OR)
        OPCASE("XOR",  OP_XOR)
        OPCASE("NOT",  OP_NOT)
        OPCASE("SHL",  OP_SHL)
        OPCASE("SHR",  OP_SHR)
        OPCASE("EQ",   OP_EQ)
        OPCASE("LT",   OP_LT)
        OPCASE("GT",   OP_GT)
        OPCASE("JMP",  OP_JMP)
        OPCASE("JZ",   OP_JZ)
        OPCASE("JNZ",  OP_JNZ)
        OPCASE("CALL", OP_CALL)
        OPCASE("RET",  OP_RET)
        OPCASE("LOAD", OP_LOAD)
        OPCASE("STORE",OP_STORE)
        OPCASE("IN",   OP_IN)
        OPCASE("OUT",  OP_OUT)
        /* else */ { code = 0xFF; }
        if (code == 0xFF) {
            fprintf(stderr, "Unknown opcode: %s\n", op);
            free_tokens(toks, ntoks); labels_free(&labels); buf_free(&out); return NULL;
        }
        buf_push_u8(&out, code);
        // immediates
        if (code == OP_PUSH) {
            if (ntoks < 2) { fprintf(stderr,"PUSH requires an immediate\n"); free_tokens(toks, ntoks); labels_free(&labels); buf_free(&out); return NULL; }
            i32 imm; if (!parse_int32(toks[1], &imm)) { fprintf(stderr,"Invalid immediate '%s'\n", toks[1]); free_tokens(toks, ntoks); labels_free(&labels); buf_free(&out); return NULL; }
            buf_push_i32_le(&out, imm);
        } else if (code == OP_JMP || code == OP_JZ || code == OP_JNZ || code == OP_CALL || code == OP_LOAD || code == OP_STORE) {
            if (ntoks < 2) { fprintf(stderr,"%s requires an address or label\n", op); free_tokens(toks, ntoks); labels_free(&labels); buf_free(&out); return NULL; }
            int addr;
            if (is_identifier(toks[1])) {
                if (!labels_get_addr(&labels, toks[1], &addr)) { fprintf(stderr,"Unknown label '%s'\n", toks[1]); free_tokens(toks, ntoks); labels_free(&labels); buf_free(&out); return NULL; }
            } else {
                i32 av; if (!parse_int32(toks[1], &av)) { fprintf(stderr,"Invalid address '%s'\n", toks[1]); free_tokens(toks, ntoks); labels_free(&labels); buf_free(&out); return NULL; }
                addr = av;
            }
            buf_push_i32_le(&out, (i32)addr);
        } else {
            // no immediate
        }
        free_tokens(toks, ntoks);
    }

    // finalize
    unsigned char *result = malloc(out.len);
    memcpy(result, out.data, out.len);
    *out_len = out.len;
    buf_free(&out);
    labels_free(&labels);
    return result;
}

// VM structure
typedef struct {
    unsigned char *code;
    size_t code_len;
    size_t ip; // byte index
    i32 stack[STACK_MAX];
    size_t sp; // next free
    i32 callstack[CALLSTACK_MAX];
    size_t csp;
    i32 mem[MEM_WORDS]; // word-addressed simple memory
} VM;

static void vm_init(VM *vm, unsigned char *code, size_t code_len) {
    vm->code = code;
    vm->code_len = code_len;
    vm->ip = 0;
    vm->sp = 0;
    vm->csp = 0;
    memset(vm->mem, 0, sizeof(vm->mem));
}

static int vm_push(VM *vm, i32 v) {
    if (vm->sp >= STACK_MAX) { fprintf(stderr,"Stack overflow\n"); return 0; }
    vm->stack[vm->sp++] = v;
    return 1;
}
static int vm_pop(VM *vm, i32 *out) {
    if (vm->sp == 0) { fprintf(stderr, "Stack underflow\n"); return 0; }
    *out = vm->stack[--vm->sp];
    return 1;
}
static int vm_read_i32(const unsigned char *code, size_t len, size_t *ip, i32 *out) {
    if (*ip + 4 > len) return 0;
    u32 b0 = code[(*ip) + 0];
    u32 b1 = code[(*ip) + 1];
    u32 b2 = code[(*ip) + 2];
    u32 b3 = code[(*ip) + 3];
    u32 u = (b0) | (b1<<8) | (b2<<16) | (b3<<24);
    *out = (i32)u;
    *ip += 4;
    return 1;
}

// Run VM until HALT or error, printing OUT to stdout
static void vm_run(VM *vm) {
    while (vm->ip < vm->code_len) {
        u8 op = vm->code[vm->ip++];
        switch (op) {
            case OP_HALT:
                return;
            case OP_PUSH: {
                i32 imm;
                if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &imm)) { fprintf(stderr,"Truncated PUSH immediate\n"); return; }
                vm_push(vm, imm);
                break;
            }
            case OP_POP: { i32 t; if (!vm_pop(vm, &t)) return; break; }
            case OP_DUP: {
                if (vm->sp == 0) { fprintf(stderr,"DUP on empty stack\n"); return; }
                if (!vm_push(vm, vm->stack[vm->sp-1])) return;
                break;
            }
            case OP_SWAP: {
                if (vm->sp < 2) { fprintf(stderr,"SWAP needs two stack elements\n"); return; }
                i32 a = vm->stack[--vm->sp];
                i32 b = vm->stack[--vm->sp];
                vm->stack[vm->sp++] = a;
                vm->stack[vm->sp++] = b;
                break;
            }
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_MOD: {
                i32 b, a;
                if (!vm_pop(vm, &b) || !vm_pop(vm, &a)) return;
                i32 res = 0;
                if (op == OP_ADD) res = (i32)((i64)a + (i64)b);
                else if (op == OP_SUB) res = (i32)((i64)a - (i64)b);
                else if (op == OP_MUL) res = (i32)((i64)a * (i64)b);
                else if (op == OP_DIV) {
                    if (b == 0) { fprintf(stderr,"Division by zero\n"); return; }
                    res = a / b;
                } else if (op == OP_MOD) {
                    if (b == 0) { fprintf(stderr,"Division by zero (mod)\n"); return; }
                    res = a % b;
                }
                vm_push(vm, res);
                break;
            }
            case OP_NEG: {
                i32 v; if (!vm_pop(vm, &v)) return; vm_push(vm, -v); break;
            }
            case OP_AND:
            case OP_OR:
            case OP_XOR: {
                i32 b,a; if (!vm_pop(vm, &b) || !vm_pop(vm, &a)) return;
                if (op == OP_AND) vm_push(vm, a & b);
                else if (op == OP_OR) vm_push(vm, a | b);
                else vm_push(vm, a ^ b);
                break;
            }
            case OP_NOT: { i32 v; if (!vm_pop(vm, &v)) return; vm_push(vm, ~v); break; }
            case OP_SHL: {
                i32 sh, v; if (!vm_pop(vm, &sh) || !vm_pop(vm, &v)) return;
                u32 uv = (u32)v;
                vm_push(vm, (i32)((uv << (sh & 31))));
                break;
            }
            case OP_SHR: {
                i32 sh, v; if (!vm_pop(vm, &sh) || !vm_pop(vm, &v)) return;
                u32 uv = (u32)v;
                vm_push(vm, (i32)(uv >> (sh & 31)));
                break;
            }
            case OP_EQ:
            case OP_LT:
            case OP_GT: {
                i32 b,a; if (!vm_pop(vm, &b) || !vm_pop(vm, &a)) return;
                int res = 0;
                if (op == OP_EQ) res = (a == b);
                else if (op == OP_LT) res = (a < b);
                else res = (a > b);
                vm_push(vm, res ? 1 : 0);
                break;
            }
            case OP_JMP: {
                i32 addr; if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &addr)) { fprintf(stderr,"Truncated JMP\n"); return; }
                if (addr < 0 || (size_t)addr > vm->code_len) { fprintf(stderr,"JMP out of bounds %d\n", addr); return; }
                vm->ip = (size_t)addr;
                break;
            }
            case OP_JZ: {
                i32 addr; if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &addr)) { fprintf(stderr,"Truncated JZ\n"); return; }
                i32 v; if (!vm_pop(vm, &v)) return;
                if (v == 0) {
                    if (addr < 0 || (size_t)addr > vm->code_len) { fprintf(stderr,"JZ out of bounds %d\n", addr); return; }
                    vm->ip = (size_t)addr;
                }
                break;
            }
            case OP_JNZ: {
                i32 addr; if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &addr)) { fprintf(stderr,"Truncated JNZ\n"); return; }
                i32 v; if (!vm_pop(vm, &v)) return;
                if (v != 0) {
                    if (addr < 0 || (size_t)addr > vm->code_len) { fprintf(stderr,"JNZ out of bounds %d\n", addr); return; }
                    vm->ip = (size_t)addr;
                }
                break;
            }
            case OP_CALL: {
                i32 addr; if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &addr)) { fprintf(stderr,"Truncated CALL\n"); return; }
                if (vm->csp >= CALLSTACK_MAX) { fprintf(stderr,"Call stack overflow\n"); return; }
                if (addr < 0 || (size_t)addr > vm->code_len) { fprintf(stderr,"CALL out of bounds %d\n", addr); return; }
                vm->callstack[vm->csp++] = (i32)vm->ip;
                vm->ip = (size_t)addr;
                break;
            }
            case OP_RET: {
                if (vm->csp == 0) return; // return from top-level ends execution
                vm->ip = (size_t)vm->callstack[--vm->csp];
                break;
            }
            case OP_LOAD: {
                i32 addr;
                if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &addr)) { fprintf(stderr,"Truncated LOAD\n"); return; }
                if (addr < 0 || addr >= MEM_WORDS) { fprintf(stderr,"LOAD out-of-bounds %d\n", addr); return; }
                vm_push(vm, vm->mem[addr]);
                break;
            }
            case OP_STORE: {
                i32 addr; if (!vm_read_i32(vm->code, vm->code_len, &vm->ip, &addr)) { fprintf(stderr,"Truncated STORE\n"); return; }
                if (addr < 0 || addr >= MEM_WORDS) { fprintf(stderr,"STORE out-of-bounds %d\n", addr); return; }
                i32 val; if (!vm_pop(vm, &val)) return;
                vm->mem[addr] = val;
                break;
            }
            case OP_IN: {
                i32 v;
                if (scanf("%d", &v) != 1) { fprintf(stderr,"Failed to read input\n"); return; }
                vm_push(vm, v);
                break;
            }
            case OP_OUT: {
                i32 v; if (!vm_pop(vm, &v)) return;
                printf("%d\n", v);
                fflush(stdout);
                break;
            }
            default:
                fprintf(stderr,"Unknown opcode 0x%02X at ip %zu\n", op, vm->ip-1);
                return;
        }
    }
}

// Example program (same factorial program as before)
// Use NULL-terminated array of strings to feed assembler
const char *program_lines[] = {
    "; compute factorial(n) recursively",
    "; calling convention: push arg, CALL main, result left on stack",
    "    PUSH 5",
    "    CALL main",
    "    OUT",
    "    HALT",
    "main: CALL fact",
    "      RET",
    "",
    "fact: DUP",
    "      PUSH 1",
    "      LT",
    "      JNZ base_case",
    "      DUP",
    "      PUSH 1",
    "      EQ",
    "      JNZ base_case",
    "      DUP",
    "      PUSH 1",
    "      SUB",
    "      CALL fact",
    "      MUL",
    "      RET",
    "base_case: POP",
    "           PUSH 1",
    "           RET",
    NULL
};

int main(void) {
    size_t code_len;
    unsigned char *code = assemble_lines(program_lines, &code_len);
    if (!code) {
        fprintf(stderr,"Assembly failed\n");
        return 1;
    }
    VM vm; vm_init(&vm, code, code_len);
    vm_run(&vm);

    // show some VM post-state (stack and a few memory words)
    printf("\n--- VM final SP = %zu ---\n", vm.sp);
    if (vm.sp > 0) {
        printf("Top of stack: %d\n", vm.stack[vm.sp-1]);
    }
    printf("--- Sample memory (first 8 words) ---\n");
    for (int i=0;i<8;i++) printf("[%d] = %d\n", i, vm.mem[i]);

    free(code);
    return 0;
}

