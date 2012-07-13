#include <config.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_LIBREADLINE
  /* JAS FIX: these conflict if readline is used.  not sure about add_history().. */
  char *readline(char *);
  void add_history();
#endif

#if 0
  /* JAS FIX: these conflict if readline is used, and are not needed otherwise. */
  void rl_callback_handler_install(char *, void (*)(char *));
  struct _hist_state* history_get_history_state(void);
  void rl_callback_read_char ();
  void rl_callback_handler_remove();
#endif

  int yywrap ( void );

#ifdef HAVE_LIBHDF5
#include <hdf5.h>
#endif

#ifdef __cplusplus
}
#endif

#if 0
typedef struct yy_buffer_state *YY_BUFFER_STATE;
void yy_delete_buffer ( YY_BUFFER_STATE );
YY_BUFFER_STATE yy_scan_string( char *yy_str );
void yy_switch_to_buffer ( YY_BUFFER_STATE new_buffer);
#endif



int yyparse(int,YYSTYPE);
void yyerror(char *s);
void log_line(char *);
int yylex(void);

void squash_spaces(char *s);
int instring(char *str, char c);
void save_function(void);
void eat_em(void);
int dd_put_argv(Scope *s, Var *v);
void unput_nextc(char c);
int send_to_plot(const char *);
char *do_help(char *input, char *path);

void parse_stream(FILE *fp);

void parse_buffer(char *buf);
Var *eval_buffer(char *buf);

/* pp.c */
void emit_prompt (void);                /* spit out prompt if interactive */
Var *pp_math (Var *, int , Var *);      /* perform math */
Var *pp_relop (Var *, int, Var *);      /* perform math */
Var *pp_set_var (Var *, Var *, Var *);  /* set variables */
Var *pp_range (Var *, Var *);           /* perform specified subset */
Var *pp_func (Var *, Var *);            /* call function with arglist */
Var *pp_mk_attrib (Var *, Var *);       /* convert ID to ID.attrib */
Var *pp_mk_arglist (Var *, Var *);      /* append arg to arglist */
Var *pp_keyword_to_arg (Var *, Var *);  /* convert keyword pair to arg */
Var *pp_add_range (Var *, Var *);       /* add range to rangelist */
Var *pp_mk_range (Var *, Var *);        /* make range with given 'from' value */
Var *pp_to_range (Var *, Var *);        /* append given 'to' value to range */
Var *pp_get_att (Var *, Var *, Var *);  /* get value of attribute */
Var *pp_parens (Var *);                 /* do parens processing (set hist) */
Var *pp_set_att (Var *, Var *, Var *, Var *);   /* set value of attribute */
Var *pp_set_where(Var *, Var *, Var *);
Var * pp_set_struct(Var *a, Var *b, Var *exp);
Var *pp_shellArgs(Var *);               /* find shell command line args */
Var *pp_argv(Var *, Var *);
Var *pp_mk_rstep(Var *r1, Var *r2);		/* add a step value to ranges */
Var *pp_help(Var *);
Var *pp_exact_help(Var *);
Var *pp_usage(Var *);
void pp_set_cvar (Var *, Var *);	/* set control variable */
Var *pp_get_cvar (char *);		/* get control variable */
Var * pp_shell(char *cmd);
void pp_print_struct(Var *v, int indent, int depth);
int pp_compare(Var *a, Var *b);

#ifdef BUILD_MODULE_SUPPORT
/* Modules related functions */
void pp_print_module_var(Var *mv);
Var *pp_call_dv_module_func(vfuncptr f, Var *args);
Var *search_in_list_of_loaded_modules(char *name);
vfuncptr find_module_func(dvModule *m, char *name);
Var *ff_load_dv_module(vfuncptr func, Var *arg);
Var *ff_unload_dv_module(vfuncptr func, Var *arg);
Var *ff_list_dv_modules(vfuncptr func, Var *arg);
Var * ff_insmod(struct _vfuncptr *, Var *);
Var * ff_rmmod(struct _vfuncptr *, Var *);
Var * ff_lsmod(struct _vfuncptr *, Var *);
#endif /* BUILD_MODULE_SUPPORT */

Var *V_DUP (Var *);
Var *set_array (Var *, Var *, Var *);
Var *extract_array (Var *, Range *);

/* symbol.c */
Var *get_sym (char *name);	/* retrieve named Sym from table */
Var *put_sym (Var *);
Var *eval (Var *);
Var * get_global_sym(char *);
Var * put_global_sym(Var *);


/* rpos.c */
size_t __BSQ2BSQ (Var * s1, Var * s2, size_t i);
size_t __BSQ2BIL (Var * s1, Var * s2, size_t i);
size_t __BSQ2BIP (Var * s1, Var * s2, size_t i);
size_t __BIL2BSQ (Var * s1, Var * s2, size_t i);
size_t __BIL2BIL (Var * s1, Var * s2, size_t i);
size_t __BIL2BIP (Var * s1, Var * s2, size_t i);
size_t __BIP2BSQ (Var * s1, Var * s2, size_t i);
size_t __BIP2BIL (Var * s1, Var * s2, size_t i);
size_t __BIP2BIP (Var * s1, Var * s2, size_t i);
size_t cpos(int x, int y, int z, Var *v);
void xpos(size_t i, Var *v, int *x, int *y, int *z);

/* pp_math.h */
#ifdef __cplusplus
extern "C" {
#endif
int extract_int (const Var * v, const size_t i);
float extract_float (Var * v, size_t i);
double extract_double (Var * v, size_t i);
#ifdef __cplusplus
}
#endif

Var *pp_add_strings(Var *a, Var *b);
Var *pp_math_strings(Var *a, int op, Var *b);

/* file.h */
void push_input_file (char *name);
void pop_input_file (void);
int is_file (char *name);

/* error.c */
#ifdef __cplusplus
extern "C" void parse_error(const char *, ...);
#else
void parse_error(const char *, ...);
#endif

/* reserved.c */
int is_reserved_var (char *);
Var *set_reserved_var (Var *, Var *, Var *);

/* ff.c */

char *unquote(char *);

int evaluate_keywords (vfuncptr, Var *, struct keywords *);
Var *get_kw (char *, struct keywords *);
Var *verify_single_arg(vfuncptr, Var *);
Var *verify_single_string(vfuncptr , Var *);
int KwToInt(char *, struct keywords *, int *);
int KwToFloat(char *, struct keywords *, float *);

/* vicar.h */
char *get_value (char *, char *);

/* read.c */

Var *LoadSpecpr(FILE *,char *,int );
Var *LoadSpecprHeaderStruct(FILE *,char *,int );
int dv_LoadISISHeader(FILE *fp, char *filename, int rec, char *element, Var **var);
Var * LoadVanilla(char *filename);
Var * LoadHDF5(char *filename);

int WriteRaw(Var *, FILE *, char *);
int WriteGRD(Var *, FILE *, char *);
int WriteSpecpr(Var *, char *, char *);
int WriteISIS(Var *, FILE *, char *, char *);
void WritePPM(Var *, FILE *, char *);
void WritePGM(Var *, FILE *, char *);
int WriteVicar(Var *, FILE *, char *);
int WriteERS(Var *, FILE *, char *);
int WriteIMath(Var *s, FILE *fp, char *filename);

int WriteAscii(Var *, char *, int);

#ifdef HAVE_LIBHDF5
void WriteHDF5(hid_t parent, char *name, Var *v);
#endif

#ifdef HAVE_LIBMAGICK
void WriteGFX_Image(Var *ob,char *filename,char *GFX_type);
#endif

int is_AVIRIS(FILE *);
int is_GRD(FILE *);
int is_Vicar(FILE *);
int is_compressed(FILE *);
int is_ISIS(FILE *);
int is_specpr(FILE *);
char *is_PNM(FILE *, char *);
int is_imath(FILE *);
FILE *uncompress(FILE *, char *);



Var *p_mknod(int, Var *, Var *);
Var *p_mkval(int, char *);
Var *evaluate(Var *);
Var * p_rnode(Var *, Var *);
Var * p_lnode(Var *, Var *);
Var * p_rlist(int , Var *, Var *);
Var * p_llist(int , Var *, Var *);


void push_input_stream(FILE *, char *filename);
size_t rpos(size_t, Var *, Var *);
Var * pp_print(Var *);

/* Calls a davinci function programatically.
   See create_args() for creating and sending args. */
Var *V_func (const char *name, Var *args);

void make_sym(Var *, int, char *);

char *get_env_var(char *);
char *expand_filename(char *);
char *enumerated_arg(Var *, char **);
Var *mem_claim(Var *);
Var *mem_malloc(void);
void free_var(Var *);
void commaize(char *);
void free_tree(Var *);
Var *rm_symtab(Var *);

int LoadSpecprHeader(FILE *, char *, int , char *, Var **);
int dv_LoadVicarHeader(FILE *, char *, int , char *, Var **);

Var *RequireKeyword(char *, struct keywords *, int, int, vfuncptr );
Var *HasValue(vfuncptr, Var *);
Var *ufunc_edit(vfuncptr, Var *);


int fixup_ranges(Var *v, Range *in, Range *out);
void split_string(char *buf, int *argc, char ***argv, char *s);
char *uppercase(char *s);
char *lowercase(char *s);
char *ltrim(char *s, char *trim_chars);
char *rtrim(char *s, char *trim_chars);
char *fix_name(const char *input_name);

int dv_getline(char **ptr, FILE *fp);

/**
 ** All the internal functions are declared here.
 **/

Var *ff_unpack (vfuncptr, Var *);
Var *ff_dfunc (vfuncptr, Var *);
Var *ff_pow (vfuncptr, Var *);
Var *ff_conv (vfuncptr, Var *);
Var *ff_dim (vfuncptr, Var *);
Var *ff_format (vfuncptr, Var *);
Var *ff_org (vfuncptr, Var *);
Var *ff_create (vfuncptr, Var *);
Var *ff_source (vfuncptr, Var *);
Var *ff_load (vfuncptr, Var *);
Var *ff_Frame_Grabber_Read(vfuncptr func, Var * arg);
Var *ff_GSE_VIS_Read(vfuncptr func, Var * arg);
Var *ff_PACI_Read(vfuncptr func, Var * arg);
Var *ff_write (vfuncptr, Var *);
Var *ff_filetype(vfuncptr , Var *);
Var *ff_list(vfuncptr, Var *);
Var *ff_atoi(vfuncptr func, Var *arg);
Var *ff_issubstring(vfuncptr func, Var * arg);
Var *ff_strlen(vfuncptr func, Var * arg);
Var *ff_strstr(vfuncptr func, Var * arg);
Var *ff_grep(vfuncptr func, Var * arg);
Var *ff_atof(vfuncptr func, Var *arg);
Var *ff_sprintf(vfuncptr func, Var *arg);
Var *ff_fprintf(vfuncptr func, Var *arg);
Var *ff_version(vfuncptr, Var *);
Var *ff_random(vfuncptr, Var *);
Var *ff_gnoise(vfuncptr func, Var *arg);
Var *ff_cluster(vfuncptr func, Var *arg);
Var *ff_ccount(vfuncptr func, Var *arg);
Var *ff_nop(vfuncptr func, Var *arg);
Var *ff_echo(vfuncptr func, Var *arg);
Var *ff_filename(vfuncptr func, Var *arg);
Var *ff_stringsubst(vfuncptr func, Var *arg);
Var *ff_rgb(vfuncptr func, Var *arg);
Var *ff_replicate(vfuncptr func, Var *arg);
Var *ff_cat(vfuncptr func, Var *arg);
Var *do_cat(Var *, Var *, int);
Var *ff_ascii(vfuncptr, Var *);
Var *ff_read_text(vfuncptr, Var *);
Var *ff_read_lines(vfuncptr, Var *);
Var *ff_load_bin5(vfuncptr, Var *);
Var *ff_string(vfuncptr, Var *);
Var *ff_delim(vfuncptr, Var *);
Var *ff_translate(vfuncptr, Var *);
Var *ff_gplot(vfuncptr, Var *);
Var *ff_plot(vfuncptr, Var *);
Var *ff_splot(vfuncptr, Var *);
Var *ff_display(vfuncptr, Var *);
Var *ff_moment(vfuncptr, Var *);
Var *ff_moments(vfuncptr, Var *);
Var *ff_interp(vfuncptr, Var *);
Var *ff_interp2d(vfuncptr, Var *);
Var *ff_flood_fill(vfuncptr, Var *);
Var *ff_fit(vfuncptr, Var *);
Var *ff_ipi(vfuncptr, Var *);
Var *ff_header(vfuncptr func, Var *);
Var *ff_bbr(vfuncptr func, Var *);
Var *ff_bbrf(vfuncptr func, Var *);
Var *ff_vignette(vfuncptr func, Var *);
Var *ff_pause(vfuncptr func, Var *);
Var *ff_btemp(vfuncptr func, Var *);
Var *ff_printf(vfuncptr func, Var *);
Var *ff_sprintf(vfuncptr func, Var *);
Var *ff_system(vfuncptr func, Var *);
Var *ff_ifill(vfuncptr func, Var *arg);
Var *ff_kfill(vfuncptr func, Var *arg);
Var *ff_jfill(vfuncptr func, Var *arg);
Var *ff_pfill(vfuncptr func, Var *arg);
Var *ff_contour(vfuncptr func, Var *arg);
Var *ff_rotation(vfuncptr func, Var *arg);
Var *ff_bop(vfuncptr func, Var *arg);
Var *ff_avg(vfuncptr func, Var *arg);
Var *ff_avg2(vfuncptr func, Var *arg);
Var *ff_avg3(vfuncptr func, Var *arg);
Var *ff_basis(vfuncptr func, Var *arg);
Var *ff_mxm(vfuncptr func, Var *arg);
Var *ff_histogram(vfuncptr func, Var *arg);
Var *ff_exit(vfuncptr func, Var *arg);
Var *ff_fsize(vfuncptr func, Var *arg);
Var *ff_history(vfuncptr func, Var *arg);
Var *ff_hedit(vfuncptr func, Var *arg);
Var *ff_sort(vfuncptr func, Var *arg);
Var *ff_unique(vfuncptr func, Var *arg);
Var *ff_min(vfuncptr func, Var *arg);
Var *ff_findmin(vfuncptr func, Var *arg);
Var *ff_minpos(vfuncptr func, Var *arg);
Var *ff_maxpos(vfuncptr func, Var *arg);
Var *ff_valpos(vfuncptr func, Var *arg);
Var *ff_rgb2hsv(vfuncptr func, Var *arg);
Var *ff_hsv2rgb(vfuncptr func, Var *arg);
Var *ff_resize(vfuncptr func, Var *arg);
Var *ff_image_resize(vfuncptr func, Var *arg);
Var *ff_resample(vfuncptr func, Var *arg);

Var *ff_read_suffix_plane(vfuncptr func, Var * arg);
Var *ff_fork(vfuncptr func, Var *arg);
Var *ff_xrt3d(vfuncptr func, Var *arg);
Var *ff_fft(vfuncptr func, Var *arg);
Var *ff_realfft(vfuncptr func, Var *arg);
Var *ff_realfft2(vfuncptr func, Var *arg);
Var *ff_realfft3(vfuncptr func, Var *arg);
Var *ff_minvert(vfuncptr func, Var *arg);
Var *ff_dct(vfuncptr func, Var *arg);
Var *ff_entropy(vfuncptr func, Var *arg);
#ifdef HAVE_LIBPROJ
Var *ff_projection(vfuncptr func, Var *arg);
#endif
Var *ff_self_convolve(vfuncptr func, Var *arg);
Var *ff_convolve(vfuncptr func, Var *arg);
Var *ff_convolve2(vfuncptr func, Var *arg);
Var *ff_convolve3(vfuncptr func, Var *arg);
Var *ff_fncc(vfuncptr func, Var * arg);
Var *ff_eval(vfuncptr func, Var *arg);

Var *ff_add_struct(vfuncptr func, Var *arg);
Var *ff_get_struct(vfuncptr func, Var *arg);


#ifdef INCLUDE_API
Var *ff_popen(vfuncptr func, Var * arg);
Var *ff_pprint(vfuncptr func, Var * arg);
Var *ff_pplot(vfuncptr func, Var * arg);
Var *ff_ptext(vfuncptr func, Var * arg);
Var *ff_pline(vfuncptr func, Var * arg);
Var *ff_pbox(vfuncptr func, Var * arg);
Var *ff_pzoom(vfuncptr func, Var * arg);
#endif

Var *ff_struct(vfuncptr func, Var *arg);
Var *ff_get_struct_key(vfuncptr func, Var* arg);

Var *ff_ramp(vfuncptr func, Var *arg);
Var *ff_sawtooth(vfuncptr func, Var *arg);

Var *ff_eigen(vfuncptr func, Var *arg);
Var *ff_pcs(vfuncptr func, Var *arg);
Var *ff_covar(vfuncptr func, Var *arg);

Var *ff_loadvan(vfuncptr func, Var *arg);
Var *ff_loadspecpr(vfuncptr func, Var *arg);

Alist make_alist(const char *name, int type, void *limits, void *value);

Var * ReadPDS(vfuncptr func, Var * arg);
Var * WritePDS(vfuncptr func, Var * arg);
Var * ff_pdshead(vfuncptr func, Var * arg);

Var * ReadFITS(vfuncptr func, Var * arg);
Var * WriteFITS(vfuncptr func, Var * arg);

Var *ff_loadcsv(vfuncptr, Var *);

#ifdef HAVE_LIBISIS
Var * ff_write_isis_cub(vfuncptr func, Var *args);
#endif /* HAVE_LIBISIS */

double bbr(double, double);
double btemp(double, double);
#ifdef __cplusplus
extern "C" Var *newVal(int org, int x, int y, int z, int format, void *data);
#else
Var *newVal(int org, int x, int y, int z, int format, void *data);
#endif
int cmp_byte(const void *, const void *);
int cmp_short(const void *, const void *);
int cmp_int(const void *, const void *);
int cmp_float(const void *, const void *);
int cmp_double(const void *, const void *);

void log_line(char *str);
int parse_args(vfuncptr func, Var *args, Alist *alist);
int make_args(int *ac, Var ***av, vfuncptr func, Var *args);
void print_history(int i);

/* void save_ufunc(char *filename); */
void vax_ieee_r(float *from, float *to);

#ifndef HAVE_STRNDUP
char *strndup(char *, int);
#else
#include <string.h>
#endif

Var *varray_subset(Var *v, Range *r);

Var *set_varray(Var *v, Range *r, Var *e);
void print_text(Var *v, int indent);
Var * create_struct(Var *v);
Var *ff_syscall(vfuncptr func, Var * arg);

Var * ff_dump(vfuncptr func, Var * arg);
Var * ff_global(vfuncptr func, Var * arg);
Var * ff_delete(vfuncptr func, Var *arg);

/* internal functions for structures */
#ifdef __cplusplus
extern "C" {
#endif
  Var *new_struct(int ac);
  void add_struct(Var *s, const char *name, Var *exp);
  Var *remove_struct(Var *, int);
  int find_struct(Var *, const char *, Var **);
  void free_struct(Var *);
  /* internal functions for text arrays */
  Var *newString(char *str);
  Var *newText(int rows, char **text);
  int get_struct_element(const Var *v, const int i, char **name, Var **data);
  int get_struct_count(const Var *v);
#ifdef __cplusplus
}
#endif

/*Text/string functions*/
Var *ff_rtrim(vfuncptr func, Var *arg);

Var *ff_equals(vfuncptr func, Var *arg);

#ifdef __cplusplus
extern "C" {
#endif
  Var * newInt(int i);
  Var * newFloat(float f);
  Var * newDouble(double f);
#ifdef __cplusplus
}
#endif
Var *ff_xplot(vfuncptr func, Var *arg);
Var *ff_vplot(vfuncptr func, Var *arg);
Var *ff_killchild(vfuncptr func, Var *arg);

Var * ff_make_debug(vfuncptr func, Var * arg);
Var * ff_cut(vfuncptr func, Var * arg);
Var * ff_crop(vfuncptr func, Var * arg);
Var * ff_raw(vfuncptr func, Var * arg);

double cosd(double);
double sind(double);
double tand(double);
double acosd(double);
double asind(double);
double atand(double);

Var * ff_audit(vfuncptr func, Var *arg);
Var * ff_identity(vfuncptr func, Var *arg);
Var * per_pixel(vfuncptr func, Var *arg);

Var * ff_exists(vfuncptr func, Var * arg);
Var * ff_unlink(vfuncptr func, Var * arg);
Var * ff_putenv(vfuncptr func, Var * arg);
Var * ff_length(vfuncptr func, Var * arg);
Var * ff_shade(vfuncptr func, Var * arg);
Var * ff_shade2(vfuncptr func, Var * arg);

Var *	ff_GSE_VIS_downshift(vfuncptr func, Var * arg);
Var *	ff_GSE_VIS_upshift(vfuncptr func, Var * arg);
Var * write_isis_planes(vfuncptr func, Var * arg);

Var *ff_remove_struct(vfuncptr func, Var * arg);
Var *ff_insert_struct(vfuncptr func, Var * arg);
Var * ff_hstats(vfuncptr func, Var * arg);
Var * ff_bindct(vfuncptr func, Var * arg);

Var * ff_deghost(vfuncptr func, Var * arg);
Var * ff_deleted(vfuncptr func, Var * arg);
Var * ff_set_deleted(vfuncptr func, Var * arg);
Var * ff_rice(vfuncptr func, Var * arg);
Var * ff_unrice(vfuncptr func, Var * arg);
Var * ff_contains(vfuncptr func, Var * arg);
Var * ff_distance_map(vfuncptr func, Var * arg);
Var * ff_slant(vfuncptr func, Var * arg);
Var * ff_unslant(vfuncptr func, Var * arg);
Var * ff_unslant_shear(vfuncptr func, Var * arg);

Var *do_convolve(Var *obj, Var *kernel, int norm, float ignore);
Var *ff_hstretch(vfuncptr func, Var * arg);
Var *ff_sstretch2(vfuncptr func, Var * arg);
Var *ff_chdir(vfuncptr func, Var * arg);
Var *ff_copy(vfuncptr func, Var * arg);
Var *ff_coreg(vfuncptr func, Var * arg);
Var *ff_coreg2(vfuncptr func, Var * arg);

Var *ff_fncc_fft2d(vfuncptr func, Var *arg);
Var *ff_fncc_ifft2d(vfuncptr func, Var *arg);
Var *ff_fncc_cmplx_mul(vfuncptr func, Var *arg);
Var *ff_fncc_fft_conv_real(vfuncptr func, Var *arg);
Var *ff_fncc_write_mat(vfuncptr func, Var * arg);

Var *ff_boxfilter(vfuncptr func, Var * arg);
Var *ff_gconvolve(vfuncptr func, Var * arg);
Var *ff_warp(vfuncptr func, Var * arg);
Var *ff_median(vfuncptr func, Var * arg);
Var *ff_window(vfuncptr func, Var * arg);
Var *ff_radial_symmetry(vfuncptr func, Var * arg);
Var *ff_radial_symmetry2(vfuncptr func, Var * arg);
Var *ff_radial_symmetry3(vfuncptr func, Var * arg);
Var *ff_local_maximum(vfuncptr func, Var * arg);
Var *ff_drawshape(vfuncptr func, Var * arg);
Var *ff_extract(vfuncptr func, Var * arg);

Var *ff_foo(vfuncptr func, Var * arg);
Var *ff_load_tdb(vfuncptr, Var *);
Var *ff_cinterp(vfuncptr, Var *);
Var *ff_blend(vfuncptr, Var *);

char *dv_locate_file(const char *);
void dump_version();
double my_round(double);

Var * ff_binary_op(const char *name, Var *a, Var *b,
                   double (*)(double, double), int);
char * try_remote_load(const char * filename);
int array_replace(Var *dst, Var *src, Range *r);

char *make_temp_file_path_in_dir(char *dir);
char * make_temp_file_path();

Var *create_args(int, ...);
Var *append_arg(Var *, char *, Var *);
int compare_vars(Var *a, Var *b);

extern void pp_print_var(Var *, char *, int, int);
Var *ff_grassfire(vfuncptr func, Var * arg);
int math_operable(Var *a, Var *b);
int compare_struct(Var *a, Var *b);
int get_struct_names(const Var *v, char ***names, const char *prefix);
Var *ff_create_text(vfuncptr func, Var * arg);
