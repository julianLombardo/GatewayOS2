#include "apps.h"
#include "../memory/heap.h"

// ============================================================
// GATEWAY JAVA IDE - Code editor + interpreter + console
// Supports: classes, methods, variables, if/else, while, for,
//   System.out.println, String, int, arrays, basic OOP
// ============================================================

#define JAVA_MAX_LINES  100
#define JAVA_LINE_LEN   80
#define JAVA_CON_LINES  60
#define JAVA_CON_COLS   76
#define JAVA_MAX_VARS   64
#define JAVA_MAX_STACK  64
#define JAVA_MAX_FUNCS  16
#define JAVA_MAX_STRS   32
#define JAVA_MAX_ARRAYS 8
#define JAVA_ARRAY_MAX  64

// --- Tokenizer ---
enum TokenType {
    T_EOF, T_NUM, T_STR, T_IDENT, T_KEYWORD,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_ASSIGN, T_EQ, T_NEQ, T_LT, T_GT, T_LTE, T_GTE,
    T_AND, T_OR, T_NOT, T_LPAREN, T_RPAREN,
    T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_COMMA, T_DOT, T_PLUSEQ, T_MINUSEQ,
    T_PLUSPLUS, T_MINUSMINUS
};

struct Token {
    TokenType type;
    char text[64];
    int num_val;
};

// --- Runtime value ---
enum ValType { VAL_INT, VAL_STR, VAL_BOOL, VAL_VOID, VAL_ARRAY };

struct Value {
    ValType type;
    int ival;
    char sval[80];
    int array_id; // for VAL_ARRAY
};

// --- Variable ---
struct Variable {
    char name[32];
    Value val;
    int scope; // 0=global, 1+=local scope depth
    bool used;
};

// --- Function/method ---
struct Function {
    char name[32];
    int start_line;    // line in source where body starts
    int param_count;
    char params[4][32]; // param names
    char param_types[4][16];
    bool used;
};

// --- Array storage ---
struct ArrayObj {
    int values[JAVA_ARRAY_MAX];
    int length;
    bool used;
};

// --- Interpreter state ---
struct JavaVM {
    Variable vars[JAVA_MAX_VARS];
    Function funcs[JAVA_MAX_FUNCS];
    ArrayObj arrays[JAVA_MAX_ARRAYS];
    Value stack[JAVA_MAX_STACK];
    int sp;
    int scope;
    bool running;
    bool error;
    char error_msg[80];
    int error_line;

    // Source
    char (*src)[JAVA_LINE_LEN + 1]; // pointer to editor lines
    int src_lines;
    int pc; // current line

    // Console output callback
    void (*print)(void* ctx, const char* text);
    void* print_ctx;
};

// --- IDE State ---
struct JavaIDEState {
    // Editor
    char lines[JAVA_MAX_LINES][JAVA_LINE_LEN + 1];
    int line_count;
    int cursor_x, cursor_y;
    int scroll_x, scroll_y;
    bool modified;

    // Console
    char console[JAVA_CON_LINES][JAVA_CON_COLS + 1];
    int con_count;
    int con_scroll;

    // UI state
    int mode; // 0=editor, 1=console
    bool running;
    int split_y; // pixel height of editor panel

    // VM
    JavaVM vm;

    // Sample programs
    int sample_idx;
};

// --- Forward declarations ---
static void java_run(JavaIDEState* ide);
static void con_print(void* ctx, const char* text);
static void con_clear(JavaIDEState* ide);

// ============================================================
// SAMPLE PROGRAMS
// ============================================================
struct SampleProg {
    const char* name;
    const char* lines[30];
    int count;
};

static const SampleProg samples[] = {
    { "Hello World", {
        "public class Hello {",
        "    public static void main(String[] args) {",
        "        System.out.println(\"Hello, World!\");",
        "        System.out.println(\"From Gateway OS2\");",
        "    }",
        "}"
    }, 6 },
    { "Variables & Math", {
        "public class MathDemo {",
        "    public static void main(String[] args) {",
        "        int a = 42;",
        "        int b = 13;",
        "        int sum = a + b;",
        "        int diff = a - b;",
        "        int prod = a * b;",
        "        int quot = a / b;",
        "        int mod = a % b;",
        "        System.out.println(\"a = \" + a);",
        "        System.out.println(\"b = \" + b);",
        "        System.out.println(\"sum = \" + sum);",
        "        System.out.println(\"diff = \" + diff);",
        "        System.out.println(\"prod = \" + prod);",
        "        System.out.println(\"quot = \" + quot);",
        "        System.out.println(\"mod = \" + mod);",
        "    }",
        "}"
    }, 18 },
    { "Loops & Conditions", {
        "public class LoopDemo {",
        "    public static void main(String[] args) {",
        "        for (int i = 1; i <= 10; i++) {",
        "            if (i % 2 == 0) {",
        "                System.out.println(i + \" is even\");",
        "            } else {",
        "                System.out.println(i + \" is odd\");",
        "            }",
        "        }",
        "        int n = 1;",
        "        while (n <= 100) {",
        "            n = n * 2;",
        "        }",
        "        System.out.println(\"Result: \" + n);",
        "    }",
        "}"
    }, 16 },
    { "FizzBuzz", {
        "public class FizzBuzz {",
        "    public static void main(String[] args) {",
        "        for (int i = 1; i <= 20; i++) {",
        "            if (i % 15 == 0) {",
        "                System.out.println(\"FizzBuzz\");",
        "            } else if (i % 3 == 0) {",
        "                System.out.println(\"Fizz\");",
        "            } else if (i % 5 == 0) {",
        "                System.out.println(\"Buzz\");",
        "            } else {",
        "                System.out.println(i);",
        "            }",
        "        }",
        "    }",
        "}"
    }, 15 },
    { "Fibonacci", {
        "public class Fibonacci {",
        "    public static void main(String[] args) {",
        "        int a = 0;",
        "        int b = 1;",
        "        System.out.println(\"Fibonacci:\");",
        "        for (int i = 0; i < 15; i++) {",
        "            System.out.println(a);",
        "            int temp = a + b;",
        "            a = b;",
        "            b = temp;",
        "        }",
        "    }",
        "}"
    }, 13 },
    { "Prime Sieve", {
        "public class Primes {",
        "    public static void main(String[] args) {",
        "        System.out.println(\"Primes to 50:\");",
        "        for (int n = 2; n <= 50; n++) {",
        "            int isPrime = 1;",
        "            for (int d = 2; d * d <= n; d++) {",
        "                if (n % d == 0) {",
        "                    isPrime = 0;",
        "                }",
        "            }",
        "            if (isPrime == 1) {",
        "                System.out.println(n);",
        "            }",
        "        }",
        "    }",
        "}"
    }, 16 },
    { "Unit Converter", {
        "public class UnitConverter {",
        "    public static void main(String[] args) {",
        "        int miles = 26;",
        "        int km = miles * 1609 / 1000;",
        "        System.out.println(\"=== Unit Converter ===\");",
        "        System.out.println(miles + \" miles = \" + km + \" km\");",
        "        int lbs = 180;",
        "        int kg = lbs * 453 / 1000;",
        "        System.out.println(lbs + \" lbs = \" + kg + \" kg\");",
        "        int fahr = 72;",
        "        int celsius = (fahr - 32) * 5 / 9;",
        "        System.out.println(fahr + \" F = \" + celsius + \" C\");",
        "        int inches = 72;",
        "        int cm = inches * 254 / 100;",
        "        System.out.println(inches + \" in = \" + cm + \" cm\");",
        "        int gallons = 10;",
        "        int liters = gallons * 3785 / 1000;",
        "        System.out.println(gallons + \" gal = \" + liters + \" L\");",
        "    }",
        "}"
    }, 20 },
    { "Multiplication Table", {
        "public class MultTable {",
        "    public static void main(String[] args) {",
        "        System.out.println(\"Multiplication Table\");",
        "        System.out.println(\"====================\");",
        "        for (int i = 1; i <= 10; i++) {",
        "            for (int j = 1; j <= 10; j++) {",
        "                int p = i * j;",
        "                if (p < 10) {",
        "                    System.out.println(\"  \" + p);",
        "                } else if (p < 100) {",
        "                    System.out.println(\" \" + p);",
        "                } else {",
        "                    System.out.println(p);",
        "                }",
        "            }",
        "            System.out.println(\"---\");",
        "        }",
        "    }",
        "}"
    }, 19 },
};
#define NUM_SAMPLES 8

// ============================================================
// CONSOLE
// ============================================================
static void con_clear(JavaIDEState* ide) {
    ide->con_count = 0;
    ide->con_scroll = 0;
    memset(ide->console, 0, sizeof(ide->console));
}

static void con_print(void* ctx, const char* text) {
    JavaIDEState* ide = (JavaIDEState*)ctx;
    if (ide->con_count >= JAVA_CON_LINES) {
        // Shift up
        for (int i = 0; i < JAVA_CON_LINES - 1; i++)
            memcpy(ide->console[i], ide->console[i + 1], JAVA_CON_COLS + 1);
        ide->con_count = JAVA_CON_LINES - 1;
    }
    strncpy(ide->console[ide->con_count], text, JAVA_CON_COLS);
    ide->console[ide->con_count][JAVA_CON_COLS] = 0;
    ide->con_count++;
    // Auto-scroll
    int vis = 8; // approximate visible lines in console
    if (ide->con_count > vis)
        ide->con_scroll = ide->con_count - vis;
}

// ============================================================
// TOKENIZER
// ============================================================
static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

static bool is_keyword(const char* s) {
    const char* kw[] = {"int", "String", "boolean", "void", "if", "else", "while", "for",
                        "return", "true", "false", "new", "public", "static", "class",
                        "System", "out", "println", "print", "length", "null"};
    for (int i = 0; i < 21; i++)
        if (strcmp(s, kw[i]) == 0) return true;
    return false;
}

struct Lexer {
    const char* src;
    int pos;
    int len;
};

static Token lex_next(Lexer* l) {
    Token t;
    memset(&t, 0, sizeof(t));

    // Skip whitespace
    while (l->pos < l->len && (l->src[l->pos] == ' ' || l->src[l->pos] == '\t'))
        l->pos++;

    if (l->pos >= l->len) { t.type = T_EOF; return t; }

    char c = l->src[l->pos];

    // String literal
    if (c == '"') {
        l->pos++;
        int si = 0;
        while (l->pos < l->len && l->src[l->pos] != '"' && si < 62) {
            if (l->src[l->pos] == '\\' && l->pos + 1 < l->len) {
                l->pos++;
                if (l->src[l->pos] == 'n') t.text[si++] = '\n';
                else if (l->src[l->pos] == 't') t.text[si++] = '\t';
                else if (l->src[l->pos] == '"') t.text[si++] = '"';
                else if (l->src[l->pos] == '\\') t.text[si++] = '\\';
                else t.text[si++] = l->src[l->pos];
            } else {
                t.text[si++] = l->src[l->pos];
            }
            l->pos++;
        }
        if (l->pos < l->len) l->pos++; // skip closing "
        t.text[si] = 0;
        t.type = T_STR;
        return t;
    }

    // Number
    if (is_digit(c)) {
        int si = 0;
        while (l->pos < l->len && is_digit(l->src[l->pos]) && si < 20) {
            t.text[si++] = l->src[l->pos++];
        }
        t.text[si] = 0;
        t.num_val = 0;
        for (int i = 0; t.text[i]; i++) t.num_val = t.num_val * 10 + (t.text[i] - '0');
        t.type = T_NUM;
        return t;
    }

    // Identifier / keyword
    if (is_alpha(c)) {
        int si = 0;
        while (l->pos < l->len && is_alnum(l->src[l->pos]) && si < 62) {
            t.text[si++] = l->src[l->pos++];
        }
        t.text[si] = 0;
        t.type = is_keyword(t.text) ? T_KEYWORD : T_IDENT;
        // boolean literals
        if (strcmp(t.text, "true") == 0) { t.type = T_NUM; t.num_val = 1; }
        if (strcmp(t.text, "false") == 0) { t.type = T_NUM; t.num_val = 0; }
        return t;
    }

    // Operators
    l->pos++;
    t.text[0] = c; t.text[1] = 0;
    char nc = (l->pos < l->len) ? l->src[l->pos] : 0;

    switch (c) {
        case '+':
            if (nc == '+') { l->pos++; t.type = T_PLUSPLUS; strcpy(t.text, "++"); }
            else if (nc == '=') { l->pos++; t.type = T_PLUSEQ; strcpy(t.text, "+="); }
            else t.type = T_PLUS;
            break;
        case '-':
            if (nc == '-') { l->pos++; t.type = T_MINUSMINUS; strcpy(t.text, "--"); }
            else if (nc == '=') { l->pos++; t.type = T_MINUSEQ; strcpy(t.text, "-="); }
            else t.type = T_MINUS;
            break;
        case '*': t.type = T_STAR; break;
        case '/':
            if (nc == '/') { t.type = T_EOF; l->pos = l->len; break; } // line comment
            t.type = T_SLASH; break;
        case '%': t.type = T_PERCENT; break;
        case '=':
            if (nc == '=') { l->pos++; t.type = T_EQ; strcpy(t.text, "=="); }
            else t.type = T_ASSIGN;
            break;
        case '!':
            if (nc == '=') { l->pos++; t.type = T_NEQ; strcpy(t.text, "!="); }
            else t.type = T_NOT;
            break;
        case '<':
            if (nc == '=') { l->pos++; t.type = T_LTE; strcpy(t.text, "<="); }
            else t.type = T_LT;
            break;
        case '>':
            if (nc == '=') { l->pos++; t.type = T_GTE; strcpy(t.text, ">="); }
            else t.type = T_GT;
            break;
        case '&':
            if (nc == '&') { l->pos++; t.type = T_AND; strcpy(t.text, "&&"); }
            break;
        case '|':
            if (nc == '|') { l->pos++; t.type = T_OR; strcpy(t.text, "||"); }
            break;
        case '(': t.type = T_LPAREN; break;
        case ')': t.type = T_RPAREN; break;
        case '{': t.type = T_LBRACE; break;
        case '}': t.type = T_RBRACE; break;
        case '[': t.type = T_LBRACKET; break;
        case ']': t.type = T_RBRACKET; break;
        case ';': t.type = T_SEMI; break;
        case ',': t.type = T_COMMA; break;
        case '.': t.type = T_DOT; break;
        default: t.type = T_EOF; break;
    }
    return t;
}

// ============================================================
// INTERPRETER - Recursive descent, executes line by line
// ============================================================
static Variable* vm_find_var(JavaVM* vm, const char* name) {
    // Search innermost scope first
    for (int s = vm->scope; s >= 0; s--) {
        for (int i = 0; i < JAVA_MAX_VARS; i++) {
            if (vm->vars[i].used && strcmp(vm->vars[i].name, name) == 0 && vm->vars[i].scope <= s)
                return &vm->vars[i];
        }
    }
    return nullptr;
}

static Variable* vm_create_var(JavaVM* vm, const char* name) {
    for (int i = 0; i < JAVA_MAX_VARS; i++) {
        if (!vm->vars[i].used) {
            vm->vars[i].used = true;
            vm->vars[i].scope = vm->scope;
            strcpy(vm->vars[i].name, name);
            memset(&vm->vars[i].val, 0, sizeof(Value));
            return &vm->vars[i];
        }
    }
    return nullptr;
}

static void vm_free_scope(JavaVM* vm, int scope) {
    for (int i = 0; i < JAVA_MAX_VARS; i++) {
        if (vm->vars[i].used && vm->vars[i].scope >= scope)
            vm->vars[i].used = false;
    }
}

static int vm_alloc_array(JavaVM* vm, int length) {
    for (int i = 0; i < JAVA_MAX_ARRAYS; i++) {
        if (!vm->arrays[i].used) {
            vm->arrays[i].used = true;
            vm->arrays[i].length = length;
            memset(vm->arrays[i].values, 0, sizeof(vm->arrays[i].values));
            return i;
        }
    }
    return -1;
}

static void vm_error(JavaVM* vm, const char* msg) {
    vm->error = true;
    vm->running = false;
    vm->error_line = vm->pc;
    strncpy(vm->error_msg, msg, 79);
}

// Forward declarations for expression parser
static Value eval_expr(JavaVM* vm, Lexer* l);
static Value eval_compare(JavaVM* vm, Lexer* l);
static Value eval_add(JavaVM* vm, Lexer* l);
static Value eval_mul(JavaVM* vm, Lexer* l);
static Value eval_unary(JavaVM* vm, Lexer* l);
static Value eval_primary(JavaVM* vm, Lexer* l);
static void exec_block(JavaVM* vm);
static void exec_statement(JavaVM* vm, const char* line);

static Value eval_primary(JavaVM* vm, Lexer* l) {
    Value v; memset(&v, 0, sizeof(v));
    if (vm->error) return v;

    Token t = lex_next(l);

    if (t.type == T_NUM) {
        v.type = VAL_INT; v.ival = t.num_val;
        return v;
    }

    if (t.type == T_STR) {
        v.type = VAL_STR; strcpy(v.sval, t.text);
        return v;
    }

    if (t.type == T_LPAREN) {
        v = eval_expr(vm, l);
        Token cl = lex_next(l); // consume )
        (void)cl;
        return v;
    }

    if (t.type == T_MINUS) {
        v = eval_primary(vm, l);
        v.ival = -v.ival;
        return v;
    }

    if (t.type == T_NOT) {
        v = eval_primary(vm, l);
        v.ival = !v.ival;
        v.type = VAL_BOOL;
        return v;
    }

    if (t.type == T_KEYWORD && strcmp(t.text, "new") == 0) {
        // new int[size]
        lex_next(l); // type name
        lex_next(l); // [
        Value sz = eval_expr(vm, l);
        lex_next(l); // ]
        int id = vm_alloc_array(vm, sz.ival);
        if (id < 0) { vm_error(vm, "Too many arrays"); return v; }
        v.type = VAL_ARRAY; v.array_id = id;
        return v;
    }

    if (t.type == T_IDENT || t.type == T_KEYWORD) {
        // Check for System.out.println
        if (strcmp(t.text, "System") == 0) {
            // Already handled at statement level
            v.type = VAL_VOID;
            return v;
        }

        // Variable lookup
        Variable* var = vm_find_var(vm, t.text);
        if (!var) {
            char err[80]; ksprintf(err, "Undefined: %s", t.text);
            vm_error(vm, err);
            return v;
        }

        // Check for array access: name[idx]
        int saved_pos = l->pos;
        Token peek = lex_next(l);
        if (peek.type == T_LBRACKET) {
            Value idx = eval_expr(vm, l);
            lex_next(l); // ]
            if (var->val.type == VAL_ARRAY) {
                int aid = var->val.array_id;
                if (aid >= 0 && aid < JAVA_MAX_ARRAYS && vm->arrays[aid].used) {
                    if (idx.ival >= 0 && idx.ival < vm->arrays[aid].length) {
                        v.type = VAL_INT;
                        v.ival = vm->arrays[aid].values[idx.ival];
                    } else { vm_error(vm, "Array index out of bounds"); }
                }
            }
            return v;
        } else if (peek.type == T_DOT) {
            // array.length
            Token field = lex_next(l);
            if (strcmp(field.text, "length") == 0 && var->val.type == VAL_ARRAY) {
                int aid = var->val.array_id;
                v.type = VAL_INT;
                v.ival = (aid >= 0 && aid < JAVA_MAX_ARRAYS) ? vm->arrays[aid].length : 0;
            }
            return v;
        } else {
            l->pos = saved_pos; // put back
        }

        v = var->val;
        return v;
    }

    return v;
}

static Value eval_unary(JavaVM* vm, Lexer* l) {
    return eval_primary(vm, l);
}

static Value eval_mul(JavaVM* vm, Lexer* l) {
    Value left = eval_unary(vm, l);
    while (!vm->error) {
        int saved = l->pos;
        Token op = lex_next(l);
        if (op.type == T_STAR) {
            Value right = eval_unary(vm, l);
            left.ival *= right.ival; left.type = VAL_INT;
        } else if (op.type == T_SLASH) {
            Value right = eval_unary(vm, l);
            if (right.ival == 0) { vm_error(vm, "Division by zero"); return left; }
            left.ival /= right.ival; left.type = VAL_INT;
        } else if (op.type == T_PERCENT) {
            Value right = eval_unary(vm, l);
            if (right.ival == 0) { vm_error(vm, "Modulo by zero"); return left; }
            left.ival %= right.ival; left.type = VAL_INT;
        } else {
            l->pos = saved; break;
        }
    }
    return left;
}

static Value eval_add(JavaVM* vm, Lexer* l) {
    Value left = eval_mul(vm, l);
    while (!vm->error) {
        int saved = l->pos;
        Token op = lex_next(l);
        if (op.type == T_PLUS) {
            Value right = eval_mul(vm, l);
            // String concatenation
            if (left.type == VAL_STR || right.type == VAL_STR) {
                char buf[80];
                if (left.type == VAL_STR && right.type == VAL_STR) {
                    ksprintf(buf, "%s%s", left.sval, right.sval);
                } else if (left.type == VAL_STR) {
                    ksprintf(buf, "%s%d", left.sval, right.ival);
                } else {
                    ksprintf(buf, "%d%s", left.ival, right.sval);
                }
                left.type = VAL_STR;
                strncpy(left.sval, buf, 79); left.sval[79] = 0;
            } else {
                left.ival += right.ival; left.type = VAL_INT;
            }
        } else if (op.type == T_MINUS) {
            Value right = eval_mul(vm, l);
            left.ival -= right.ival; left.type = VAL_INT;
        } else {
            l->pos = saved; break;
        }
    }
    return left;
}

static Value eval_compare(JavaVM* vm, Lexer* l) {
    Value left = eval_add(vm, l);
    while (!vm->error) {
        int saved = l->pos;
        Token op = lex_next(l);
        if (op.type == T_EQ) {
            Value right = eval_add(vm, l);
            if (left.type == VAL_STR && right.type == VAL_STR)
                left.ival = (strcmp(left.sval, right.sval) == 0);
            else left.ival = (left.ival == right.ival);
            left.type = VAL_BOOL;
        } else if (op.type == T_NEQ) {
            Value right = eval_add(vm, l);
            left.ival = (left.ival != right.ival); left.type = VAL_BOOL;
        } else if (op.type == T_LT) {
            Value right = eval_add(vm, l);
            left.ival = (left.ival < right.ival); left.type = VAL_BOOL;
        } else if (op.type == T_GT) {
            Value right = eval_add(vm, l);
            left.ival = (left.ival > right.ival); left.type = VAL_BOOL;
        } else if (op.type == T_LTE) {
            Value right = eval_add(vm, l);
            left.ival = (left.ival <= right.ival); left.type = VAL_BOOL;
        } else if (op.type == T_GTE) {
            Value right = eval_add(vm, l);
            left.ival = (left.ival >= right.ival); left.type = VAL_BOOL;
        } else {
            l->pos = saved; break;
        }
    }
    return left;
}

static Value eval_expr(JavaVM* vm, Lexer* l) {
    Value left = eval_compare(vm, l);
    while (!vm->error) {
        int saved = l->pos;
        Token op = lex_next(l);
        if (op.type == T_AND) {
            Value right = eval_compare(vm, l);
            left.ival = (left.ival && right.ival); left.type = VAL_BOOL;
        } else if (op.type == T_OR) {
            Value right = eval_compare(vm, l);
            left.ival = (left.ival || right.ival); left.type = VAL_BOOL;
        } else {
            l->pos = saved; break;
        }
    }
    return left;
}

// Skip to matching brace (handles nesting)
static int find_matching_brace(JavaVM* vm, int from) {
    int depth = 0;
    for (int i = from; i < vm->src_lines; i++) {
        for (int j = 0; vm->src[i][j]; j++) {
            if (vm->src[i][j] == '{') depth++;
            if (vm->src[i][j] == '}') { depth--; if (depth == 0) return i; }
        }
    }
    return vm->src_lines;
}

// Trim leading whitespace
static const char* trim(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Check if line starts with a prefix (after trimming)
static bool starts_with(const char* line, const char* prefix) {
    line = trim(line);
    int plen = strlen(prefix);
    for (int i = 0; i < plen; i++) {
        if (line[i] != prefix[i]) return false;
    }
    return true;
}

static void exec_statement(JavaVM* vm, const char* line) {
    if (vm->error || !vm->running) return;

    line = trim(line);
    if (line[0] == 0 || line[0] == '/' || line[0] == '{' || line[0] == '}') return;
    if (starts_with(line, "public ") || starts_with(line, "class ") ||
        starts_with(line, "static ")) return; // skip declarations

    // System.out.println(...)
    if (starts_with(line, "System.out.println(") || starts_with(line, "System.out.print(")) {
        bool newline = starts_with(line, "System.out.println(");
        const char* p = line;
        while (*p && *p != '(') p++;
        if (*p == '(') p++;
        // Find the matching )
        char expr_buf[JAVA_LINE_LEN];
        int ei = 0;
        int depth = 1;
        while (*p && depth > 0 && ei < JAVA_LINE_LEN - 1) {
            if (*p == '(') depth++;
            if (*p == ')') { depth--; if (depth == 0) break; }
            expr_buf[ei++] = *p++;
        }
        expr_buf[ei] = 0;

        Lexer lex = { expr_buf, 0, ei };
        Value v = eval_expr(vm, &lex);

        char out[80];
        if (v.type == VAL_STR) ksprintf(out, "%s", v.sval);
        else if (v.type == VAL_BOOL) ksprintf(out, "%s", v.ival ? "true" : "false");
        else ksprintf(out, "%d", v.ival);

        if (newline) vm->print(vm->print_ctx, out);
        else { /* for print without newline - just add to console too */ vm->print(vm->print_ctx, out); }
        return;
    }

    // Variable declaration: int x = expr; or String s = expr;
    if (starts_with(line, "int ") || starts_with(line, "String ") || starts_with(line, "boolean ")) {
        const char* p = line;
        // Skip type
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        // Check for array: int[] name = new int[size]
        bool is_array = false;
        if (p > line + 2 && *(p - 2) == '[' && *(p - 1) == ']') {
            // Actually need to re-parse: "int[] name"
        }
        // Simpler: check if line has "[]"
        if (strncmp(trim(line), "int[]", 5) == 0) {
            is_array = true;
            p = trim(line) + 5;
            while (*p == ' ') p++;
        }

        char var_name[32];
        int ni = 0;
        while (*p && is_alnum(*p) && ni < 30) var_name[ni++] = *p++;
        var_name[ni] = 0;

        while (*p == ' ') p++;

        Variable* var = vm_create_var(vm, var_name);
        if (!var) { vm_error(vm, "Too many variables"); return; }

        if (*p == '=') {
            p++;
            while (*p == ' ') p++;
            // Find ;
            char expr_buf[JAVA_LINE_LEN];
            int ei = 0;
            while (*p && *p != ';' && ei < JAVA_LINE_LEN - 1) expr_buf[ei++] = *p++;
            expr_buf[ei] = 0;

            Lexer lex = { expr_buf, 0, ei };
            var->val = eval_expr(vm, &lex);
        } else {
            if (is_array) { var->val.type = VAL_ARRAY; var->val.array_id = -1; }
            else { var->val.type = VAL_INT; var->val.ival = 0; }
        }
        return;
    }

    // Assignment: x = expr; or x += expr; or x++; x--;
    // Also: arr[idx] = expr;
    {
        Lexer lex = { line, 0, (int)strlen(line) };
        Token first = lex_next(&lex);
        if (first.type == T_IDENT) {
            int saved = lex.pos;
            Token op = lex_next(&lex);

            // x++ or x--
            if (op.type == T_PLUSPLUS || op.type == T_MINUSMINUS) {
                Variable* var = vm_find_var(vm, first.text);
                if (var) {
                    if (op.type == T_PLUSPLUS) var->val.ival++;
                    else var->val.ival--;
                }
                return;
            }

            // arr[idx] = expr
            if (op.type == T_LBRACKET) {
                Value idx = eval_expr(vm, &lex);
                lex_next(&lex); // ]
                Token eq = lex_next(&lex);
                if (eq.type == T_ASSIGN) {
                    Value val = eval_expr(vm, &lex);
                    Variable* var = vm_find_var(vm, first.text);
                    if (var && var->val.type == VAL_ARRAY) {
                        int aid = var->val.array_id;
                        if (aid >= 0 && aid < JAVA_MAX_ARRAYS && vm->arrays[aid].used) {
                            if (idx.ival >= 0 && idx.ival < vm->arrays[aid].length)
                                vm->arrays[aid].values[idx.ival] = val.ival;
                        }
                    }
                }
                return;
            }

            // x = expr
            if (op.type == T_ASSIGN) {
                Variable* var = vm_find_var(vm, first.text);
                if (!var) {
                    char err[80]; ksprintf(err, "Undefined: %s", first.text);
                    vm_error(vm, err); return;
                }
                var->val = eval_expr(vm, &lex);
                return;
            }

            // x += expr or x -= expr
            if (op.type == T_PLUSEQ || op.type == T_MINUSEQ) {
                Variable* var = vm_find_var(vm, first.text);
                if (!var) { vm_error(vm, "Undefined variable"); return; }
                Value v = eval_expr(vm, &lex);
                if (op.type == T_PLUSEQ) var->val.ival += v.ival;
                else var->val.ival -= v.ival;
                return;
            }

            lex.pos = saved;
        }
    }
}

// Execute a block of lines (between { and })
static void exec_block(JavaVM* vm) {
    while (vm->pc < vm->src_lines && vm->running && !vm->error) {
        const char* line = trim(vm->src[vm->pc]);

        if (line[0] == '}') { vm->pc++; return; }

        // if statement
        if (starts_with(line, "if (") || starts_with(line, "if(")) {
            const char* p = line;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;
            char cond_buf[JAVA_LINE_LEN];
            int ci = 0;
            int depth = 1;
            while (*p && depth > 0 && ci < JAVA_LINE_LEN - 1) {
                if (*p == '(') depth++;
                if (*p == ')') { depth--; if (depth == 0) break; }
                cond_buf[ci++] = *p++;
            }
            cond_buf[ci] = 0;

            Lexer lex = { cond_buf, 0, ci };
            Value cond = eval_expr(vm, &lex);

            // Find the { on same line or next
            vm->pc++;
            while (vm->pc < vm->src_lines && !strchr(vm->src[vm->pc], '{') &&
                   !strchr(vm->src[vm->pc - 1], '{')) vm->pc++;
            // pc should now be at or past the {
            // Check if { was on the if line
            if (strchr(line, '{')) {
                // { was on the if line, pc is already at next line
            }

            if (cond.ival) {
                vm->scope++;
                exec_block(vm);
                vm_free_scope(vm, vm->scope);
                vm->scope--;

                // Check for else
                if (vm->pc < vm->src_lines) {
                    const char* next = trim(vm->src[vm->pc]);
                    if (starts_with(next, "} else") || starts_with(next, "else")) {
                        // Skip else block
                        if (starts_with(next, "} else if")) {
                            // else if - skip entire chain
                            int end = find_matching_brace(vm, vm->pc);
                            vm->pc = end + 1;
                            // May be more else/else-if
                            while (vm->pc < vm->src_lines && starts_with(trim(vm->src[vm->pc]), "else")) {
                                end = find_matching_brace(vm, vm->pc);
                                vm->pc = end + 1;
                            }
                        } else {
                            // Skip else body
                            vm->pc++;
                            int end = find_matching_brace(vm, vm->pc - 1);
                            vm->pc = end + 1;
                        }
                    }
                }
            } else {
                // Skip if body
                int end = find_matching_brace(vm, vm->pc - 1);
                vm->pc = end + 1;

                // Check for else
                if (vm->pc < vm->src_lines) {
                    const char* next = trim(vm->src[vm->pc]);
                    if (starts_with(next, "} else if") || starts_with(next, "else if")) {
                        // Recurse as new if
                        // Adjust line to look like "if (...)"
                        const char* eif = next;
                        while (*eif && !(*eif == 'i' && *(eif+1) == 'f')) eif++;
                        // Create temp adjusted line
                        // Just continue - the next iteration will process it
                        continue;
                    } else if (starts_with(next, "} else") || starts_with(next, "else")) {
                        vm->pc++;
                        vm->scope++;
                        exec_block(vm);
                        vm_free_scope(vm, vm->scope);
                        vm->scope--;
                    }
                }
            }
            continue;
        }

        // while loop
        if (starts_with(line, "while (") || starts_with(line, "while(")) {
            const char* p = line;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;
            char cond_buf[JAVA_LINE_LEN];
            int ci = 0;
            int depth = 1;
            while (*p && depth > 0 && ci < JAVA_LINE_LEN - 1) {
                if (*p == '(') depth++;
                if (*p == ')') { depth--; if (depth == 0) break; }
                cond_buf[ci++] = *p++;
            }
            cond_buf[ci] = 0;

            int body_start = vm->pc + 1;
            int body_end = find_matching_brace(vm, vm->pc);
            int iterations = 0;

            while (iterations < 10000) {
                Lexer lex = { cond_buf, 0, ci };
                Value cond = eval_expr(vm, &lex);
                if (vm->error || !cond.ival) break;

                vm->pc = body_start;
                vm->scope++;
                exec_block(vm);
                vm_free_scope(vm, vm->scope);
                vm->scope--;
                iterations++;
            }
            if (iterations >= 10000) vm_error(vm, "Infinite loop detected");
            vm->pc = body_end + 1;
            continue;
        }

        // for loop: for (init; cond; incr) { ... }
        if (starts_with(line, "for (") || starts_with(line, "for(")) {
            const char* p = line;
            while (*p && *p != '(') p++;
            if (*p == '(') p++;

            // Parse three parts separated by ;
            char init_buf[JAVA_LINE_LEN] = {0};
            char cond_buf[JAVA_LINE_LEN] = {0};
            char incr_buf[JAVA_LINE_LEN] = {0};
            int bi = 0, part = 0;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                if (*p == ')') { depth--; if (depth == 0) break; }
                if (*p == ';' && depth == 1) {
                    if (part == 0) { init_buf[bi] = 0; bi = 0; part = 1; }
                    else if (part == 1) { cond_buf[bi] = 0; bi = 0; part = 2; }
                    p++; continue;
                }
                if (part == 0 && bi < JAVA_LINE_LEN - 1) init_buf[bi++] = *p;
                else if (part == 1 && bi < JAVA_LINE_LEN - 1) cond_buf[bi++] = *p;
                else if (part == 2 && bi < JAVA_LINE_LEN - 1) incr_buf[bi++] = *p;
                p++;
            }
            if (part == 2) incr_buf[bi] = 0;

            int body_start = vm->pc + 1;
            int body_end = find_matching_brace(vm, vm->pc);

            vm->scope++;
            // Execute init
            exec_statement(vm, init_buf);

            int iterations = 0;
            int cond_len = strlen(cond_buf);
            int incr_len = strlen(incr_buf);

            while (iterations < 10000 && !vm->error) {
                Lexer lex = { cond_buf, 0, cond_len };
                Value cond = eval_expr(vm, &lex);
                if (vm->error || !cond.ival) break;

                vm->pc = body_start;
                vm->scope++;
                exec_block(vm);
                vm_free_scope(vm, vm->scope);
                vm->scope--;

                // Execute increment
                Lexer ilex = { incr_buf, 0, incr_len };
                Token it = lex_next(&ilex);
                if (it.type == T_IDENT) {
                    Token op = lex_next(&ilex);
                    Variable* var = vm_find_var(vm, it.text);
                    if (var) {
                        if (op.type == T_PLUSPLUS) var->val.ival++;
                        else if (op.type == T_MINUSMINUS) var->val.ival--;
                        else if (op.type == T_PLUSEQ) {
                            Value rv = eval_expr(vm, &ilex);
                            var->val.ival += rv.ival;
                        } else if (op.type == T_MINUSEQ) {
                            Value rv = eval_expr(vm, &ilex);
                            var->val.ival -= rv.ival;
                        } else if (op.type == T_ASSIGN) {
                            var->val = eval_expr(vm, &ilex);
                        }
                    }
                }
                iterations++;
            }
            if (iterations >= 10000) vm_error(vm, "Infinite loop detected");

            vm_free_scope(vm, vm->scope);
            vm->scope--;
            vm->pc = body_end + 1;
            continue;
        }

        exec_statement(vm, line);
        vm->pc++;
    }
}

static void java_run(JavaIDEState* ide) {
    JavaVM* vm = &ide->vm;

    // Reset VM
    memset(vm->vars, 0, sizeof(vm->vars));
    memset(vm->funcs, 0, sizeof(vm->funcs));
    memset(vm->arrays, 0, sizeof(vm->arrays));
    vm->sp = 0;
    vm->scope = 0;
    vm->running = true;
    vm->error = false;
    vm->error_msg[0] = 0;
    vm->error_line = -1;
    vm->src = ide->lines;
    vm->src_lines = ide->line_count;
    vm->print = con_print;
    vm->print_ctx = ide;
    vm->pc = 0;

    con_clear(ide);
    con_print(ide, "--- Program Output ---");

    // Find main method and execute from there
    int main_start = -1;
    for (int i = 0; i < ide->line_count; i++) {
        if (starts_with(trim(ide->lines[i]), "public static void main")) {
            main_start = i + 1; // line after main declaration
            break;
        }
    }

    if (main_start < 0) {
        // No main - just execute all lines
        vm->pc = 0;
        exec_block(vm);
    } else {
        vm->pc = main_start;
        exec_block(vm);
    }

    if (vm->error) {
        char err[80];
        ksprintf(err, "ERROR line %d: %s", vm->error_line + 1, vm->error_msg);
        con_print(ide, err);
    }

    con_print(ide, "--- Done ---");
    ide->running = false;
}

// ============================================================
// SYNTAX HIGHLIGHTING COLORS
// ============================================================
static uint32_t syntax_color(const char* word) {
    // Keywords
    const char* kw[] = {"public", "static", "void", "class", "int", "String",
                        "boolean", "if", "else", "while", "for", "return",
                        "new", "true", "false", "null"};
    for (int i = 0; i < 16; i++)
        if (strcmp(word, kw[i]) == 0) return RGB(200, 120, 255); // purple

    if (strcmp(word, "System") == 0 || strcmp(word, "out") == 0 ||
        strcmp(word, "println") == 0 || strcmp(word, "print") == 0 ||
        strcmp(word, "length") == 0)
        return RGB(100, 200, 255); // cyan - stdlib

    return 0; // not a keyword
}

// ============================================================
// DRAWING
// ============================================================
static void java_draw(Window* win, int cx, int cy, int cw, int ch) {
    JavaIDEState* ide = (JavaIDEState*)win->userdata;
    if (!ide) return;

    uint32_t bg_editor = RGB(30, 30, 40);
    uint32_t bg_console = RGB(15, 15, 20);
    uint32_t bg_gutter = RGB(40, 40, 55);
    uint32_t fg_text = RGB(220, 220, 230);
    uint32_t fg_linenum = RGB(90, 90, 120);
    uint32_t fg_string = RGB(180, 220, 100);
    uint32_t fg_number = RGB(255, 180, 100);
    uint32_t fg_comment = RGB(80, 110, 80);
    uint32_t fg_bracket = RGB(200, 200, 100);
    uint32_t fg_cursor = RGB(255, 255, 255);

    int toolbar_h = 22;
    int status_h = 14;
    int console_h = ch / 4;
    if (console_h < 60) console_h = 60;
    int editor_h = ch - toolbar_h - console_h - status_h - 2;
    int font_w = font_char_width(FONT_SMALL);
    int font_h = 10; // line height
    int gutter_w = 30;

    // --- Toolbar ---
    fb_fillrect(cx, cy, cw, toolbar_h, RGB(50, 50, 65));
    fb_hline(cx, cy + toolbar_h - 1, cw, RGB(70, 70, 90));

    // Buttons
    int bx = cx + 4;
    nx_draw_button(bx, cy + 2, 38, 18, "Run", false, true); bx += 42;
    nx_draw_button(bx, cy + 2, 44, 18, "Clear", false, false); bx += 48;
    nx_draw_button(bx, cy + 2, 36, 18, "New", false, false); bx += 40;

    // Sample selector
    bx += 8;
    nx_draw_button(bx, cy + 2, 12, 18, "<", false, false); bx += 14;
    char samp[40];
    ksprintf(samp, "Sample: %s", samples[ide->sample_idx].name);
    font_draw_string(bx + 2, cy + 6, samp, RGB(180, 180, 200), RGB(50, 50, 65), FONT_SMALL);
    bx += strlen(samp) * font_w + 6;
    nx_draw_button(bx, cy + 2, 12, 18, ">", false, false);

    // Title
    const char* title = "Gateway Java IDE";
    int tw = strlen(title) * font_char_width(FONT_SMALL);
    font_draw_string(cx + cw - tw - 8, cy + 6, title, RGB(120, 120, 160), RGB(50, 50, 65), FONT_SMALL);

    int ey = cy + toolbar_h;

    // --- Editor ---
    fb_fillrect(cx, ey, cw, editor_h, bg_editor);

    int visible_rows = editor_h / font_h;
    int visible_cols = (cw - gutter_w - 4) / font_w;

    // Gutter
    fb_fillrect(cx, ey, gutter_w, editor_h, bg_gutter);
    fb_vline(cx + gutter_w, ey, editor_h, RGB(60, 60, 80));

    for (int i = 0; i < visible_rows; i++) {
        int line_idx = ide->scroll_y + i;
        if (line_idx >= ide->line_count) break;
        int ly = ey + i * font_h;

        // Line number
        char lnum[8]; ksprintf(lnum, "%3d", line_idx + 1);
        font_draw_string(cx + 2, ly + 1, lnum, fg_linenum, bg_gutter, FONT_SMALL);

        // Current line highlight
        if (line_idx == ide->cursor_y && ide->mode == 0) {
            fb_fillrect(cx + gutter_w + 1, ly, cw - gutter_w - 1, font_h, RGB(40, 40, 55));
        }

        // Error line highlight
        if (ide->vm.error && line_idx == ide->vm.error_line) {
            fb_fillrect(cx + gutter_w + 1, ly, cw - gutter_w - 1, font_h, RGB(80, 20, 20));
        }

        // Syntax-highlighted text
        const char* text = ide->lines[line_idx];
        int tx = cx + gutter_w + 4;
        int col = 0;
        bool in_string = false;
        bool in_comment = false;

        // Check for // comment
        for (int j = 0; text[j] && text[j + 1]; j++) {
            if (text[j] == '/' && text[j + 1] == '/') { in_comment = true; break; }
        }

        int j = 0;
        while (text[j] && col < ide->scroll_x + visible_cols) {
            if (col >= ide->scroll_x) {
                int px = tx + (col - ide->scroll_x) * font_w;
                uint32_t color = fg_text;

                if (in_comment) {
                    // Check if we're at or past the //
                    const char* cc = text;
                    int cpos = -1;
                    for (int k = 0; cc[k] && cc[k+1]; k++) {
                        if (cc[k] == '/' && cc[k+1] == '/') { cpos = k; break; }
                    }
                    if (cpos >= 0 && j >= cpos) color = fg_comment;
                }

                if (text[j] == '"') { in_string = !in_string; color = fg_string; }
                else if (in_string) color = fg_string;
                else if (is_digit(text[j]) && !in_string) color = fg_number;
                else if (text[j] == '{' || text[j] == '}' || text[j] == '(' ||
                         text[j] == ')' || text[j] == '[' || text[j] == ']')
                    color = fg_bracket;
                else if (is_alpha(text[j]) && !in_string) {
                    // Check if it's a keyword
                    char word[32]; int wi = 0;
                    int save_j = j;
                    while (text[j] && is_alnum(text[j]) && wi < 30) word[wi++] = text[j++];
                    word[wi] = 0;
                    j = save_j;
                    uint32_t kc = syntax_color(word);
                    if (kc) color = kc;
                }

                uint32_t line_bg = (line_idx == ide->cursor_y && ide->mode == 0) ? RGB(40, 40, 55) : bg_editor;
                if (ide->vm.error && line_idx == ide->vm.error_line) line_bg = RGB(80, 20, 20);
                font_draw_char(px, ly + 1, text[j], color, line_bg, FONT_SMALL);
            }
            j++; col++;
        }
    }

    // Cursor (in editor mode)
    if (ide->mode == 0 && (timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = cx + gutter_w + 4 + (ide->cursor_x - ide->scroll_x) * font_w;
        int cy2 = ey + (ide->cursor_y - ide->scroll_y) * font_h;
        if (cx2 >= cx + gutter_w && cx2 < cx + cw && cy2 >= ey && cy2 < ey + editor_h) {
            fb_fillrect(cx2, cy2, 2, font_h, fg_cursor);
        }
    }

    // --- Separator ---
    int sep_y = ey + editor_h;
    fb_fillrect(cx, sep_y, cw, 2, RGB(70, 70, 90));

    // --- Console ---
    int cony = sep_y + 2;
    fb_fillrect(cx, cony, cw, console_h, bg_console);

    // Console header
    font_draw_string(cx + 4, cony + 1, "Console", RGB(100, 100, 140), bg_console, FONT_SMALL);
    fb_hline(cx + 2, cony + 11, cw - 4, RGB(40, 40, 60));

    int con_visible = (console_h - 14) / font_h;
    for (int i = 0; i < con_visible; i++) {
        int ci = ide->con_scroll + i;
        if (ci >= ide->con_count) break;
        uint32_t cc = RGB(180, 220, 180);
        if (starts_with(ide->console[ci], "ERROR")) cc = RGB(255, 100, 100);
        if (starts_with(ide->console[ci], "---")) cc = RGB(100, 140, 180);
        font_draw_string(cx + 4, cony + 13 + i * font_h, ide->console[ci], cc, bg_console, FONT_SMALL);
    }

    // --- Status bar ---
    int sty = cony + console_h;
    fb_fillrect(cx, sty, cw, status_h, RGB(50, 50, 65));
    char status[80];
    if (ide->mode == 0) {
        ksprintf(status, " Ln %d, Col %d | %d lines | EDITOR | F5: Run | F6: Console",
                 ide->cursor_y + 1, ide->cursor_x + 1, ide->line_count);
    } else {
        ksprintf(status, " CONSOLE | Esc/F6: Back to Editor | Click editor to edit");
    }
    font_draw_string(cx + 2, sty + 2, status, RGB(150, 150, 180), RGB(50, 50, 65), FONT_SMALL);
}

// ============================================================
// INPUT HANDLING
// ============================================================
static void java_ensure_visible(JavaIDEState* ide, int editor_h) {
    int visible_rows = editor_h / 10;
    if (visible_rows < 1) visible_rows = 10;
    if (ide->cursor_y < ide->scroll_y) ide->scroll_y = ide->cursor_y;
    if (ide->cursor_y >= ide->scroll_y + visible_rows) ide->scroll_y = ide->cursor_y - visible_rows + 1;
    if (ide->scroll_y < 0) ide->scroll_y = 0;
}

static void java_load_sample(JavaIDEState* ide, int idx) {
    if (idx < 0 || idx >= NUM_SAMPLES) return;
    ide->line_count = samples[idx].count;
    for (int i = 0; i < samples[idx].count; i++) {
        strncpy(ide->lines[i], samples[idx].lines[i], JAVA_LINE_LEN);
        ide->lines[i][JAVA_LINE_LEN] = 0;
    }
    ide->cursor_x = 0; ide->cursor_y = 0;
    ide->scroll_x = 0; ide->scroll_y = 0;
    ide->modified = false;
    con_clear(ide);
}

static void java_key(Window* win, uint8_t key) {
    JavaIDEState* ide = (JavaIDEState*)win->userdata;
    if (!ide) return;

    // F5 = Run
    if (key == KEY_F5) {
        java_run(ide);
        ide->mode = 1; // switch to console view focus
        return;
    }

    // F6 = toggle editor/console focus
    if (key == KEY_F6) {
        ide->mode = 1 - ide->mode;
        return;
    }

    if (ide->mode == 1) {
        // Console mode - only scroll
        if (key == KEY_UP && ide->con_scroll > 0) ide->con_scroll--;
        if (key == KEY_DOWN && ide->con_scroll < ide->con_count - 1) ide->con_scroll++;
        if (key == KEY_PGUP) ide->con_scroll -= 5;
        if (key == KEY_PGDN) ide->con_scroll += 5;
        if (ide->con_scroll < 0) ide->con_scroll = 0;
        // Escape goes back to editor
        if (key == 0x1B) ide->mode = 0;
        return;
    }

    // Editor mode
    int line_len = strlen(ide->lines[ide->cursor_y]);

    switch (key) {
        case KEY_UP:
            if (ide->cursor_y > 0) {
                ide->cursor_y--;
                int nl = strlen(ide->lines[ide->cursor_y]);
                if (ide->cursor_x > nl) ide->cursor_x = nl;
            }
            break;
        case KEY_DOWN:
            if (ide->cursor_y < ide->line_count - 1) {
                ide->cursor_y++;
                int nl = strlen(ide->lines[ide->cursor_y]);
                if (ide->cursor_x > nl) ide->cursor_x = nl;
            }
            break;
        case KEY_LEFT:
            if (ide->cursor_x > 0) ide->cursor_x--;
            else if (ide->cursor_y > 0) {
                ide->cursor_y--;
                ide->cursor_x = strlen(ide->lines[ide->cursor_y]);
            }
            break;
        case KEY_RIGHT:
            if (ide->cursor_x < line_len) ide->cursor_x++;
            else if (ide->cursor_y < ide->line_count - 1) {
                ide->cursor_y++; ide->cursor_x = 0;
            }
            break;
        case KEY_HOME: ide->cursor_x = 0; break;
        case KEY_END: ide->cursor_x = line_len; break;
        case KEY_PGUP:
            ide->cursor_y -= 10; if (ide->cursor_y < 0) ide->cursor_y = 0;
            break;
        case KEY_PGDN:
            ide->cursor_y += 10;
            if (ide->cursor_y >= ide->line_count) ide->cursor_y = ide->line_count - 1;
            break;
        case KEY_TAB:
            // Insert 4 spaces
            if (line_len + 4 < JAVA_LINE_LEN) {
                for (int i = line_len + 4; i >= ide->cursor_x + 4; i--)
                    ide->lines[ide->cursor_y][i] = ide->lines[ide->cursor_y][i - 4];
                for (int i = 0; i < 4; i++)
                    ide->lines[ide->cursor_y][ide->cursor_x + i] = ' ';
                ide->cursor_x += 4;
                ide->modified = true;
            }
            break;
        case KEY_ENTER:
            if (ide->line_count < JAVA_MAX_LINES) {
                // Split line at cursor
                for (int i = ide->line_count; i > ide->cursor_y + 1; i--)
                    memcpy(ide->lines[i], ide->lines[i - 1], JAVA_LINE_LEN + 1);
                ide->line_count++;
                // New line gets text after cursor
                strcpy(ide->lines[ide->cursor_y + 1], ide->lines[ide->cursor_y] + ide->cursor_x);
                ide->lines[ide->cursor_y][ide->cursor_x] = 0;
                ide->cursor_y++; ide->cursor_x = 0;
                // Auto-indent: match previous line's leading whitespace
                const char* prev = ide->lines[ide->cursor_y - 1];
                int indent = 0;
                while (prev[indent] == ' ' && indent < 20) indent++;
                // Extra indent if prev line ends with {
                int pl = strlen(ide->lines[ide->cursor_y - 1]);
                if (pl > 0 && ide->lines[ide->cursor_y - 1][pl - 1] == '{') indent += 4;
                if (indent > 0) {
                    int cl = strlen(ide->lines[ide->cursor_y]);
                    for (int i = cl; i >= 0; i--)
                        ide->lines[ide->cursor_y][i + indent] = ide->lines[ide->cursor_y][i];
                    for (int i = 0; i < indent; i++)
                        ide->lines[ide->cursor_y][i] = ' ';
                    ide->cursor_x = indent;
                }
                ide->modified = true;
            }
            break;
        case KEY_BACKSPACE:
            if (ide->cursor_x > 0) {
                char* l = ide->lines[ide->cursor_y];
                for (int i = ide->cursor_x - 1; l[i]; i++) l[i] = l[i + 1];
                ide->cursor_x--;
                ide->modified = true;
            } else if (ide->cursor_y > 0) {
                // Merge with previous line
                int prev_len = strlen(ide->lines[ide->cursor_y - 1]);
                strcat(ide->lines[ide->cursor_y - 1], ide->lines[ide->cursor_y]);
                for (int i = ide->cursor_y; i < ide->line_count - 1; i++)
                    memcpy(ide->lines[i], ide->lines[i + 1], JAVA_LINE_LEN + 1);
                ide->line_count--;
                ide->cursor_y--;
                ide->cursor_x = prev_len;
                ide->modified = true;
            }
            break;
        case KEY_DELETE:
            if (ide->cursor_x < line_len) {
                char* l = ide->lines[ide->cursor_y];
                for (int i = ide->cursor_x; l[i]; i++) l[i] = l[i + 1];
                ide->modified = true;
            }
            break;
        default:
            if (key >= 0x20 && key < 0x7F && line_len < JAVA_LINE_LEN - 1) {
                char* l = ide->lines[ide->cursor_y];
                for (int i = line_len + 1; i > ide->cursor_x; i--) l[i] = l[i - 1];
                l[ide->cursor_x] = (char)key;
                ide->cursor_x++;
                ide->modified = true;
            }
            break;
    }

    java_ensure_visible(ide, 200); // approximate
}

static void java_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    JavaIDEState* ide = (JavaIDEState*)win->userdata;
    if (!ide || !left) return;

    int cx = win->x + 1;
    int cy = win->y + 23; // below title bar

    // Layout constants (must match java_draw)
    int ch = win->h - 23;
    int toolbar_h = 22;
    int status_h = 14;
    int console_h = ch / 4;
    if (console_h < 60) console_h = 60;
    int editor_h = ch - toolbar_h - console_h - status_h - 2;
    int font_w = font_char_width(FONT_SMALL);
    int font_h = 10;
    int gutter_w = 30;

    int toolbar_top = cy;
    int editor_top = cy + toolbar_h;
    int console_top = editor_top + editor_h + 2;

    // Toolbar buttons
    if (my >= toolbar_top && my < toolbar_top + 22) {
        int bx = cx + 4;
        // Run button
        if (mx >= bx && mx < bx + 38) { java_run(ide); ide->mode = 1; return; }
        bx += 42;
        // Clear button
        if (mx >= bx && mx < bx + 44) { con_clear(ide); return; }
        bx += 48;
        // New button
        if (mx >= bx && mx < bx + 36) {
            ide->line_count = 1;
            memset(ide->lines, 0, sizeof(ide->lines));
            ide->cursor_x = 0; ide->cursor_y = 0;
            ide->scroll_x = 0; ide->scroll_y = 0;
            con_clear(ide);
            ide->mode = 0;
            return;
        }
        bx += 48;
        // < sample
        if (mx >= bx && mx < bx + 12) {
            ide->sample_idx = (ide->sample_idx - 1 + NUM_SAMPLES) % NUM_SAMPLES;
            java_load_sample(ide, ide->sample_idx);
            ide->mode = 0;
            return;
        }
        bx += 14;
        int slen = strlen(samples[ide->sample_idx].name) * font_char_width(FONT_SMALL) + 60;
        bx += slen;
        // > sample
        if (mx >= bx && mx < bx + 12) {
            ide->sample_idx = (ide->sample_idx + 1) % NUM_SAMPLES;
            java_load_sample(ide, ide->sample_idx);
            ide->mode = 0;
            return;
        }
    }

    // Click in editor area: switch to editor mode and position cursor
    if (my >= editor_top && my < editor_top + editor_h) {
        ide->mode = 0;
        int row = (my - editor_top) / font_h + ide->scroll_y;
        int col = (mx - cx - gutter_w - 4) / font_w + ide->scroll_x;
        if (row < 0) row = 0;
        if (row >= ide->line_count) row = ide->line_count - 1;
        if (col < 0) col = 0;
        int ll = strlen(ide->lines[row]);
        if (col > ll) col = ll;
        ide->cursor_y = row;
        ide->cursor_x = col;
        return;
    }

    // Click in console area: switch to console mode
    if (my >= console_top) {
        ide->mode = 1;
        return;
    }
}

static void java_close(Window* win) {
    if (win->userdata) kfree(win->userdata);
}

// ============================================================
// APP LAUNCHER
// ============================================================
extern "C" void app_launch_javaide() {
    Window* w = wm_create_window("Gateway Java IDE", 60, 30, 560, 440,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    JavaIDEState* ide = (JavaIDEState*)kmalloc(sizeof(JavaIDEState));
    memset(ide, 0, sizeof(JavaIDEState));

    // Load first sample
    ide->sample_idx = 0;
    java_load_sample(ide, 0);

    w->userdata = ide;
    w->on_draw = java_draw;
    w->on_key = java_key;
    w->on_mouse = java_mouse;
    w->on_close = java_close;
}
