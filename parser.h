#ifndef PARSER_H
#define PARSER_H

#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <sys/time.h>
#include <sys/types.h>

#ifdef HAVE_VALUES_H
#include <values.h>
#endif

#include <float.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#include "system.h"


// NOTE(rswinkle) this is only used 3 times, twice in ff_ix.c
// and once in tools.c.  Is it really necessary?  Also it precludes
// checking for malloc failure...
#define memdup(p, l) memcpy(malloc(l), (p), l)

#include "parser_types.h"

#define V_NAME(v) (v)->name /* NAME of SYMbol in union */
#define V_TYPE(v) (v)->type /* type of var */

#define V_KEYVAL(v) (v)->value.keyval /* keyword value */
#define V_STRING(v) (v)->value.string
#define V_RANGE(v) (&((v)->value.range)) /* range value */
#define V_NODE(v) (&((v)->value.node))
#define V_SYM(v) (&((v)->value.sym)) /* SYMbol value in union */

#define V_DATA(v) V_SYM(v)->data      /* pointer to data */
#define V_INT(v) (*((int*)V_DATA(v))) /* derefernce as a single int */
/* #define V_INT64(v)  (*((int64 *)V_DATA(v))) / * derefernce as a single int64 */
#define V_FLOAT(v) (*((float*)V_DATA(v)))   /* derefernce as a single float */
#define V_DOUBLE(v) (*((double*)V_DATA(v))) /* derefernce as a single dbl */
#define V_FORMAT(v) V_SYM(v)->format
#define V_DSIZE(v) V_SYM(v)->dsize
#define V_SIZE(v) V_SYM(v)->size
#define V_ORDER(v) V_SYM(v)->order
#define V_ORG(v) V_SYM(v)->order

#define V_HISTORY(v) V_SYM(v)->history
#define V_TITLE(v) V_SYM(v)->title

#define V_TEXT(v) (v)->value.textarray

#define V_STRUCT(v) (v)->value.vstruct
#define V_ARGS(v) (v)->value.args
#define V_MODULE(v) (v)->value.mod
#define V_FUNC(v) (v)->value.function

#define newVar (Var*)mem_malloc

// The following define the various types for Var->type
enum {
	ID_NONE  = 0, /* a non value */
	ID_ERROR = 99,
	ID_BASE  = 100, /* in case of conflicts */
	ID_UNK,         /* Unknown type - also used as a generic type */
	ID_STRING,      /* NULL terminated character string */
	ID_KEYWORD,     /* keyword argument */
	ID_VAL,         /* everything with dim != 0 */
	ID_STRUCT,      /* Structure */
	ID_TEXT,        /*1-D Array of Strings*/

	ID_IVAL, /* Integer value */
	ID_RVAL, /* real value */
	ID_ID,   /* Identifier */

	ID_LIST,   /* Statement list */
	ID_IF,     /* if statement */
	ID_ELSE,   /* else statement */
	ID_WHILE,  /* while statement */
	ID_CONT,   /* continue statement */
	ID_BREAK,  /* break statement */
	ID_RETURN, /* return statement */

	ID_RANGES, /* list of ranges */
	ID_RSTEP,  /* list of ranges */
	ID_RANGE,  /* single range value */
	ID_SET,    /* equivalence expression */
	ID_OR,     /* logical (||) or */
	ID_AND,    /* logical (&&) and */
	ID_EQ,     /* logical (==) equals */
	ID_NE,     /* logical (!=) not equals */
	ID_LT,     /* logical (<) less than */
	ID_GT,     /* logical (>) greater than */
	ID_LE,     /* logical (<=) less than or equal */
	ID_GE,     /* logical (>=) greater than or equal */
	ID_ADD,    /* addition */
	ID_SUB,    /* subtraction */
	ID_MULT,   /* multiplcation */
	ID_DIV,    /* division */
	ID_MOD,    /* modulo divison */
	ID_UMINUS, /* unary minus */
	ID_LSHIFT, /* left shift */
	ID_RSHIFT, /* right shift */
	ID_FUNCT,  /* function */
	ID_ARRAY,  /* application of ranges to array */
	ID_ARG,    /* list of arguments */
	ID_ARGS,   /* single argument */

	ID_FOR,     /* for loop */
	ID_FOREACH, /* foreach loop */
	ID_EACH,    /* foreach val */
	ID_ARGV,    /* $VALUE argument. Evalue at run */

	ID_INC,    /* increment value */
	ID_DEC,    /* decrement value */
	ID_INCSET, /* increment value */
	ID_DECSET, /* decrement value */
	ID_MULSET, /* *= value */
	ID_DIVSET, /* /= value */

	ID_POW,   /* exponent */
	ID_CAT,   /* concatenate */
	ID_ENUM,  /* enumerated argument, not parsed */
	ID_DECL,  /* Declaration */
	ID_WHERE, /* Where */
	ID_DEREF, /* Structure dereference */

	ID_CONSTRUCT,   /* Structure constructor */
	ID_DECONSTRUCT, /* Structure deconstructor */

	ONE_AXIS, /* argument options */
	ANY_AXIS, /* argument options */

	ID_LINE, /* A lexical token */

	ID_MODULE,   /* davinci module variable ID */
	ID_FUNCTION, /* davinci module function variable ID */
	ID_PARALLEL, /* parallelization */
	ID_VARARGS,  /* varargs arguments */

	ID_FPTR /* a function pointer */
};

// element format.
// Var->value.Sym->format
//#define UINT8       1
//#define INT16       2
//#define INT32       3
#define BYTE 1
#define SHORT       2
#define INT       3
#define FLOAT       4
#define VAX_FLOAT 5
#define VAX_INTEGER 6
#define INT64 7
#define DOUBLE 8
#define USHORT 9 // drd Bug 2208 Loading a particular hdf5 file kills davinci

// drd Bug 2208 Loading a particular hdf5 file kills davinci
#define NBYTES(a)                    \
	((a) == INT ? 4 : ((a) == USHORT \
	                       ? 2       \
	                       : ((a) == INT64 ? 8 : ((a) == VAX_FLOAT ? 4 : ((a) == VAX_INTEGER ? 2 : (a))))))

/**
 ** Data axis order
 ** Var->value.Sym->order
 **
 ** !!! CAUTION: these values must be 0 based.  They are used as array
 **              indices below.
 **/

#define BSQ 0
#define BIL 1
#define BIP 2

#define Format2Str(i) FORMAT2STR[(i)]
#define Org2Str(i) (((i) >= BSQ && (i) <= BIP) ? ORG2STR[(i)] : "undef")

#define GetSamples(s, org) (s)[((org) == BIP ? 1 : 0)]
#define GetLines(s, org) (s)[((org) == BSQ ? 1 : 2)]
#define GetBands(s, org) (s)[((org) == BIP ? 0 : ((org) == BIL ? 1 : 2))]

#define GetX(s) GetSamples(V_SIZE(s), V_ORG(s))
#define GetY(s) GetLines(V_SIZE(s), V_ORG(s))
#define GetZ(s) GetBands(V_SIZE(s), V_ORG(s))
#define GetNbytes(s) NBYTES(V_FORMAT(s))

#define saturate(v, lo, hi) ((v) > (lo) ? ((v) < (hi) ? (v) : (hi)) : (lo))
#define saturate_byte(v) saturate(v, 0, 255)
#define saturate_short(v) saturate(v, (SHRT_MIN), (SHRT_MAX))
#define saturate_int(v) saturate(v, (INT_MIN), (INT_MAX))
#define saturate_float(v) v
#define saturate_double(v) v

#define HBUFSIZE 8192
#define PATH_SEP ' '
#define RECORD_SUFFIX '#'


typedef double (*dfunc)(double);
typedef double (*ddfunc)(double, double);

struct _vfuncptr {
	const char* name;
	vfunc fptr;
	void* fdata;
	void* fdata2;
};


// This structure used by parse_args.
typedef struct Alist {
	const char* name;
	int type;
	void* limits;
	void* value;
	int filled;
} Alist;

#define YYSTYPE Vptr
#define YYDEBUG 1
//extern YYSTYPE yylval;

#include "globals.h"
/*
extern char error_buf[16384];
//extern char pp_input_buf[8192];
extern int orders[3][3];
extern Var* VZERO;
extern char* ORG2STR[];
extern char* FORMAT2STR[];
extern int VERBOSE;
extern int DEPTH;
extern int SCALE;
extern int debug;
extern Var *curnode;
*/

#if defined(_WIN32) && !defined(__MINGW32__)
#define readline w_readline
#define add_history w_add_history
#define history_get_history_state w_history_get_history_state
#endif


#endif /* PARSER_H */
