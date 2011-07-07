/******************************** symbol.c *********************************/
#include "parser.h"

/**
 **
 ** Symbol table management routines.
 **
 **		get_sym(name)	- search symbol table for named symbol
 **		put_sym(name)	- Insert (replace) named symbol in table
 **
 ** The symbol table is linked together using V_NEXT(v).
 **/

/**
 ** get_sym()	 - search symbol table for named symbol
 **/


Var *
HasValue(vfuncptr func, Var *arg)
{
  int ac;
  Var **av, *v;

  make_args(&ac, &av, func, arg);
  if (ac == 2 && (v = eval(av[1])) != NULL) {
    return(newInt(1));
  }
  return(newInt(0));
}


/**
 ** remove a symbol from the symtab.
 ** this destroys the symtab and hands back the value.
 **/

Var *
rm_symtab(Var *v)
{
  Scope *scope = scope_tos();
  Symtable *s, *t;

  if (scope->symtab == NULL) return(NULL);
  if (scope->symtab->value == v) {
    s = scope->symtab;
    scope->symtab = s->next;
    free(s);
    return(v);
  } else {
    for (s = scope->symtab ; s->next != NULL ; s = s->next) {
      if (s->next->value == v) {
        t = s->next;
        s->next = t->next;
        t->next = NULL;
        free(t);
        return(v);
      }
    }
  }
  return(NULL);
}

Var *
ff_delete(vfuncptr func, Var *arg)
{
  Var *obj;
  Alist alist[2];
  alist[0] = make_alist( "obj", ID_UNK,    NULL,    &obj);
  alist[1].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);
  if (obj == NULL) return(NULL);
	
  free_var(obj);

  return(NULL);
}

Var *
search_symtab(Scope *scope, char *name)
{
  Symtable *s;
  for (s = scope->symtab ; s != NULL ; s = s->next) {
    if (V_NAME(s->value) && !strcmp(V_NAME(s->value), name))
      return(s->value);
  }
  return(NULL);
}

Var *get_sym(char *name)
{
  Scope *scope = scope_tos();
  Var *v;

  if ((v = dd_find(scope, name)) != NULL) return(v);
  if ((v = search_symtab(scope, name)) != NULL) return(v);
  return(NULL);
}

Var *get_global_sym(char *name)
{
  Scope *scope = global_scope();
  Var *v;

  if ((v = dd_find(scope, name)) != NULL) return(v);
  if ((v = search_symtab(scope, name)) != NULL) return(v);
  return(NULL);
}

/**
 ** delete symbol from symbol table
 **/

void
rm_sym(char *name)
{
  Scope *scope = scope_tos();
  Symtable *s, *last = NULL;

  if (name == NULL) return;

  for (s = scope->symtab ; s != NULL ; s = s->next) {
    if (!strcmp(V_NAME(s->value), name)) {
      if (last != NULL) {
        last->next = s->next;
      } else {
        scope->symtab = s->next;
      }
      free_var(s->value);
      free(s);
      return;
    }
    last = s;
  }
  return;
}

/**
 ** put_sym()	 - store symbol in symbol table
 **/

Var *
sym_put(Scope *scope, Var *s)
{
  Symtable *symtab;
  Var *v, tmp;
  /**
   ** If symbol already exists, we must re-use its structure.
   ** and, at the same time, free up its old data values
   **/
  if ((v = get_sym(V_NAME(s))) != NULL) {

    mem_claim(s);
    /* swap around the values */
    tmp = *v;
    *v = *s;
    *s = tmp;

    /* put the names back */
    tmp.name = v->name;
    v->name = s->name;
    s->name = tmp.name;

    free_var(s);
    s = v;
  } else {
    symtab = (Symtable *)calloc(1, sizeof(Symtable));
    symtab->value = s;
    symtab->next = scope->symtab;
    scope->symtab = symtab;
  }
  /**
   ** Possible this has already been done.  do it again for safety.
   **/
  mem_claim(s);
  return(s);
}


Var *
put_sym(Var *s)
{
  return(sym_put(scope_tos(), s));
}
Var *
put_global_sym(Var *s)
{
  return(sym_put(global_scope(), s));
}


/**
 ** Force symtab evaluation
 **/

Var *
eval(Var *v)
{
  if (v == NULL) return(NULL);
  if (V_NAME(v)) v = get_sym(V_NAME(v));
  return(v);
}

/**
 ** enumerate the symbol table.
 **/

extern UFUNC **ufunc_list;
extern int nufunc;
extern struct _vfuncptr vfunclist[];
extern int nsfunc;

Var *
ff_list(vfuncptr func, Var *arg)
{
  Scope *scope = scope_tos();
  Symtable *s= scope->symtab;

  Var *v;
  int i;
  int list_ufuncs = 0, list_sfuncs = 0;
  Alist alist[3];
  alist[0] = make_alist( "ufunc",    INT,    NULL,    &list_ufuncs);
  alist[1] = make_alist( "sfunc",    INT,    NULL,    &list_sfuncs);
  alist[2].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (list_ufuncs == 0 && list_sfuncs == 0) {
    for (s = scope->symtab ; s != NULL ; s = s->next) {
      v = s->value;
      if (V_NAME(v) != NULL) pp_print_var(v, V_NAME(v), 0, 0);
    }
  } else {
    int nfuncs = 0, nsfunc;
    int count = 0;
    char **names;

    if (list_ufuncs){
      nfuncs += nufunc;
    }
    if (list_sfuncs){
      nsfunc = 0;
      while(vfunclist[nsfunc].name != NULL)
        nsfunc++;

      nfuncs += nsfunc;
    }

    names = calloc(nfuncs, sizeof(char *));
    if (list_ufuncs){
      for (i = 0 ; i < nufunc ; i++) {
        names[i] = strdup(ufunc_list[i]->name);
      }
      count += nufunc;
    }
    if (list_sfuncs){
      for (i = 0 ; i < nsfunc ; i++) {
        names[i+count] = strdup(vfunclist[i].name);
      }
    }
    return(newText(nfuncs, names));
  }

  return(NULL);
}


void
free_var(Var *v)
{
  int type;
  int i;
  if (v == NULL) return;

  type=V_TYPE(v);
  switch (type) {
    case ID_IVAL:
    case ID_RVAL:
    case ID_VAL:
      if (V_DATA(v)) free(V_DATA(v));
      break;
    case ID_STRING:
      if (V_STRING(v)) free(V_STRING(v));
      break;
    case ID_STRUCT:
      free_struct(v);
      break;
    case ID_TEXT:                     /*Added: Thu Mar  2 16:03:49 MST 2000*/
      for (i=0;i< V_TEXT(v).Row; i++){
        free(V_TEXT(v).text[i]);
      }
      free(V_TEXT(v).text);
      break;
    case ID_ARGS:
      if (V_ARGS(v)) {
        Narray_free(V_ARGS(v), NULL);
        free(V_ARGS(v));
      }
      break;

#ifdef BUILD_MODULE_SUPPORT
    case ID_MODULE:
      del_module(v);
      break;
#endif /* BUILD_MODULE_SUPPORT */

    default:
      break;
  }
  if (V_NAME(v)) free(V_NAME(v));
  free(v);
}
