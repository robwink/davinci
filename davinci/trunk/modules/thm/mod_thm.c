#include "parser.h"
#include "ff_modules.h"
#include "dvio.h"

typedef struct {
  float *emiss;
  float *maxbtemp;
} emissobj;

static float *convolve(float *obj, float *kernel, int ox, int oy, int oz, int kx, int ky, int kz, int norm, float ignore);
static Var *do_my_convolve(Var *obj, Var *kernel, int norm, float ignore, int kernreduce);
static float *sawtooth(int x, int y, int z);
static int *do_corners(Var *pic_a, float nullval);
static float *unslantc(float *data, int *leftedge, int width, int x, int y, int z, float ignore);
static float *unshearc(float *w_pic, float angle, int x, int y, int z, float nullv);
static float *rad2tb(Var *radiance, Var *temp_rad, int *bandlist, int bx, float nullval);
static float *tb2rad(float *btemp, Var *temp_rad, int *bandlist, int bx, float nullval, int x, int y);
static emissobj *themissivity(Var *rad, int *blist, float nullval, char *fname, int b1, int b2);


static Var *thm_y_shear(vfuncptr, Var *);
static Var *thm_kfill(vfuncptr, Var *);
static Var *thm_convolve(vfuncptr, Var *);
static Var *thm_ramp(vfuncptr, Var *);
static Var *thm_corners(vfuncptr, Var *);
static Var *thm_sawtooth(vfuncptr, Var *);
static Var *thm_deplaid(vfuncptr, Var *);
static Var *thm_rectify(vfuncptr, Var *);
static Var *thm_reconst(vfuncptr func, Var *arg);
static Var *thm_rad2tb(vfuncptr func, Var *);
static Var *thm_themissivity(vfuncptr func, Var *);
static Var *thm_white_noise_remove1(vfuncptr func, Var *);
static Var *thm_maxpos(vfuncptr func, Var *);
static Var *thm_minpos(vfuncptr func, Var *);
static Var *thm_unscale(vfuncptr func, Var *);
static Var *thm_cleandcs(vfuncptr func, Var *);
static Var *thm_white_noise_remove2(vfuncptr func, Var *);
static Var *thm_sstretch2(vfuncptr func, Var *);

static dvModuleFuncDesc exported_list[] = {
  { "deplaid", (void *) thm_deplaid },
  { "rectify", (void *) thm_rectify },
  { "reconst", (void *) thm_reconst},
  { "convolve", (void *) thm_convolve },
  { "rad2tb", (void *) thm_rad2tb },
  { "themis_emissivity", (void *) thm_themissivity},
  { "maxpos", (void *) thm_maxpos },
  { "minpos", (void *) thm_minpos },
  { "kfill", (void *) thm_kfill },
  { "ramp", (void *) thm_ramp },
  { "unscale", (void *) thm_unscale },
  { "y_shear", (void *) thm_y_shear },
  { "corners", (void *) thm_corners },
  { "sawtooth", (void *) thm_sawtooth },
  { "cleandcs", (void *) thm_cleandcs },
  { "white_noise_remove1", (void *) thm_white_noise_remove1},
  { "white_noise_remove2", (void *) thm_white_noise_remove2 },
  { "sstretch", (void *) thm_sstretch2 },
};

static dvModuleInitStuff is = {
  exported_list, 18,
  NULL, 0
};

dv_module_init(
    const char *name,
    dvModuleInitStuff *init_stuff
    )
{
    *init_stuff = is;
    
    parse_error("Loaded module thm.");

    return 1; /* return initialization success */

}

void
dv_module_fini(const char *name)
{
  parse_error("Unloaded module thm.");
}

Var *
thm_y_shear(vfuncptr func, Var * arg)
{
  Var     *pic_v = NULL;            /* the picture */
  Var     *out;                     /* the output picture */
  float   *pic = NULL;              /* the new sheared picture */
  float    angle = 0;               /* the shear angle */
  float    shift;                   /* the shift/pixel */
  float    nullv = 0;               /* the null value to put in all blank space */
  int	   trim = 0;		    /* whether to trim the data or not */
  int      lshift;                  /* the largest shift (int) */
  int      x, y, z;                 /* dimensions of the original picture */
  int      i, j, k;                 /* loop indices */
  int      nx, ni, nj;              /* memory locations */

  Alist alist[5];
  alist[0] = make_alist("picture",		ID_VAL,		NULL,	&pic_v);
  alist[1] = make_alist("angle",		FLOAT,		NULL,	&angle);
  alist[2] = make_alist("null",                 FLOAT,          NULL,   &nullv);
  alist[3] = make_alist("trim",		        INT,		NULL,    &trim);
  alist[4].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no picture got passed to the function */
  if (pic_v == NULL){
    parse_error("y_shear() - 4/25/04");
    parse_error("It takes an input array and shears the y position of each pixel according to a shear angle in degrees.\n");
    parse_error("Syntax:  b = thm.y_shear(picture,angle,null,trim)");
    parse_error("example: b = thm.y_shear(picture=a,angle=8,null=-32768,trim=1)");
    parse_error("example: b = thm.y_shear(a,8,-32768,1)\n");
    parse_error("The shear angle may be between -80 and 80 degrees");
    parse_error("The null option fills in a specified value into all blank space");
    parse_error("hello");
    parse_error("If you want to unshear the array an equal but opposite angle and want it to have the same dimensions as the original, then use the \"trim=1\" option\n");
    return NULL;
  }

  /* x, y, and z dimensions of the original picture */
  x = GetX(pic_v);
  y = GetY(pic_v);
  z = GetZ(pic_v);

  if (angle == 0) {
    parse_error("Why did you bother to shear the data by a 0 angle?\n"); return NULL;
  }

  if (angle > 80 || angle < -80) {
    parse_error("Try to stay between -80 and 80, love.  Our motto is \"No Crashy Crashy.\""); return NULL;
  }

  /* calculating the number of rows to add to the picture to accomodate the shear */
  shift = tan((M_PI / 180.0) * angle);
  lshift = (int)((x*fabs(shift)-0.5)+1)*(shift/fabs(shift));

  if (lshift == 0) {
    parse_error("The shear is too small to accomplish anything. Try again bozo.\n"); return NULL;
  }

  /* for trim == 0 */
  if(trim == 0) {

    /* create the new picture array */
    pic = (float *)calloc(sizeof(float), x*z*(y+abs(lshift)));

    if (pic == NULL) {
      parse_error("ERROR! Unable to allocate %d bytes\n", sizeof(float)*x*z*(y+abs(lshift)));
      return NULL;
    }

    /* fill in the null value into all pic elements */
    if(nullv != 0) {
      for(k=0; k<z; k++) {
	for(j=0; j<y+abs(lshift); j++) {
	  for(i=0; i<x; i++) {
	    ni = k*(y+abs(lshift))*x + j*x + i;
	    pic[ni] = nullv;
	  }
	}
      }
    }

    /* extract the picture directly into a sheared picture */
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
        for(i=0; i<x; i++) {

          /* the shifted y pixel */
	    /* if the angle is greater than 0 */
 	    if(lshift>0) {
	      ni = k*(y+abs(lshift))*x + ((int)(j+abs(lshift)-(int)(shift*i+0.5)))*x + i;
	    }

	    /* if the angle is less than 0 */
	    if(lshift<0) {
	      ni = k*(y+abs(lshift))*x + (j+(int)(fabs(shift)*i+0.5))*x + i;
	    }

	    pic[ni] = extract_float(pic_v, cpos(i, j, k, pic_v));
        }
      }
    }
    /* final output */
    out = newVal(BSQ, x, y+abs(lshift), z, FLOAT, pic);
  }

  /* for trim != 0 */
  if(trim != 0) {

    if(y-abs(lshift)<=0) {
      parse_error("The trimmed array has a length of %d, you nincompoop. Stop trying to crash me.\n", y-abs(lshift));
      return NULL;
    }

    /*create the new picture array */
    pic = (float *)calloc(sizeof(float), x*z*(y-abs(lshift)));

    if (pic == NULL) {
      parse_error("ERROR! Unable to allocate %d bytes\n", sizeof(float)*x*z*(y+abs(lshift)));
      return NULL;
    }

    /* fill in the null value into all pic elements */
    if(nullv != 0) {
      for(k=0; k<z; k++) {
	for(j=0; j<y-abs(lshift); j++) {
	  for(i=0; i<x; i++) {
	    ni = k*(y-abs(lshift))*x + j*x + i;
	    pic[ni] = nullv;
	  }
	}
      }
    }

    /* extract the picture directly into a sheared, but trimmed, array */
    for(k=0; k<z; k++) {
      for(j=0; j<y-abs(lshift); j++) {
        for(i=0; i<x; i++) {

          /* the index of the new array */
          nx = k*(y-abs(lshift))*x + j*x + i;

          /* the shifted y pixel */
	    /* if the angle is greater than 0 */
 	    if(lshift>0) {
            nj = j + (int)(fabs(shift)*i+0.5);
          }

          /* if the angle is less than 0 */
          if(lshift<0) {
            nj = j + abs(lshift) - (int)(fabs(shift)*i+0.5);
          }

          pic[nx] = extract_float(pic_v, cpos(i, nj, k, pic_v));
        }
      }
    }
    /* final output */
    out = newVal(BSQ, x, y-abs(lshift), z, FLOAT, pic);
  } 
  return out;
}


Var *
thm_kfill(vfuncptr func, Var * arg)
{
  Var *pic_v = NULL;                                    /* the picture */
  Var *wgt_v = NULL;	                                /* the weighting array */
  Var *itr_v = NULL;	                                /* the number of iterations */
  Var *out;		                                /* the output picture */
  int    w;                                             /* width of the weight matrix (= height) */
  int    x,y,z;
  float *wgt = NULL;                                    /* storage location of the weight array */
  int    c;                                             /* general array counter */
  int    n;                                             /* number of elements - generally */
  int    i, j, k, mi, mj;                               /* general array counters */
  float  min_wgt;                                       /* minimum value of weight */
  float *pic = NULL, *pic2 = NULL, *tpic = NULL;
  float  pixel;                                         /* working var */
  int    wth;                                           /* (w-1)/2 */
  int    pic_x, pic_y;                                  /* dims of the expanded picture */
  int    px, py, pi;
  int    pl, pr, pu, pd, r, pave;                       /* the nearest neighbor positions */
  float  sum_non_zero_mask;
  int    nn;
  int    niter = 0;
  int    nzeroes = 0;

  
  Alist alist[4];
  alist[0] = make_alist("picture",		ID_VAL,		NULL,	&pic_v);
  alist[1] = make_alist("weight",		ID_VAL,		NULL,	&wgt_v);
  alist[2] = make_alist("iterations", 		ID_VAL,		NULL,	&itr_v);
  alist[3].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if (pic_v == NULL && wgt_v == NULL){
    parse_error("kfill() - 4/25/04");
    parse_error("Fills in null points of a 2 dimensional float array by linear interpolation of nearest neighbors.");
    parse_error("Will not handle 3-d arrays!\n");
    parse_error("Syntax:  b = thm.kfill(picture,weight,iterations)");
    parse_error("example: b = thm.kfill(b,wt)");
    parse_error("Set iterations to limit the maximum number of pixels away nearest neighbor must be to be valid.");
    parse_error("The weight array is produced by weight_array() - written by Phil.\n");
    return NULL;
  }
  if (pic_v == NULL) {
    parse_error("ERROR! picture not specified.\n"); return NULL;
  }
  
  if (wgt_v == NULL){
    parse_error("ERROR! weight matrix not specified.\n"); return NULL;
  }
  
  x = GetX(pic_v);		  /* get the x dimension of the image */
  y = GetY(pic_v);		  /* get the y dimension of the image */
  z = GetZ(pic_v);                /* get the z dimension of the image */
  if (z > 1){
    parse_error("Will not handle 3D arrays.\n"); return NULL;
  }

  w = GetX(wgt_v);		  /* get the x dimension of the weighting array */
  if (GetY(wgt_v) != w || GetZ(wgt_v) > 1){
    parse_error("Weight array must be the same dim in X & Y with a Z dim of 1.\n");
    return NULL;
  }

  if ((w%2) == 0 || w < 7){
    parse_error("Width should be an odd number greater than 5.\n");
    return NULL;
  }

  if (itr_v != NULL){
    niter = extract_int(itr_v, cpos(0, 0, 0, itr_v));
  }

  /* extract weight data */
  wgt = (float *)calloc(sizeof(float), w*w);
  if (wgt == NULL){
    parse_error("ERROR! Unable to alloc %d bytes.\n", sizeof(float)*w*w);
    return NULL;
  }

  c = 0;
  for(j = 0; j < w; j++){
    for(i = 0; i < w; i++){
      wgt[c++] = extract_float(wgt_v, cpos(i, j, 0, wgt_v));
    }
  }
  
  wth = (w-1)/2;		  /* the "radius" of the weighting array */
  
  /* extract picture data */
  pic_x = x+2*wth; pic_y = y+2*wth;
  pic = (float *)calloc(sizeof(float), pic_x*pic_y);
  if (pic == NULL){
    parse_error("ERROR! Unable to alloc %d bytes.\n", sizeof(float)*pic_x*pic_y);
    free(wgt); /* cleanup before exit */
    return NULL;
  }

  pic2 = (float *)calloc(sizeof(float), pic_x*pic_y);
  if (pic2 == NULL){
    parse_error("ERROR! Unable to alloc %d bytes.\n", sizeof(float)*pic_x*pic_y);
    free(wgt); /* cleanup before exit */
    free(pic);
    return NULL;
  }

  for(j = 0; j < y; j++){
    for(i = 0; i < x; i++){
      pic[(j+wth)*pic_x+(i+wth)] = extract_float(pic_v, cpos(i, j, 0, pic_v));
    }
  }

  /* find min of the weight array */
  n = w*w;
  min_wgt = wgt[0]; /* assume minimum */
  for(i = 1; i < n; i++){
    if (wgt[i] < min_wgt){ min_wgt = wgt[i]; }
  }

  if (min_wgt == 0.0){ min_wgt = 1.0; } /* sanity */

  /* divide wgt array by the minimum */
  for(i = 0; i < n; i++){ wgt[i] = wgt[i] / min_wgt; }

  /* iterate through data */
  for(k = 1; (k <= niter) || (niter == 0); k++){

    /* copy pic onto pic2 */
    memcpy(pic2, pic, sizeof(float)*pic_x*pic_y);
    nzeroes = 0;

    for(j = 0; j < y; j++){
      for(i = 0; i < x; i++){  
	
	py = j+wth; px = i+wth;
	pi = py * pic_x + px;
	
	if (pic[pi] == 0.0){

	  pl = 0;
	  pr = 0;
	  pu = 0;
	  pd = 0;
	  r = 1;
	  pave = 0;
	  nn = 0;

	  /* loop through the data to find the nearest neighbors in the 4 cardinal directions */
	  while(r <= wth) {
	    pl = (pl == 0 && pic[pi - r] > 0)?r:pl;
	    pr = (pr == 0 && pic[pi + r] > 0)?r:pr;
	    pu = (pu == 0 && pic[pi - r * pic_x] > 0)?r:pu;
	    pd = (pd == 0 && pic[pi + r * pic_x] > 0)?r:pd;

	    /* in the case that we don't have any near neighbors, set values to ave p */
	    if (r == wth) {
              nn = ((pl>0)?1:0) + ((pr>0)?1:0) + ((pu>0)?1:0) + ((pd>0)?1:0);
              if (nn > 0) {
		pave = (pl + pr + pu + pd) / nn;
		pl = (pl == 0)?pave:pl;
		pr = (pr == 0)?pave:pr;
		pu = (pu == 0)?pave:pu;
		pd = (pd == 0)?pave:pd;
	      }
	    }

	    r += 1;
	  }

	  if (nn>1) {
	    nzeroes++;
	    pixel = 0.0;
	    sum_non_zero_mask = 0.0;
	  
	    for(mj = wth - pu; mj <= wth + pd; mj++){
	      for(mi = wth - pl; mi <= wth + pr; mi++){
		pixel += pic[(j+mj)*pic_x+(i+mi)] * wgt[mj*w+mi];
		if (pic[(j+mj)*pic_x+(i+mi)] > 0.0){sum_non_zero_mask += wgt[mj*w+mi];}
	      }
	    }

	    if (sum_non_zero_mask == 0.0){ sum_non_zero_mask = 1.0; } /* sanity */

	    /* divide by weight */
	    /* assign pixel to location */
	    pic2[pi] = pixel / sum_non_zero_mask;
	  }
	}
      }
    }

    /* swap pic & pic2 arrays */
    tpic = pic; pic = pic2; pic2 = tpic;

    if (nzeroes == 0){
      break;
    }
  }

  if (niter == 0){
    parse_error("Done in %d iterations.\n", k);
  }

  /* construct return value */
  pic2 = (float *)realloc(pic2, sizeof(float)*x*y);
  if (pic2 == NULL){
    parse_error("blah");
    free(wgt);
    free(pic);
    return NULL;
  }

  /* copy data out */
  for(j = 0; j < y; j++){
    for(i = 0; i < x; i++){
      pic2[j*x+i] = pic[(j+wth)*pic_x+(i+wth)];
    }
  }
  out = newVal(BSQ, x, y, 1, FLOAT, pic2);

  /* cleanup before exit */
  free(wgt);
  free(pic);

  return out;
}








Var *
thm_convolve(vfuncptr func, Var * arg)
{
  Var *obj = NULL;                         /* the object to be smoothed */
  Var *kernel = NULL;                      /* the kernel with which to smooth */
  int norm = 1;                            /* initializing normalization to 1 */
  float ignore = 0;                        /* initializing ignore to 0 */
  int kernreduce = 0;                      /* option to use kernel reduction near obj boundaries */

  Alist alist[6];
  alist[0] = make_alist("object",       ID_VAL,         NULL,	      &obj);
  alist[1] = make_alist("kernel",       ID_VAL,         NULL,      &kernel);
  alist[2] = make_alist("normalize",    INT,            NULL,        &norm);
  alist[3] = make_alist("ignore",       FLOAT,          NULL,      &ignore);
  alist[4] = make_alist("kernreduce",   INT,            NULL,  &kernreduce);
  alist[5].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if (obj == NULL && kernel == NULL) {
    parse_error("Convolve() - 4/25/04");
    parse_error("Convolve function that will NOT fill in nullvalues of smoothed array with interpolated points.");
    parse_error("Instead it will fill null pixels in with the null value.");
    parse_error("Also provides for kernels that contain float values.\n");
    parse_error("Syntax:  b = thm.convolve(object, kernel, normalize, ignore, kernreduce)");
    parse_error("example: b = thm.convolve(a,clone(1.0,10,10,1,),ignore=0,0)");
    parse_error("object - may be any 1,2 or 3-D array.");
    parse_error("kernel - the convolve kernel, may also be any 1,2 or 3-d array.");
    parse_error("normalize - whether or not to normalize the output to the weight of the kernel. Default is 1 (yes).");
    parse_error("ignore - the null value of pixels to be ignored in calculation. Default is 0.");
    parse_error("kernreduce - whether to reduce the kernel on both sides as calculation approaches object boundary. Default is 0 (no).\n");
    return NULL;
  }
  if (obj == NULL) {
    parse_error("No object specified\n");
    return NULL;
  }
  if (kernel == NULL) {
    parse_error("No kernel specified\n");
    return NULL;
  }

  return(do_my_convolve(obj, kernel, norm, ignore, kernreduce));
}

static Var *do_my_convolve(Var *obj, Var *kernel, int norm, float ignore, int kernreduce)
{

  float *data;                             /* the smoothed array */
  int a, b, c;                             /* x, y, z position of kernel element */
  int x_pos, y_pos, z_pos;                 /* x, y, z position of object element */
  int obj_x, obj_y, obj_z;                 /* total x, y, z dimensions of object */
  int kx_center, krn_x;                    /* x radius and total x dimension of kernel */
  int ky_center, krn_y;                    /* y radius and total y dimension of kernel */
  int kz_center, krn_z;                    /* z radius and total z dimension of kernel */
  int x, y, z;                             /* x, y, z position of object convolved element */
  int i, j, k;                             /* memory locations */
  float kval, oval;                        /* value of kernel and object element */
  float *wt;                               /* array of number of elements in sum */
  int objsize;                             /* total 1-d size of object */
  int a_opp, b_opp, c_opp;                 /* x, y, z position of anti-symmetric kernel element */
  int x_opp, y_opp, z_opp;                 /* x, y, z position of anti-symmetric kernel element */

  obj_x = GetX(obj);
  obj_y = GetY(obj);
  obj_z = GetZ(obj);
  krn_x = GetX(kernel);
  krn_y = GetY(kernel);
  krn_z = GetZ(kernel);
  kx_center = krn_x/2;
  ky_center = krn_y/2;
  kz_center = krn_z/2;
  
  objsize = obj_x * obj_y * obj_z;

  data = (float *)calloc(sizeof(float), objsize);
  wt = (float *)calloc(sizeof(float), objsize);

  if (data == NULL || wt == NULL) {
    parse_error("Unable to allocate memory");
    return NULL ;
  }

  if(kernreduce==0) {
 
    for (i = 0 ; i < objsize ; i++) {                           /* loop through every element in object */
      if(extract_float(obj, i) == ignore) { data[i] = ignore; continue; }
      xpos(i, obj, &x, &y, &z);                                 /* compute current x,y,z position in object */
      for (a = 0 ; a < krn_x ; a++) {                           /* current x position of kernel */
	x_pos = x + a - kx_center;                              /* where the current operation is being done in x */
	if (x_pos < 0 || x_pos >= obj_x) continue;
	for (b = 0 ; b < krn_y ; b++) {                         /* current y position of kernel */    
	  y_pos = y + b - ky_center;                            /* where the current operation is being done in y */
	  if (y_pos < 0 || y_pos >= obj_y) continue;
	  for (c = 0 ; c < krn_z ; c++) {                       /* current z position of kernel */
	    z_pos = z + c - kz_center;                          /* where the current operation is being done in z */
	    if (z_pos < 0 || z_pos >= obj_z) continue;
	    j = cpos(x_pos, y_pos, z_pos, obj);                 /* position in 1-D object at (x_pos,y_pos,z_pos) */
	    k = cpos(a, b, c, kernel);                          /* position in 1-D kernel at (a,b,c) */
	    kval = extract_float(kernel,k);                     /* value of kernel at (k) */
	    oval = extract_float(obj, j);                       /* value of object at (j) */
	    if (oval != ignore && kval != ignore) {
	      wt[i] += kval;                                    /* sum of values in used pixels of kernel*/
	      data[i] += kval * oval;
	    }
	  } 
	}
      }

      if (norm != 0 && wt[i] != 0) {
	data[i] /= wt[i];
      }
    } 
  }

  if(kernreduce==1) {

    for (i = 0 ; i < objsize ; i++) {                           /* loop through every element in object */
      if(extract_float(obj, i) == ignore) { data[i] = ignore; continue; }
      xpos(i, obj, &x, &y, &z);                                 /* compute current x,y,z position in object */
      for (a = 0 ; a < krn_x ; a++) {                           /* current x position of kernel */
	a_opp = krn_x - a - 1;                                  /* anti-symmetric x position of kernel */
	x_pos = x + a - kx_center;                              /* where the current operation is being done in x */
	x_opp = x + a_opp - kx_center;                          /* anti-symmetric x position of object */
	if (x_pos < 0 || x_pos >= obj_x || x_opp < 0 || x_opp >= obj_x) continue;
	for (b = 0 ; b < krn_y ; b++) {                         /* current y position of kernel */    
	  b_opp = krn_y - b - 1;                                /* anti-symmetric y position of kernel */
	  y_pos = y + b - ky_center;                            /* where the current operation is being done in y */
	  y_opp = y + b_opp - ky_center;                        /* anti-symmetric y position in object */
	  if (y_pos < 0 || y_pos >= obj_y || y_opp < 0 || y_opp >= obj_y) continue;
	  for (c = 0 ; c < krn_z ; c++) {                       /* current z position of kernel */
	    c_opp = krn_z - c - 1;                              /* anti-symmetric z position of kernel */
	    z_pos = z + c - kz_center;                          /* where the current operation is being done in z */
	    z_opp = z + c_opp - kz_center;                      /* anti-symmetric z position in object */
	    if (z_pos < 0 || z_pos >= obj_z || z_opp < 0 || z_opp >= obj_z) continue;
	    j = cpos(x_pos, y_pos, z_pos, obj);                 /* position in 1-D object at (x_pos,y_pos,z_pos) */
	    k = cpos(a, b, c, kernel);                          /* position in 1-D kernel at (a,b,c) */
	    kval = extract_float(kernel,k);                     /* value of kernel at (k) */
	    oval = extract_float(obj, j);                       /* value of object at (j) */
	    if (oval != ignore && kval != ignore) {
	      wt[i] += kval;                                    /* sum of used pixels in kernel */
	      data[i] += kval * oval;
	    }
	  } 
	}
      }

      if (norm != 0 && wt[i] != 0) {
	data[i] /= wt[i];
      }
    } 
  }

  free(wt);
  return(newVal(V_ORG(obj), V_SIZE(obj)[0], V_SIZE(obj)[1], V_SIZE(obj)[2], FLOAT, data));
}






float *convolve(float *obj, float *kernel, int ox, int oy, int oz, int kx, int ky, int kz, int norm, float ignore)
{

  float   *data;                                /* the smoothed array */
  float   *wt;                                  /* array of number of elements in sum */
  int      a, b, c;                             /* x, y, z position of kernel element */
  int      x_pos, y_pos, z_pos;                 /* x, y, z position of object element */
  int      kx_center;                           /* x radius and total x dimension of kernel */
  int      ky_center;                           /* y radius and total y dimension of kernel */
  int      kz_center;                           /* z radius and total z dimension of kernel */
  int      x, y, z;                             /* x, y, z position of object convolved element */
  int      i, j, k;                             /* memory locations */
  float    kval, oval;                          /* value of kernel and object element */
  int      objsize;                             /* total 1-d size of object */

  kx_center = kx/2;
  ky_center = ky/2;
  kz_center = kz/2;
  
  objsize = ox * oy * oz;

  data = (float *)calloc(sizeof(float), objsize);
  wt = (float *)calloc(sizeof(float), objsize);

  if (data == NULL || wt == NULL) {
    parse_error("Unable to allocate memory");
    return NULL ;
  }
 
  for (i = 0 ; i < objsize ; i++) {                               /* loop through every element in object */
    if(obj[i] == ignore) { data[i] = ignore; continue; }

    z = i/(ox*oy);                                                /* current x position in object */
    y = (i - z*ox*oy)/ox ;                                        /* current y position in object */
    x =  i - z*ox*oy - y*ox;                                      /* current z position in object */

    for (a = 0 ; a < kx ; a++) {                                  /* current x position of kernel */
      x_pos = x + a - kx_center;                                  /* where the current operation is being done in x */
      if (x_pos < 0 || x_pos >= ox) continue;
      for (b = 0 ; b < ky ; b++) {                                /* current y position of kernel */    
	y_pos = y + b - ky_center;                                /* where the current operation is being done in y */
	if (y_pos < 0 || y_pos >= oy) continue;
	for (c = 0 ; c < kz ; c++) {                              /* current z position of kernel */
	  z_pos = z + c - kz_center;                              /* where the current operation is being done in z */
	  if (z_pos < 0 || z_pos >= oz) continue;
	  j = z_pos*ox*oy + y_pos*ox + x_pos;                     /* position in 1-D object at (x_pos,y_pos,z_pos) */
	  k = c*kx*ky + b*kx + a;                                 /* position in 1-D kernel at (a,b,c) */
	  kval = kernel[k];                                       /* value of kernel at (k) */
	  oval = obj[j];                                          /* value of object at (j) */
	  if (oval != ignore && kval != ignore) {
	    wt[i] += kval;                                        /* sum of values of pixels of the kernel used */
	    data[i] += (kval * oval);
	  }
	} 
      }
    }

    if (norm != 0 && wt[i] != 0) {
      data[i] /= (float)wt[i];
    }
  } 

  free(wt);
  return(data);
}







Var *
thm_ramp(vfuncptr func, Var * arg)
{
    Var *pic_1 = NULL;		/* picture one */
    Var *pic_2 = NULL;		/* picture two */
    Var *out;			/* the output picture */
    float *pict1 = NULL;	/* storage location of picture 1 */
    float *pict2 = NULL;	/* storage location of picture 2 */
    int *ol1 = NULL;		/* the overlap in picture 1 */
    int *ol2 = NULL;		/* the overlap in picture 2 */
    int nullv = 0;		/* the null value that is to be ignored when calculating overlap */
    int r1 = -1, r2 = -1;	/* beginning and ending rows of the overlap */
    int c1 = -1, c2 = -1;	/* beginning and ending columns of the overlap */
    int i, j, k;			/* loop indices */
    int w, x, y, z;		/* dimensions of the input pictures */
    int u, v;			/* dimensions of the overlapping area */
    int m, n;			/* fill-in value and zero value */
    int t1, t2;			/* memory location */
    int ct = 1;			/* counter */
    int num = 2;		/* fill-in number */
    int olm1 = 2, olm2 = 2;	/* maximum values for the ol1 & ol2 arrays */
	int up, down, left, right;	/* some neighborhood indices */

    Alist alist[4];
    alist[0] = make_alist("pic1", ID_VAL, NULL, &pic_1);
    alist[1] = make_alist("pic2", ID_VAL, NULL, &pic_2);
    alist[2] = make_alist("ignore", INT, NULL, &nullv);
    alist[3].name = NULL;

    if (parse_args(func, arg, alist) == 0)
	return (NULL);

    /* if no pictures got passed to the function */
    if (pic_1 == NULL || pic_2 == NULL) {
	parse_error("ramp() - 4/25/04");
	parse_error
	    ("Calculates a 0 - 1 float ramp between two overlapping pictures.");
	parse_error
	    ("You need to pass me two overlapping pictures contained in arrays of identical size, and an ignore value.\n");
	parse_error("Syntax:  b = thm.ramp(pic1,pic2,ignore)");
	parse_error("example: b = thm.ramp(a1,a2,ignore=-32768");
	parse_error
	    ("pic1 - may be any 2-d array - float, int, short, etc.");
	parse_error
	    ("pic2 - may be any 2-d array - float, int, short, etc.");
	parse_error("ignore - the non-data pixel values. Default is 0.\n");
	return NULL;
    }

    /* x and y dimensions of the original pictures */
    x = GetX(pic_1);
    y = GetY(pic_1);
    w = GetX(pic_2);
    z = GetY(pic_2);

    if (x != w || y != z) {
	parse_error
	    ("The two pictures need to be of the exact same dimensions.\n");
	return NULL;
    }

    /* keith is using an array that is +-1 larger, so that he doesn't
     * accidentially hit an edge 
     */
    w = x+2;
    z = y+2;

    /* create the two picture arrays and the overlap arrays */
    pict1 = (float *) calloc(sizeof(float), w * (z));
    pict2 = (float *) calloc(sizeof(float), w * (z));
    ol1 = (int *) calloc(sizeof(int), w * (z));
    ol2 = (int *) calloc(sizeof(int), w * (z));

    /* extract the two pictures into their storage arrays */
    r1 = y+1;
    r2 = 0;
    c1 = x+1;
    c2 = 0;

    for (j = 1; j <= y ; j++) {
	for (i = 1; i <= x ; i++) {
	    t1 = j * w + i;
	    pict1[t1] = extract_float(pic_1, cpos(i - 1, j - 1, 0, pic_1));
	    pict2[t1] = extract_float(pic_2, cpos(i - 1, j - 1, 0, pic_2));

	    /* find left and right limits */
	    if (pict1[t1] != nullv && pict2[t1] != nullv) {
		if (j < r1) { r1 = j; }
		if (j > r2) { r2 = j; }
		if (i < c1) { c1 = i; }
		if (i > c2) { c2 = i; }
	    }

	}
    }

    /*
    ** set all the edge values
    */
    for (k = 0; k <= y+1 ; k++) {
	t1 = k * w + 0;
	pict1[t1] = nullv;
	pict2[t1] = nullv;
	ol1[t1] = -1;
	ol2[t1] = -1;
	
	t1 = k * w + (x+1);
	pict1[t1] = nullv;
	pict2[t1] = nullv;
	ol1[t1] = -1;
	ol2[t1] = -1;
    }
    for (k = 0; k <= x+1 ; k++) {
	t1 = 0 * w + k;
	pict1[t1] = nullv;
	pict2[t1] = nullv;
	ol1[t1] = -1;
	ol2[t1] = -1;

	t1 = (y+1) * w + k;
	pict1[t1] = nullv;
	pict2[t1] = nullv;
	ol1[t1] = -1;
	ol2[t1] = -1;
    }

    /* loop through the picts one time to find the edge */
    /* if any of my src neighbors are null, then set me to 1 */
    for (j = r1; j <= r2; j++) {
	for (i = c1; i <= c2; i++) {
	    t1 = j * w + i;
	    if (pict1[t1] != nullv && pict2[t1] != nullv) {
		up = (j-1) * w + i;
		down = (j+1) * w + i;
		left = t1-1;
		right = t1+1;
		if (pict1[left] == nullv || pict1[right] == nullv
		    || pict1[up] == nullv || pict1[down] == nullv) {
		    ol1[t1] = 1;
		}
		if (pict2[left] == nullv || pict2[right] == nullv
		    || pict2[up] == nullv || pict2[down] == nullv) {
		    ol2[t1] = 1;
		}
	    }
	}
    }

    /* loop through the overlap arrays filling the appropriate value in */
    while (ct > 0) {
	ct = 0;
	for (j = r1; j <= r2; j++) {
	    for (i = c1; i <= c2; i++) {
		t1 = j * w + i;
		if (pict1[t1] != nullv && pict2[t1] != nullv) {
		    up = (j-1) * w + i;
		    down = (j+1) * w + i;
		    left = t1 - 1;
		    right = t1 + 1;
		    n = num-1;
		    if (ol1[t1] == 0
			&& ((ol1[left] == n) || (ol1[right] == n)
			    || (ol1[up] == n) || (ol1[down] == n))) {
			ol1[t1] = num;
			ct += 1;
		    }
		    if (ol2[t1] == 0
			&& ((ol2[left] == n) || (ol2[right] == n)
			    || (ol2[up] == n) || (ol2[up] == n))) {
			ol2[t1] = num;
			ct += 1;
		    }
		}
	    }
	}
	num += 1;
    }

    /* reallocate the size of pict1 for use as an output array and free pict2 */
    free(pict1);
    free(pict2);
    pict1 = (float *) calloc(sizeof(float), x * y);

    /* loop through one last time creating the output array */
    for (j = 0; j < y; j++) {
	for (i = 0; i < x; i++) {
	    t1 = j * x + i;
	    t2 = (j + 1) * w + i + 1;
	    if (ol1[t2] > 0) {
		pict1[t1] =
		    (float) (ol2[t2]) / (float) (ol2[t2] + ol1[t2]);
	    }
	}
    }

    free(ol1);
    free(ol2);

    out = newVal(BSQ, x, y, 1, FLOAT, pict1);
    return out;
}






Var *
thm_corners(vfuncptr func, Var * arg)
{
  Var     *pic_a = NULL;                               /* the input pic */
  float    nullval = 0;                                /* null value */
  int     *cns = NULL;                                 /* corners output array */
  
  Alist alist[3];
  alist[0] = make_alist("picture",		ID_VAL,		NULL,	&pic_a);
  alist[1] = make_alist("null",                 FLOAT,          NULL,   &nullval);
  alist[2].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no picture got passed to the function */
  if (pic_a == NULL) {
    parse_error("corners() - 4/25/04");
    parse_error("Feed corners() a 1-band projected ISIS image and a null value.");
    parse_error("It will return the x and y values of the four corners.\n");
    parse_error("Syntax:  b = corners(picture,null)");
    parse_error("example: b = corners(a,null=-32768)");
    parse_error("picture - any 3-D projected image.");
    parse_error("null - the non-data pixel values. Default is 0.");
    parse_error("If an image is passed with more than one band, corners will only use the first band.\n");
    return NULL;
  }

  cns = do_corners(pic_a, nullval);
  return(newVal(BSQ, 1, 8, 1, INT, cns));
}


int *do_corners(Var *pic_a, float nullval)
{
  typedef unsigned char byte;

  byte    *pic = NULL;                                 /* the modified pic */
  int      x, y;                                       /* dimensions of the original pic */
  int      i, j;                                       /* loop indices */
  int      trx = -1, try = -1, tlx = -1, tly = -1;     /* xy positions of top corners */
  int      brx = -1, bry = -1, blx = -1, bly = -1;     /* xy positions of bottom corners */
  int      tmyv = -1, bmyv = -1;                       /* top most y val, bottom most y val */
  int      lmxv = -1, rmxv = -1;                       /* left most x val, right most x val */
  int      lmyv = -1, rmyv = -1;                       /* left most y val, right most y val */
  int      lmyva = -1, rmyva = -1;                     /* opposite approach of y lmyv, rmyv */
  int     *row_avg, *col_avg;                          /* row and column avg */
  int     *corners = NULL;                             /* corners array */
  float    pic_val = 0;                                /* extracted float val from pic */

  /* x and y dimensions of the original picture */
  x = GetX(pic_a);
  y = GetY(pic_a);

  /* create row_avg and col_avg arrays */
  row_avg = (int *)calloc(sizeof(int), y);
  col_avg = (int *)calloc(sizeof(int), x);

  /* create corners array */
  corners = (int *)calloc(sizeof(int),8);
  for(i=0;i<=7;i++) {
    corners[i] = -1;
  }
  
  if (row_avg == NULL) {
    parse_error("\nError! Unable to allocate %d bytes for row_avg\n", sizeof(int)*y);
    return NULL;
  }
  if (col_avg == NULL) {
    parse_error("\nError! Unable to allocate %d bytes for col_avg\n", sizeof(int)*x);
    return NULL;
  }
  
  /* allocate memory for the pic */
  pic = (byte *)calloc(sizeof(byte),y*x);
  
  if (pic == NULL) {
    parse_error("\nERROR! AHHHHH...I can't remember the picture!\n");
    return NULL;
  }
  
  /* extract the picture, row_avg and col_avg */
  for(j=0; j<y; j++) {
    for(i=0; i<x; i++) {
      pic_val = extract_float(pic_a, cpos(i, j, 0, pic_a));
      if(pic_val != nullval) {
	//printf("%f ",pic_val);
	pic[j*x+i] = 1;
	row_avg[j] += pic[j*x+i];
	col_avg[i] += pic[j*x+i];
      }
    }
    //printf("%d\n",row_avg[j]);
  }

  /* loop through row_avg */
  j=0;
  while(tmyv == -1 || bmyv == -1 && j < y) {
    if(row_avg[j] != 0 && tmyv == -1) {
      tmyv = j;
    }
    if(row_avg[y-j-1] != 0 && bmyv == -1) {
      bmyv=y-j-1;
    }
    j+=1;
  }

  /* loop through col_avg */
  i=0;
  while(lmxv == -1 || rmxv == -1 && i < x) {
    if(col_avg[i] != 0 && lmxv == -1) {
      lmxv = i;
    }
    if(col_avg[x-i-1] != 0 && rmxv == -1) {
      rmxv=x-i-1;
    }
    i+=1;
  }

  /* find corresponding y vals to the left most and right most x vals */
  j=0;
  while(lmyv == -1 || rmyv == -1 && j < y) {
    if(pic[j*x+lmxv] != 0 && lmyv == -1) {
      lmyv = j;
    }
    if(pic[(y-j-1)*x+lmxv] != 0 && lmyva == -1) {
      lmyva = y-j-1;
    }
    if(pic[(y-j-1)*x+rmxv] != 0 && rmyv == -1) {
      rmyv = y-j-1;
    }
    if(pic[j*x+rmxv] != 0 && rmyva == -1) {
      rmyva=j;
    }
    j+=1;
  }

  /* case where top of image leans to the left of the bottom of the image */
  /* approach top most x value from the right */
  if(rmyv>=lmyv) {
    i=0;
    while(corners[2] == -1 || corners[4] == -1 && i < x) {
      if(pic[tmyv*x+(x-i-1)] != 0 && corners[2] == -1) {
	corners[2] = x-i;
	corners[3] = tmyv + 1;
	corners[0] = lmxv + 1;
	corners[1] = lmyv + 1;
      }
      if(pic[bmyv*x+i] != 0 && corners[4] == -1 ) {
	corners[4] = i + 1;
	corners[5] = bmyv + 1;
	corners[6] = rmxv + 1;
        corners[7] = rmyv + 1;
      }
      i+=1;
    } 
  }
  
  /* case where top of image leans to the right of the bottom of the image */
  /* approach top most x value from the left */
  if(lmyv>=rmyv) {
    i=0;
    while(corners[0] == -1 || corners[6] == -1 && i < x) {
      if(pic[tmyv*x+i] != 0 && corners[0] == -1) {
	corners[0] = i + 1;
	corners[1] = tmyv + 1;
	corners[2] = rmxv + 1;
	corners[3] = rmyva + 1;
      }
      if(pic[bmyv*x+(x-i-1)] != 0 && corners[6] == -1 ) {
	corners[6] = x-i;
	corners[7] = bmyv + 1;
	corners[4] = lmxv + 1;
	corners[5] = lmyva + 1;
      }
      i+=1;
    }
  }

  /* clean up memory */
  free(pic);
  free(col_avg);
  free(row_avg);

  /* return array */  
  return(corners);
}






Var *
thm_sawtooth(vfuncptr func, Var * arg)
{

  Var *out;                   /* output array */
  float *tooth;               /* the output of sawtooth routine */
  int x = 0, y = 0, z = 0;

  Alist alist[4];
  alist[0] = make_alist("x",       INT,         NULL,	&x);
  alist[1] = make_alist("y",       INT,         NULL,	&y);
  alist[2] = make_alist("z",       INT,         NULL,   &z);
  alist[3].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (x == 0 || y == 0 || z == 0) {
    parse_error("sawtooth() - 4/25/04");
    parse_error("Creates a sawtooth filter kernel of size x by y by z\n");
    parse_error("Syntax: a = thm.sawtooth(1,300,1)\n");
    return NULL;
  }

  tooth = sawtooth(x, y, z);
  out = newVal(BSQ, x, y, z, FLOAT, tooth);
  return out;
}

float *sawtooth(int x, int y, int z)
{
  float *kernel;                           /* the end kernel array */
  int i, j, k;                             /* loop indices */
  float xp, yp, zp;                        /* value of orthogonal parts */
  float xm, ym, zm;                        /* multiplier value in x, y, z */
  float total;                             /* sum value of filter */

  kernel = (float *)calloc(sizeof(float), x*y*z);
  xm = (x>1)?4.0/((float)x):1.0;
  ym = (y>1)?4.0/((float)y):1.0;
  zm = (z>1)?4.0/((float)z):1.0;

  for(k=0; k<z; k++) {
    zp = (z>1)?(((z+1)/2)-abs((z/2)-k))*zm:1.0;
    for(j=0; j<y; j++) {
      yp = (y>1)?(((y+1)/2)-abs((y/2)-j))*ym:1.0;
      for(i=0; i<x; i++) {
	xp = (x>1)?(((x+1)/2)-abs((x/2)-i))*xm:1.0;
	kernel[k*x*y + j*x + i] = xp*yp*zp;
	total += xp*yp*zp;
      }
    }
  }

  total = (float)x*(float)y*(float)z/total;

  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	kernel[k*x*y + j*x + i] = kernel[k*x*y + j*x + i]*total;
      }
    }
  }

  return(kernel);
}







Var *
thm_deplaid(vfuncptr func, Var * arg)
{

  typedef unsigned char byte;

  Var      *data = NULL;                            /* the original radiance data */
  Var      *out = NULL;                             /* the output array */
  float    *rdata = NULL;                           /* the deplaided array */
  float    *row_sum;                                /* a row sum array used to determine blackmask */
  float    *row_avg, *row_avgs;                     /* the row and smoothed row average arrays */
  float    *col_avg;                                /* the column average of both runs combined */
  float    *col_avga, *col_avgb;                    /* the column averages of run a and b */
  float    *col_avgs;                               /* the smoothed column averages of run a and b */
  float    *row_bright, *col_bright;                /* brightness arrays */
  //float    *row_brights, *col_brights;              /* deplaided brightness arrays */
  byte     *ct_map;                                 /* map of # of bands per pixel */
  byte     *row_ct, *col_ct;                        /* max number of bands in all rows and all columns */
  byte     *blackmask;                              /* the blackmask and temperature mask */
  float    *tmask;                                  /* the temporary temperature mask */
  float     nullval = -32768;                       /* the null value */
  float     tmask_max = 1.15;                       /* maximum threshold for temperature masking */
  float     tmask_min = 0.80;                       /* minimum threshold for temperature masking */
  int       filt_len = 150;                         /* filter length */
  int      *row_wt, *col_wt, *col_wta, *col_wtb;    /* weight arrays for row and column averages */
  int       x = 0, y = 0, z = 0;                    /* dimensions of the pic */
  int       i, j, k;                                /* loop indices */
  int       chunks = 0, chunksa = 0, chunksb = 0;   /* number of chunks image is broken into */
  int       cca = 0, ccb = 250;                     /* line indices for the chunk a and chunk b calculations */
  int       ck = 0, cka = 0, ckb = 0;               /* number of chunk that calculation is on */
  float     tv = 0;                                 /* temporary pixel value */
  float    *filt1, *filt2, *sawkern;                /* various convolve kernels */
  int       dump = 0;                               /* flag to return the temperature mask */
  int       b10 = 10;                               /* the band-10 designation */
  float    *b_avg, *b_ct;                           /* band average and band count - used to normalize bands for blackmask */

  Alist alist[8];
  alist[0] = make_alist("data", 		ID_VAL,		NULL,	&data);
  alist[1] = make_alist("null",                 FLOAT,          NULL,   &nullval);
  alist[2] = make_alist("tmask_max",            FLOAT,          NULL,   &tmask_max);
  alist[3] = make_alist("tmask_min",            FLOAT,          NULL,   &tmask_min);
  alist[4] = make_alist("filt_len",             INT,            NULL,   &filt_len);
  alist[5] = make_alist("b10",                  INT,            NULL,   &b10);
  alist[6] = make_alist("dump",                 INT,            NULL,   &dump);
  alist[7].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no data got passed to the function */
  if (data == NULL) {
    parse_error("\nDeplaid() - 5/19/04\n");
    parse_error("Deplaids multiband THEMIS spectral cubes\n");
    parse_error("Syntax: b = thm.deplaid(data,null,tmask_max,tmask_min,filt_len,b10)");
    parse_error("example b = thm.deplaid(a)");
    parse_error("example b = thm.deplaid(data=a,null=-32768,tmask_max=1.25,tmask_min=.85,filt_len=200,b10=3)");
    parse_error("example b = thm.deplaid(a,0,tmask_max=1.21)");
    parse_error("example b = thm.deplaid(a,0,1.10,.90,180,8)\n");
    parse_error("data:       geometrically projected and rectified radiance cube of at most 10 bands");
    parse_error("null:       the value of non-data pixels -                         default is -32768");
    parse_error("tmask_max:  max threshold used to create the temperature mask -    default is 1.15");
    parse_error("tmask_min:  min threshold used to create the temperature mask -    default is 0.80");
    parse_error("filt_len:   length of filter used in plaid removal -               default is 150");
    parse_error("b10:        the band (1 - 10) in the data that is THEMIS band 10 - default is 10\n");
    parse_error("TROUBLESHOOTING");
    parse_error("You should designate b10 when NOT feeding deplaid 10 band data, b10 = 0 for no band-10 data.");
    parse_error("You can return the blackmask/temperature mask by using \"dump=1\".");
    parse_error("If you get brightness smear, then bring the tmask_max and/or min closer to 1.0.");
    parse_error("If you still see long-wavelength plaid, such as in cold images, increase filt_len.");
    parse_error("If the image looks washed-out check your null value.\n");
    return NULL;
  }

  /* x, y and z dimensions of the data */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);

  /* setting the band-10 of the data */
  b10 -= 1;
  if(b10 >= z || b10 <-1) {
    parse_error("Invalid band 10 designation!");
    if(z == 10) {
      parse_error("b10 being reset to 10\n");
      b10 = 9;
    } else {
      parse_error("assuming NO band 10 data, b10 being reset to %d\n",z-1);
      b10 == z-1;
    }
  }

  /* number of chunks of data in the y direction */
  chunksa = ((y-100)/500) + 1;                                 /* number of chunks starting at zero */
  chunksb = ((y-350)/500) + 2;                                 /* number of chunks starting at 250 */
  if(y<350) chunksb = 1;
  chunks = ((y-100)/250) + 1;

  /* create row and mask arrays */
  row_avg = (float *)calloc(sizeof(float), y*z);
  row_wt = (int *)calloc(sizeof(int), y*z);
  blackmask = (byte *)malloc(sizeof(byte)*x*y);
  tmask = (float *)calloc(sizeof(float), x*y);
  ct_map = (byte *)calloc(sizeof(byte), x*y);
  row_ct = (byte *)calloc(sizeof(byte), y);
  col_ct = (byte *)calloc(sizeof(byte), x);

  /* check to make sure that tmask_max, tmask_min and filt_len are reasonable values */
  if(tmask_max < tmask_min || tmask_max == 0) {
    parse_error("tmask_max must be larger than tmask_min, dopo!");
    parse_error("you should keep tmask_max > 1.0 and tmask_min < 1.0");
    parse_error("values are being reset to 1.15 and 0.80");
    tmask_max = 1.15;
    tmask_min = 0.80;
  }
  if(filt_len < 1) {
    parse_error("the filter must have some length, you tool!");
    parse_error("filter being reset to 150");
    filt_len = 150;
  }

  /* b_avg is a 1xNx1 array of the average of each of the N bands */
  b_avg = (float *)calloc(sizeof(float), z);
  /* c_ct is the weight array for b_avg */
  b_ct = (float *)calloc(sizeof(float), z);

  /* find the averages of each band so's I can normalize them */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = extract_float(data, cpos(i,j,k, data));

	if(tv != nullval) {
	  b_avg[k] += tv;
	  b_ct[k] += 1;
	}
      }
    }
    b_avg[k] /= b_ct[k];
  }

  free(b_ct);

  /* construct blackmask, tempmask and row_avg */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = extract_float(data, cpos(i,j,k, data));

	if(tv != nullval) {
	  blackmask[x*j + i] = 1;
	  tmask[x*j + i] += tv/b_avg[k];                             /* tmask is sum of all bands */
	  row_avg[j] += tv/b_avg[k];                                 /* calculating row totals for tmask */
	  ct_map[x*j + i] += 1;                                      /* incrementing pixel count_map */
	  row_wt[j] += 1;                                            /* calculating row weight for tmask */
	}

	/* setting the col and row count arrays */
	if(k==z-1) {
	  if(row_ct[j] < ct_map[j*x + i]) row_ct[j] = ct_map[j*x + i];
	  if(col_ct[i] < ct_map[j*x + i]) col_ct[i] = ct_map[j*x + i];
	  if(ct_map[x*j + i] != 0) tmask[x*j + i] /= (float)ct_map[x*j + i];
	}
      }

      if(k == z-1 && row_wt[j] != 0) {
	row_avg[j] /= row_wt[j];                                     /* calculating row avg for tmask */
	row_wt[j] = 0;
      }
    }
  }

  /* continue to construct the temperature mask */
  /* smooth the row avg of tempmask with a sawtooth filter */
  sawkern = sawtooth(1, 301, 1);
  row_sum = convolve(row_avg, sawkern, 1, y, 1, 1, 301, 1, 1, 0);
  free(sawkern);
  free(ct_map);
  free(b_avg);

  /* loop through setting the tempmask values */
  /* pixels with tmask values greater than 1.25 or less than .8 of the row avg are set to zero */
  for(j=0; j<y; j++) {
    for(i=0; i<x; i++) {
      if(tmask[x*j+i] < row_sum[j]*tmask_max && tmask[x*j+i] > row_sum[j]*tmask_min) blackmask[x*j+i] += 1;
    }
    for(k=0; k<z; k++) {
      row_avg[k*y + j] = 0;
    }
  }
  free(tmask);
  free(row_sum);

  /* return the temperature mask if dump = 1 */
  if(dump == 1) {
    free(row_avg);
    free(row_wt);
    out = newVal(BSQ, x, y, 1, BYTE, blackmask);
    return out;
  }

  /* more initialization */
  col_avg = (float *)calloc(sizeof(float), x*chunks*z);      /* col_avg contains enough space for all 250 line chunks */
  col_avga = (float *)calloc(sizeof(float), x*chunksa*z);
  col_avgb = (float *)calloc(sizeof(float), x*chunksb*z);
  col_wta = (int *)calloc(sizeof(int), x*chunksa*z);
  col_wtb = (int *)calloc(sizeof(int), x*chunksb*z);

  if(col_avg == NULL || col_avga == NULL || col_avgb == NULL || col_wta == NULL || col_wtb == NULL) { 
    parse_error("Could not allocate enough memory to continue\n");
    return NULL;
  }

  /* remove the horizontal and vertical plaid */
  /* loop through data and extract row_avg, and all necessary column averages */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = extract_float(data, cpos(i,j,k, data));

	/* if not a null val and tempmask ok */
	if(tv != nullval && ((blackmask[x*j + i] > 1 && k != b10) || k == b10)) {

	  if(col_ct[i] == z) {
	    row_avg[y*k + j] += tv;                                  /* calculate tempmasked row total of data */
	    row_wt[y*k + j] += 1;                                    /* calculate tempmasked row weight of data */
	  }
	  
	  if(row_ct[j] == z) {
	    col_avga[k*chunksa*x + cka*x + i] += tv;                 /* calculate tempmasked col total of chunka */
	    col_avgb[k*chunksb*x + ckb*x + i] += tv;                 /* calculate tempmasked col total of chunkb */
	    col_wta[k*chunksa*x + cka*x + i] += 1;                   /* calculate tempmasked col weight of chunka */
	    col_wtb[k*chunksb*x + ckb*x + i] += 1;                   /* calculate tempmasked col weight of chunkb */
	  }
	}
 
	/* if at the end of a chunk and there is less than 100 rows of data left, keep going in present chunk */
	if(((y-j) <= 100) && (ccb == 499 || cca == 499)) {
	  cca -= 100; 
	  ccb -= 100;
	}

	/* if at the end of chunk then divide column total by column weight and construct chunk of col_avg */
	if(cca == 499) {
	  /* divide col_avga by the number of non-zero lines summed to create average */
	  if(col_wta[k*chunksa*x + cka*x + i] > 0) {
	    col_avga[k*chunksa*x + cka*x + i] /= (float)col_wta[k*chunksa*x + cka*x + i];
	  } else { /* if col_wta is 0 don't divide */
	    col_avga[k*chunksa*x + cka*x + i] = 0;
	  }
	  /* avg col_avga and col_avgb for the 250 line chunk */
	  col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + (ckb-1)*x + i]);
	  if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + (ckb-1)*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	}

	if(ccb == 499) {
	  /* divide col_avgb by the number of non-zero lines summed to create average */
	  if(col_wtb[k*chunksb*x + ckb*x + i] > 0) {
	    col_avgb[k*chunksb*x + ckb*x + i] /= (float)col_wtb[k*chunksb*x + ckb*x + i];
	  } else { /* if col_wtb is 0 don't divide */
	    col_avgb[k*chunksb*x + ckb*x + i] = 0;
	  }
	  /* avg col_avga and col_avgb for the 250 line chunk */
	  if(ckb > 0) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + (cka-1)*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + (cka-1)*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	}

	if(j == y-1) {
	  if(col_wta[k*chunksa*x + cka*x + i] > 0) {
	    col_avga[k*chunksa*x + cka*x + i] /= (float)col_wta[k*chunksa*x + cka*x + i];
	  } else {
	    col_avga[k*chunksa*x + cka*x + i] = 0;
	  }
	  if(col_wtb[k*chunksb*x + ckb*x + i] > 0) {
	    col_avgb[k*chunksb*x + ckb*x + i] /= (float)col_wtb[k*chunksb*x + ckb*x + i];
	  } else {
	    col_avgb[k*chunksb*x + ckb*x + i] = 0;
	  }
	  /* avg col_avga and col_avgb for second to last 250 line chunk */
	  if(cca>ccb && chunksb > 1) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + (ckb-1)*x + i]);
	    if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + (ckb-1)*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	  if(ccb>cca && chunksa > 1) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + (cka-1)*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + (cka-1)*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	  /* avg col_avga and col_avgb for the final 250 line chunk */
	  if(ck<chunks-1 && chunksb > 1) {
	    col_avg[k*chunks*x + (ck+1)*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + (ck+1)*x + i] /= 2.0;
	  }
	  if(chunksa == 1 && chunksb == 1) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	}
      }

      cca += 1;                                              /* increment the chunka line indice */
      ccb += 1;                                              /* increment the chunkb line indice */
      if(cca == 500) {                                       /* if at end of chunk, start new chunk */
	cca = 0;
	cka += 1;
	ck += 1;
      }
      if(ccb == 500) {                                       /* if at end of chunk, start new chunk */
	ccb = 0;
	if(ckb > 0) ck += 1;                                 /* don't increment the ck until cka and ckb have at least one finished chunk */
	ckb += 1;
      }

      if(row_avg[y*k + j] != 0 && row_wt[y*k + j] != 0) {
	row_avg[y*k + j] /= (float)row_wt[y*k + j];          /* perform division for row_avg values*/
	row_wt[y*k + j] = 0;
      }
    }
    cka = 0;
    ckb = 0;
    cca = 0;
    ccb = 250;
    ck = 0;
  }

  /* clean up col_avg arrays */
  free(col_avga);
  free(col_avgb);
  free(col_wta);
  free(col_wtb);
  free(row_ct);
  free(col_ct);
  free(blackmask);

  /* operate on the row_avg and row_avgs arrays */

  /* create filt1 & filt2 */
  filt1 = (float *)malloc(sizeof(float)*filt_len);
  for(i=0; i<filt_len; i++) {
    filt1[i] = 1.0;
  }
  //filt2 = sawtooth(7,1,1);

  /* */
  row_bright = (float *)calloc(sizeof(float), y);
  col_bright = (float *)calloc(sizeof(float), chunks*x);

  /* smooth row_avg array */
  row_avgs = convolve(row_avg, filt1, 1, y, z, 1, filt_len, 1, 1, 0);

  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      row_avg[k*y + j] -= row_avgs[k*y + j];                                /* subtract smoothed from original row_avg */

      if(z > 1 && k != b10) {                                               /* if there is more than one band and current band isn't band 10 */
	row_bright[j] += row_avg[k*y + j];                                  /* add up the brightness information */
	row_wt[j] += 1;
      }

      if(z > 1 && k == z-1) row_bright[j] /= (float)row_wt[j];              /* make row_bright an avg rather than a sum */
    }
  }

  /* smooth the row brightness array to deplaid the luminosity */
  //row_brights = convolve(row_bright, filt2, 1, y, 1, 1, 7, 1, 1, 0);

  /* remove brightness information from row_avg */
  if(z > 1) {
    for(k=0; k<z; k++) {
      if(k != b10) {
	for(j=0; j<y; j++) {
	  row_avg[k*y + j] -= row_bright[j];                         /* brightness difference removed from row_avg */
	}
      }
    }
  }

  /* clean up row arrays */
  //free(row_brights);
  free(row_bright);
  free(row_avgs);
  free(row_wt);

  /* operate on the col_avg arrays */
  col_wt = (int *)calloc(sizeof(int), x*chunks);

  /* smooth the col_avg array */
  col_avgs = convolve(col_avg, filt1, x, chunks, z, filt_len, 1, 1, 1, 0);
  free(filt1);

  for(k=0; k<z; k++) {
    for(j=0; j<chunks; j++) {
      for(i=0; i<x; i++) {
	col_avg[k*chunks*x + j*x + i] -= col_avgs[k*chunks*x + j*x + i];             /* subtract smoothed from original col_avg */

	if(z>1 && k != b10) {                                                        /* if more than one band and not band 10 */
	  col_bright[j*x + i] += col_avg[k*chunks*x + j*x + i];                      /* sum column brightness excluding band 10 */
	  col_wt[j*x + i] += 1;                                                      /* sum column weight array excluding band 10 */
	}
	if(z > 1 && k == z-1) col_bright[j*x + i] /= (float)col_wt[j*x + i];         /* make col_bright an avg rather than a sum */
      }
    }
  }

  /* smooth the column brightness array to deplaid the luminosity */
  //  col_brights = convolve(col_bright, filt1, x, chunks, 1, filt_len, 1, 1, 1, 0);
  
  /* remove brightness information */
  if(z > 1) {
    for(k=0; k<z; k++) {
      if(k != b10) {
	for(j=0; j<chunks; j++) {
	  for(i=0; i<x; i++) {
	    col_avg[k*chunks*x + j*x + i] -= col_bright[j*x + i];                  /* brightness difference removed from col_avg */
	  }
	}
      }
    }
  }

  /* clean up column arrays*/
  free(col_avgs);
  //  free(col_brights);
  free(col_bright);
  free(col_wt);
 
  /* extract data into rdata removing plaid along the way */
  /* create rdata array */
  rdata = (float *)malloc(sizeof(float)*x*y*z);

  for(k=0; k<z; k++) {
    cca = 0;
    ck = 0;
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = extract_float(data, cpos(i,j,k, data));
	if(tv != nullval) {
	  rdata[x*y*k + x*j + i] = tv - row_avg[k*y + j] - col_avg[k*chunks*x + ck*x + i];
	} else {
	  rdata[x*y*k + x*j + i] = tv;
	}
      }
      cca += 1;
      if(cca == 250 && ck<(chunks-1)) {
	cca = 0;
	ck+=1;                                                                         /* chunk tracker */
      }
    }
  }

  /* final memory cleanup */
  free(row_avg);
  free(col_avg);

  /* return array */
  out = newVal(BSQ, x, y, z, FLOAT, rdata);
  return out;
}






Var *
thm_rectify(vfuncptr func, Var * arg)
{

  typedef unsigned char byte;

  Var     *obj = NULL;                 /* the picture */
  Var     *out;                        /* the output picture */
  float   *pic = NULL;                 /* the new sheared picture */
  float    angle = 0;                  /* the shear angle */
  float    shift;                      /* the shift/pixel */
  float    nullo = -3.402822655e+38;   /* the null value in the projected cube */
  float    nullv = -32768;             /* the null value to put in all blank space */
  float    yiz = 0;                    /* temporary float value */
  int	   trim = 0;		       /* whether to trim the data or not */
  int      lshift;                     /* the largest shift (int) */
  int      u, x, y, z;                 /* dimensions of the picture */
  int      i, j, k;                    /* loop indices */
  int      ni;                         /* memory locations */
  int      nx, nu, nz;
  int     *leftmost, *rightmost;       /* leftmost and rightmost pixel arrays */
  int      w = 0,width = 0;            /* width values */
  int     *cns = NULL;                 /* the corners array */
  float    angle1 = 0, angle2 = 0;     /* possible angle variables */
  float    trust = 0;                  /* use top corners or bottom corners */

  Alist alist[4];
  alist[0] = make_alist("object",		ID_VAL,		NULL,	&obj);
  alist[1] = make_alist("null",                 FLOAT,          NULL,   &nullo);
  alist[2] = make_alist("trust",                FLOAT,          NULL,   &trust);
  alist[3].name = NULL;

  if (parse_args(func, arg, alist) == 0) return NULL;

  /* if no picture got passed to the function */
  if (obj == NULL){
    parse_error("rectify() - 4/29/04");
    parse_error("Extracts rectangle of data from projected cubes\n");
    parse_error("Syntax:  b = thm.rectify(obj,null,trust)");
    parse_error("example: b = thm.rectify(obj=a)");
    parse_error("example: b = thm.rectify(a)");
    parse_error("example: b = thm.rectify(obj=a,null=0,trust=2)\n");
    parse_error("obj - any projected cube");
    parse_error("null - the value in non-data pixels of projected cube. Default is -3.402822655e+38.");
    parse_error("trust - where in the image the angle should be determined.");
    parse_error("        0 = top, 1 = bottom, .25 = 25 percent down the image, etc.  Default is 0 (top).\n");
    parse_error("TROUBLESHOOTING");
    parse_error("The null value will ALWAYS be set to -32768 when rectifying!\n");
    return NULL;
  }

  /* x, y, and z dimensions of the original picture */
  x = GetX(obj);
  y = GetY(obj);
  z = GetZ(obj);    

  /* calling corners to find the angle */
  cns = do_corners(obj,nullo);
  if (trust == 0) angle = (360.0/(2.0*3.14159265))*(atan2((cns[3]-cns[1]),(cns[2]-cns[0])));
  if (trust == 1) angle = (360.0/(2.0*3.14159265))*(atan2((cns[7]-cns[5]),(cns[6]-cns[4])));
  if (trust > 0 && trust < 1) {
    angle1 = (360.0/(2.0*3.14159265))*(atan2((cns[3]-cns[1]),(cns[2]-cns[0])));
    angle2 = (360.0/(2.0*3.14159265))*(atan2((cns[7]-cns[5]),(cns[6]-cns[4])));
    angle = ((1-trust)*angle1)+(trust*angle2);
  }    

  /* calculating the number of rows to add to the picture to accomodate the shear */
  shift = tan((M_PI / 180.0) * angle);
  lshift = (int)(x*fabs(shift)+0.5)*(shift/fabs(shift));

  /* y dimension of sheared/rectified image */
  u = y + abs(lshift);

  /* assign memory to leftmost and rightmost arrays */
  leftmost = malloc(sizeof(int)*u);
  rightmost = calloc(sizeof(int), u);
  
  /* set leftmost array values to maximum x */
  for (j = 0; j < u; j++) {
    leftmost[j] = x-1;
  }

  /* find leftedge, rightedge, and max width of sheared array */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	
	/* the shifted y pixel */
	/* if the angle is greater than 0 */
	if(lshift>0) ni = ((int)(j+fabs(lshift)-(int)(shift*i+0.5)))*x + i;
	
	/* if the angle is less than 0 */
	if(lshift<0) ni = (j+(int)(fabs(shift)*i+0.5))*x + i;

	/* finding the x, y and z in the sheared array */
	nz = ni/(u*x);
	nu = (ni - nz*u*x)/x;
	nx = ni - nz*u*x - nu*x;

	yiz = extract_float(obj, cpos(i, j, k, obj));

	/* set leftmost and rightmost values */
	if(yiz != nullo) {
	  if(nx < leftmost[nu]) leftmost[nu] = nx;
	  if(nx > rightmost[nu]) rightmost[nu] = nx;
	}
      }
    }
  }
  for (j = 0; j < u; j++) {
    w = rightmost[j] - leftmost[j] + 1;
    if(w > width) width = w;
  }

  free(rightmost);

  /* fix leftmost to allow for maximum width */
  for (j = 0; j < u; j++) {
    leftmost[j] = min(leftmost[j], x-width);
  }

  /* assign memory to sheared and slanted array */
  pic = (float *)malloc(sizeof(float)*z*u*width);

  /* extract the data into a sheared and slanted array */
  for (k = 0; k < z; k++) {
    for(j = 0; j < u; j++) {
      for(i = 0; i < width; i++) {
	/* set all values to nullv */
	pic[k*u*width + j*width + i] = nullv;

	/* if not above the data */
        if (leftmost[j] != x-width) {
	  nx = i + leftmost[j];

	  /* top of image leans to the left of the bottom, i.e. angle < 0 */
	  if (lshift < 0) nu = j - (int)(fabs(shift)*nx+0.5);
	  /* top of image leans to the right of the bottom, i.e. angle > 0 */
	  if (lshift > 0) nu = j + (int)(nx*shift+0.5) - abs(lshift);

	  if (nu >= 0 && nu <= y && nx >= 0 && nx <= x && (yiz = extract_float(obj, cpos(nx,nu,k,obj))) != nullo) pic[k*u*width + j*width + i] = yiz;
	}
      }
    }
  }

  /* final output */
  out = new_struct(4);
  add_struct(out, "data", newVal(BSQ, width, u, z, FLOAT, pic));
  add_struct(out, "leftedge", newVal(BSQ, 1, u, 1, INT, leftmost));
  add_struct(out, "width", newInt(x));
  add_struct(out, "angle", newFloat(angle));
  return out;
}


Var*
thm_reconst(vfuncptr func, Var * arg)
{
  
  Var    *obj=NULL;                 /* input rectify structre */
  Var    *out;                      /* output data */
  Var    *vdata=NULL;               /* data  var */
  Var    *vleftedge=NULL;           /* leftedge var */
  Var    *w,*a;                     /* width and angle var */ 
  int    *leftedge;                 /* leftedge array */
  float   angle=0;                  /* angle value */
  int     width=0;                  /* width value */
  int     x,y,z;                    /* rectified image size */
  int     i,j,k;                    /* loop indices */
  float  *new_data=NULL;            /* changing data size */
  float   null=-32768;              /* null value */
  int     lshift;                   /* vert shift max shift */
  float   shift;                    /* tan of the shift */
  
  
  Alist alist[3]; 
  alist[0] = make_alist("rect",      ID_STRUCT,  NULL,  &obj);
  alist[1] = make_alist("null",      FLOAT,      NULL,  &null);
  alist[2].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if(obj==NULL ) {
    parse_error("\nTakes rectified cubes and returns");
    parse_error("them to projected geometry\n");
    parse_error("$1 = rectify structure");
    parse_error("null = null value of the data (default -32768)\n");
    parse_error("c.edwards 12/15/04\n");
    return NULL;
  }
  
  /* get stuff from structre */
  find_struct(obj,"data",&vdata);
  find_struct(obj,"leftedge",&vleftedge);
  find_struct(obj,"angle",&a);
  find_struct(obj,"width",&w);
  width = extract_int(w, 0);
  angle = -extract_float(a,0);
  
  if(angle == 0) {
    parse_error("Why did you bother to shear the data by a 0 angle?\n"); return NULL;
  }
  if(angle > 80 || angle < -80) {
    parse_error("Try to stay between -80 and 80, love.  Our motto is \"No Crashy Crashy.\""); return NULL;
  }
  
  /* get x,y,z */
  x=GetX(vdata);
  y=GetY(vdata);
  z=GetZ(vdata);
  new_data=(float *)calloc(sizeof(float),x*y*z);
  leftedge=(int *)calloc(sizeof(int),y);
  
  /* extract the data  and leftedge*/
  for(j=0;j<y;j++) {
    leftedge[j]=extract_int(vleftedge,cpos(0,j,0,vleftedge));
  }
  for (k=0;k<z;k++) {
    for (j=0;j<y;j++) {
      for (i=0;i<x;i++) {
	new_data[x*y*k + x*j + i]=extract_float(vdata,cpos(i,j,k,vdata));
      }
    }
  }

  /* run unslantc */
  new_data=unslantc(new_data,leftedge,width,x,y,z,null);
  
  /* calculating the number of rows to add to the picture to accomodate the shear */
  shift = tan((M_PI / 180.0) * angle);
  lshift = (int)((width*fabs(shift)-0.5)+1)*(shift/fabs(shift));

  /* error handling */
  if(lshift == 0) {
    parse_error("The shear is too small to accomplish anything. Try again bozo.\n"); return NULL;
  }
  if(y-abs(lshift)<=0) {
    parse_error("The trimmed array has a length of %d, you nincompoop. Stop trying to crash me.\n", y-abs(lshift));
    return NULL;
  }
  
  /* run unshearc */
  new_data=unshearc(new_data,angle,width,y,z,null);
  
  /* return the data */  
  out = newVal(BSQ, width, y-abs(lshift), z, FLOAT, new_data);
  return(out);
}




static float *unslantc(float *data, int *leftedge, int width, int x, int y, int z, float ignore)
{
  /* set up variables */
  int i,j,k,p;
  float *odata;
  
  /* allocate memory for the new data */
  odata = calloc(width*y*z, sizeof(float));
  
  /* extract the data and shift it */
  for (j=0;j<y;j++) {
    for (i=0;i<width;i++) {
      for (k=0;k<z;k++) {
 	if (i >= leftedge[j] && i < leftedge[j]+x) {
	  odata[width*y*k + width*j +i] = data[x*y*k + x*j + (i-leftedge[j])];
	} else {
	  odata[width*y*k + width*j +i] = ignore;
	}
      }
    }
  } 

  /* return the data */
  return(odata);
}




static float *unshearc(float *w_pic, float angle, int x, int y, int z, float nullv)
{

  float   *pic=NULL;
  float    shift;                   /* the shift/pixel */
  int      lshift;                  /* the largest shift (int) */
  int      i, j, k;                 /* loop indices */
  int      nx, ni, nj;              /* memory locations */
  Var     *out;

  /* calculating the number of rows to add to the picture to accomodate the shear */
  shift = tan((M_PI / 180.0) * angle);
  lshift = (int)((x*fabs(shift)-0.5)+1)*(shift/fabs(shift));
  
   /*create the new picture array */
  pic = (float *)calloc(sizeof(float), x*z*(y-abs(lshift)));
  
  if (pic == NULL) {
    parse_error("ERROR! Unable to allocate %d bytes\n", sizeof(float)*x*z*(y+abs(lshift)));
    return NULL;
  }
  
  /* fill in the null value into all pic elements */
  if(nullv != 0) {
    for(k=0; k<z; k++) {
      for(j=0; j<y-abs(lshift); j++) {
	for(i=0; i<x; i++) {
	  ni = k*(y-abs(lshift))*x + j*x + i;
	  pic[ni] = nullv;
	}
      }
    }
  }
  
  /* extract the picture directly into a sheared, but trimmed, array */
  for(k=0; k<z; k++) {
    for(j=0; j<y-abs(lshift); j++) {
      for(i=0; i<x; i++) {
	
	/* the index of the new array */
	nx = k*(y-abs(lshift))*x + j*x + i;
	
	/* the shifted y pixel */
	/* if the angle is greater than 0 */
	if(lshift>0) {
	  nj = j + (int)(fabs(shift)*i+0.5);
	}
	
	/* if the angle is less than 0 */
	if(lshift<0) {
	  nj = j + abs(lshift) - (int)(fabs(shift)*i+0.5);
	}
	pic[nx] = w_pic[x*y*k + nj*x + i];
      }
    }
  }
  return(pic);
}





static float *deplaid(float *data, int x, int y, int z, float nullval, int b10)
{

  typedef unsigned char byte;

  float    *rdata = NULL;                           /* the deplaided array */
  float    *row_sum;                                /* a row sum array used to determine blackmask */
  float    *row_avg, *row_avgs;                     /* the row and smoothed row average arrays */
  float    *col_avg;                                /* the column average of both runs combined */
  float    *col_avga, *col_avgb;                    /* the column averages of run a and b */
  float    *col_avgs;                               /* the smoothed column averages of run a and b */
  float    *row_bright, *col_bright;                /* brightness arrays */
  float    *row_brights, *col_brights;              /* deplaided brightness arrays */
  byte     *ct_map;
  byte     *row_ct, *col_ct;
  byte     *blackmask;                              /* the blackmask and temperature mask */
  float    *tmask;                                  /* the temporary temperature mask */
  float     tmask_max = 1.15;                       /* maximum threshold for temperature masking */
  float     tmask_min = 0.80;                       /* minimum threshold for temperature masking */
  int       filt_len = 150;                         /* filter length */
  int      *row_wt, *col_wt, *col_wta, *col_wtb;    /* weight arrays for row and column averages */
  int       i, j, k;                                /* loop indices */
  int       chunks = 0, chunksa = 0, chunksb = 0;   /* number of chunks image is broken into */
  int       cca = 0, ccb = 250;                     /* line indices for the chunk a and chunk b calculations */
  int       ck = 0, cka = 0, ckb = 0;               /* number of chunk that calculation is on */
  float     tv = 0;                                 /* temporary pixel value */
  float    *filt1, *sawkern;                        /* various convolve kernels */
  float    *b_avg, *b_ct;
  int       dump = 0;                               /* flag to return the temperature mask */

  /* setting the band-10 of the data */
  b10 -= 1;
  if(b10 >= z || b10 <-1) {
    parse_error("Invalid band 10 designation!");
    if(z == 10) {
      parse_error("b10 being reset to 10\n");
      b10 = 9;
    } else {
      parse_error("assuming NO band 10 data, b10 being reset to %d\n",z-1);
      b10 == z-1;
    }
  }

  /* number of chunks of data in the y direction */
  chunksa = ((y-100)/500) + 1;                                 /* number of chunks starting at zero */
  chunksb = ((y-350)/500) + 2;                                 /* number of chunks starting at 250 */
  if(y<350) chunksb = 1;
  chunks = ((y-100)/250) + 1;

  /* create row and mask arrays */
  row_avg = (float *)calloc(sizeof(float), y*z);
  row_wt = (int *)calloc(sizeof(int), y*z);
  blackmask = (byte *)malloc(sizeof(byte)*x*y);
  tmask = (float *)calloc(sizeof(float), x*y);
  ct_map = (byte *)calloc(sizeof(byte), x*y);
  row_ct = (byte *)calloc(sizeof(byte), y);
  col_ct = (byte *)calloc(sizeof(byte), x);

  /* check to make sure that tmask_max, tmask_min and filt_len are reasonable values */
  if(tmask_max < tmask_min || tmask_max == 0) {
    parse_error("tmask_max must be larger than tmask_min, dopo!");
    parse_error("you should keep tmask_max > 1.0 and tmask_min < 1.0");
    parse_error("values are being reset to 1.15 and 0.80");
    tmask_max = 1.15;
    tmask_min = 0.80;
  }
  if(filt_len < 1) {
    parse_error("the filter must have some length, you tool!");
    parse_error("filter being reset to 150");
    filt_len = 150;
  }

  b_avg = (float *)calloc(sizeof(float), z);
  b_ct = (float *)calloc(sizeof(float), z);

  /* find the averages of each band so's I can normalize them */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = data[k*y*x + j*x + i];

	if(tv != nullval) {
	  b_avg[k] += tv;
	  b_ct[k] += 1;
	}
      }
    }
    b_avg[k] /= b_ct[k];
  }

  free(b_ct);

  /* construct blackmask, tempmask and row_avg */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = data[k*y*x + j*x + i];

	if(tv != nullval) {
	  blackmask[x*j + i] = 1;
	  tmask[x*j + i] += tv/b_avg[k];                             /* tmask is sum of all bands */
	  row_avg[j] += tv/b_avg[k];                                 /* calculating row totals for tmask */
	  ct_map[x*j + i] += 1;                                      /* incrementing pixel count_map */
	  row_wt[j] += 1;                                            /* calculating row weight for tmask */
	}

	/* setting the col and row count arrays */
	if(k==z-1) {
	  if(row_ct[j] < ct_map[j*x + i]) row_ct[j] = ct_map[j*x + i];
	  if(col_ct[i] < ct_map[j*x + i]) col_ct[i] = ct_map[j*x + i];
	  if(ct_map[x*j + i] != 0) tmask[x*j + i] /= (float)ct_map[x*j + i];
	}
      }
 
      if(k == z-1 && row_wt[j] != 0) {
	row_avg[j] /= row_wt[j];                                     /* calculating row avg for tmask */
	row_wt[j] = 0;
      }
    }
  }

  /* continue to construct the temperature mask */
  /* smooth the row avg of tempmask with a sawtooth filter */
  sawkern = sawtooth(1, 301, 1);
  row_sum = convolve(row_avg, sawkern, 1, y, 1, 1, 301, 1, 1, 0);
  free(sawkern);
  free(ct_map);
  free(b_avg);

  /* loop through setting the tempmask values */
  /* pixels with tmask values greater than 1.25 or less than .8 of the row avg are set to zero */
  for(j=0; j<y; j++) {
    for(i=0; i<x; i++) {
      if(tmask[x*j+i] < row_sum[j]*tmask_max && tmask[x*j+i] > row_sum[j]*tmask_min) blackmask[x*j+i] += 1;
    }
    for(k=0; k<z; k++) {
      row_avg[k*y + j] = 0;
    }
  }
  free(tmask);
  free(row_sum);

  /* return the temperature mask if dump = 1 */

  /* more initialization */
  col_avg = (float *)calloc(sizeof(float), x*chunks*z);      /* col_avg contains enough space for all 250 line chunks */
  col_avga = (float *)calloc(sizeof(float), x*chunksa*z);
  col_avgb = (float *)calloc(sizeof(float), x*chunksb*z);
  col_wta = (int *)calloc(sizeof(int), x*chunksa*z);
  col_wtb = (int *)calloc(sizeof(int), x*chunksb*z);

  if(col_avg == NULL || col_avga == NULL || col_avgb == NULL || col_wta == NULL || col_wtb == NULL) { 
    parse_error("Could not allocate enough memory to continue\n");
    return NULL;
  }

  /* remove the horizontal and vertical plaid */
  /* loop through data and extract row_avg, and all necessary column averages */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = data[k*y*x + j*x + i];

	if(tv != nullval && blackmask[x*j + i] > 1) {              /* if not a null val and tempmask ok */

	  if(col_ct[i] == z) {
	    row_avg[y*k + j] += tv;                                  /* calculate tempmasked row total of data */
	    row_wt[y*k + j] += 1;                                    /* calculate tempmasked row weight of data */
	  }
	  
	  if(row_ct[j] == z) {
	    col_avga[k*chunksa*x + cka*x + i] += tv;                 /* calculate tempmasked col total of chunka */
	    col_avgb[k*chunksb*x + ckb*x + i] += tv;                 /* calculate tempmasked col total of chunkb */
	    col_wta[k*chunksa*x + cka*x + i] += 1;                   /* calculate tempmasked col weight of chunka */
	    col_wtb[k*chunksb*x + ckb*x + i] += 1;                   /* calculate tempmasked col weight of chunkb */
	  }
	}
 
	/* if at the end of a chunk and there is less than 100 rows of data left, keep going in present chunk */
	if(((y-j) <= 100) && (ccb == 499 || cca == 499)) {
	  cca -= 100; 
	  ccb -= 100;
	}

	/* if at the end of chunk then divide column total by column weight and construct chunk of col_avg */
	if(cca == 499) {
	  /* divide col_avga by the number of non-zero lines summed to create average */
	  if(col_wta[k*chunksa*x + cka*x + i] > 0) {
	    col_avga[k*chunksa*x + cka*x + i] /= (float)col_wta[k*chunksa*x + cka*x + i];
	  } else { /* if col_wta is 0 don't divide */
	    col_avga[k*chunksa*x + cka*x + i] = 0;
	  }
	  /* avg col_avga and col_avgb for the 250 line chunk */
	  col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + (ckb-1)*x + i]);
	  if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + (ckb-1)*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	}

	if(ccb == 499) {
	  /* divide col_avgb by the number of non-zero lines summed to create average */
	  if(col_wtb[k*chunksb*x + ckb*x + i] > 0) {
	    col_avgb[k*chunksb*x + ckb*x + i] /= (float)col_wtb[k*chunksb*x + ckb*x + i];
	  } else { /* if col_wtb is 0 don't divide */
	    col_avgb[k*chunksb*x + ckb*x + i] = 0;
	  }
	  /* avg col_avga and col_avgb for the 250 line chunk */
	  if(ckb > 0) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + (cka-1)*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + (cka-1)*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	}

	if(j == y-1) {
	  if(col_wta[k*chunksa*x + cka*x + i] > 0) {
	    col_avga[k*chunksa*x + cka*x + i] /= (float)col_wta[k*chunksa*x + cka*x + i];
	  } else {
	    col_avga[k*chunksa*x + cka*x + i] = 0;
	  }
	  if(col_wtb[k*chunksb*x + ckb*x + i] > 0) {
	    col_avgb[k*chunksb*x + ckb*x + i] /= (float)col_wtb[k*chunksb*x + ckb*x + i];
	  } else {
	    col_avgb[k*chunksb*x + ckb*x + i] = 0;
	  }
	  /* avg col_avga and col_avgb for second to last 250 line chunk */
	  if(cca>ccb && chunksb > 1) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + (ckb-1)*x + i]);
	    if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + (ckb-1)*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	  if(ccb>cca && chunksa > 1) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + (cka-1)*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + (cka-1)*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	  /* avg col_avga and col_avgb for the final 250 line chunk */
	  if(ck<chunks-1 && chunksb > 1) {
	    col_avg[k*chunks*x + (ck+1)*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + (ck+1)*x + i] /= 2.0;
	  }
	  if(chunksa == 1 && chunksb == 1) {
	    col_avg[k*chunks*x + ck*x + i] = (col_avga[k*chunksa*x + cka*x + i] + col_avgb[k*chunksb*x + ckb*x + i]);
	    if(col_avga[k*chunksa*x + cka*x + i] > 0 && col_avgb[k*chunksb*x + ckb*x + i] > 0) col_avg[k*chunks*x + ck*x + i] /= 2.0;
	  }
	}
      }

      cca += 1;                                              /* increment the chunka line indice */
      ccb += 1;                                              /* increment the chunkb line indice */
      if(cca == 500) {                                       /* if at end of chunk, start new chunk */
	cca = 0;
	cka += 1;
	ck += 1;
      }
      if(ccb == 500) {                                       /* if at end of chunk, start new chunk */
	ccb = 0;
	if(ckb > 0) ck += 1;                                 /* don't increment the ck until cka and ckb have at least one finished chunk */
	ckb += 1;
      }

      if(row_avg[y*k + j] != 0 && row_wt[y*k + j] != 0) {
	row_avg[y*k + j] /= (float)row_wt[y*k + j];          /* perform division for row_avg values*/
	row_wt[y*k + j] = 0;
      }
    }
    cka = 0;
    ckb = 0;
    cca = 0;
    ccb = 250;
    ck = 0;
  }

  /* clean up col_avg arrays */
  free(col_avga);
  free(col_avgb);
  free(col_wta);
  free(col_wtb);
  free(col_ct);
  free(row_ct);
  free(blackmask);

  /* operate on the row_avg and row_avgs arrays */

  /* create filt1 */
  filt1 = (float *)malloc(sizeof(float)*filt_len);
  for(i=0; i<filt_len; i++) {
    filt1[i] = 1.0;
  }

  /* */
  row_bright = (float *)calloc(sizeof(float), y);
  col_bright = (float *)calloc(sizeof(float), chunks*x);

  /* smooth row_avg array */
  row_avgs = convolve(row_avg, filt1, 1, y, z, 1, filt_len, 1, 1, 0);

  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      row_avg[k*y + j] -= row_avgs[k*y + j];                                /* subtract smoothed from original row_avg */

      if(z > 1 && k != b10) {                                               /* if there is more than one band and current band isn't band 10 */
	row_bright[j] += row_avg[k*y + j];                                  /* add up the brightness information */
	row_wt[j] += 1;
      }

      if(z > 1 && k == z-1) row_bright[j] /= (float)row_wt[j];              /* make row_bright an avg rather than a sum */
    }
  }

  /* smooth the row brightness array to deplaid the luminosity */
  //row_brights = convolve(row_bright, filt1, 1, y, 1, 1, filt_len, 1, 1, 0);

  /* remove brightness information from row_avg */
  if(z > 1) {
    for(k=0; k<z; k++) {
      if(k != b10) {
	for(j=0; j<y; j++) {
	  row_avg[k*y + j] -= row_bright[j];                         /* brightness difference removed from row_avg */
	}
      }
    }
  }

  /* clean up row arrays */
  //  free(row_brights);
  free(row_bright);
  free(row_avgs);
  free(row_wt);

  /* operate on the col_avg arrays */
  col_wt = (int *)calloc(sizeof(int), x*chunks);

  /* smooth the col_avg array */
  col_avgs = convolve(col_avg, filt1, x, chunks, z, filt_len, 1, 1, 1, 0);
  free(filt1);

  for(k=0; k<z; k++) {
    for(j=0; j<chunks; j++) {
      for(i=0; i<x; i++) {
	col_avg[k*chunks*x + j*x + i] -= col_avgs[k*chunks*x + j*x + i];             /* subtract smoothed from original col_avg */

	if(z>1 && k != b10) {                                                            /* if more than one band and not band 10 */
	  col_bright[j*x + i] += col_avg[k*chunks*x + j*x + i];                      /* sum column brightness excluding band 10 */
	  col_wt[j*x + i] += 1;                                                      /* sum column weight array excluding band 10 */
	}
	if(z > 1 && k == z-1) col_bright[j*x + i] /= (float)col_wt[j*x + i];     /* make col_bright an avg rather than a sum */
      }
    }
  }

  /* smooth the column brightness array to deplaid the luminosity */
  //  col_brights = convolve(col_bright, filt1, x, chunks, 1, filt_len, 1, 1, 1, 0);
  
  /* remove brightness information */
  if(z > 1) {
    for(k=0; k<z; k++) {
      if(k != b10) {
	for(j=0; j<chunks; j++) {
	  for(i=0; i<x; i++) {
	    col_avg[k*chunks*x + j*x + i] -= col_bright[j*x + i];                  /* brightness difference removed from col_avg */
	  }
	}
      }
    }
  }

  /* clean up column arrays*/
  free(col_avgs);
  //  free(col_brights);
  free(col_bright);
  free(col_wt);
 
  /* extract data into rdata removing plaid along the way */
  /* create rdata array */
  rdata = (float *)malloc(sizeof(float)*x*y*z);

  for(k=0; k<z; k++) {
    cca = 0;
    ck = 0;
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv = data[k*y*x + j*x + i];
	if(tv != nullval) {
	rdata[x*y*k + x*j + i] = tv - row_avg[k*y + j] - col_avg[k*chunks*x + ck*x + i];
	} else {
	  rdata[k*x*y + j*x + i] = tv;
	}
      }
      cca += 1;
      if(cca == 250 && ck<(chunks-1)) {
	cca = 0;
	ck+=1;                                                                         /* chunk tracker */
      }
    }
  }

  /* final memory cleanup */
  //free(row_avg);
  free(col_avg);

  /* return array */
  return rdata;
}






Var *
thm_rad2tb(vfuncptr func, Var * arg)
{

  /* wrapper used to call rad2tb */

  Var         *rad = NULL;                     /* the original radiance */
  Var         *bandlist = NULL;                /* list of bands to be converted to brightness temperature */
  Var         *temp_rad = NULL;                /* temperature/radiance lookup file */
  Var         *out = NULL;                     /* end product */
  char        *fname = NULL;                   /* the pointer to the filename */
  FILE        *fp = NULL;                      /* pointer to file */
  struct       iom_iheader h;                  /* temp_rad_v4 header structure */
  float        nullval = -32768;               /* default null value of radiance data */
  float       *b_temps = NULL;                 /* brightness temperatures computed by rad2tb */
  int         *blist = NULL;                   /* the integer list of bands extracted from bandlist or created */
  int          x, y, z;                        /* dimensions of the data */
  int          bx = 0;                         /* x-dimension of the bandlist */
  int          i;                              /* loop index */
  
  Alist alist[5];
  alist[0] = make_alist("rad", 		 ID_VAL,         NULL,   &rad);
  alist[1] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[2] = make_alist("null",          FLOAT,          NULL,   &nullval);
  alist[3] = make_alist("temp_rad_path", ID_STRING,      NULL,   &fname);
  alist[4].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (rad == NULL) {
    parse_error("rad2tb() - 7/07/04");
    parse_error("THEMIS radiance to THEMIS Brightness Temperature Converter\n");
    parse_error("Uses /themis/calib/temp_rad_v4 as conversion table.");
    parse_error("Assumes 10 bands of radiance for right now.\n");
    parse_error("Syntax:  b = thm.rad2tb(rad,bandlist,null,temp_rad_path)");
    parse_error("example: b = thm.rad2tb(a)");
    parse_error("example: b = thm.rad2tb(a,1//2//3//4//5//6//7//8//9,0)");
    parse_error("example: b = thm.rad2tb(rad=a,bandlist=4//9//10,null=-32768,temp_rad_path=\"/themis/calib/temp_rad_v3\")");
    parse_error("rad - any float valued 3-D radiance array.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("null - non-data pixel value. Default is -32768 AND 0.");
    parse_error("temp_rad_path - alternate path to temp_rad lookup table.  Default is /themis/calib/temp_rad_v4.\n");
    return NULL;
  }

  if (bandlist != NULL) {
    bx = GetX(bandlist);
    blist = (int *)malloc(sizeof(int)*bx);
    for(i=0;i<bx;i++) {
      blist[i] = extract_int(bandlist,cpos(i,0,0,bandlist));
    }
  }

  if (bandlist == NULL) {
    blist = (int *)malloc(sizeof(int)*10);
    for(i=0;i<10;i++) {
      blist[i] = i+1;
    }
    bx = 10;
  }

  x = GetX(rad);
  y = GetY(rad);
  z = GetZ(rad);

  /* initialize temp_rad header */
  iom_init_iheader(&h);

  /* load temp_rad_v4 table into memory */
  if (fname == NULL) fname = strdup("/themis/calib/temp_rad_v4");
  fp = fopen(fname, "rb");

  if (fp == NULL) {
    parse_error("Can't open look up table /themis/calib/temp_rad_v4!\n");
    return(NULL);
  }

  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);

  /* convert to brightness temperature and return */
  b_temps = rad2tb(rad, temp_rad, blist, bx, nullval);

  free(blist);
  out = newVal(BSQ, x, y, z, FLOAT, b_temps);
  return(out);
 
}


float *rad2tb(Var *radiance, Var *temp_rad, int *bandlist, int bx, float nullval)
{

  float    *btemps;                       /* the output interpolated brightness temperature data */
  float    *m = NULL, *b = NULL;          /* slope and intercept arrays */
  float    *temps = NULL;                 /* linear temperature lookup array extracted from temp_rad */
  float    *rads = NULL;                  /* linear radiance lookup array extracted from temp_rad */
  int       x, y;                         /* dimensions of input/output arrays */
  int       pt1, pt2, mid;                /* array indices used in finding bounding points */
  int       w;                            /* y-size of lookup table */
  int       i, j, k;                      /* loop indices */
  int       band;                         /* which band is currently being converted */
  float     cur_val = 0.0;                /* temporary extracted radiance value*/

  /*
    radiance is a THEMIS radiance cube of up to 10 bands
    temp_rad is the radiance/temperature conversion table
    bandlist is an integer list of which THEMIS bands are in the radiance cube
    bx is the number of elements in bandlist, also the number of bands in the radiance cube
    nullval is the null data value
  */

  x = GetX(radiance);
  y = GetY(radiance);
  w = GetY(temp_rad);

  /* assigning memory for the interpolated data, temps and rads */
  btemps = (float *)calloc(sizeof(FLOAT), x*y*bx);
  temps = (float *)malloc(sizeof(FLOAT)*w);
  rads = (float *)malloc(sizeof(FLOAT)*w);
  
  /* slopes and intercepts arrays */
  m = (float *)calloc(sizeof(FLOAT), w-1);
  b = (float *)calloc(sizeof(FLOAT), w-1);

  /* extract the linear temperature array */
  for (i=0;i<w;i++) {
    temps[i] = extract_float(temp_rad,cpos(0,i,0,temp_rad));
  }

  /* loop through the bands */
  for(k=0;k<bx;k++) {

    band = bandlist[k];

    /* extract linear radiances array */
    for(i=0;i<w;i++) {
      rads[i] = extract_float(temp_rad,cpos(band,i,0,temp_rad));
    }

    /* calculate the slopes and intercepts */
    for(i=1;i<w;i++){
      m[i-1] = (temps[i]-temps[i-1])/(rads[i]-rads[i-1]);
      b[i-1] = temps[i-1] - m[i-1]*rads[i-1];
    }

    /* work on a point at a time */
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	cur_val = extract_float(radiance,cpos(i,j,k,radiance));

	if(cur_val == -32768 || cur_val == 0 || cur_val == nullval) btemps[k*y*x + j*x + i] = 0;

	if(cur_val != -32768 && cur_val != 0 && cur_val != nullval) {

	  /* locate the two bounding points in the rads array containing the radiance value */
	  pt1 = 0;
	  pt2 = w;
	  mid = 0;
	
	  while((pt2-pt1) > 1) {
	    mid = (pt1+pt2)/2;
	    if(cur_val > rads[mid]) pt1 = mid;
	    if(cur_val < rads[mid]) pt2 = mid;
	    if(cur_val == rads[mid]) pt1 = pt2 = mid;
	  }

	  if(temps[pt2] == temps[pt1]) btemps[k*y*x + j*x + i] = temps[pt1];
	  else btemps[k*y*x + j*x + i] = m[pt1]*cur_val + b[pt1];
	}
      }
    }
  }

  free(temps);
  free(rads);
  free(m);
  free(b);
  return(btemps);
}



float *tb2rad(float *btemp, Var *temp_rad, int *bandlist, int bx, float nullval, int x, int y)
{

  float    *irads;                        /* output interpolated radiance values */
  float    *m = NULL, *b = NULL;          /* slope and intercept arrays */
  float    *temps = NULL;                 /* linear temperature lookup array extracted from temp_rad */
  float    *rads = NULL;                  /* linear radiance lookup array extracted from temp_rad */
  int       pt1, pt2, mid;                /* array indices used in finding bounding points */
  int       w;                            /* y-size of lookup table */
  int       i, j, k;                      /* loop indices */
  int       band;                         /* which band is currently being converted */
  float     cur_val = 0.0;                /* temporary extracted radiance value*/

  /*
    btemp is a single THEMIS brightness temperature map
    temp_rad is the radiance/temperature conversion table
    bandlist is an integer list of which THEMIS bands are in the radiance cube
    bx is the number of elements in bandlist, also the number of bands in the radiance cube
    nullval is the null data value
  */

  w = GetY(temp_rad);

  /* assigning memory for the interpolated data, temps and rads */
  irads = (float *)calloc(sizeof(FLOAT), x*y*bx);
  temps = (float *)malloc(sizeof(FLOAT)*w);
  rads = (float *)malloc(sizeof(FLOAT)*w);
  
  /* slopes and intercepts arrays */
  m = (float *)calloc(sizeof(FLOAT), w-1);
  b = (float *)calloc(sizeof(FLOAT), w-1);

  /* extract the linear temperature array */
  for (i=0;i<w;i++) {
    temps[i] = extract_float(temp_rad,cpos(0,i,0,temp_rad));
  }

  /* loop through the bands */
  for(k=0;k<bx;k++) {

    band = bandlist[k];

    /* extract linear radiances array */
    for(i=0;i<w;i++) {
      rads[i] = extract_float(temp_rad,cpos(band,i,0,temp_rad));
    }

    /* calculate the slopes and intercepts */
    for(i=1;i<w;i++){
      m[i-1] = (rads[i]-rads[i-1])/(temps[i]-temps[i-1]);
      b[i-1] = rads[i-1] - m[i-1]*temps[i-1];
    }

    /* work on a point at a time */
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	cur_val = btemp[j*x + i];
	if(cur_val == 0) irads[k*y*x + j*x + i] = nullval;

	if(cur_val > 0) {

	  /* locate the two bounding points in the temps array containing the temperature value */
	  pt1 = 0;
	  pt2 = w;
	  mid = 0;
	
	  while((pt2-pt1) > 1) {
	    mid = (pt1+pt2)/2;
	    if(cur_val > temps[mid]) pt1 = mid;
	    if(cur_val < temps[mid]) pt2 = mid;
	    if(cur_val == temps[mid]) pt1 = pt2 = mid;
	  }

	  if(rads[pt2] == rads[pt1]) irads[k*y*x + j*x + i] = rads[pt1];
	  else irads[k*y*x + j*x + i] = m[pt1]*cur_val + b[pt1];
	}
      }
    }
  }

  free(temps);
  free(rads);
  free(m);
  free(b);
  return(irads);
}








Var *
thm_themissivity(vfuncptr func, Var * arg)
{

  Var         *rad = NULL;                     /* the original radiance */
  Var         *bandlist = NULL;                /* list of bands to be converted to brightness temperature */
  Var         *out = NULL;                     /* end product */
  float        nullval = -32768;               /* default null value of radiance data */
  char        *fname = NULL;                   /* the pointer to the filename */
  int          b1 = 3, b2 = 9;                 /* the boundary bands used to determine highest brightness temperature */
  int         *blist = NULL;                   /* the integer list of bands extracted from bandlist or created */
  int          i;                              /* loop index */
  int          bx, x, y, z;                    /* size of the original array */
  emissobj    *e_struct;                       /* emissivity structure output from themissivity */
  
  Alist alist[7];
  alist[0] = make_alist("rad", 		 ID_VAL,         NULL,   &rad);
  alist[1] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[2] = make_alist("null",          FLOAT,          NULL,   &nullval);
  alist[3] = make_alist("temp_rad_path", ID_STRING,      NULL,   &fname);
  alist[4] = make_alist("b1",            INT,            NULL,   &b1);
  alist[5] = make_alist("b2",            INT,            NULL,   &b2);
  alist[6].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (rad == NULL) {
    parse_error("themissivity() - 7/07/04");
    parse_error("THEMIS radiance to THEMIS emissivity converter\n");
    parse_error("Syntax:  b = thm.themissivity(rad,bandlist,null,temp_rad_path,b1,b2)");
    parse_error("example: b = thm.themissivity(a)");
    parse_error("example: b = thm.themissivity(a,1//2//3//4//5//6//7//8//9,0,b1=4,b2=8)");
    parse_error("example: b = thm.themissivity(rad=a,bandlist=4//9//10,null=-2,temp_rad_path=\"/themis/calib/temp_rad_v3\")");
    parse_error("rad - any 3-D radiance array.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("null - non-data pixel value. Default is -32768 AND 0.");
    parse_error("temp_rad_path - alternate path to temp_rad lookup table.  Default is /themis/calib/temp_rad_v4.");
    parse_error("b1 - first band to search for maximum brightness temperature. Default is 3. ");
    parse_error("b2 - end band to search for maximum brightness temperature. Default is 9.\n");
    return NULL;
  }

  if (bandlist != NULL) {
    bx = GetX(bandlist);
    blist = (int *)malloc(sizeof(int)*bx);
    for(i=0;i<bx;i++) {
      blist[i] = extract_int(bandlist,cpos(i,0,0,bandlist));
    }
  }

  if (bandlist == NULL) {
    blist = (int *)malloc(sizeof(int)*10);
    for(i=0;i<10;i++) {
      blist[i] = i+1;
    }
    bx = 10;
  }

  x = GetX(rad);
  y = GetY(rad);
  z = GetZ(rad);

  if(z != bx){
    parse_error("bandlist not same dimension as input radiance!");
    return(out);
  }

  if (fname == NULL) fname = strdup("/themis/calib/temp_rad_v4");

  e_struct = themissivity(rad, blist, nullval, fname, b1, b2);

  if(e_struct) {
    out = new_struct(2);
    add_struct(out, "emiss", newVal(BSQ, x, y, z, FLOAT, e_struct->emiss));
    add_struct(out, "maxbtemp", newVal(BSQ, x, y, 1, FLOAT, e_struct->maxbtemp));

    free(e_struct);
    return(out);
  }

  else {
    return(NULL);
  }
}
 



emissobj *themissivity(Var *rad, int *blist, float nullval, char *fname, int b1, int b2)
{

  Var         *temp_rad = NULL;                /* temperature/radiance lookup file */
  Var         *out = NULL;                     /* end product */
  FILE        *fp = NULL;                      /* pointer to file */
  struct       iom_iheader h;                  /* temp_rad_v4 header structure */
  float       *emiss = NULL;                   /* the output emissivity */
  float       *b_temps = NULL;                 /* brightness temperatures computed by rad2tb */
  float       *max_b_temp = NULL;              /* maximum brightness temperatures for radiance cube (1-D) */
  float       *irad = NULL;                    /* interpolated radiance from max_b_temp */
  int          x, y, z;                        /* dimensions of the data */
  int          bx = 0;                         /* x-dimension of the bandlist */
  int          i, j, k;                        /* loop index */
  float        temp_val;                       /* guess */
  emissobj    *e_struct = NULL;

  x = GetX(rad);
  y = GetY(rad);
  z = GetZ(rad);

  /* stuff for b1 and b2 */
  if(b2>z){
    parse_error("Bad limits for max_b_temp. Setting b1 = 1 and b2 = %d",z);
    b1 = 1;
    b2 = z;
  }

  /* initialize temp_rad header */
  iom_init_iheader(&h);

  /* load temp_rad_v4 table into memory */
  fp = fopen(fname, "rb");

  if (fp == NULL) {
    parse_error("Can't open look up table /themis/calib/temp_rad_v4!\n");
    return(NULL);
  }

  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);

  /* convert to brightness temperature and return */
  b_temps = rad2tb(rad, temp_rad, blist, z, nullval);

  e_struct = (emissobj *)calloc(sizeof(emissobj),1);
  e_struct->emiss = (float *)calloc(sizeof(float), x*y*z);
  e_struct->maxbtemp = (float *)calloc(sizeof(float), x*y);

  /* find max_b_temp */
  for(k=b1-1;k<b2;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	if(b_temps[k*x*y + j*x + i] > e_struct->maxbtemp[j*x + i]) e_struct->maxbtemp[j*x + i] = b_temps[k*x*y + j*x + i];
      }
    }
  }

  free(b_temps);

  /* compute the interpolated radiance from max_b_temp */
  irad = tb2rad(e_struct->maxbtemp, temp_rad, blist, z, nullval, x, y);

  /* divide the interpolated radiance by the original radiance and set to emissivity */
  for(k=0;k<z;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	temp_val = extract_float(rad,cpos(i,j,k,rad));
	if (temp_val != 0 && irad[k*y*x + j*x + i] != 0 && temp_val != nullval && irad[k*y*x + j*x + i] != nullval) {
	  e_struct->emiss[k*y*x + j*x + i] = temp_val / irad[k*y*x + j*x + i];
	}
      }
    }
  }

  free(blist);
  free(irad);

  return(e_struct);
}





Var *
thm_white_noise_remove1(vfuncptr func, Var * arg)
{

  Var         *rad = NULL;                     /* the original radiance */
  Var         *bandlist = NULL;                /* list of bands to be converted to brightness temperature */
  Var         *temp_rad = NULL;                /* temperature/radiance lookup file */
  Var         *out = NULL;                     /* end product */
  char        *fname = NULL;                   /* the pointer to the filename */
  FILE        *fp = NULL;                      /* pointer to file */
  struct       iom_iheader h;                  /* temp_rad_v4 header structure */
  float        nullval = -32768;               /* default null value of radiance data */
  float       *emiss = NULL;                   /* the output emissivity */
  float       *s_emiss = NULL;                 /* smoothed emissivity */
  float       *b_temps = NULL;                 /* brightness temperatures computed by rad2tb */
  float       *max_b_temp = NULL;              /* maximum brightness temperatures for radiance cube (1-D) */
  float       *irad = NULL;                    /* interpolated radiance from max_b_temp */
  float       *kern = NULL;                    /* smoothing kernel */
  int         *blist = NULL;                   /* the integer list of bands extracted from bandlist or created */
  int          x, y, z;                        /* dimensions of the data */
  int          bx = 0;                         /* x-dimension of the bandlist */
  int          i, j, k;                        /* loop index */
  float        temp_val;                       /* guess */
  int          b1 = 3, b2 = 9;                 /* the boundary bands used to determine highest brightness temperature */
  int          k_size = 7;                     /* size of the smoothing kernel */
  
  Alist alist[7];
  alist[0] = make_alist("rad", 		 ID_VAL,         NULL,   &rad);
  alist[1] = make_alist("k_size",        INT,            NULL,   &k_size);
  alist[2] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[3] = make_alist("null",          FLOAT,          NULL,   &nullval);
  alist[4] = make_alist("b1",            INT,            NULL,   &b1);
  alist[5] = make_alist("b2",            INT,            NULL,   &b2);
  alist[6].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (rad == NULL) {
    parse_error("white_noise_remove1() - 7/10/04");
    parse_error("White noise removal algorithm for THEMIS radiance cubes");
    parse_error("Converts to emissivity, smoothes and multiplies by unsmoothed brightness temperature\n");
    parse_error("Syntax:  b = thm.white_noise_remove1(rad,k_size,bandlist,null,b1,b2)");
    parse_error("example: b = thm.white_noise_remove1(a)");
    parse_error("example: b = thm.white_noise_remove1(a,7,1//2//3//4//5//6//7//8//9,0,b1=4,b2=8)");
    parse_error("example: b = thm.white_noise_remove1(rad=a,k_size=7,bandlist=4//9//10,null=-2)");
    parse_error("rad - any 3-D radiance array.");
    parse_error("k_size - the size of the smoothing kernel. Default is 5.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("null - non-data pixel value. Default is -32768 AND 0.");
    parse_error("b1 - first band to search for maximum brightness temperature. Default is 3. ");
    parse_error("b2 - end band to search for maximum brightness temperature. Default is 9.\n");
    return NULL;
  }

  if (bandlist != NULL) {
    return(bandlist);
    bx = GetX(bandlist);
    blist = (int *)malloc(sizeof(int)*bx);
    for(i=0;i<bx;i++) {
      blist[i] = extract_int(bandlist,cpos(i,0,0,bandlist));
    }
  }

  if (bandlist == NULL) {
    blist = (int *)malloc(sizeof(int)*10);
    for(i=0;i<10;i++) {
      blist[i] = i+1;
    }
    bx = 10;
  }

  x = GetX(rad);
  y = GetY(rad);
  z = GetZ(rad);

  /* stuff for b1 and b2 */
  if(b2>z){
    parse_error("Bad limits for max_b_temp. Setting b1 = 1 and b2 = %d",z);
    b1 = 1;
    b2 = z;
  }

  /* initialize temp_rad header */
  iom_init_iheader(&h);

  /* load temp_rad_v4 table into memory */
  fname = strdup("/themis/calib/temp_rad_v4");
  fp = fopen(fname, "rb");

  if (fp == NULL) {
    parse_error("Can't open look up table /themis/calib/temp_rad_v4!\n");
    return(NULL);
  }

  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);

  /* convert to brightness temperature and return */
  b_temps = rad2tb(rad, temp_rad, blist, bx, nullval);

  /* allocate memory for max_b_temp */
  max_b_temp = (float *)calloc(sizeof(FLOAT), x*y);

  /* find max_b_temp */
  for(k=b1-1;k<=b2;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	if(b_temps[k*x*y + j*x + i] > max_b_temp[j*x + i]) max_b_temp[j*x + i] = b_temps[k*x*y + j*x + i];
      }
    }
  }

  free(b_temps);

  /* compute the interpolated radiance from max_b_temp */
  irad = tb2rad(max_b_temp, temp_rad, blist, bx, nullval, x, y);

  /* allocate memory for emiss */
  emiss = (float *)calloc(sizeof(FLOAT), x*y*z);

  /* divide the interpolated radiance by the original radiance and set to emissivity */
  for(k=0;k<z;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	temp_val = extract_float(rad,cpos(i,j,k,rad));
	if (temp_val != 0 && irad[k*y*x + j*x + i] != 0 && temp_val != nullval && irad[k*y*x + j*x + i] != nullval) {
	  emiss[k*y*x + j*x + i] = temp_val / irad[k*y*x + j*x + i];
	}
      }
    }
  }

  free(blist);
  free(max_b_temp);

  /* create kernel and smooth the emissivity */
  kern = (float *)malloc(sizeof(FLOAT)*k_size*k_size);
  for(j=0;j<k_size;j++) {
    for(i=0;i<k_size;i++) {
      kern[j*k_size + i] = 1.0;
    }
  }

  s_emiss = convolve(emiss, kern, x, y, z, k_size, k_size, 1, 1, 0);

  /* multiply by irad to get smooth picture - reuse emiss array */
  for(k=0;k<z;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	emiss[k*y*x + j*x + i] = s_emiss[k*y*x + j*x + i] * irad[k*y*x + j*x + i];
      }
    }
  }
  
  /* reset the null values for chirs to stop bitching */
  for(k=0;k<z;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	if(emiss[k*y*x + j*x + i]==0) emiss[k*y*x + j*x + i]=nullval;
      }
    }
  }

  free(s_emiss);
  free(irad);

  out = newVal(BSQ, x, y, z, FLOAT, emiss);
  return out;
}





Var*
thm_maxpos(vfuncptr func, Var *arg)
{
 
  Var    *data = NULL;                      /* the orignial data */
  Var    *out = NULL;                       /* the output structure */
  int    *pos = NULL;                       /* the output position */
  float   ignore = -32768;                  /* null value */
  float  *val = NULL;                       /* the output values */
  int     i, j, k, l;                       /* loop indices */
  int     x=0, y=0, z=0;                    /* size of the data */
  float   ni1=0, ni2=-32798, ni3=-852;      /* temp values and positions */
  int     opt=0;                            /* return array option */
  int     iter=1;                           /* number of iterations to include */
   
  Alist alist[5];
  alist[0] = make_alist("data",      ID_VAL,    NULL,  &data);
  alist[1] = make_alist("ret",       INT,       NULL,  &opt);
  alist[2] = make_alist("iter",      INT,       NULL,  &iter);
  alist[3] = make_alist("ignore",    FLOAT,     NULL,  &ignore);
  alist[4].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if (data == NULL) {
    parse_error("\nUsed to find the position of the max point in an array\n");
    parse_error("$1 = the data");
    parse_error("Prints the value and location\n");
    parse_error("ret=1 returns the value and location in a structure\n");
    parse_error("iter= # of points to find in the data");
    parse_error("if iter >1 it will automatically return the points\n");
    parse_error("ignore = the value to ignore\n");
    parse_error("c.edwards 5/19/04\n");
    return NULL;
  }

  if (iter > 1 ) {
    opt=1;
  }

  /* create array for postion */
  pos = (int *)calloc(sizeof(int), 3*iter);
  val = (float *)calloc(sizeof(float), iter);
  
  /* x, y and z dimensions of the data */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);

  /* find the maximum point and its position */  
  for(l=0; l<iter; l++) {
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  
	  /* ni1 = current value */
	  /* ni2 = curent max value */
	  /* ni3 = old max val */
	  ni1 = extract_float(data, cpos(i,j,k,data));
	  if(ni1>ni2 && (ni3 == -852 || (ni1<ni3 && ni3 != -852)) && ni1 != ignore) {
	    ni2=ni1;
	    pos[3*l+2]=k+1;
	    pos[3*l+1]=j+1;
	    pos[3*l+0]=i+1;
	  }
	}
      }
    }
    ni3=ni2;
    val[l]=ni2;
    ni2=-32798;
  }

  if(iter==1) {
    /* return the findings */
    printf("\nLocation: %i,%i,%i\n",pos[0],pos[1],pos[2]);
    printf("%.13f\n\n",ni3);
    
    if(opt==0) return NULL;
  }

  if(opt!=0) {
    out = new_struct(2);
    add_struct(out,"pos",newVal(BSQ, 3, iter, 1, INT, pos));
    add_struct(out,"val",newVal(BSQ, 1, iter, 1, FLOAT, val));
    return out;
  }
}



Var*
thm_minpos(vfuncptr func, Var *arg)
{

  Var    *data = NULL;                 /* the orignial data */
  Var    *out = NULL;                  /* the output struture */
  int    *pos = NULL;                  /* the output postion */
  float   ignore = -32768;             /* null value */
  float  *val = NULL;                  /* the output values */
  int     i, j, k, l;                  /* loop indices */
  int     x=0, y=0, z=0;               /* size of the data */
  float   ni1=0, ni2=32798, ni3=852;   /* temp values and positions */
  int     opt=0;                       /* return array option */
  int     iter=1;                      /* number of iterations to include */

  Alist alist[5];
  alist[0] = make_alist("data",      ID_VAL,    NULL,  &data);
  alist[1] = make_alist("ret",       INT,       NULL,  &opt);
  alist[2] = make_alist("iter",      INT,       NULL,  &iter);
  alist[3] = make_alist("ignore",    FLOAT,     NULL,  &ignore);
  alist[4].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
 
  if (data == NULL) {
    parse_error("\nUsed to find the position of the min point in an array\n");
    parse_error("$1 = the data");
    parse_error("Prints the value and location\n");
    parse_error("ret = 1 returns the value and location in a structure\n");
    parse_error("iter = # of points to find in the data");
    parse_error("if iter >1 it will automatically return the points\n");
    parse_error("ignore = the value to ignore\n");
    parse_error("c.edwards 5/19/04\n");
    return NULL;
  }

  if (iter > 1 ) {
    opt=1;
  }
  
  /* create array for position and value*/
  pos = (int *)calloc(sizeof(int), 3*iter);
  val = (float *)calloc(sizeof(float), iter);
  
  /* x, y and z dimensions of the data */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);

  /* find the minimum point and its position */  
  for(l=0; l<iter; l++) {
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  
	  /* ni1 = current value */
	  /* ni2 = curent max value */
	  /* ni3 = old max val */
	  ni1 = extract_float(data, cpos(i,j,k,data));
	  if(ni1<ni2 && (ni3 == 852 || (ni1>ni3 && ni3 != 852)) && ni1 != ignore) {
	    ni2=ni1;
	    pos[3*l+2]=k+1;
	    pos[3*l+1]=j+1;
	    pos[3*l+0]=i+1;
	  }
	}
      }
    }
    ni3=ni2;
    val[l]=ni2;
    ni2=32798;
  }

  /* return the findings */
  if(iter==1) {    
    printf("\nLocation: %i,%i,%i\n",pos[0],pos[1],pos[2]);
    if(ni2<-32768) {
      printf("%.9e\n\n",ni3);
    }
    if(ni2>=-32768) {
      printf("%.9f\n\n",ni3);
    }
    if(opt==0) return NULL;
  }

  if(opt!=0) {
    out = new_struct(2);
    add_struct(out,"pos",newVal(BSQ, 3, iter, 1, INT, pos));
    add_struct(out,"val",newVal(BSQ, 1, iter, 1, FLOAT, val));
    return out;
  }
}



Var *
thm_unscale(vfuncptr func, Var * arg)
{

  Var      *pds = NULL;                  /* the original pds structure */
  Var      *out = NULL;                  /* the output scaled data */
  Var      *w_pds=NULL;                  /* data from struct */
  float    *w_pic=NULL;                  /* working data */
  Var      *struc=NULL;                  /* second struct */
  Var      *bin=NULL;                    /* band_bin struct */
  Var      *baset, *multt;               /* base/multiplier temp*/
  double   *mult, *base;                 /* base/multiplier */
  float     core_null=-32768;            /* core null value */
  int       i, j, k;                     /* loop indices */
  int       x=0, y=0, z=0, x1=0;         /* size of the picture */
  float     tv1=0;
  float     tv2=0;                       /* temp pixel value */
  
  
  Alist alist[2];
  alist[0] = make_alist("obj",        ID_STRUCT,     NULL,   &pds);
  alist[1].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* get structures */
  find_struct(pds,"spectral_qube",&struc);
  find_struct(struc,"data",&w_pds);
  /*weird line that doesn't work hard sets core_null vals*/
  //core_null = find_struct(struc,"core_null",NULL);
  find_struct(struc,"band_bin",&bin);
  find_struct(bin,"band_bin_multiplier",&multt);
  find_struct(bin,"band_bin_base",&baset);
 
  if(pds==NULL || struc==NULL || w_pds==NULL || multt==NULL || baset==NULL) {
    parse_error("\nUsed to unscale a pds from short to float data\n");
    parse_error("$1 = the pds structure\n");
    parse_error("core_null values will be set to -32768");
    parse_error("c.edwards 5/17/05\n");
    return NULL;
  }
  
  /* set up base and multiplier arrays */
  x1 = GetX(multt);
  
  base = (double *)calloc(sizeof(double), x1);
  mult = (double *)calloc(sizeof(double), x1);
 
  for(i=0; i<x1; i++) {
    mult[i]=extract_double(multt,cpos(i,0,0,multt));
    base[i]=extract_double(baset,cpos(i,0,0,multt));
  }

  /* x, y and z dimensions of the data */
  x = GetX(w_pds);
  y = GetY(w_pds);
  z = GetZ(w_pds);

  /* allocate memory for the picture */
  w_pic = (float *)calloc(sizeof(float), x*y*z);  
  
  /* loop through and unscale data */
  for(k=0; k<z; k++) {
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
  	tv1 = extract_float(w_pds, cpos(i,j,k,w_pds));
  	tv2 = (float)((tv1*mult[k])+base[k]);
  	if (tv1 == core_null ) {
  	  tv2 = -32768.0;
  	}
  	w_pic[x*y*k + x*j + i]=tv2;
      }
    }
  }
  
  printf("Core_null values set to -32768.0\n");
  
  /* clean up and return data */
  free(base);
  free(mult);
  
  out=newVal(BSQ,x,y,z,FLOAT,w_pic);
  return out;
}



Var *
thm_cleandcs(vfuncptr func, Var * arg)
{

  typedef unsigned char byte;
  
  Var    *pic = NULL;            /* the orignial dcs pic */
  Var    *out = NULL;            /* the output pic */
  byte   *w_pic;                 /* modified image */
  int     i, j, k;               /* loop indices */
  int     x=0, y=0, z=0;         /* size of the picture */
  byte    tv=0;                  /* temp pixel value */
  int     opt=2;                 /* option for possible clean */
  
  Alist alist[3];
  alist[0] = make_alist("pic",      ID_VAL,     NULL,  &pic);
  alist[1] = make_alist("opt",      INT,        NULL,  &opt);
  alist[2].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  if ( opt < 1 || opt > 2) opt=2;

  if (pic == NULL) {
    parse_error("\nUsed to remove extraneous color from a dcs image\n");
    parse_error("$1 = the dcs picture to clean");
    parse_error("opt = 1 more clean but less data is saved");
    parse_error("opt = 2 less clean but more data is saved(Default)\n");
    parse_error("c.edwards 6/14/04\n");
    return NULL;
  }

  /* x, y and z dimensions of the data */
  x = GetX(pic);
  y = GetY(pic);
  z = GetZ(pic);
  
  /* allocate memory for the picture */
  w_pic = (byte *)calloc(sizeof(byte), x*y*z);
  
  if(w_pic == NULL) return NULL;

  /* loop through data and extract new points with null value of 0 ignored */
  for(j=0; j<y; j++) {
    for(i=0; i<x; i++) {
      for(k=0; k<z; k++) {
	tv = (byte)extract_int(pic, cpos(i,j,k, pic));
    	w_pic[j*x*z + i*z + k] = tv;
	if (k==z-1) {
	  if((w_pic[j*x*z + i*z + k] == 0) + (w_pic[j*x*z + i*z + (k-1)] == 0) + (w_pic[j*x*z + i*z + (k-2)] == 0) >= opt ) {
	    w_pic[j*x*z + i*z + k]=0;
	    w_pic[j*x*z + i*z + (k-1)]=0;
	    w_pic[j*x*z + i*z + (k-2)]=0;
	  } 
	}
      }
    }
  }
  
  /* return the modified data */
  out = newVal(BIP, z, x, y, BYTE, w_pic);
  return out;
}



Var*
thm_white_noise_remove2(vfuncptr func, Var *arg)
{
  typedef unsigned char byte;

  byte   *bc = NULL;                /* band count for orignal stuff */        
  Var    *data = NULL;              /* the original data */
  Var    *out = NULL;               /* the output structure */
  float   nullval=-32768;           /* null value */  
  float  *w_pic, *w_pic2;           /* working data */
  float   tv=0;                     /* temporary value */
  float   *total;                   /* the sum of the data (luminosity) */
  int     i, j, k;                  /* loop indices */
  int     x=0, y=0, z=0;            /* size of the data */
  float  *kern;                     /* kernel for the convolve */
  int     b10=10;                   /* band option */
  float  *band_10;                  /* band 10 to smooth */
  int     filt=7;                   /* the filter size */
  int     kernsize;                 /* number of kern elements */ 
  int     vb=1;                     /* verbosity for updataes */

  Alist alist[6]; 
  alist[0] = make_alist("data",      ID_VAL,    NULL,  &data);
  alist[1] = make_alist("null",      FLOAT,     NULL,  &nullval);
  alist[2] = make_alist("b10",       INT,       NULL,  &b10);
  alist[3] = make_alist("filt",      INT,       NULL,  &filt);
  alist[4] = make_alist("verbose",   INT,       NULL,  &vb);
  alist[5].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);
 
  if (data == NULL) {
    parse_error("\nUsed to remove white noise from data");
    parse_error("More bands are better\n");
    parse_error("$1 = the data to remove the noise from");
    parse_error("null = null value for the image (Default is -32768)");
    parse_error("b10 = the atmospheric band (Default is 10)");
    parse_error("If b10=0, then there is no band 10 information");
    parse_error("filt = the filter size to use (Default is 7)\n");
    parse_error("NOTE: This WILL CHANGE the data and");
    parse_error("should only be used for \"pretty pictures\"");
    parse_error("Band 10 will be smoothed by convolution\n");
    parse_error("Last Modified by c.edwards 8/10/04");
    parse_error("Added filt and band 10 smooth\n");
    parse_error("There is yet another option to clean images");
    parse_error("You must source \"/u/cedwards/christopher.dvrc\"");
    parse_error("The function is rmnoise_pca()");
    parse_error("It has more limitations but retains more color information");
    parse_error("Type \"rmnoise_pca()\" for help\n");
    return NULL;
  }

  /* set the band number for c */
  if(b10 != 0 ) {
    b10-=1;
    if(b10<0 || b10>9) b10=9;
  }

  /* x, y, and z dimensions of the data  */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);
  kernsize=filt*filt;
  
  /* more error handling */
  if(z < 2) {
    printf("\nSorry you need more bands to remove noise\n\n");
    return NULL;
  }
  if(vb==1) printf("\nMORE UPDATES! CHECK THE HELP!\n");

  /* allocate memory for the data */
  bc = (byte *)calloc(sizeof(byte), x*y);
  w_pic = (float *)calloc(sizeof(float), x*y*z);
  w_pic2 = (float *)calloc(sizeof(float), x*y*z);
  total = (float *)calloc(sizeof(float), x*y);
  kern = (float *)calloc(sizeof(float), kernsize);
  band_10 = (float *)calloc(sizeof(float),x*y);  

  if(w_pic == NULL) return NULL;

  /* if there is a band 10 */
  if(b10 != 0 ) {
    /* extract values and make luminosity map */
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  tv = extract_float(data, cpos(i,j,k, data));
	  if(k!=b10) {
	    total[x*j + i]+=tv;
	    if(tv!=nullval) bc[x*j + i]+=1;
	  }
	  if(k==(x-1)) band_10[x*j + i]=tv;
	  w_pic[x*y*k + x*j + i]=tv;
	}
      }
    }
    
    /* divide by the luminosity map and check for null values */
    for(k=0; k<z; k++) {
      if(k!=b10) {
	for(j=0; j<y; j++) {
	  for(i=0; i<x; i++) {
	    if(bc[x*j + i] == b10) w_pic2[x*y*k + x*j + i]=w_pic[x*y*k + x*j + i]/total[x*j + i];  
	  }
	}
      }
    }
    
    /* convolve over the image */
    for(i=0;i<kernsize;i++) {
      kern[i]=1.0;
    }
    w_pic2=convolve(w_pic2,kern,x,y,z,filt,filt,1,1,0);
    band_10=convolve(band_10,kern,x,y,1,filt,filt,1,1,0);
    
  /* convert back to normal space and fill other pixels */
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  if(k!=b10) {
	    if(bc[x*j + i] == b10) w_pic2[x*y*k + x*j + i]*=total[x*j + i];
	    if(bc[x*j + i] < b10) w_pic2[x*y*k + x*j + i]=w_pic[x*y*k + x*j + i];
	  }
	  if(k==b10) w_pic2[x*y*k + x*j + i]=band_10[x*j + i];
	}
      }
    }
  }
  
  /*if there is not a band 10 */
  if(b10 == 0 ) {
    /* extract values and make luminosity map */
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  tv = extract_float(data, cpos(i,j,k, data));
	  total[x*j + i]+=tv;
	  w_pic[x*y*k + x*j + i]=tv;
	}
      }
    }
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  w_pic2[x*y*k + x*j + i]=w_pic[x*y*k + x*j + i]/total[x*j + i];  
	}
      }
    }
  
    /* convolve over the image */
    for(i=0;i<kernsize;i++) {
      kern[i]=1.0;
    }
    w_pic2=convolve(w_pic2,kern,x,y,z,filt,filt,1,1,0);
    
    /* convert back to normal space and fill other pixels */
    for(k=0; k<z; k++) {
      for(j=0; j<y; j++) {
	for(i=0; i<x; i++) {
	  w_pic2[x*y*k + x*j + i]*=total[x*j + i];
	  if(w_pic[x*y*k + x*j + i]==nullval) w_pic2[x*y*k + x*j + i]=nullval;
	}
      }
    }
  }

  /*clean up and return the image */
  free(w_pic);
  free(total);
  free(kern);
  free(band_10);
  free(bc);

  out=newVal(BSQ,x,y,z,FLOAT,w_pic2);
  return out;
}


Var * 
thm_sstretch2(vfuncptr func, Var * arg)
{

  /* takes data and stretches it similar to sstretch() davinci function*/
  /* but does it band by band */

  typedef unsigned char byte;
  
  Var       *data=NULL;         /* input */
  Var       *out=NULL;          /* output */
  float     *w_data=NULL;       /* working data2 */
  byte      *w_data2=NULL;      /* working data */
  float      ignore=-32768;     /* ignore value*/
  int        x,y,z;             /* indices */
  double     sum = 0;           /* sum of elements in data */
  double     sumsq = 0;         /* sum of the square of elements in data */
  double     stdv = 0;          /* standard deviation */
  int        cnt = 0;           /* total number of non-null points */
  int        i,j,k;
  float      tv,max=-32768;
  float      v=40; 
  
  Alist alist[4];
  alist[0] = make_alist("data", 	  ID_VAL,	NULL,	&data);
  alist[1] = make_alist("ignore", 	  FLOAT,	NULL,	&ignore);
  alist[2] = make_alist("variance",       FLOAT,        NULL,   &v);
  alist[3].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if (data == NULL) {
    parse_error("\nUsed to sstretch data band by band");
    parse_error("Similar to the davinci function sstretch()\n");
    parse_error("$1=data to be stretched");
    parse_error("ignore=value to ignore (Default=-32768)");
    parse_error("variance=variance of the stretch (Default=40)\n");
    parse_error("c.edwards 3/26/05\n");
    return NULL;
  }

  /* x, y and z dimensions of the data */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);
  max = ignore;

  /* create out array */
  w_data = (float *)calloc(sizeof(float), x*y*z);
  w_data2 = (byte *)calloc(sizeof(byte),x*y*z);
  
  /* stretch each band separately */
  for(k=0;k<z;k++) {
    sum = 0;
    sumsq = 0;
    stdv = 0;
    cnt = 0;
    tv = 0;
    
    /* calculate the sum and the sum of squares */
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	tv=extract_float(data, cpos(i,j,k, data));
	w_data[k*y*x + j*x + i]=tv;
	if( tv != ignore){
	  sum += (double)tv;
	  sumsq += (double)(tv*tv);
	  cnt += 1;
	}
      }
    }
    
    /* calculate standard deviation */
    stdv = sqrt((sumsq - (sum*sum/cnt))/(cnt-1));
    sum /= (double)cnt;
    
    /* fill in stretched values */
    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	if(w_data[k*y*x + j*x + i] != ignore) w_data[k*y*x + j*x + i] = (float)((w_data[k*y*x + j*x + i] - sum)*(v/stdv)+127);
	if(w_data[k*y*x + j*x + i] < 0) w_data[k*y*x + j*x + i] = 0; 
	if(w_data[k*y*x + j*x + i] > 255) w_data[k*y*x + j*x + i] = 255; 
      }
    }
  }
  
  /*convert to bip */
  for(j=0; j<y; j++) {
    for(i=0; i<x; i++) {
      for(k=0; k<z; k++) {
	w_data2[j*x*z + i*z + k]=(byte)w_data[k*y*x + j*x + i];
      }
    }
  }
  
  /* clean up and return data */
  free(w_data);
  out = newVal(BIP,z, x, y, BYTE, w_data2);	
  return(out);
}
