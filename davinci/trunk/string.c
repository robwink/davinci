#include "parser.h"

/**
 ** Convert a string to int representation
 **/

Var *
ff_atoi(vfuncptr func, Var *arg)
{
	Var *v = NULL;

	Alist alist[2];
	alist[0] = make_alist( "object",    ID_UNK,    NULL,    &v);
	alist[1].name = NULL;

	if (parse_args(func, arg, alist) == 0) return(NULL);

	if (v == NULL) {
        parse_error("%s(): expected a string.", func->name);
        return (NULL);
    }
    if (V_TYPE(v) != ID_STRING && V_TYPE(v)!=ID_TEXT ){
        parse_error("%s: Invalid object type", func->name);
        return(NULL);
    }

    if (V_TYPE(v) == ID_STRING){
		return(newInt(strtod(V_STRING(v), NULL)));
    } else {
        int l;
        int *data = (int *)calloc(V_TEXT(v).Row, sizeof(int));
        for (l=0;l<V_TEXT(v).Row;l++){
            data[l]=strtod(V_TEXT(v).text[l],NULL);
        }
		return(newVal(BSQ, 1, l, 1, INT, data));
    }
}

Var *
ff_atof(vfuncptr func, Var * arg)
{
	Var *v = NULL, *s;

	Alist alist[2];
	alist[0] = make_alist( "object",    ID_UNK,    NULL,    &v);
	alist[1].name = NULL;

	if (parse_args(func, arg, alist) == 0) return(NULL);

	if (v == NULL) {
        parse_error("%s(): expected a string.", func->name);
        return (NULL);
	}

	if (V_TYPE(v) != ID_STRING && V_TYPE(v) != ID_TEXT) {
		parse_error("Invalid object type");
		return (NULL);
	}

	if (V_TYPE(v) == ID_STRING) {
		if (!strcmp(func->name, "atof")) {
			s = newVal(BSQ, 1, 1, 1, FLOAT, NULL);
			V_DATA(s) = (float *) calloc(1, sizeof(float));
			V_FLOAT(s) = (float) strtod(V_STRING(v), NULL);
		} else if (!strcmp(func->name, "atod")) {
			s = newVal(BSQ, 1, 1, 1, DOUBLE, NULL);
			V_DATA(s) = (double *) calloc(1, sizeof(double));
			V_DOUBLE(s) = (double) strtod(V_STRING(v), NULL);
		}
	} else {
		int line;
		int nlines = V_TEXT(v).Row;
		if (!strcmp(func->name, "atof")) {
			float *data = (float *) calloc(nlines, sizeof(float));
			s = newVal(BSQ, 1, nlines, 1, FLOAT, NULL);
			for (line = 0; line < nlines; line++) {
				data[line] = strtod(V_TEXT(v).text[line], NULL);
			}
			V_DATA(s) = data;
		} else if (!strcmp(func->name, "atod")) {
			double *data = (double *) calloc(nlines, sizeof(double));
			s = newVal(BSQ, 1, nlines, 1, DOUBLE, NULL);
			for (line = 0; line < nlines; line++) {
				data[line] = strtod(V_TEXT(v).text[line], NULL);
			}
			V_DATA(s) = data;
		}
	}

	return (s);
}


/*
** Add something to a string.
** This takes a char* so that it can be used in a loop by add_text()
*/

char *
do_add_strings(char *s1, Var *v, int flag)
{
    int len;
    char buf[256];
    char *ptr, *s2;

    if (V_TYPE(v) == ID_STRING) {
        s2 = V_STRING(v);
    } else {
        switch(V_FORMAT(v)) {
        case BYTE:
        case SHORT:
        case INT:
            sprintf(buf, "%d", extract_int(v,0)); break;
        case FLOAT:
        case DOUBLE:
            sprintf(buf, "%.*g", SCALE, extract_double(v,0)); break;
        }
        s2 = buf;
    }

    len = strlen(s2) + strlen(s1) + 1;
    ptr = (char *)calloc(1, len);

    if (flag) {
        strcpy(ptr, s2);
        strcat(ptr, s1);
    } else {
        strcpy(ptr, s1);
        strcat(ptr, s2);
    }
    return(ptr);
}

/*
** add something to a TEXT
*/


Var *
do_add_text(Var *text, Var *v, int flag) 
{
    char *s1, *s2;
    int len;
    char *ptr;
    int rows;
    char **data;
    int i;

    switch (V_TYPE(v)) {
    case ID_TEXT:
        if (V_TEXT(text).Row != V_TEXT(v).Row) {
            parse_error("Unable to add TEXTs with different number of rows.\n");
            return(NULL);
        }
        rows = V_TEXT(text).Row;
        data = calloc(rows, sizeof(char *));
        for (i = 0 ; i < rows ; i++) {
            s1 = V_TEXT(text).text[i];
            s2 = V_TEXT(v).text[i];
            len = strlen(s2) + strlen(s1) + 1;
            ptr = (char *)calloc(1, len);
            if (flag) {
                strcpy(ptr, s1);
                strcat(ptr, s2);
            } else {
                strcpy(ptr, s1);
                strcat(ptr, s2);
            }
            data[i] = ptr;
        }
        return(newText(rows, data));
    case ID_STRING:
    case ID_VAL:
        rows = V_TEXT(text).Row;
        data = calloc(rows, sizeof(char *));
        for (i = 0 ; i < rows ; i++) {
            data[i] = do_add_strings(V_TEXT(text).text[i], v, flag);
        }
        return(newText(rows, data));
    }
    return(NULL);
}

/**
 ** pp_add_strings()
 **
 ** This is called from pp_math.c
 **/

Var *
pp_add_strings(Var *a, Var *b)
{
    if (V_TYPE(a) == ID_TEXT) {
        return(do_add_text(a, b, 0));
    } else if (V_TYPE(b) == ID_TEXT) {
        return(do_add_text(b, a, 1));
    } else if (V_TYPE(a) == ID_STRING) {
        return(newString(do_add_strings(V_STRING(a), b, 0)));
    } else {
        return(newString(do_add_strings(V_STRING(b), a, 1)));
    }
}

Var *
string_subset(Var *v, Var *range)
{
    Range *r=V_RANGE(range);
    
    int i,lo,hi,step;
    int count=0;
    Var *o;

    /*Get high/low/step in x (hey, it's only a string so's there only x)*/

    lo=r->lo[0];
    hi=r->hi[0];
    step=r->step[0];
	

    /*Do some necessary offseting if needed*/

    if (hi==0) hi=strlen(V_STRING(v));
    if (hi > strlen(V_STRING(v))) hi=strlen(V_STRING(v));
    if (step==0) step=1;
    if (lo==0) lo=1;
	
    lo--;
    hi--;

    /*Check for user boo-boo's */

    if (lo >= strlen(V_STRING(v)) || lo > hi){
        parse_error("Invalid range"); 
        return(NULL);
    }

    /*Okay, now prep our new Var and copy over*/

    o=newVar();
    V_TYPE(o)=ID_STRING;
    V_STRING(o)=(char *)calloc(((hi-lo)/step+1)+1,sizeof(char));/*extra +1 for the NULL*/

    for (i=lo;i<=hi;i+=step){
        V_STRING(o)[count++]=V_STRING(v)[i];
    }
    V_STRING(o)[count]='\0';

    return(o);
}

Var *
set_string(Var *to,Range *r, Var *from)
{

    Var *src;
    Var *dest;
    int lo,hi,step;
    int count=0;
    int target_length;
    int dest_length;
    int src_length;
    char *string;

    /*Get high/low/step in x (hey, it's only a string so's there only x)*/

    lo=r->lo[0];
    hi=r->hi[0];
    step=r->step[0];
	

    /*Do some necessary offseting if needed*/

    if (hi==0) hi=strlen(V_STRING(to));
    if (hi > strlen(V_STRING(to))) hi=strlen(V_STRING(to));
    if (step==0) step=1;
    if (lo==0) lo=1;
	
    lo--;
    hi--;
    target_length=(hi-lo)+1;

    /*Check for user boo-boo's */

    if (lo >= strlen(V_STRING(to)) || lo > hi){
        parse_error("Invalid range"); 
        return(NULL);
    }


    if (V_TYPE(from) != ID_STRING) {
        parse_error("Hey, what are you trying to do to this little string?!");
        return(NULL);
    }

    /*Set up and copy*/

    dest=V_DUP(to);
    src=V_DUP(from);

    src_length=strlen(V_STRING(src));
    dest_length=strlen(V_STRING(dest));

    free(V_STRING(to));

    /*New string length =
      original string length - range specified + 
      inserted string length +1 (for the NULL)*/

    V_STRING(to)=(char *)calloc(dest_length - target_length + src_length + 1,sizeof(char));
    string=V_STRING(to);
    memcpy(string,V_STRING(dest),lo);
    count+=lo;
    memcpy((string+count),V_STRING(from),src_length);
    count+=src_length;
    memcpy((string+count),(V_STRING(dest)+hi+1),dest_length-hi-1);
    count+=dest_length-hi-1;
    string[count]='\0';

    return(src);
}

Var *ff_strlen(vfuncptr func, Var * arg)
{
    Var *s1 = NULL;
	int i;
    Alist alist[2];

    alist[0] = make_alist("string", ID_UNK, NULL, &s1);
    alist[1].name = NULL;

    if (parse_args(func, arg, alist) == 0)
        return (NULL);
    if (s1 == NULL) {
        parse_error("Not enough arguments to function: %s()", func->name);
        return (NULL);
    }

    if (V_TYPE(s1) == ID_TEXT) {
        int n = V_TEXT(s1).Row;
        int *r = calloc(n, sizeof(int));
        for (i = 0; i < n; i += 1) {
            if (V_TEXT(s1).text[i]) {
                r[i] = strlen(V_TEXT(s1).text[i]);
            } else {
                r[i] = 0;
            }
        }
		return (newVal(BSQ, 1, n, 1, INT, r));
    } else if (V_TYPE(s1) == ID_STRING) {
        return (newInt(strlen(V_STRING(s1))));
    } else {
        parse_error("Invalid type");
        return (NULL);
    }
}
