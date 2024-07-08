/*
 *  Universal Lambda interpreter
 *
 *  Copyright 2008-2024 irori <irorin@gmail.com>
 *  This code is licensed under the MIT License (see LICENSE file for details).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

#define INITIAL_HEAP_SIZE 128*1024
#define RDSTACK_SIZE    100000

/**********************************************************************
 *  Storage management
 **********************************************************************/

/* TAG STRUCTURE
 *
 *  -------- -------- -------- ------00   Pair
 *  -------- -------- -------- ------01   Int
 *  -------- -------- -------- ------10   Combinator
 *  -------- -------- -------- -----011   Character
 *  -------- -------- -------- -----111   Miscellaneous
 */

struct tagPair;
typedef struct tagPair *Cell;
#define CELL(x) ((Cell)(x))
#define TAG(c)  ((int)(c) & 0x03)

/* pair */
typedef struct tagPair {
    Cell car;
    Cell cdr;
} Pair;
#define ispair(c)       (TAG(c) == 0)
#define car(c)          ((c)->car)
#define cdr(c)          ((c)->cdr)
#define SET(c,fst,snd)  ((c)->car = (fst), (c)->cdr = (snd))

/* integer */
#define isint(c)        (TAG(c) == 1)
#define mkint(n)        CELL(((n) << 2) + 1)
#define intof(c)        ((signed int)(c) >> 2)

/* combinator */
#define iscomb(c)       (TAG(c) == 2)
#define mkcomb(n)       CELL(((n) << 2) + 2)
#define combof(c)       ((int)(c) >> 2)
#define COMB_S          mkcomb(0)
#define COMB_K          mkcomb(1)
#define COMB_I          mkcomb(2)
#define COMB_B          mkcomb(3)
#define COMB_C          mkcomb(4)
#define COMB_SP         mkcomb(5)
#define COMB_BS         mkcomb(6)
#define COMB_CP         mkcomb(7)
#define COMB_IOTA       mkcomb(8)
#define COMB_KI         mkcomb(9)
#define COMB_READ       mkcomb(10)
#define COMB_WRITE      mkcomb(11)
#define COMB_INC        mkcomb(12)
#define COMB_CONS       mkcomb(13)
#define COMB_PUTC       mkcomb(14)
#define COMB_RETURN     mkcomb(15)

/* character */
#define ischar(c)       (((int)(c) & 0x07) == 0x03)
#define mkchar(n)       CELL(((n) << 3) + 0x03)
#define charof(c)       ((int)(c) >> 3)

/* immediate objects */
#define isimm(c)        (((int)(c) & 0x07) == 0x07)
#define mkimm(n)        CELL(((n) << 3) + 0x07)
#define NIL             mkimm(0)
#define COPIED          mkimm(1)
#define UNUSED_MARKER   mkimm(2)
#define LAMBDA          mkimm(3)

Pair *heap_area, *free_ptr;
int heap_size, next_heap_size;

int gc_notify = 0;
double total_gc_time = 0.0;

void gc_run(Cell *save1, Cell *save2);
void rs_copy(void);
Cell copy_cell(Cell c);

void errexit(char *fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    vfprintf(stderr, fmt, arg);
    va_end(arg);

    exit(1);
}

void storage_init(int size)
{
    heap_size = size;
    heap_area = malloc(sizeof(Pair) * heap_size);
    if (heap_area == NULL)
        errexit("Cannot allocate heap storage (%d cells)\n", heap_size);
    assert(((int)heap_area & 3) == 0 && (sizeof(Pair) & 3) == 0);
    
    free_ptr = heap_area;
    heap_area += heap_size;
    next_heap_size = heap_size * 3 / 2;
}

Cell pair(Cell fst, Cell snd)
{
    Cell c;
    if (free_ptr >= heap_area)
        gc_run(&fst, &snd);

    assert(free_ptr < heap_area);
    c = free_ptr++;
    car(c) = fst;
    cdr(c) = snd;
    return c;
}

Cell alloc(int n)
{
    Cell p;
    if (free_ptr + n > heap_area)
        gc_run(NULL, NULL);

    assert(free_ptr + n <= heap_area);
    p = free_ptr;
    free_ptr += n;
    return p;
}


void gc_run(Cell *save1, Cell *save2)
{
    static Pair* free_area = NULL;
    int num_alive;
    Pair *scan;
    clock_t start = clock();

    if (free_area == NULL) {
        free_area = malloc(sizeof(Pair) * next_heap_size);
        if (free_area == NULL)
            errexit("Cannot allocate heap storage (%d cells)\n",
                    next_heap_size);
    }

    free_ptr = scan = free_area;
    free_area = heap_area - heap_size;
    heap_area = free_ptr + next_heap_size;

    rs_copy();
    if (save1)
        *save1 = copy_cell(*save1);
    if (save2)
        *save2 = copy_cell(*save2);

    while (scan < free_ptr) {
        car(scan) = copy_cell(car(scan));
        cdr(scan) = copy_cell(cdr(scan));
        scan++;
    }

    num_alive = free_ptr - (heap_area - next_heap_size);
    if (gc_notify)
        fprintf(stderr, "GC: %d / %d\n", num_alive, heap_size);

    if (heap_size != next_heap_size || num_alive * 8 > next_heap_size) {
        heap_size = next_heap_size;
        if (num_alive * 8 > next_heap_size)
            next_heap_size = num_alive * 8;

        free(free_area);
        free_area = NULL;
    }

    total_gc_time += (clock() - start) / (double)CLOCKS_PER_SEC;

    if (free_ptr >= heap_area)
        gc_run(save1, save2);
}

Cell copy_cell(Cell c)
{
    Cell r;

    if (!ispair(c))
        return c;
    if (car(c) == COPIED)
        return cdr(c);

    r = free_ptr++;
    car(r) = car(c);
    if (car(c) == COMB_I) {
        Cell tmp = cdr(c);
        while (ispair(tmp) && car(tmp) == COMB_I)
            tmp = cdr(tmp);
        cdr(r) = tmp;
    }
    else
        cdr(r) = cdr(c);
    car(c) = COPIED;
    cdr(c) = r;
    return r;
}

/**********************************************************************
 *  Reduction Machine
 **********************************************************************/

typedef struct {
    Cell *sp;
    Cell stack[RDSTACK_SIZE];
} RdStack;

RdStack rd_stack;

void rs_init(void)
{
    int i;
    rd_stack.sp = rd_stack.stack + RDSTACK_SIZE;

    for (i = 0; i < RDSTACK_SIZE; i++)
        rd_stack.stack[i] = UNUSED_MARKER;
}

void rs_copy(void)
{
    Cell *c;
    for (c = rd_stack.stack + RDSTACK_SIZE - 1; c >= rd_stack.sp; c--)
        *c = copy_cell(*c);
}

int rs_max_depth(void)
{
    int i;
    for (i = 0; i < RDSTACK_SIZE; i++) {
        if (rd_stack.stack[i] != UNUSED_MARKER)
            break;
    }
    return RDSTACK_SIZE - i;
}

void rs_push(Cell c)
{
    if (rd_stack.sp <= rd_stack.stack)
        errexit("runtime error: stack overflow\n");
    *--rd_stack.sp = c;
}

#define TOP             (*rd_stack.sp)
#define POP             (*rd_stack.sp++)
#define PUSH(c)         rs_push(c)
#define PUSHED(n)       (*(rd_stack.sp+(n)))
#define DROP(n)         (rd_stack.sp += (n))
#define ARG(n)          cdr(PUSHED(n))
#define APPLICABLE(n)   (bottom - rd_stack.sp > (n))

/**********************************************************************
 *  Loader
 **********************************************************************/

typedef struct {
    char **argv;
    FILE *fp;
    int ch;
    int bit;
} InputStream;
InputStream input;

void input_init(char **argv)
{
    input.argv = argv;
    if (*input.argv) {
        input.fp = fopen(*input.argv, "r");
        if (input.fp == NULL)
            errexit("cannot open %s\n", *input.argv);
    }
    else
        input.fp = stdin;
    input.ch = 0;
    input.bit = 0;
}

int read_char(void)
{
    int c;
    while ((c = fgetc(input.fp)) == EOF) {
        if (*input.argv == NULL)
            break;      /* input.fp is stdin */
        fclose(input.fp);
        if (*++input.argv) {
            input.fp = fopen(*input.argv, "r");
            if (input.fp == NULL)
                errexit("cannot open %s\n", *input.argv);
        }
        else
            input.fp = stdin;
    }
    return c;
}

int read_bit(void)
{
    if (!input.bit) {
        input.ch = read_char();
        if (input.ch == EOF)
            errexit("unexpected EOF\n");
        input.bit = 0x80;
    }
    int r = (input.ch & input.bit) ? 1 : 0;
    input.bit >>= 1;
    return r;
}

Cell parse(void)
{
    Cell c;
    if (read_bit()) {           /* variable */
        int i;
        for (i = 0; read_bit(); i++)
            ;
        c = mkint(i);
    }
    else if (read_bit()) {      /* application */
        PUSH(parse());
        c = parse();
        c = pair(TOP, c);
        POP;
    }
    else                        /* lambda */
        c = pair(LAMBDA, parse());

    return c;
}

#define IS_K1(x) (ispair(x) && car(x) == COMB_K)
#define IS_B2(x) (ispair(x) && ispair(car(x)) && car(car(x)) == COMB_B)

Cell unabstract(Cell t)
{
    if (isint(t)) {
        if (t == mkint(0))
            return COMB_I;
        else
            return pair(COMB_K, mkint(intof(t)-1));
    }
    else if (ispair(t)) {
        Cell f, g;
        PUSH(cdr(t));
        PUSH(unabstract(car(t)));
        g = PUSHED(1) = unabstract(PUSHED(1));
        f = TOP;
        if (IS_K1(f)) {
            if (g == COMB_I) {
                /* S (K x) I => x */
                f = cdr(f);
            }
            else if (IS_K1(g)) {
                /* S (K x) (K y) => K (x y) */
                car(g) = cdr(f);        /* x y */
                cdr(f) = g;             /* K (x y) */
            }
            else if (IS_B2(g)) {
                /* S (K x) (B y z) => B* x y z */
                car(f) = COMB_BS;       /* B* x */
                car(car(g)) = f;        /* B* x y z */
                f = g;
            }
            else {
                /* S (K x) y => B x y */
                car(f) = COMB_B;        /* B x */
                f = pair(f, g);         /* B x y */
            }
        }
        else if (IS_K1(g)) {
            if (IS_B2(f)) {
                /* S (B x y) (K z) => C' x y z */
                car(car(f)) = COMB_CP;
                car(g) = f;
                f = g;
            }
            else {
                /* S x (K y) => C x y */
                f = cdr(g);
                SET(g, COMB_C, TOP);    /* C x */
                f = pair(g, f);         /* C x y */
            }
        }
        else if (IS_B2(f)) {
            /* S (B x y) z => S' x y z */
            car(car(f)) = COMB_SP;      /* S' x y */
            f = pair(f, g);             /* S' x y z */
        }
        else {
            /* S x y */
            f = pair(COMB_S, f);
            f = pair(f, PUSHED(1));
        }
        DROP(2);
        return f;
    }
    else
        return pair(COMB_K, t);
}

Cell translate(Cell t)
{
    if (!ispair(t))
        return t;
    if (car(t) == LAMBDA)
        return unabstract(translate(cdr(t)));
    else {
        Cell s;
        PUSH(cdr(t));
        PUSH(translate(car(t)));
        s = translate(PUSHED(1));
        s = pair(TOP, s);
        DROP(2);
        return s;
    }
}

Cell load_program(void)
{
    return translate(parse());
}

void unparse(Cell e)
{
    if (ispair(e)) {
        putchar('`');
        unparse(car(e));
        unparse(cdr(e));
    }
    else if (e == COMB_S) putchar('S');
    else if (e == COMB_K) putchar('K');
    else if (e == COMB_I) putchar('I');
    else if (e == COMB_B) putchar('B');
    else if (e == COMB_C) putchar('C');
    else if (e == COMB_SP) printf("S'");
    else if (e == COMB_BS) printf("B*");
    else if (e == COMB_CP) printf("C'");
    else if (e == COMB_KI) printf("`ki");
    else putchar('?');
}

/**********************************************************************
 *  Reducer
 **********************************************************************/

int reductions;

void eval(Cell root)
{
    Cell *bottom = rd_stack.sp;
    PUSH(root);

    for (;;) {
        while (ispair(TOP))
            PUSH(car(TOP));

        if (TOP == COMB_I && APPLICABLE(1))
        { /* I x -> x */
            POP;
            TOP = cdr(TOP);
        }
        else if (TOP == COMB_S && APPLICABLE(3))
        { /* S f g x -> f x (g x) */
            Cell a = alloc(2);
            SET(a+0, ARG(1), ARG(3));   /* f x */
            SET(a+1, ARG(2), ARG(3));   /* g x */
            DROP(3);
            SET(TOP, a+0, a+1); /* f x (g x) */
        }
        else if (TOP == COMB_K && APPLICABLE(2))
        { /* K x y -> I x */
            Cell x = ARG(1);
            DROP(2);
            SET(TOP, COMB_I, x);
            TOP = cdr(TOP);     /* shortcut reduction of I */
        }
        else if (TOP == COMB_B && APPLICABLE(3))
        { /* B f g x -> f (g x) */
            Cell f, gx;
            gx = pair(ARG(2), ARG(3));
            f = ARG(1);
            DROP(3);
            SET(TOP, f, gx);
        }
        else if (TOP == COMB_C && APPLICABLE(3))
        { /* C f g x -> f x g */
            Cell fx, g;
            fx = pair(ARG(1), ARG(3));
            g = ARG(2);
            DROP(3);
            SET(TOP, fx, g);
        }
        else if (TOP == COMB_SP && APPLICABLE(4))
        { /* SP c f g x -> c (f x) (g x) */
            Cell a = alloc(3);
            SET(a+0, ARG(2), ARG(4));   /* f x */
            SET(a+1, ARG(3), ARG(4));   /* g x */
            SET(a+2, ARG(1), a+0);      /* c (f x) */
            DROP(4);
            SET(TOP, a+2, a+1);         /* c (f x) (g x) */
        }
        else if (TOP == COMB_BS && APPLICABLE(4))
        { /* BS c f g x -> c (f (g x)) */
            Cell a, c;
            a = alloc(2);
            SET(a+0, ARG(3), ARG(4));   /* g x */
            SET(a+1, ARG(2), a+0);      /* f (g x) */
            c = ARG(1);
            DROP(4);
            SET(TOP, c, a+1);           /* c (f (g x)) */
        }
        else if (TOP == COMB_CP && APPLICABLE(4))
        { /* BS c f g x -> c (f x) g */
            Cell a, g;
            a = alloc(2);
            SET(a+0, ARG(2), ARG(4));   /* f x */
            SET(a+1, ARG(1), a+0);      /* c (f x) */
            g = ARG(3);
            DROP(4);
            SET(TOP, a+1, g);           /* c (f x) g */
        }
        else if (TOP == COMB_IOTA && APPLICABLE(1))
        { /* IOTA x -> x S K */
            Cell xs = pair(ARG(1), COMB_S);
            POP;
            SET(TOP, xs, COMB_K);
        }
        else if (TOP == COMB_KI && APPLICABLE(2))
        { /* KI x y -> I y */
            DROP(2);
            car(TOP) = COMB_I;
        }
        else if (TOP == COMB_CONS && APPLICABLE(3))
        { /* CONS x y f -> f x y */
            Cell fx, y;
            fx = pair(ARG(3), ARG(1));
            y = ARG(2);
            DROP(3);
            SET(TOP, fx, y);
        }
        else if (TOP == COMB_READ && APPLICABLE(2))
        { /* READ NIL f -> CONS CHAR(c) (READ NIL) f
                        -> I KI f */
            int c = read_char();
            if (c == EOF) {
                POP;
                SET(TOP, COMB_I, COMB_KI);
            }
            else {
                Cell a = alloc(2);
                SET(a+0, COMB_CONS, mkchar(c == EOF ? 256 : c));
                SET(a+1, COMB_READ, NIL);
                POP;
                SET(TOP, a+0, a+1);
            }
        }
        else if (TOP == COMB_WRITE && APPLICABLE(1))
        { /* WRITE x -> x PUTC RETURN */
            POP;
            Cell a = pair(cdr(TOP), COMB_PUTC); /* x PUTC */
            SET(TOP, a, COMB_RETURN);           /* x PUTC RETURN */
        }
        else if (TOP == COMB_PUTC && APPLICABLE(3))
        { /* PUTC x y i -> putc(eval(x INC NUM(0))); WRITE y */
            Cell a = alloc(2);
            SET(a+0, ARG(1), COMB_INC); /* x INC */
            SET(a+1, a+0, mkint(0));    /* x INC NUM(0) */
            DROP(2);
            eval(a+1);

            if (!isint(TOP))
                errexit("invalid output format (result was not a number)\n");
            if (intof(TOP) >= 256)
                errexit("invalid character %d\n", intof(TOP));

            putchar(intof(TOP));
            POP;

            ARG(1) = cdr(TOP);  /* y */
            POP;
            car(TOP) = COMB_WRITE;      /* WRITE y */
        }
        else if (TOP == COMB_RETURN)
            return;
        else if (TOP == COMB_INC && APPLICABLE(1))
        { /* INC x -> eval(x)+1 */
            Cell c = ARG(1);
            POP;
            eval(c);

            c = POP;
            if (!isint(c))
                errexit("invalid output format (attempted to apply inc to a non-number)\n");
            SET(TOP, COMB_I, mkint(intof(c) + 1));
        }
        else if (ischar(TOP) && APPLICABLE(2)) {
            int c = charof(TOP);
            if (c <= 0) {  /* CHAR(0) f z -> z */
                Cell z = ARG(2);
                DROP(2);
                SET(TOP, COMB_I, z);
            }
            else {       /* CHAR(n+1) f z -> f (CHAR(n) f z) */
                Cell a = alloc(2);
                Cell f = ARG(1);
                SET(a+0, mkchar(c-1), f);       /* CHAR(n) f */
                SET(a+1, a+0, ARG(2));          /* CHAR(n) f z */
                DROP(2);
                SET(TOP, f, a+1);               /* f (CHAR(n) f z) */
            }
        }
        else if (isint(TOP) && APPLICABLE(1))
            errexit("invalid output format (attempted to apply a number)\n");
        else
            return;
        reductions++;
    }
}

void eval_print(Cell root)
{
    eval(pair(COMB_WRITE,
              pair(root,
                   pair(COMB_READ, NIL))));
}

/**********************************************************************
 *  Main
 **********************************************************************/

int main(int argc, char *argv[])
{
    Cell root;
    clock_t start;
    char *prog_file = NULL;
    int i;
    int print_stats = 0;
    int parse_only = 0;
    
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-g") == 0)
            gc_notify = 1;
        else if (strcmp(argv[i], "-s") == 0)
            print_stats = 1;
        else if (strcmp(argv[i], "-p") == 0)
            parse_only = 1;
        else if (strcmp(argv[i], "-u") == 0)
            setbuf(stdout, NULL);
        else
            errexit("unknown option %s\n", argv[i]);
    }

    input_init(argv + i);
    storage_init(INITIAL_HEAP_SIZE);
    rs_init();

    root = load_program();
    if (parse_only) {
        unparse(root);
        return 0;
    }

    start = clock();
    eval_print(root);

    if (print_stats) {
        double evaltime = (clock() - start) / (double)CLOCKS_PER_SEC;

        printf("\n%d reductions\n", reductions);
        printf("  total eval time --- %5.2f sec.\n", evaltime - total_gc_time);
        printf("  total gc time   --- %5.2f sec.\n", total_gc_time);
        printf("  max stack depth --- %d\n", rs_max_depth());
    }
    return 0;
}
