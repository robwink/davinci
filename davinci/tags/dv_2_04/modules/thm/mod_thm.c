#include "parser.h"
#include "ff_modules.h"
#include "dvio.h"

#define XAXIS 1;
#define YAXIS 2;

/* Enviornment Variable set up for THEMIS Radiance Temperature conversion table */
static const char *DV_SCRIPT_FILES_ENV="DV_SCRIPT_FILES";
static const char *RAD_TEMP_DEFAULT_PATH="/themis/calib";
static const char *RAD_TEMP_LOOKUP_TBL="temp_rad_v4";
static const char *DV_INST_PARAMS = "instrument_parameters";


/* Define emissivity structure type */
typedef struct {
  float *emiss;
  float *maxbtemp;
} emissobj;

/* Static Dependencies */
static float *column_fill(float *column, int y, int z, int csize, float ignore);
static float *convolve(float *obj, float *kernel, int ox, int oy, int oz, int kx, int ky, int kz, int norm, float ignore);
static int *do_corners(Var *pic_a, float nullval);
static float *sawtooth(int x, int y, int z); 
static float *rad2tb(Var *radiance, Var *temp_rad, int *bandlist, int bx, float nullval, float maxem);
static float *tb2rad(float *btemp, Var *temp_rad, int *bandlist, int bx, float nullval, int x, int y);
static emissobj *themissivity(Var *rad, int *blist, float nullval, char *fname, int b1, int b2, float maxem);
static float *bbrw_k(float *btemp, int *bandlist, int x, int y, int z, float nullval);
static double *radcorrspace(Var *measured, float *bbody);
static double *minimize_1d(Var *measured, float *bbody, double em_start, double *rad_start, double *rad_end, double *slopes);
static char *get_temp_rad(char *fname);

/* THM Module Functions */
static Var *thm_corners(vfuncptr, Var *);
static Var *thm_column_fill(vfuncptr, Var *);
static Var *thm_deplaid(vfuncptr, Var *);
static Var *thm_emiss2rad(vfuncptr, Var*);
static Var *thm_rad2tb(vfuncptr, Var *);
static Var *thm_radcorr(vfuncptr, Var *);
static Var *thm_reconstitute(vfuncptr, Var *);
static Var *thm_rectify(vfuncptr, Var *);
static Var *thm_supersample(vfuncptr, Var *);
static Var *thm_themissivity(vfuncptr, Var *);
static Var *thm_unscale(vfuncptr, Var *);
static Var *thm_white_noise_remove1(vfuncptr, Var *);
static Var *thm_white_noise_remove2(vfuncptr, Var *);


static dvModuleFuncDesc exported_list[] = {
  { "column_fill", (void *) thm_column_fill},
  { "convolve", (void *) ff_convolve2 },
  { "corners", (void *) thm_corners },
  { "deplaid", (void *) thm_deplaid },
  { "emiss2rad", (void *) thm_emiss2rad },
  { "kfill", (void *) ff_kfill },
  { "ipi", (void *) ff_ipi },
  { "rad2tb", (void *) thm_rad2tb },
  { "radcorr", (void *) thm_radcorr },
  { "ramp", (void *) ff_ramp },
  { "reconstitute", (void *) thm_reconstitute },
  { "rectify", (void *) thm_rectify },
  { "sstretch", (void *) ff_sstretch2 },
  { "supersample", (void *) thm_supersample },
  { "themis_emissivity", (void *) thm_themissivity },
  { "unscale", (void *) thm_unscale },
  { "white_noise_remove1", (void *) thm_white_noise_remove1 },
  { "white_noise_remove2", (void *) thm_white_noise_remove2 }
}; 

static dvModuleInitStuff is = {
  exported_list, sizeof(exported_list)/sizeof(dvModuleFuncDesc),
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


/**
*** This returns x-y pairs stretched out into a 1x8 array (x1,y1,x2,y2...)
***
*** This makes an implicit assertion that the ignore value is exactly above
*** or below all values in the image.  It also calculates the row and column
*** summations, and assumes that these are not zero.
***
**/
Var *
thm_corners(vfuncptr func, Var * arg)
{
  Var     *pic_a = NULL;                               /* the input pic */
  float    nullval = 0;                                /* null value */
  int     *cns = NULL;                                 /* corners output array */
  
  Alist alist[3];
  alist[0] = make_alist("picture",		ID_VAL,		NULL,	&pic_a);
  alist[1] = make_alist("ignore",               FLOAT,          NULL,   &nullval);
  alist[2].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no picture got passed to the function */
  if (pic_a == NULL) {
    parse_error("corners() - 4/25/04");
    parse_error("Feed corners() a 1-band projected ISIS image and a null value.");
    parse_error("It will return the x and y values of the four corners.\n");
    parse_error("Syntax:  b = corners(picture,ignore)");
    parse_error("example: b = corners(a,ignore=-32768)");
    parse_error("picture - any 3-D projected image.");
    parse_error("ignore - the non-data pixel values. Default is 0.");
    parse_error("If an image is passed with more than one band, corners will only use the first band.\n");
    return NULL;
  }

  cns = do_corners(pic_a, nullval);
  return(newVal(BSQ, 1, 8, 1, INT, cns));
}



/**
*** Steps:
***    Compute a blackmask that excludes any pixels that have a difference more than 1.15x 
***    or 0.80x from the smoothed row average.
***
**/

Var *
thm_deplaid(vfuncptr func, Var * arg)
{
  /* updated 07/06/2005 to change "null" arguments to accept "ignore". - kjn*/

  /* updated after the comparison with neglecting luminosity conservation. 07/13/05 - kjn */

  /* Lord knows why, but I updated the algorithm to create a continuous column average correction **
  ** instead of having discrete boundaries. Also moved the help text to thm.gih. 10/18/2009 - kjn */

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
  byte     *ct_map;                                 /* map of # of bands per pixel */
  byte     *row_ct, *col_ct;                        /* max number of bands in all rows and all columns */
  byte     *blackmask;                              /* the blackmask and temperature mask */
  float    *tmask;                                  /* the temporary temperature mask */
  float     nullval = -32768;                       /* the null value assigned by incoming arguments "null" and/or "ignore"*/
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
  
  //handle axis options
  char     *options[] =  {
    "x", "y", "z", "xy", NULL
  };
  int       axis = 0;
  char     *ptr = NULL;
  
  Alist alist[10];
  alist[0] = make_alist("data", 		ID_VAL,		NULL,	&data);
  alist[1] = make_alist("ignore",               FLOAT,          NULL,    &nullval);
  alist[2] = make_alist("tmask_max",            FLOAT,          NULL,    &tmask_max);
  alist[3] = make_alist("tmask_min",            FLOAT,          NULL,    &tmask_min);
  alist[4] = make_alist("filt_len",             INT,            NULL,    &filt_len);
  alist[5] = make_alist("b10",                  INT,            NULL,    &b10);
  alist[6] = make_alist("dump",                 INT,            NULL,    &dump);
  alist[7] = make_alist("null",                 FLOAT,          NULL,    &nullval); // can be removed when all legacy programs are dead
  alist[8] = make_alist("axis",                 ID_ENUM,        options, &ptr);
  alist[9].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no data got passed to the function */
  if (data == NULL) {
    parse_error("\nType thm.deplaid(?) for help\n");
    return NULL;
  }
  
  if (ptr == NULL) {
    axis = 3;
  } else {
    if (strchr(ptr, 'x') || strchr(ptr, 'X')) axis |= XAXIS;
    if (strchr(ptr, 'y') || strchr(ptr, 'Y')) axis |= YAXIS;
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
  chunksb = ((y-350)/500) + 2;                                 /* number of chunks starting at -250 */
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
  /* pixels with tmask values greater than 1.15 or less than .8 of the row avg are set to zero */
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
	
  /* create brightness arrays for luminosity conservation */
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
  free(col_bright);
  free(col_avgs);
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

	  // deplaid only in the x direction
          if(axis==1) tv-=row_avg[k*y +j];

	  // deplaid in the y direction
          if(axis==2) tv-=col_avg[k*chunks*x + ck*x + i];

	  // deplaid in both directions
          if(axis==3) {

	    if(cca <= 125 && ck != 0) {
	      //for the first 125 lines we proportionally calculate plaid from the current and previous chunk
	      tv = tv - row_avg[k*y + j] - ((125.0-(float)cca)/250.0)*col_avg[k*chunks*x + (ck-1)*x + i] - 
		((125.0+(float)cca)/250.0)*col_avg[k*chunks*x + ck*x + i];
	    } else if (cca >= 126 && ck != chunks-1) {
	      //for the last 125 lines we proportionally calculate plaid from the current and next chunk
	      tv = tv - row_avg[k*y + j] - ((float)cca/250.0)*col_avg[k*chunks*x + (ck+1)*x + i] - 
		((250.0-(float)cca)/250.0)*col_avg[k*chunks*x + ck*x + i];
	    } else {
	      //if we're in the first 125 lines of the first chunk or the last 125 in the last chunk
	      //then we need to simply use the plaid from the current chunk
	      tv = tv - row_avg[k*y + j] - col_avg[k*chunks*x + ck*x + i];
	    }
	  }
          rdata[x*y*k + x*j + i] = tv;
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
thm_rectify(vfuncptr func, Var * arg)
{
  /* updated 07/06/2005 to change "null" arguments to accept "ignore".     - kjn */
  /* updated 07/19/2005 fixed bug that sometimes chopped off ends of data. - kjn */
  /* updated 07/26/2005 to check for ignore value before running corners.  - kjn */
  /* updated 07/27/2005 to fix top and bottom leftedge values.             - kjn */
  /* updated 07/27/2005 to handle errors from un-rectifiable arrays.       - kjn */
  /* Mon Sep 19 15:14:21 MST 2005 (NsG), Fixed out of bounds error in final loop */
  /* Mon Oct 17 18:28:45 MST 2005 added zero angle error handle            - kjn */

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
  int      force = 0;                  /* provides an option to override ignore value safety check */
  float    ign = 0;                    /* the value rectify believes to be the correct ignore value */

  Alist alist[6];
  alist[0] = make_alist("object",		ID_VAL,		NULL,	&obj);
  alist[1] = make_alist("ignore",               FLOAT,          NULL,   &nullo);
  alist[2] = make_alist("trust",                FLOAT,          NULL,   &trust);
  alist[3] = make_alist("force",                INT,            NULL,   &force);
  alist[4] = make_alist("null",                 FLOAT,          NULL,   &nullo); //can be removed when all legacy programs are dead
  alist[5].name = NULL;

  if (parse_args(func, arg, alist) == 0) return NULL;

  /* if no picture got passed to the function */
  if (obj == NULL){
    parse_error("\nType thm.rectify(?) for help\n");
    return NULL;
  }

  /* x, y, and z dimensions of the original picture */
  x = GetX(obj);
  y = GetY(obj);
  z = GetZ(obj);    

  /* Perform check to make sure ignore value is appropriate for the cube */
  /* do_corners will crash davinci if ignore value is wrong.             */
  /* Assumes that the ignore value will be found in the first row in the */
  /* first band, and that the ignore value will always be the smallest   */
  /* value in the array.                                                 */
  for(i=0; i<x; i++) {
    yiz = extract_float(obj, cpos(i, 0, 0, obj));
    ign = (yiz<ign)?yiz:ign;
  }
  if(ign != nullo && force == 0) {
    parse_error("\nERROR: The ignore value specified could not be found in the first row!");
    parse_error("I believe the real ignore value is %f",ign);
    parse_error("If you believe the ignore value specified is correct set force = 1 and re-run\n");
    return NULL;
    //return(newInt(101));
  }

  /* calling corners to find the angle */
  cns = do_corners(obj,nullo);

  /* exit if corners are on the edge of the image */
  if(cns[2]-cns[0] == x-1 || cns[6]-cns[4] == x-1 || cns[5]-cns[1] == y-1 || cns[7]-cns[3] == y-1){
    parse_error("ERROR! Cannot find the corners of the image!");
    free(cns);
    return NULL;
    //return(newInt(102));
  }

  if (trust == 0) angle = (360.0/(2.0*3.14159265))*(atan2((cns[3]-cns[1]),(cns[2]-cns[0])));
  if (trust == 1) angle = (360.0/(2.0*3.14159265))*(atan2((cns[7]-cns[5]),(cns[6]-cns[4])));
  if (trust > 0 && trust < 1) {
    angle1 = (360.0/(2.0*3.14159265))*(atan2((cns[3]-cns[1]),(cns[2]-cns[0])));
    angle2 = (360.0/(2.0*3.14159265))*(atan2((cns[7]-cns[5]),(cns[6]-cns[4])));
    angle = ((1-trust)*angle1)+(trust*angle2);
  }

  if(angle == 0) {
    parse_error("Total y_shear angle is zero.  Cannot rectify.");
    parse_error("A possible reason is that the cube is off of the image edge.");

    angle2 = (360.0/(2.0*3.14159265))*(atan2((cns[7]-cns[5]),(cns[6]-cns[4])));
    free(cns);

    if(angle2 != 0) {
      parse_error("The angle of the bottom of the cube is %f",angle2);
      parse_error("Rerun rectify with trust=1 option");
      return NULL;
      //return(newInt(103));
    }
    return NULL;
    //return(newInt(104));
  }

  free(cns);

  /* calculating the number of rows to add to the picture to accomodate the shear */
  shift = tan((M_PI / 180.0) * angle);
  lshift = (int)(x*fabs(shift)+0.5)*(shift/fabs(shift));

  /* y dimension of sheared/rectified image */
  u = y + abs(lshift);

  /* assign memory to leftmost and rightmost arrays */
  leftmost = calloc(sizeof(int), u);
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
	nu = ni/x;
	nx = ni - nu*x;

	yiz = extract_float(obj, cpos(i, j, k, obj));

	/* set leftmost and rightmost values */
	if(yiz != nullo) {
	  if(nx < leftmost[nu]) leftmost[nu] = nx;
	  if(nx > rightmost[nu]) rightmost[nu] = nx;
	}
      }
    }
  }
	
  /**
  *** Find the maximum width of the non-ignore pixels
  *** which is equal to the width of the output image
  **/
  for (j = 0; j < u; j++) {
    w = rightmost[j] - leftmost[j] + 1;
    if(w > width) width = w;
  }

  free(rightmost);

  /**
  *** The remaining issue with rectify is that rows at the top and the
  *** bottom of the image can slope away from being rectilinear.  This
  *** is because the leftedge is computed by looking for the farthest
  *** left point in all bands.  Unfortunately the bands do not overlap
  *** perfectly and the leftedge trails away.  Simply throwing out these
  *** rows will not work either.  The leftedge must be fixed at this
  *** point before the data is extracted. This may result in a leftedge
  *** value that is negative, but that's ok.
  **/

  /* from the top of leftmost */
  /* search through leftmost from top looking for first value != x-1 */
  k = 0;    // position counter
  while(leftmost[k] == x-1){ k++; }  // position of the first real value

  /* look into the array 20 pixels and 120 pixels to calculate leftmost angle */
  j = k+20;
  /* if leftmost[j] is a dropout, keep searching */
  while(leftmost[j] == x-1 && j<u){ j++; }

  i = k+120;
  /* if leftmost[i] is a dropout, keep searching */
  while(leftmost[i] == x-1 && i<u){ i++; }

  /* reuse 'angle1' to hold the slope of the leftmost */
  angle1 = (leftmost[i] - leftmost[j])/((float)i-(float)j);

  /* now force the first 20 values to that slope */
  for(j=k; j<=k+20; j++) {
    /* now calculate slope between current point and leftmost[i] above */
    angle2 = (leftmost[i] - leftmost[j])/((float)i-(float)j);
    /* if the slopes differ by more than 5 percent force leftmost[j] to conform to good slope */
    if(fabs(1.0 - angle2/angle1) > .05) {
      leftmost[j] = leftmost[i] - (i-j)*angle1;
    }
  }

  /* now from the bottom of leftmost */
  /* search through leftmost from bottom looking for first value != x-1 */
  k = u-1;    // position counter
  while(leftmost[k] == x-1){ k--; }  // position of the first real value

  /* look into the array 20 pixels and 120 pixels to calculate leftmost angle */
  j = k-20;
  /* if leftmost[j] is a dropout, keep searching */
  while(leftmost[j] == x-1 && j>=0){ j--; }

  i = k-120;
  /* if leftmost[i] is a dropout, keep searching */
  while(leftmost[i] == x-1 && i>=0){ i--; }

  /* reuse 'angle1' to hold the slope of the leftmost */
  angle1 = (leftmost[i] - leftmost[j])/((float)i-(float)j);

  /* now force the first 20 values to that slope */
  for(j=k; j>=k-20; j--) {
    /* now calculate slope between current point and leftmost[i] above */
    angle2 = (leftmost[i] - leftmost[j])/((float)i-(float)j);
    /* if the slopes differ by more than 5 percent force leftmost[j] to conform to good slope */
    if(fabs(1.0 - angle2/angle1) > .05) {
      leftmost[j] = leftmost[i] - (i-j)*angle1;
    }
  }

  /* assign memory to sheared and slanted array */
  pic = (float *)malloc(sizeof(float)*z*u*width);
  if (pic == NULL) {
    parse_error("Memory allocation for pic failed: z=%i, u=%i, width=%i", z, u, width);
    parse_error("Probable cause: the image is too big to be rectified with this algorithm.");
    return NULL;
  }
  /* extract the data into a sheared and slanted array */
  for (j = 0; j < u; j++) {
    for (i = 0; i < width; i++) {

      /* create the new x and y values in the rectified array */
      nx = i + leftmost[j];
      /* top of image leans to the left of the bottom, i.e. angle < 0 */
      if (lshift < 0) nu = j - (int)(fabs(shift)*nx+0.5);
      /* top of image leans to the right of the bottom, i.e. angle > 0 */
      if (lshift > 0) nu = j + (int)(nx*shift+0.5) - abs(lshift);

      for (k = 0; k < z; k++) {
	/* set all values to nullv */
	pic[k*u*width + j*width + i] = nullv;

	/* if not above the data nor looking off the original array */
	/* I removed this condition (leftmost[j] != x-1 && i + leftmost[j] < x) from the following if statement */
	/* bug here: nu and nx were allowed to go upto and equal to the bounds.  */ 
	if (nu >= 0 && nu < y && nx >= 0 && nx < x && (yiz = extract_float(obj, cpos(nx,nu,k,obj))) != nullo){ 
	  pic[k*u*width + j*width + i] = yiz;
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
thm_reconstitute(vfuncptr func, Var * arg)
{
  /* replaced older version.  Makes only one copy of array. 07/27/05 - kjn */

  Var    *obj = NULL;               /* input rectify structre    */
  Var    *out = NULL;               /* output data               */
  Var    *vdata = NULL;             /* data var                  */
  Var    *vleftedge = NULL;         /* leftedge var              */
  Var    *w = NULL,*a = NULL;       /* width and angle var       */ 
  float  *new_data = NULL;          /* reconstituted float data  */
  float   angle = 0;                /* angle value               */
  float   ign = -32768;             /* null data value           */
  int     x,y,z;                    /* rectified image size      */
  int     nx = 0, ny = 0;           /* new image x and y sizes   */
  int     ni = 0, nj = 0;           /* parameterized coordinates */
  int     i,j,k;                    /* loop indices              */
  int     lshift;                   /* vert shift max shift      */
  float   shift;                    /* tan of the shift          */
  int     leftedge = 0;             /* temporary leftedge value  */

  Alist alist[2]; 
  alist[0] = make_alist("structure", ID_STRUCT, NULL, &obj);
  alist[1].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if(obj==NULL ) {
    parse_error("reconstitute() - 7/27/05");
    parse_error("Unslants and unshears an output structure from rectify()");
    parse_error("Syntax:  b = thm.reconstitute(structure)");
    parse_error("example: b = thm.reconstitute(a)\n");
    parse_error("\'structure\' is the output from rectify()");
    parse_error("The structure must contain .data, .leftedge, .angle, and .width elements.");
    parse_error("The output will have null data values set to -32768\n");
    return NULL;
  }
  
  /* get stuff from structure */
  find_struct(obj,"data",&vdata);
  find_struct(obj,"leftedge",&vleftedge);
  find_struct(obj,"angle",&a);
  find_struct(obj,"width",&w);
  nx = extract_int(w,0);
  angle = extract_float(a,0);

  /* get x,y,z */
  x=GetX(vdata);
  y=GetY(vdata);
  z=GetZ(vdata);

  /* calculate shift, lshift and new y dimension */
  shift = tan((M_PI / 180.0) * angle);
  lshift = (int)(nx*fabs(shift)+0.5)*(shift/fabs(shift));
  ny = y - abs(lshift);

  /* create new array and loop through and set all points to ignore value */
  new_data=(float *)malloc(sizeof(float)*nx*ny*z);

  for(k=0; k<z; k++) {
    for(j=0; j<ny; j++) {
      for(i=0; i<nx; i++) {
	new_data[k*nx*ny + j*nx + i] = -32768;
      }
    }
  }

  /* extract data directly into unsheared and unslanted array */
  for(j=0; j<y; j++){
    leftedge = extract_int(vleftedge,cpos(0,j,0,vleftedge));
    for(i=0; i<x; i++){

      /* parameterize the rectified coordinates */
      ni = i + leftedge;
      /* top of image leans to the left of the bottom, i.e. angle < 0 */
      if (lshift < 0) nj = j - (int)(fabs(shift)*ni + 0.5);
       /* top of image leans to the right of the bottom, i.e. angle > 0 */
      if (lshift > 0) nj = j + (int)(ni*shift + 0.5) - abs(lshift);

      for(k=0; k<z; k++){
	/* if not outside of the reconstituted cube */
	if (nj >= 0 && nj < ny && ni >= 0 && ni < nx){ 
	  new_data[k*ny*nx + nj*nx + ni] = extract_float(vdata,cpos(i,j,k,vdata));
	}
      }
    }
  }

  /* return the data */  
  out = newVal(BSQ, nx, ny, z, FLOAT, new_data);
  return(out);
}

Var *
thm_supersample(vfuncptr func, Var * arg)
{
  Var       *data = NULL;                  /* original input data */
  Var       *out = NULL;                   /* final output davinci object */
  int        x,y,z;                        /* size of data */
  int        i,j,k;                        /* loop indices */
  int        nx,ny,ni;                     /* indices */
  int        mx,my;                        /* indices */
  int        type = 0;                     /* type of supersample */
  int        factor = 3;                   /* factor of supersample */
  float     *ssdata = NULL;                /* supersampled data */

  Alist alist[4];
  alist[0] = make_alist("data", 		ID_VAL,		NULL,	&data);
  alist[1] = make_alist("type",                 INT,            NULL,   &type);
  alist[2] = make_alist("factor",               INT,            NULL, &factor);
  alist[3].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no data got passed to the function */
  if (data == NULL) {
    parse_error("\nsupersample() - 7/13/2005\n");
    parse_error("Takes any 3-d array and supersamples to N times the angular resolution.");
    parse_error("Syntax:  thm.supersample(data, type, factor)");
    parse_error("Example: thm.supersample(data=a, type=1, factor=5)");
    parse_error("Example: thm.supersample(a, 2)\n");
    parse_error("data: any 3-d array where data is to be supersampled in the xy plane.");
    parse_error("type: supersample method to use. Default is 0.");
    parse_error("      type=0: each original pixel is placed in the center of the supersampled pixel grid.");
    parse_error("              all other pixels in the supersampled grid are filled with 0.");
    //    parse_error("      type=1: same as type 0 but all pixels are linearly interpolated from original pixels.");
    parse_error("      type=2: fills every pixel in supersampled pixel grid with original pixel value.\n");
    parse_error("factor: multiplication factor of original pixel to supersample grid. Default is 3.");
    parse_error("        so a factor=3 takes 1 pixel and makes a 3x3 pixel array.\n");
    return NULL;
  }

  /* get dimensions of the data */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);

  /* make new array */
  ssdata = (float *)calloc(sizeof(float), factor*x*factor*y*z);
	
  /* extract data into larger array for types 0 and 1 */
  if(type<2) {
    for(k=0;k<z;k++) {
      for(j=0;j<factor*y;j++) {
        ny = j/factor;
        my = j%factor;
        for(i=0;i<factor*x;i++) {
          ni = k*factor*factor*x*y + j*factor*x + i;
          nx = i/factor;
          mx = i%factor;
          if(mx == 1 && my == 1) ssdata[ni] = extract_float(data, cpos(nx,ny,k,data));
        }
      }
    }
  }

  /* fill in other pixels with interpolated pixels for type 1 */
  if(type==1) {
  }

  /* extract data into larger array for type 2 */
  if(type==2) {
    for(k=0;k<z;k++) {
      for(j=0;j<factor*y;j++) {
        ny = j/factor;
        for(i=0;i<factor*x;i++) {
          ni = k*factor*factor*x*y + j*factor*x + i;
          nx = i/factor;
          ssdata[ni] = extract_float(data, cpos(nx,ny,k,data));
        }
      }
    }
  }

  /* final output */
  out = newVal(BSQ, factor*x, factor*y, z, FLOAT, ssdata);
  return(out);
}



Var *
thm_rad2tb(vfuncptr func, Var * arg)
{
  /* updated 07/06/2005 to change "null" arguments to accept "ignore". - kjn*/
  /* updated 11/17/2007 to allow the setting of a maximum emissivity. - kjn*/
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
  float        maxem = 1.0;
  int         *blist = NULL;                   /* the integer list of bands extracted from bandlist or created */
  int          x, y, z;                        /* dimensions of the data */
  int          bx = 0;                         /* x-dimension of the bandlist */
  int          i;                              /* loop index */
  
  Alist alist[6];
  alist[0] = make_alist("rad",              ID_VAL,          NULL,   &rad);
  alist[1] = make_alist("bandlist",         ID_VAL,          NULL,   &bandlist);
  alist[2] = make_alist("ignore",           FLOAT,           NULL,   &nullval);
  alist[3] = make_alist("temp_rad_path",    ID_STRING,       NULL,   &fname);
  alist[4] = make_alist("max_emiss",        FLOAT,           NULL,   &maxem);
  alist[5].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (rad == NULL) {
    parse_error("rad2tb() - 11/17/07");
    parse_error("THEMIS radiance to THEMIS Brightness Temperature Converter\n");
    parse_error("Uses temp_rad_v4 in $DV_SCRIPT_FILES/instrument_parameters or /themis/calib as the default conversion table.");
    parse_error("Assumes 10 bands of radiance for right now.\n");
    parse_error("Syntax:  b = thm.rad2tb(rad,bandlist,ignore,temp_rad_path,max_emiss)");
    parse_error("example: b = thm.rad2tb(a)");
    parse_error("example: b = thm.rad2tb(a,1//2//3//4//5//6//7//8//9,0)");
    parse_error("example: b = thm.rad2tb(rad=a,bandlist=4//9//10,ignore=-32768,temp_rad_path=\"/themis/calib/temp_rad_v3\")");
    parse_error("rad - any float valued 3-D radiance array.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("ignore - non-data pixel value. Default is -32768 AND 0.");
    parse_error("temp_rad_path - alternate path to temp_rad lookup table.  Default is $DV_SCRIPT_FILES/instrument_parameters/temp_rad_v4.\n");
    parse_error("max_emiss - maximum allowable value for emissivity. Default is 1.0");
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
    bx = GetZ(rad);
    if(bx == 10) {
      blist = (int *)malloc(sizeof(int)*10);
      for(i=0;i<10;i++) {
	blist[i] = i+1;
      }
      bx = 10;
    } else if(bx == 3) {
      parse_error("Warning! Assuming that rad is a 3 band night time radiance image");
      parse_error("Setting bandlist to 4//9//10.  If this is incorrect, please specify the bandlist");
      blist = (int *)malloc(sizeof(int)*3);
      blist[0] = 4;
      blist[1] = 9;
      blist[2] = 10;
      bx = 3;      
    } else if(bx == 1) {
      parse_error("Warning! Assuming that rad is a 1 band night time radiance image");
      parse_error("Assuming band 9.  If this is incorrect, please specify the bandlist");
      blist = (int *)malloc(sizeof(int)*1);
      blist[0] = 9;
      bx = 1;
    } else {
      parse_error("Warning! rad contains a non-standard number of bands");
      parse_error("Please specify the bandlist");
      return(NULL);
    }      
  }

  x = GetX(rad);
  y = GetY(rad);
  z = GetZ(rad);

  /* initialize temp_rad header */
  iom_init_iheader(&h);

  /* load temp_rad_v4 table into memory */
  if (fname == NULL) {
    fname=get_temp_rad(fname);
  }
  
  fp = fopen(fname, "rb");
  if (fp == NULL) {
    printf("Can't open look up table: %s\n",fname);
    free(fname);
    return(NULL);
  }

  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);

  /* convert to brightness temperature and return */
  b_temps = rad2tb(rad, temp_rad, blist, bx, nullval, maxem);

  free(blist);
  out = newVal(BSQ, x, y, z, FLOAT, b_temps);
  return(out);
 
}


float *rad2tb(Var *radiance, Var *temp_rad, int *bandlist, int bx, float nullval, float maxem)
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
    maxem is the maximum allowable emissivity value
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

    /* extract linear radiances to temperatures array */
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
        
	/* correct for maximum emissivity designation */
	cur_val *= 1.0/maxem;
        
	if(cur_val == -32768 || cur_val == 0 || cur_val == nullval) {
          btemps[k*y*x + j*x + i] = 0;
	} else {
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
  /* updated 07/06/2005 to change "null" arguments to accept "ignore". - kjn*/
  /* updated 11/17/2007 to allow input of maximum emissivity to synthesize slopes in spectra. - kjn*/
	
  Var         *rad = NULL;                     /* the original radiance */
  Var         *bandlist = NULL;                /* list of bands to be converted to brightness temperature */
  Var         *out = NULL;                     /* end product */
  float        nullval = -32768;               /* default null value of radiance data */
  float        maxem = 1.0;                    /* maximum value for emissivity */
  char        *fname = NULL;                   /* the pointer to the filename */
  int          b1 = 3, b2 = 9;                 /* the boundary bands used to determine highest brightness temperature */
  int         *blist = NULL;                   /* the integer list of bands extracted from bandlist or created */
  int          i;                              /* loop index */
  int          bx, x, y, z;                    /* size of the original array */
  emissobj    *e_struct;                       /* emissivity structure output from themissivity */
  
  Alist alist[8];
  alist[0] = make_alist("rad", 		 ID_VAL,         NULL,   &rad);
  alist[1] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[2] = make_alist("ignore",        FLOAT,          NULL,   &nullval);
  alist[3] = make_alist("temp_rad_path", ID_STRING,      NULL,   &fname);
  alist[4] = make_alist("b1",            INT,            NULL,   &b1);
  alist[5] = make_alist("b2",            INT,            NULL,   &b2);
  alist[6] = make_alist("max_emiss",     FLOAT,          NULL,   &maxem);
  alist[7].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (rad == NULL) {
    parse_error("themis_emissivity() - THEMIS radiance to THEMIS emissivity converter\n");
    parse_error("Syntax:  b = thm.themis_emissivity(rad=VAL[,bandlist=VAL][,ignore=FLOAT][,temp_rad_path=STRING][,b1=INT][,b2=INT][,max_emiss=FLOAT])");
    parse_error("example: b = thm.themis_emissivity(a)");
    parse_error("example: b = thm.themis_emissivity(a,1//2//3//4//5//6//7//8//9,0,b1=4,b2=8)");
    parse_error("example: b = thm.themis_emissivity(rad=a,bandlist=4//9//10,ignore=-2,temp_rad_path=\"/themis/calib/temp_rad_v3\")");
    parse_error("example: b = thm.themis_emissivity(a,max_emiss=0.95)");
    parse_error("rad - any 3-D radiance array.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("ignore - non-data pixel value. Default is -32768 AND 0.");
    parse_error("temp_rad_path - alternate path to temp_rad lookup table.  Default is $DV_SCRIPT_FILES/temp_rad_v4.");
    parse_error("b1 - first band to search for maximum brightness temperature. Default is 3. ");
    parse_error("b2 - end band to search for maximum brightness temperature. Default is 9.");
    parse_error("max_emiss - maximum allowed emissivity for highest brightness temperature. Default is 1.\n");
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

  /* load temp_rad_v4 table into memory */
  fname=get_temp_rad(fname);
  
  e_struct = themissivity(rad, blist, nullval, fname, b1, b2, maxem);

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



emissobj *themissivity(Var *rad, int *blist, float nullval, char *fname, int b1, int b2, float maxem)
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
  if(b1 < 1 || b2 > z || b1 > z || b2 < 1 || (b1 > b2)){
    parse_error("Bad limits for max_b_temp. Setting b1 = 1 and b2 = %d",z);
    b1 = 1;
    b2 = z;
  }

  /* initialize temp_rad header */
  iom_init_iheader(&h);

  /* load temp_rad_v4 table into memory */
  fp = fopen(fname, "rb");
  if (fp == NULL) {
    printf("Can't open look up table: %s\n",fname);
    free(fname);
    return(NULL);
  }
	
  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);
  
  /* convert to brightness temperature and return */
  b_temps = rad2tb(rad, temp_rad, blist, z, nullval, maxem);

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
thm_emiss2rad(vfuncptr func, Var * arg)
{
  /* created 09/22/2005 - kjn*/
  /* caught segmentation fault bug when improperly called. 11/18/2007 - kjn */

  Var         *estruct = NULL;                 /* the emissivity structure                                     */
  Var         *bandlist = NULL;                /* list of bands to be converted to brightness temperature      */
  Var         *temp_rad = NULL;                /* temperature/radiance lookup file                             */
  Var         *emiss = NULL;                   /* emissivity component of the emissivity structure             */
  Var         *btemps = NULL;                  /* brightness temperature component of the emissivity structure */
  Var         *out = NULL;                     /* end product                                                  */
  char        *fname = NULL;                   /* the pointer to the filename                                  */
  FILE        *fp = NULL;                      /* pointer to file                                              */
  struct       iom_iheader h;                  /* temp_rad_v4 header structure                                 */
  float        nullval = -32768;               /* default null value of radiance data                          */
  float       *rad = NULL;                     /* radiance computed from emissivity                            */
  float       *btemp = NULL;                   /* float version of Var btemps                                  */
  int         *blist = NULL;                   /* the integer list of bands extracted from bandlist or created */
  int          x, y, z;                        /* dimensions of the data                                       */
  int          bx = 0;                         /* x-dimension of the bandlist                                  */
  int          i, j, k;                        /* loop indices                                                 */
  
  Alist alist[5];
  alist[0] = make_alist("estruct",       ID_STRUCT,      NULL,   &estruct);
  alist[1] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[2] = make_alist("ignore",        FLOAT,          NULL,   &nullval);
  alist[3] = make_alist("temp_rad_path", ID_STRING,      NULL,   &fname);
  alist[4].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (estruct == NULL) {
    parse_error("emiss2rad() - 9/23/05");
    parse_error("THEMIS Emissivity to THEMIS Radiance Converter\n");
    parse_error("Uses temp_rad_v4 in $DV_SCRIPT_FILES or /themis/calib as the default conversion table.");
    parse_error("Syntax:  b = thm.emiss2rad(emiss,bandlist,ignore,temp_rad_path)");
    parse_error("example: b = thm.emiss2rad(a)");
    parse_error("example: b = thm.emiss2rad(a,1//2//3//4//5//6//7//8//9,0)");
    parse_error("example: b = thm.emiss2rad(rad=a,bandlist=4//9//10,ignore=-32768,temp_rad_path=\"/themis/calib/temp_rad_v3\")");
    parse_error("emiss - the product of thm.themissivity containing .emiss and .maxbtemp components.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("ignore - non-data pixel value. Default is -32768 AND 0.");
    parse_error("tem_rad_path - alternate path to temp_rad lookup table.  Default is $DV_SCRIPT_FILES/temp_rad_v4.\n");
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

  /* extract structure elements */
  find_struct(estruct,"emiss",&emiss);
  find_struct(estruct,"maxbtemp",&btemps);

  x = GetX(emiss);
  y = GetY(emiss);
  z = GetZ(emiss);

  /* initialize temp_rad header */
  iom_init_iheader(&h);

  /* load temp_rad_v4 table into memory */
  fname=get_temp_rad(fname);
  
  fp = fopen(fname, "rb");
  if (fp == NULL) {
    printf("Can't open look up table: %s\n",fname);
    free(fname);
    return(NULL);
  }
  
  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);

  /* extract the brightness temperatures into a float array */
  if(btemps == NULL) {
    parse_error("The input struct did not contain a maxbtemp element");
    free(blist);
    return(NULL);
  }

  btemp = (float *)malloc(sizeof(float)*x*y);
  for(j=0;j<y;j++) {
    for(i=0;i<x;i++) {
      btemp[j*x + i] = extract_float(btemps,cpos(i,j,0,btemps));
    }
  }

  /* convert maxbtemp to radiance */
  rad = tb2rad(btemp, temp_rad, blist, bx, nullval, x, y);
  free(btemp);
  free(blist);

  /* bbrads will now be a multi-band blackbody radiance cube **
  ** it must be multiplied by the emissivity cube to produce **
  ** the measured THEMIS radiances.                          */

  /* multiply blackbody radiance by emissivities */
  for(k=0;k<z;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	rad[k*y*x + j*x + i] *= extract_float(emiss,cpos(i,j,k,emiss));
	if(rad[k*y*x + j*x + i] == 0) rad[k*y*x + j*x + i] = nullval;
      }
    }
  }

  out = newVal(BSQ, x, y, z, FLOAT, rad);
  return(out);
 
}



Var *
thm_white_noise_remove1(vfuncptr func, Var * arg)
{
  /* last updated 07/06/2005 to change "null" arguments to accept "ignore". - kjn*/

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
  
  Alist alist[8];
  alist[0] = make_alist("rad", 		 ID_VAL,         NULL,   &rad);
  alist[1] = make_alist("k_size",        INT,            NULL,   &k_size);
  alist[2] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[3] = make_alist("ignore",        FLOAT,          NULL,   &nullval);
  alist[4] = make_alist("b1",            INT,            NULL,   &b1);
  alist[5] = make_alist("b2",            INT,            NULL,   &b2);
  alist[6] = make_alist("null",          FLOAT,          NULL,   &nullval); //can be removed when all legacy programs are dead
  alist[7].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if (rad == NULL) {
    parse_error("white_noise_remove1() - 7/10/04");
    parse_error("White noise removal algorithm for THEMIS radiance cubes");
    parse_error("Converts to emissivity, smoothes and multiplies by unsmoothed brightness temperature\n");
    parse_error("Syntax:  b = thm.white_noise_remove1(rad,k_size,bandlist,ignore,b1,b2)");
    parse_error("example: b = thm.white_noise_remove1(a)");
    parse_error("example: b = thm.white_noise_remove1(a,7,1//2//3//4//5//6//7//8//9,0,b1=4,b2=8)");
    parse_error("example: b = thm.white_noise_remove1(rad=a,k_size=7,bandlist=4//9//10,ignore=-2)");
    parse_error("rad - any 3-D radiance array.");
    parse_error("k_size - the size of the smoothing kernel. Default is 5.");
    parse_error("bandlist - an ordered list of THEMIS bands in rad. Default is 1:10.");
    parse_error("ignore - non-data pixel value. Default is -32768 AND 0.");
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

  /* stuff for b1 and b2 */
  if(b2>z){
    parse_error("Bad limits for max_b_temp. Setting b1 = 1 and b2 = %d",z);
    b1 = 1;
    b2 = z;
  }

  /* initialize temp_rad header */
  iom_init_iheader(&h);
  
  /* load temp_rad_v4 table into memory */
  fname=get_temp_rad(fname);
  
  fp = fopen(fname, "rb");
  if (fp == NULL) {
    printf("Can't open look up table: %s\n",fname);
    free(fname);
    return(NULL);
  }

  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);

  /* convert to brightness temperature and return */
  b_temps = rad2tb(rad, temp_rad, blist, bx, nullval, 1.0);

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
  Var      *c_null=NULL;                 /* core_null var */
  float     core_null=-32768;            /* core null value */
  int       i, j, k;                     /* loop indices */
  int       x=0, y=0, z=0, x1=0;         /* size of the picture */
  float     tv1=0;
  float     tv2=0;                       /* temp pixel value */
  
 
  Alist alist[2];
  alist[0] = make_alist("obj",        ID_STRUCT,     NULL,   &pds);
  alist[1].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  if(pds==NULL) {
    parse_error("unscale() - 07/06/05\n");
    parse_error("\nUsed to unscale a pds from short to float data\n");
    parse_error("$1 = the pds structure\n");
    parse_error("core_null values will be set to -32768");
    parse_error("c.edwards\n");
    return NULL;
  }

  /* get structures */
  find_struct(pds,"spectral_qube",&struc);
  find_struct(struc,"data",&w_pds);
  find_struct(struc,"core_null",&c_null);
  core_null=extract_float(c_null, 0);
  find_struct(struc,"band_bin",&bin);
  find_struct(bin,"band_bin_multiplier",&multt);
  find_struct(bin,"band_bin_base",&baset);
  
  
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
  
  printf("core_null values set to -32768.0\n");
  
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
  /* last updated 07/06/2005 to change "null" arguments to accept "ignore". - kjn*/
  /* updated Fri Oct 14 10:41:19 MST 2005 to have better help section - kjn */

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

  Alist alist[5]; 
  alist[0] = make_alist("data",      ID_VAL,    NULL,  &data);
  alist[1] = make_alist("filt",      INT,       NULL,  &filt);
  alist[2] = make_alist("b10",       INT,       NULL,  &b10);
  alist[3] = make_alist("ignore",    FLOAT,     NULL,  &nullval);
  alist[4].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);
 
  if (data == NULL) {
    parse_error("white_noise_remove2() - 7/10/04");
    parse_error("White noise removal algorithm for THEMIS radiance cubes");
    parse_error("Low-pass filtered in \'relative color space\'\n");
    parse_error("Syntax:  b = thm.white_noise_remove2(data,filt,b10,ignore)");
    parse_error("example: b = thm.white_noise_remove2(a)");
    parse_error("example: b = thm.white_noise_remove2(a,filt=9,b10=3,ignore=0)");
    parse_error("data - any 3-D radiance array.");
    parse_error("filt - the size of the smoothing kernel. Default is 7.");
    parse_error("b10 - the z location of THEMIS band 10. Default is 10.");
    parse_error("ignore - non-data pixel value. Default is -32768./n");
    parse_error("NOTE: This WILL CHANGE the data and");
    parse_error("should only be used for \"pretty pictures\"");
    parse_error("Band 10 will be smoothed by convolution\n");
    parse_error("Last Modified by k.nowicki Fri Oct 14 10:41:19 MST 2005/n");
    parse_error("NOTE: A more advanced function exists.");
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




double *minimize_1d(Var *measured, float *bbody, double em_start, double *rad_start, double *rad_end, double *slopes)
{
  int          i, j, k;
  int          x, y, z;
  int          w;
  double      *em = NULL;
  double       para_rad_min = 0, para_em_min = 0;
  double       rs = 0.000005;                                
  double      *min_coords=NULL;
  double      *rad = NULL;
  double      *val = NULL;
  double       val_parabola = 0;

  /* returns a 2xNx1 float array with em and rad minimized values (in that order) */

  /* measured is the original radiance cube */
  /* bbody is the black body theoretical radiance */
  /* em_start is the starting value for the em multiplier */
  /* rad_start is the starting value for the rad offset */
  /* slope is rad/em. slope=0 is 0 em_step */
  /* assumes that there will never be a slope of infinity (rad must always change to minimize function) */

  x = GetX(measured);
  y = GetY(measured);
  z = GetZ(measured);

  em         = (double *)malloc(sizeof(double) * 3);
  min_coords = (double *)calloc(sizeof(double), z*2);
  rad        = (double *)malloc(sizeof(double) * 3);
  val        = (double *)malloc(sizeof(double) * 3);

  if(z == 10) z=9;                    // there is no need to rad_corr band 10

  /* loop through all bands in the provided themis image */
  for(k=0; k<z; k++){

    /*  reset val array */
    for(w=0; w<3; w++){ val[w] = 0; }

    /* initializing rad and em for 1-D search */
    if(slopes[k] == 0){
      for(w=0; w<3; w++){ 
	rad[w] = 0 + rs*w; 
	em[w]  = em_start;
      }
    }

    /* initializing rad and em for search through the trough */
    else{
      rad[0] = rad_start[k*2];
      rad[2] = rad_end[k*2];
      rad[1] = (0.2 + (slopes[k]*rad[0]))/slopes[k];
     
      em[0] = 0.8;
      em[1] = 1.0;
      em[2] = 1.2;
    }

    /* acquire vals for each initial search points */
    for(w=0; w<3; w++){
      for(j=0; j<y; j++){
	for(i=0; i<x; i++){
	  if(bbody[k*x*y + j*x + i] != 0) val[w] += pow((extract_float(measured,cpos(i,j,k,measured)) - (em[w]*bbody[k*x*y + j*x + i] + rad[w])), 2);
	}
      }
    }

    /* locate coordinates of minimal point in rad/val parabola */
    if(slopes[k] == 0){
      para_rad_min = rad[1]-.5*(pow(rad[1]-rad[0],2)*(val[1]-val[2]) - pow(rad[1]-rad[2],2)*(val[1]-val[0]))/((rad[1]-rad[0])*(val[1]-val[2]) - (rad[1]-rad[2])*(val[1]-val[0])); 
      para_em_min = em_start;
    }
    else{
      para_em_min = em[1]-.5*(pow(em[1]-em[0],2)*(val[1]-val[2]) - pow(em[1]-em[2],2)*(val[1]-val[0]))/((em[1]-em[0])*(val[1]-val[2]) - (em[1]-em[2])*(val[1]-val[0])); 
      para_rad_min = (para_em_min - (em[0] - slopes[k]*rad[0]))/slopes[k];
    }

    /* store minimal points to min_coords array */
    min_coords[0+k*2] = para_rad_min;
    min_coords[1+k*2] = para_em_min;

    /* calculate val at given rad/em spot */
    val_parabola = 0;
    for(j=0; j<y; j++){
      for(i=0; i<x; i++){
	if(bbody[k*y*x + j*x + i] != 0) val_parabola += pow((extract_float(measured,cpos(i,j,k,measured)) - (para_em_min*bbody[k*y*x + j*x + i] + para_rad_min)), 2);
      }
    }
  }

  /* clean up */
  free(em);
  free(rad);
  free(val);
  
  return(min_coords);
}





double *radcorrspace(Var *measured, float *bbody)
{
  int     x, y, z;                   // size of arrays
  int     i, j, k;                   // loop indices
  int     m, n;
  double  em_off = 0.8;              // em offset
  double  rad_off = -0.0001;         // rad offset
  double  em_step = 0.001;
  double  rad_step = 0.0000005;
  double *rcspace = NULL;

  x = GetX(measured);
  y = GetY(measured);
  z = GetZ(measured);

  rcspace = (double *)calloc(sizeof(double), z*400*400);

  for(k=0;k<z;k++) {                      /* looping through bands */
    parse_error("band=%d",k+1);
    for(m=0;m<400;m++) {                  /* looping through em */
      em_off = 0.8 + m*em_step;
      for(n=0;n<400;n++) {                /* looping through rad */
        rad_off = -0.0001 + n*rad_step;
        for(j=0;j<y;j++) {
          for(i=0;i<x;i++) {
            if(bbody[k*x*y + j*x + i] != 0) rcspace[k*400*400+m*400+n]+=pow((extract_float(measured,cpos(i,j,k,measured)) - (em_off*bbody[k*x*y + j*x + i] + rad_off)),2);
          }
        }
      }
    }
  }
  parse_error("x-axis is rad and y-axis is em");
  return(rcspace);
}



float *bbrw_k(float *btemp, int *bandlist, int x, int y, int z, float nullval)
{
  /* compute spectral radiance in units of W cm-2 str-1 micron-1
     see DeWitt and Incropera in Thermal Radiometry
     input temperatures in K
     z MUST be the number of bands in bandlist
     btemp is a single band of maximum brightness temperature with dimensions x*y */

  int         i, j, k;
  float      *irads = NULL;
  float      *wavelengths = NULL;
  float       c1 = 11911.0;                          // a constant in units of W cm-2 micron^4 str-1 
  float       c2 = 14387.9;                          // c constant in units of micron K
  int         wl = 0;                                // current themis band in bandlist
  float       numerator = 0.0;

  /* wavelengths of themis bands in microns */
  wavelengths = (float *)calloc(sizeof(FLOAT), 10);
  wavelengths[0] = 6.76665;
  wavelengths[1] = 6.76665;
  wavelengths[2] = 7.88883;
  wavelengths[3] = 8.51176;
  wavelengths[4] = 9.30155;
  wavelengths[5] = 10.1658;
  wavelengths[6] = 10.9904;
  wavelengths[7] = 11.7530;
  wavelengths[8] = 12.5552;
  wavelengths[9] = 14.8079;

  /* create new calculated radiance array */
  irads = (float *)calloc(sizeof(FLOAT), x*y*z);

  for(k=0; k<z; k++) {
    wl = bandlist[k] - 1;
    numerator = (c1/pow(wavelengths[wl],5));

    for(j=0; j<y; j++) {
      for(i=0; i<x; i++) {
	if(btemp[j*x + i] != nullval) {
	  irads[k*y*x + j*x + i] = numerator/(exp(c2/(wavelengths[wl] * btemp[j*x + i]))-1.0);
	}
      }
    }
  }

  free(wavelengths);
  return(irads);
}






Var *thm_radcorr(vfuncptr func, Var * arg)
{
  char       *fname = NULL;                  // the pointer to the filename
  double     *min_try1 = NULL;               // holds minimal coordinates for em 0.8
  double     *min_try2 = NULL;               // holds minimal coordinates for em 1.2
  double     *min_try3 = NULL;               // holds minimal coordinates for trough 
  double     *slopes = NULL;                 // defined as em/rads
  FILE       *fp = NULL;
  float      *btemps = NULL;
  float      *maxbtemp = NULL;               // max brightness temp of radiance returned from maxbtemp()
  float       nullval = -32768;
  float      *irad = NULL;
  int         b1=3, b2=9;                    // band limits for maxbtemp search
  int        *blist = NULL;                  // the integer list of bands extracted from bandlist or created
  int         a, b, bx; 
  int         i, j, k;
  int         x, y, z;
  int         space = 0;                     // set to anything other than 0 to return rad corr space
  struct      iom_iheader h;                 // temp_rad_v4 header structure
  Var        *rad = NULL;                    // original input data
  Var        *out = NULL;                    // output
  Var        *bandlist = NULL;               // list of bands to be converted to brightness temperature
  Var        *temp_rad = NULL;


  Alist alist[8];
  alist[0] = make_alist("radiance",      ID_VAL,         NULL,	 &rad);
  alist[1] = make_alist("bandlist",      ID_VAL,         NULL,   &bandlist);
  alist[2] = make_alist("ignore",        FLOAT,          NULL,   &nullval);
  alist[3] = make_alist("temp_rad_path", ID_STRING,      NULL,   &fname);
  alist[4] = make_alist("b1",            INT,            NULL,   &b1);
  alist[5] = make_alist("b2",            INT,            NULL,   &b2);
  alist[6] = make_alist("space",         INT,            NULL,   &space);
  alist[7].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no data got passed to the function */
  if (rad == NULL) {
    parse_error("\nradcorr() - 07/04/2005\n");
    parse_error("Produces a 2x1x10 float array containing the radiance correction and em offset values for a given array");
    parse_error("Values are minimized RMS differences of radiance from blackbody of same temperature");
    parse_error("Uses inverse parabolic interpolation for minimization algorithm\n");
    parse_error("Usage:b=thm.radcorr(radiance, bandlist, ignore, temp_rad_path, b1, b2, space)");
    parse_error("      b=thm.radcorr(a.data[x1,x2,y1,y2])\n");
    parse_error("\'radiance\' is a small (50x50x10 pixels) array selected from the entire radiance array");
    parse_error("\'bandlist\' is a list of THEMIS bands in radiance array. Default is ordered bands 1-10");
    parse_error("\'ignore\' is non-data value. Default is -32768");
    parse_error("\'temp_rad_path\' is the path and filename of the temperature vs radiance lookup table");
    parse_error("\'b1\' is the first band in the ordered list to search for the maximum brightness temperature. Default = 3");
    parse_error("\'b2\' is the last band in the ordered list to search for the maximum brightness temperature. Default = 9");
    parse_error("\'space\' is a binary flag to return a complete, bounded solution space");
    return NULL;
  }

  if (bandlist != NULL) {
    bx = GetX(bandlist);
    blist = (int *)malloc(sizeof(int)*bx);
    for(k=0;k<bx;k++) {
      blist[k] = extract_int(bandlist,cpos(k,0,0,bandlist));
    }
  }

  if (bandlist == NULL) {
    blist = (int *)malloc(sizeof(int)*10);
    for(i=0;i<10;i++) {
      blist[i] = i+1;
    }
    bx = 10;
  }

  /* get dimensions of the radiance cube */
  x = GetX(rad);
  y = GetY(rad);
  z = GetZ(rad);

  if(z != bx){
    parse_error("bandlist not same dimension as input radiance!");
    return(out);
  }

  /* initialize temp_rad header */
  iom_init_iheader(&h);
  
  /* load temp_rad_v4 table into memory */
  fname=get_temp_rad(fname);
  
  fp = fopen(fname, "rb");
  if (fp == NULL) {
    printf("Can't open look up table: %s\n",fname);
    free(fname);
    return(NULL);
  }
  
  temp_rad = dv_LoadVicar(fp, fname, &h);
  fclose(fp);
  free(fname);
  
  maxbtemp = (float *)calloc(sizeof(float), x*y);
  slopes = (double *)calloc(sizeof(double),z);
  
  /* initialize slopes' array to 0, for minimize_1d to be able to use slope array */
  for(k=0; k<z; k++){ slopes[k] = 0.0; }
  
  /* convert to brightness temperature */
  btemps = rad2tb(rad, temp_rad, blist, bx, nullval, 1.0);
  
  /* loop through btemps eliminating all pixels not containing a full set of bands*/
  for(j=0; j<y; j++) {
    for(i=0; i<x; i++) {
      bx = 1;                                          // using bx as a temporary flag value from now on
      for(k=0; k<z; k++) {
	if(btemps[k*x*y + j*x + i] == 0.0) bx = 0;
      }
      for(k=0; k<z; k++) {
	if(bx == 0) btemps[k*x*y + j*x + i] = 0;
      }
    }
  }
  
  /* find max_b_temp */
  for(k=b1-1;k<b2;k++) {
    for(j=0;j<y;j++) {
      for(i=0;i<x;i++) {
	if(btemps[k*x*y + j*x + i] > maxbtemp[j*x + i]) maxbtemp[j*x + i] = btemps[k*x*y + j*x + i];
      }
    }
  }
  
  free(btemps);
  
  /* Calculate radiance from maxbtemp same as bbrw written by Phil */
  irad = bbrw_k(maxbtemp, blist, x, y, z, 0);
  
  free(maxbtemp);
  free(blist);
  
  /* return radcorrspace to user */
  if(space != 0) {
    min_try1 = radcorrspace(rad, irad);
    
    free(slopes);
    free(irad);
    
    out = newVal(BSQ, 400, 400, z, DOUBLE, min_try1);
    return(out);
  }
  
  /* find minimal points in solution space along em = .8 and em = 1.2 */
  min_try1 = minimize_1d(rad, irad, 0.8, NULL, NULL, slopes);
  min_try2 = minimize_1d(rad, irad, 1.2, NULL, NULL, slopes);

  /* determine slope of the trough */
  for(k=0; k<z; k++){
    slopes[k] = (min_try2[k*2 + 1] - min_try1[k*2 + 1])/(min_try2[k*2] - min_try1[k*2]); 
  }

  /* find minimum along the trough */
  min_try3 = minimize_1d(rad, irad, 0.0, min_try1, min_try2, slopes);
  
  out = newVal(BSQ, 2, 1, z, DOUBLE, min_try3);

  free(irad);
  free(min_try2);
  free(min_try1);
  free(slopes);

  return(out);
}



Var *
thm_column_fill(vfuncptr func, Var * arg)
{
  Var     *col_in = NULL;                                 /* the input column */
  Var     *out;		                                  /* the output */
  float   *column = NULL;                                 /* extracted input column */
  float    ignore = -32768.0;                             /* the null data value */
  int      csize = 10;                                    /* default csize of 10 corresponding to 500 lines chunks */
  int      x=0,y=0,z=0;                                   /* dimensions of column */
  int      i,j,k;                                         /* array counters */
  float   *column_out = NULL;                             /* filled and ready to be returned column */
  float   sum = 0;                                        /* the sum of column to check if any values exist */

  Alist alist[4];
  alist[0] = make_alist("column",		ID_VAL,		NULL,	&col_in);
  alist[1] = make_alist("chunk_size",           INT,            NULL,   &csize);
  alist[2] = make_alist("ignore",               FLOAT,          NULL,   &ignore);
  alist[3].name = NULL;
  
  if (parse_args(func, arg, alist) == 0) return(NULL);
  
  if (col_in == NULL){
    parse_error("column_fill() - 7/13/05");
    parse_error("Takes a partially filled column, calculates averages over number of lines specified in \'chunk_size\'");
    parse_error("Fills in null points by linear interpolation of nearest neighbors and fills array.");
    parse_error("To be used to produce a column of radiation correction values ");
    parse_error("Syntax:  b = thm.column_fill(column,chunk_size,ignore)");
    parse_error("example: b = thm.column_fill(b,ignore=0)");
    parse_error("  \'column\'     can be any 1xNxM array");
    parse_error("  \'chunk_size\' defaulted to 10 to correspond to a 500 line chunk.");
    parse_error("  \'ignore\'     is defaulted to -32768");
    return NULL;
  }

  x = GetX(col_in);
  y = GetY(col_in);
  z = GetZ(col_in);

  if (x != 1) {
    parse_error("Error occurred while running src_col_fill()");
    parse_error("ERROR!, Not a column. Try again bozo!");
    return NULL;
  }

  /* extract col_in into column */
  column = (float *)calloc(sizeof(float),y*z);
 
  for(k=0;k<z;k++) {
    for(j=0;j<y;j++) {
      column[k*y + j] = extract_float(col_in, cpos(0,j,k,col_in));
       /* make sure there are values in column before calling d_SCF */
      if(column[k*y + j] != ignore) sum += column[k*y + j];
    }
  }

  if(sum == 0) {
    out = newVal(BSQ, x, y, z, FLOAT, column);
    return out;
  }

  /* call do_SCF() to do the work */
  column_out = column_fill(column,y,z,csize,ignore);

  free(column);

  out = newVal(BSQ, x, y, z, FLOAT, column_out);
  return out;
}



float *column_fill(float *column, int y, int z, int csize, float ignore)
{
  float   *kern = NULL;                                   /* convolve kernel */
  float   *cols = NULL;                                   /* smoothed column */
  float   *colss = NULL;                                  /* second smoothed column */
  int      j=0, k=0;                                      /* loop indices */
  int      position_before=0, position_after=0;           /* position of nearest filled points before and after the current pixel */
  float    val_before=-32768, val_after=-32768;           /* value of nearest filled points before and after the current pixel */
  int      flag_before=0, flag_after=0;                   /* flags to keep looking for nearest point */
  float    dist_before=0.0, dist_after=0.0;               /* distance to the bounding points */

  /* create smoothing kernel same size as chunk_size */
  kern = (float *)calloc(sizeof(float),csize);

  for(j=0;j<csize;j+=1){
    kern[j] = 1.0;
  }

  /* smooth column by kern using smoothy() */
  cols = convolve(column, kern, 1, y, z, 1, csize, 1, 1, ignore);

  /* linearly interpolate and fill blank spaces in the array */
  /* for each point we look before and after to look for the nearest values with which to interpolate */
  /* if we arrive at the end of the array then we use the single bounding value and fill to the edge */
  for(k=0;k<z;k++) {
    if(k!=9) {
      for(j=0;j<y;j++) {
	val_before=ignore;
	val_after=ignore;
	flag_before=0;
	flag_after=0;

	if(cols[k*y + j] == ignore) {
	  position_before = j-1;
	  position_after = j+1;
	  while(flag_before == 0 || flag_after == 0) {
	    if(position_before<0) flag_before=1;
	    if(position_after>=y) flag_after=1;
	    
	    if(flag_before==0 && cols[k*y + position_before] != ignore) {
	      val_before = cols[k*y + position_before];
	      flag_before = 1;
	    }
	    
	    if(flag_after==0 && cols[k*y + position_after] != ignore) {
	      val_after = cols[k*y + position_after];
	      flag_after = 1;
	    }
	    
	    if(flag_before==0) position_before -= 1;
	    if(flag_after==0) position_after += 1;
	  }
	  /* now we should have the position and values of the nearest filled points */
	  
	  /* take care of the first pixel in the array */
	  if(j==0) cols[k*y + j] = val_after;
	
	  /* now for every other pixel */
	  if(val_after == ignore & j!=0) cols[k*y + j] = val_before;
	  if(val_after != ignore && j!=0) {
	    dist_before = fabs(position_before-j);
	    dist_after = fabs(position_after-j);
	    cols[k*y + j] = (dist_after*val_before + dist_before*val_after)/(dist_before+dist_after);
	  }
	}
      }
    }
  }

  /* smooth the data again */
  colss = convolve(cols, kern, 1, y, z, 1, csize, 1, 1, ignore);

  /* clean up and return data */
  free(kern);
  free(cols);

  return colss;
}







/* Static Functions */
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
	row_avg[j] += pic[j*x+i];		// <- possible problem, see comment at top
	col_avg[i] += pic[j*x+i];		// <- possible problem
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

static char *get_temp_rad(char *fname) {
  /* load temp_rad_v4 table into memory */

  if (fname == NULL) {
    const char *dv_sfiles_val = getenv(DV_SCRIPT_FILES_ENV);
    
    if (dv_sfiles_val == NULL){
      dv_sfiles_val = RAD_TEMP_DEFAULT_PATH;
      fname = calloc(1, strlen(dv_sfiles_val)+1+strlen(RAD_TEMP_LOOKUP_TBL)+1); /* 1+1 = "/"+"\0" */
      sprintf(fname,"%s/%s",dv_sfiles_val, RAD_TEMP_LOOKUP_TBL);
    } else {
      const char *dv_inst_params = DV_INST_PARAMS;
      fname = calloc(1, strlen(dv_sfiles_val)+1+strlen(dv_inst_params)+1+strlen(RAD_TEMP_LOOKUP_TBL)+1); /* 1+1 = "/"+"\0" */
      sprintf(fname,"%s/%s/%s",dv_sfiles_val, dv_inst_params, RAD_TEMP_LOOKUP_TBL);
    }
  }
  return(fname);
}


/* Static Ported Dependencies */
//static Var *do_my_convolve(Var *obj, Var *kernel, int norm, float ignore, int kernreduce); - ported to ff_convolve.c
//static Var *do_ipi(Var *coords_array, Var *values_array); - ported to ff_ipi.c 
//static float round_dp(float input, float decimal);  - ported to ff_contour.c 
//static Var *rotation(Var *obj, float angle, float ign); ported to ff_rotate.c

/* Ported Davinci Functions */
//static Var *thm_kfill(vfuncptr, Var *); - ported to ff_ifill.c 
//static Var *thm_convolve(vfuncptr, Var *); - ported to ff_convolve.c
//static Var *thm_ramp(vfuncptr, Var *); -ported to ff_ramp.c
//static Var *thm_sawtooth(vfuncptr, Var *); - ported to ff_filter.c
//static Var *thm_maxpos(vfuncptr, Var *); - ported to ff_avg.c
//static Var *thm_minpos(vfuncptr, Var *); - ported to ff_avg.c
//static Var *thm_valpos(vfuncptr, Var *); - ported to ff_avg.c
//static Var *thm_sstretch2(vfuncptr, Var *); - ported to ff_hstretch.c
//static Var *thm_ipi(vfuncptr, Var *); - ported to ff_ipi.c
//static Var *thm_interp2d(vfuncptr, Var *); - ported to ff_interp.c
//static Var *thm_contour(vfuncptr, Var *); - ported to ff_contour.c 
//static Var *thm_rotation(vfuncptr, Var *); - ported to ff_rotate.c
//static Var *thm_resample(vfuncptr, Var *); - ported to ff_resample.c 


/* Old functions */
static void position_fill(int *pos, int elem, int iter, Var *obj);
static Var *thm_minpos_v1(vfuncptr, Var *);
static Var *thm_maxpos_v1(vfuncptr, Var *);
//static Var *thm_cleandcs(vfuncptr, Var *);
static Var *thm_y_shear(vfuncptr, Var *);
static Var *thm_marsbin(vfuncptr, Var *);
static Var *thm_destripe(vfuncptr, Var *);

void position_fill(int *pos, int elem, int iter, Var *obj) {
  int x,y,z;
  int org;
  int i,j,k;

  x = GetX(obj);
  y = GetY(obj);
  z = GetZ(obj);
  i = 0;
  j = 0;
  k = 0;
  org = V_ORG(obj);

  if (org == 0) {          // bsq organization
    k = elem/(x*y);
    j = (elem - k*x*y)/x;
    i = elem - k*x*y - j*x;
  } else if (org == 1) {   // bil organization
    j = elem/(x*z);
    k = (elem - j*x*z)/x;
    i = elem - j*x*z - k*x;
  } else if (org == 2) {   // bip organization
    j = elem/(z*x);
    i = (elem - j*z*x)/z;
    k = elem - j*z*x - k*z;
  }
  pos[iter*3 + 2] = (float)(k + 1); // z position
  pos[iter*3 + 1] = (float)(j + 1); // y position
  pos[iter*3 + 0] = (float)(i + 1); // x position
}



Var*
thm_maxpos_v1(vfuncptr func, Var *arg)
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
thm_minpos_v1(vfuncptr func, Var *arg)
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



Var *thm_destripe(vfuncptr func, Var *arg){
 
  int         filt_size = 9;                               /* convolution kernel size */
  int         ignore = -32768;                             /* non-data pixel value */
  Var        *data = NULL;                                 /* the unscaled, calibrated EDR */
  Var        *out = NULL;                                  /* return structure */
  Var        *t_level = NULL;                              /* user defined threshold level for spikes */

  Alist alist[5];
  alist[0] = make_alist("data",         ID_VAL,   NULL,  &data);
  alist[1] = make_alist("ignore",       INT,      NULL,  &ignore);
  alist[2] = make_alist("filt_size",    INT,      NULL,  &filt_size);
  alist[3] = make_alist("t_level",      ID_VAL,   NULL,  &t_level);
  alist[4].name = NULL;

  /* immediately exit the function upon verification of an empty argument list */
  if(parse_args(func, arg, alist) == 0){ return(NULL);  }

  /* if no data is supplied, then display help information about the function */
  if(data == NULL){
    parse_error("\ndestripe() - 08/01/05\n");
    parse_error("Removes both vertical and horizontal stripes in calibrated EDR images.\n");
    parse_error("Syntax:  result = c_destripe(image,[ignore],[filt_size],[t_level])");
    parse_error("Example: result = c_destripe(img)");
    parse_error("         result = c_destripe(img,ignore=422)\n");
    parse_error("\'image\'     - the calibrated EDR image");
    parse_error("[ignore]    - optional ignore values.          Default = -32768");
    parse_error("[filt_size] - optional filt_size for convolve. Default = 9");
    parse_error("[t_level]   - set spike threshold level.       Default = 2*sigma");
    parse_error("NOTE: the t_level array must be of the same bandlength as the image");
    return(NULL);
  }

  if((t_level != NULL)&&(GetZ(t_level) != GetZ(data))){
    parse_error("Error! The supplied threshold array is not of the same bandlength as the supplied image.");
    return(NULL);
  }

  float     *avg = NULL;                       /* the average of the image along a particular axis */
  float     *bxcr_filt = NULL;                 /* boxcar kernel */
  float     *data_array = NULL;                /* float data array */
  float     *high_pass = NULL;                 /* the high-pass filter of the average */
  float     *new_avg = NULL;                   /* used to hold a copy of average that will be manipulated to be feed into a high-pass filter */
  float     *mean = NULL;                      /* the mean of a band */
  float     *std = NULL;                       /* standard deviation */
  float      aleft,aright;                     /* the good left/right value of a spike for spike re-adjustment */
  int        axis;                             /* the axis of destripe */
  int        count;
  int        idel, i_neighbor;                 /* location of temp position for spike smoothing */
  int        spike_width = 7;                  /* the range of a spike */
  int        x, y, z;                          /* the dimensions of the image */
  int        i, j, k;
  
  out = new_struct(3);
  
  /* retrieve the dimensions of the image that will be destriped */
  x = GetX(data);
  y = GetY(data);
  z = GetZ(data);  
  
  /* allocate memory for the array */
  data_array = (float *)malloc(sizeof(float)*x*y*z);
  
  /* extract davinci array into a float array for use */
  for(k=0; k<z; k++){
    for(j=0; j<y; j++){
      for(i=0; i<x; i++){ 
	data_array[k*x*y + j*x+ i] = extract_float(data,cpos(i,j,k,data)); 
      }
    }
  }
  
  /*   the axis loop is to go through data_array[] twice, once for the          **
  **   y-axis destripe (which finds a X*1*Z row correction) and then again for  **
  **   x-axis destripe (a 1*Y*Z column correction) on the y-destriped image     */
  
  for(axis=0; axis<=1; axis++){
    
    /* transpose the image by transposing the definition of the boundary */
    if(axis == 1){ x = GetY(data);   y = GetX(data); }
    
    /* average along the y-direction yields an array of [x,1,z] */
    avg = (float *)malloc(sizeof(float)*x*z);
    
    for(k=0; k<z; k++){
      
      /* cycling through columns (axis = 0) or rows (axis = 1) */
      for(i=0; i<x; i++){
	
	/* reset the average of that column/row */
	avg[k*x + i] = 0;
	count = 0;
	
	for(j=0; j<y; j++){
	  /* sum the non-ignore values in the column/row */
	  if(axis == 0 && data_array[k*x*y + j*x + i] != ignore){ 
	    avg[k*x + i] += data_array[k*x*y + j*x + i];
	    count++;
	  }
	  if(axis == 1 && data_array[k*y*x + i*y + j] != ignore){ 
	    avg[k*x + i] += data_array[k*y*x + i*y + j]; 
	    count++;
	  }
	}
	/* calculate the average */
	if(count != 0)  avg[k*x + i] /= (float)count;

	/* set dropout row/columns to the ignore value */
	if(count == 0)  avg[k*x + i] = ignore;
      }
    }

    /* create the boxcar convolve kernel */
    bxcr_filt = (float *)malloc(sizeof(float)*filt_size);
    for(i=0; i<filt_size; i++){ bxcr_filt[i] = 1.0; };
    
    /* although a misnomer here, high_pass is the low_pass filter of the average */
    high_pass = convolve(avg, bxcr_filt, x, 1, z, filt_size, 1, 1, 1, ignore);   
    
    /* now high_pass is a high-pass filter of average */
    for(k=0; k<z; k++){ 
      for(i=0; i<x; i++){ 
	high_pass[k*x + i] = avg[k*x + i] - high_pass[k*x + i]; 
      }
    }
    
    // **************************************************************************************************** //
    // * note: in the previous version, there was a sub-diff array to hold the array diff[3:], but this   * //
    // *       is circumvented this time by going ahead and accessing the diff array for purpose of cal-  * //
    // *       culating the standard deviation; also the first and last pixel of the diff array is not    * //
    // *       included because they are well outside the boundary of the rest of the data set            * //
    // *                                                                                                  * //
    // **************************************************************************************************** //
    
    if(t_level == NULL){
      /* allocate space for the mean of high_pass[1:x-1] */
      mean = (float *)malloc(sizeof(float)*z);
      
      for(k=0; k<z; k++){
	/* reset the mean of each band */
	mean[k] = 0;
	for(i=2; i<x; i++){ mean[k] += high_pass[k*x + i]; }
	mean[k] /= (float)(x-2);
      }
      
      /* allocate space of the same dimensions as mean */
      std = (float *)malloc(sizeof(float)*z);
      
      /* standard deviation */
      for(k=0; k<z; k++){
	std[k] = 0;
	/* top of equation */
	for(i=2; i<x; i++){ std[k] += pow(high_pass[k*x + i] - mean[k],2); }
	/* divide the top by n-1 */
	std[k] /= (float)(x-3);
	/* take square root and double to get twice sigma */
	std[k] = pow(std[k],0.5)*2.0;
      }
    }

    free(mean);
    
    /* extract values if user provides a custom threshold level */
    if(t_level != NULL){ 
      for(k=0; k<z; k++){ 
	std[k] = extract_float(t_level,cpos(0,0,k,t_level)); 
      }
    }
    
    /* everywhere in the difference array that has values below the stddev*2 value, set it to zero */
    for(k=0; k<z; k++){
      for(i=0; i<x; i++){ 
	if(fabs(high_pass[k*x + i]) < std[k]) high_pass[k*x + i] = 0; 
      } 
    }

    free(std);

    /* allocate memory for average array without stripes */
    new_avg = (float *)malloc(sizeof(float)*x*z);
    for(k=0; k<z; k++){ 
      for(i=0; i<x; i++){ 
	new_avg[k*x+i] = avg[k*x + i]; 
      }
    }
    
    /* dealing with spike values, look to the left and right up to 7 values away 
       to find values to be averaged then to replace the spike's value.           */
    for(j=0; j<z; j++){
      for(i=1; i<=x-2; i++){
	
	/* check for spikes */
	if(high_pass[j*x+i] != 0){
	  
	  /******* Searching to the left of the spike  *******/
	  /* set 'aleft' to default value in the event the following cases are not matched */
	  aleft = new_avg[j*x+i];
	  
	  /* verify that to the left of the spike, neighboring values aren't also spikes up to distance of 7 */
	  for(idel=1; idel<=spike_width; idel++){
	    /* location of a neighboring value */
	    i_neighbor = i - idel;
	    
	    /* if we run off the edge of the array, use spike value */
	    if(i_neighbor < 0){ 
	      aleft = new_avg[j*x + i];
	      break;
	    }
	    
	    /* Phil's assumption is that if a high_pass pixel is 0 then there isn't a spike there.  **
	    ** Unfortunately, zero values also occur in dropouts. So I've added the extra condition **
	    ** of new_avg not being the ignore value.                                               */
	    if(high_pass[j*x + i_neighbor] == 0 && new_avg[j*x + i_neighbor] != ignore){ 
	      aleft = new_avg[j*x + i_neighbor];
	      break;
	    }
	  }
	  
	  /******* Searching to the right of the spike  *******/
	  /* set 'aright' to default value in the even the following cases are not matched */
	  aright = new_avg[j*x+i];
	  
	  /* verify that to the right of the spike, neighboring values aren't also spikes up to distance of 7 */
	  for(idel=1; idel<=spike_width; idel++){
	    /* location of neighboring value */
	    i_neighbor = i + idel;
	    
	    /* if we run off the edge of the array, use immediate right */
	    if(i_neighbor > x-1){
	      aright = new_avg[j*x + i];
	      break;
	    }
	    
	    /* Again, I need to add the condition of non-dropout points as above */
	    if(high_pass[j*x + i_neighbor] == 0 && new_avg[j*x + i_neighbor] != ignore){
	      aright = new_avg[j*x + i_neighbor];
	      break;
	    }
	  }
	  
	  /* replace spike with the average of the left and right bounding values     **
	  ** Here, once again, I have to add the condition that neither of the points **
	  ** to the left or the right are dropouts.  If they are then the new_avg     **
	  ** should be set to ignore and the high_pass filter will overpass it.       */
	  if(aleft != ignore && aright == ignore) { new_avg[j*x+i] = aleft; }
	  else if(aleft == ignore && aright != ignore) { new_avg[j*x+i] = aright; }
	  else { new_avg[j*x + i] = (aleft+aright)/2.0; }
	}
      }
    }
    
    /* destroy high_pass to be recreated by convolve */
    free(high_pass);
    
    /* peform a high-pass filter over the average of the image */
    high_pass = convolve(new_avg, bxcr_filt, x, 1, z, filt_size, 1, 1, 1, ignore);
    
    for(k=0; k<z; k++){ 
      for(i=0; i<x; i++){ 
	high_pass[k*x + i] = avg[k*x + i] - high_pass[k*x + i]; 
      } 
    }

    free(bxcr_filt);
    free(new_avg);
    free(avg);
    
    /* subtract the high_pass from every pixel in the array */
    for(k=0; k<z; k++){
      if(axis == 0){ 
	for(j=0; j<y; j++){ 
	  for(i=0; i<x; i++){ 
	    data_array[k*x*y + j*x + i] -= high_pass[k*x + i]; 
	  } 
	} 
      }
      if(axis == 1){ 
	for(i=0; i<y; i++){ 
	  for(j=0; j<x; j++){ 
	    data_array[k*x*y + j*y + i] -= high_pass[k*x + j]; 
	  } 
	} 
      }  	  
    }
    
    /* reset the axes */
    if(axis == 1){
      x = GetX(data);   
      y = GetY(data); 
    }
    
    /* output the difference array */
    if(axis == 0){ add_struct(out, "diffrow", newVal(BSQ,x,1,z,FLOAT,high_pass)); }
    if(axis == 1){ add_struct(out, "diffcol", newVal(BSQ,1,y,z,FLOAT,high_pass)); }
  }
  
  add_struct(out, "data", newVal(BSQ,x,y,z,FLOAT,data_array));
  
  return(out);
}


Var *thm_marsbin(vfuncptr func, Var *arg){
  double      *calc_plane = NULL;                                     // holds the data_plane only mirrored reversed across both axises //
  double      *data_plane = NULL;                                     // the plane that holds all the data //
  float        ignore = 0;                                            // user define stuff //
  float        latitude,longitude;                                    // the non-tampered lat, lon ranges to determine if they are within user's bound //
  float        lower_lat, lower_lon, upper_lat, upper_lon;            // user define stuff //
  float        resolution;                                            // more user define stuff //
  int          ignore_flag = 0;                                       // trigger flag //
  int          lat_range, lon_range;                                  // the size of the data_plane //
  int          xdim, ydim;                                            // the size of the incoming txt file //
  int          i,j;  
  int         *lat = NULL;                                            // modified latitude values based on user defined boundaries //
  int         *lon = NULL;                                            // modified longitude values based on user defined boundaries //
  int         *inv_plane = NULL;                                      // holds the obs_plane only mirrored resversed across both axises //
  int         *obs_plane = NULL;                                      // keeps track of how many samples are at a given lat/lon point //
  Var         *data = NULL;                                           // the incoming data file //
  Var         *out = NULL;                                            // the outgoing 

  double *hi = NULL;

  Alist alist[8];
  alist[0] = make_alist("upper_longitude",   FLOAT,    NULL, &upper_lon);
  alist[1] = make_alist("lower_longitude",   FLOAT,    NULL, &lower_lon);
  alist[2] = make_alist("upper_latitude",    FLOAT,    NULL, &upper_lat);
  alist[3] = make_alist("lower_latitude",    FLOAT,    NULL, &lower_lat);
  alist[4] = make_alist("resolution",        FLOAT,    NULL, &resolution);
  alist[5] = make_alist("data",              ID_VAL,   NULL, &data);
  alist[6] = make_alist("ignore",            FLOAT,    NULL, &ignore);
  alist[7].name = NULL;

  // immediately exit the function upon verification of an empty argument list //
  if(parse_args(func, arg, alist) == 0){ return(NULL);  }

  if(data == NULL){
    parse_error("");
    parse_error("Function takes an array of values of which each element in the array corresponds to a");
    parse_error("unique longitude and latitude and plots each element into a generic NxN array. If");
    parse_error("multiple values correspond to a specific cell in the generic NxN array, then that");
    parse_error("value is averaged with previous values.");
    parse_error("");
    parse_error("The function outputs a davinci structure that has two components, the first being the");
    parse_error("array of averaged values, and the second being an array tallying occurences in each cell");
    parse_error("");
    parse_error("Syntax:  output=marsbin(upper longitude, lower longitude, upper latitude, lower latitude");
    parse_error("                        ,resolution, value array, [ignore])");
    parse_error("           *where: [ignore] - an optional argument to ignore certain values x");
    parse_error("Example: b=marsbin(360,0,90,-90,1,a)");
    return(NULL);
  }
  else{
    xdim = GetX(data);                                                // retrieve how many sample data column there is //
    ydim = GetY(data);                                                // retrieve how many sample there is //
    
    // allocate memory for ydim size array to hold latitude and longitude values individually //
    lat = (int *)malloc(sizeof(int)*ydim);
    lon = (int *)malloc(sizeof(int)*ydim);
    
    // translate float latitudes and longitudes into integer format //
    for(i=0; i<ydim; i++){
      lat[i] = (int)((extract_float(data,cpos(1,i,0,data)) - lower_lat)*resolution)+1;
      lon[i] = (int)((extract_float(data,cpos(0,i,0,data)) - lower_lon)*resolution)+1;
      
      if(lat[i] == 0.0){ lat[i] = -1; }
      if(lon[i] == 0.0){ lon[i] = -1; }
    }
    
    // calculate the dimension of the output image //
    lat_range = (int)((upper_lat-lower_lat)*resolution);
    lon_range = (int)((upper_lon-lower_lon)*resolution);
    
    // allocate memory for the data plane and the observation plane //
    calc_plane = (double *)malloc(sizeof(double)*lat_range*lon_range*(xdim-2));
    data_plane = (double *)malloc(sizeof(double)*lat_range*lon_range*(xdim-2));
    inv_plane  = (int *)malloc(sizeof(int)*lat_range*lon_range);
    obs_plane  = (int *)malloc(sizeof(int)*lat_range*lon_range);
    
    // clear both arrays of any memory junk values //
    for(i=0; i<lat_range*lon_range*(xdim-2); i++){ data_plane[i] = 0.0; }
    for(i=0; i<lat_range*lon_range; i++){ obs_plane[i] = 0; }
    
    // begin inputting data into the data_plane and updating the obs_plane //
    for(i=0; i<ydim; i++){
      
      // check to see if the lat & lon is within range, can't use lat[] and lon[] for they have been reformated //
      longitude = extract_float(data, cpos(0,i,0,data));
      latitude =  extract_float(data, cpos(1,i,0,data));
      
      // check to see if any of the data in a lat/lon pixel has an ignore value //
      for(j=0; j<xdim-2; j++){ if(fabs(extract_float(data, cpos((j+2),i,0,data)) - (float)(ignore)) < .005){ ignore_flag = 1; } }
      
      // check to see if the sample data is within the user defined boundary //
      if((latitude <= upper_lat)&&(latitude >= lower_lat)&&(longitude <= upper_lon)&&(longitude >= lower_lon)&&(ignore_flag != 1)){ 
	
	// update observation plane //
	obs_plane[(lat[i]-1)*lon_range + lon[i]] += 1; 
	
	// put data from file to specific location denoted by <array_x, array_y> in each plane //
	for(j=0; j<xdim-2; j++){ data_plane[(j*lat_range*lon_range)+((lat[i]-1)*lon_range)+(lon[i])] += extract_double(data,cpos((j+2),i,0,data)); }
      }
      else{ ignore_flag = 0; }                                        //this might potentially be a fatal mistake, but at this point i will leave it //
    }
    
    // divide each data plane with observation plane //
    for(i=0; i<(lat_range*lon_range); i++){
      if(obs_plane[i] != 0){
	for(j=0; j<xdim-2; j++){ data_plane[(j*lat_range*lon_range)+i] /= obs_plane[i]; }
      }
    }	
    
    // flip the data_plane and the obs_plane across both the x and y axis //
    j = lat_range*lon_range*(xdim-2);
    for(i=0; i<lat_range*lon_range*(xdim-2); i++){
      calc_plane[j] = data_plane[i];
      j--;
    }
    j = lat_range*lon_range;
    for(i=0; i<lat_range*lon_range; i++){
      inv_plane[j]  = obs_plane[i];
      j--;
    }
    
    // clean up the unused arrays //
    free(data_plane);
    free(lat);
    free(lon);
    free(obs_plane);
    
    out = new_struct(2);
    add_struct(out, "nobs", newVal(BSQ,lon_range,lat_range,1,INT,inv_plane));
    add_struct(out, "cube", newVal(BSQ,lon_range,lat_range,xdim-2,DOUBLE,calc_plane));
    
    return(out);
  }
}


Var *
thm_rampold(vfuncptr func, Var * arg)
{
  /* made more efficient and fixed several bugs Oct 14, 2005                         **
  ** added ability to speed up ramp calculation by setting a maximum # of iterations **
  ** Fri Oct 14 16:36:44 MST 2005                                                    */

  Var    *pic_1 = NULL;		 /* picture one                                   */
  Var    *pic_2 = NULL;		 /* picture two                                   */
  Var    *out = NULL;		 /* the output picture                            */
  float  *ramp = NULL; 	         /* storage location of output ramp               */
  int    *ol1 = NULL;	         /* the overlap in picture 1                      */
  int    *ol2 = NULL;	         /* the overlap in picture 2                      */
  float   nullv = 0.0;	         /* the ignore value                              */
  float   tmpval = 0.0;          /* a temporary extracted value from the pictures */
  int     r1 = -1, r2 = -1;      /* beginning and ending rows of the overlap      */
  int     c1 = -1, c2 = -1;      /* beginning and ending columns of the overlap   */
  int     i, j, k;	         /* loop indices                                  */
  int     x, y;		         /* dimensions of the input pictures              */
  int     t1;  			 /* memory location                               */
  int     ct = 1;		 /* counter                                       */
  int     n=-1, num = 1;	 /* fill-in number                                */
  int     up, down, left, right; /* some neighborhood indices                     */
  int     pare = 100000;         /* maximum # of pixels away from edge to search  */

  Alist alist[5];
  alist[0] = make_alist("pic1",   ID_VAL, NULL, &pic_1);
  alist[1] = make_alist("pic2",   ID_VAL, NULL, &pic_2);
  alist[2] = make_alist("stop",   INT,    NULL, &pare);
  alist[3] = make_alist("ignore", FLOAT,  NULL, &nullv);
  alist[4].name = NULL;

  if (parse_args(func, arg, alist) == 0)
    return (NULL);
  
  /* if no pictures got passed to the function */
  if (pic_1 == NULL || pic_2 == NULL) {
    parse_error("ramp() - Fri Oct 14 14:45:09 MST 2005");
    parse_error("Calculates a 0 - 1 float ramp between two overlapping pictures.");
    parse_error("You need to pass me two overlapping pictures contained in arrays\nof identical size, and an ignore value.\n");
    parse_error("Syntax:  b = thm.ramp(pic1, pic2, stop, ignore)");
    parse_error("example: b = thm.ramp(a1, a2, stop=100, ignore=-32768");
    parse_error("pic1 - may be any 2-d array - float, int, short, etc.");
    parse_error("pic2 - may be any 2-d array - float, int, short, etc.");
    parse_error("stop - maximum number of pixels away from edge to search. Default 100000");
    parse_error("ignore - the non-data pixel values. Default is 0.\n");
    parse_error("NOTES:\n");
    parse_error("You need to multiply the ramp*pic2 and (1-ramp)*pic1.");
    parse_error("The ramp is only found for OVERLAPPING regions.");
    parse_error("Non-overlapping regions from pic1 and pic2 need to be added to make a full blend.");
    parse_error("You can speed up ramp calculation by setting \'stop\'. 100 works well.");
    return NULL;
  }
  
  /* x and y dimensions of the original pictures */
  x = GetX(pic_1);
  y = GetY(pic_1);
  
  if (x != GetX(pic_2) || y != GetY(pic_2)) {
    parse_error("The two pictures need to be of the exact same dimensions.\n");
    return NULL;
  }
  
  /* create the two overlap arrays */
  ol1 = (int *) malloc(sizeof(int)*x*y);
  ol2 = (int *) malloc(sizeof(int)*x*y);
  
  /* lines and columns bounding data */
  r1 = y-1;
  r2 = 0;
  c1 = x-1;
  c2 = 0;
  
  /* extract profiles of images */
  for (j = 0; j < y ; j++) {
    for (i = 0; i < x ; i++) {
      t1 = j * x + i;
      ol1[t1] = -1;
      ol2[t1] = -1;
      
      tmpval = extract_float(pic_1, cpos(i, j, 0, pic_1));
      if(tmpval != nullv) ol1[t1] = 0;
      
      tmpval = extract_float(pic_2, cpos(i, j, 0, pic_2));
      if(tmpval != nullv) ol2[t1] = 0;
      
      /* find left and right limits of overlapping area */
      if (ol1[t1] != -1 && ol2[t1] != -1) {
	if (j < r1) { r1 = j; }
	if (j > r2) { r2 = j; }
	if (i < c1) { c1 = i; }
	if (i > c2) { c2 = i; }
      }
    }
  }
 
  /* loop through the overlap arrays filling in the appropriate value.   **
  ** On the first iteration we search for any pixels with a neighbor of  **
  ** -1 and we set that pixel to 1.  On all other iterations we look for **
  ** neighbors with values num-1. If "stop" number is specified, we stop **
  ** searching for distances and set all values to value of "pare".      */
  k=1;
  while (ct > 0) {
    ct = 0;
    
    for (j = r1; j <= r2; j++) {
      for (i = c1; i <= c2; i++) {
	t1 = j * x + i;
	
	/* neighbor pixel positions */
	up = (j-1) * x + i;
	down = (j+1) * x + i;
	left = t1 - 1;
	right = t1 + 1;
	
	/* safety against falling off the edge */
	if(j==0) up = t1;
	if(j==y-1) down = t1;
	if(i==0) left = t1;
	if(i==x-1) right = t1;
	
	if (ol1[t1] == 0
	    && ((ol1[left] == n) || (ol1[right] == n)
                || (ol1[up] == n) || (ol1[down] == n))) {
	  ol1[t1] = num;
	  ct += 1;
	}
	if (ol2[t1] == 0
	    && ((ol2[left] == n) || (ol2[right] == n)
                || (ol2[up] == n) || (ol2[down] == n))) {
	  ol2[t1] = num;
	  ct += 1;
	}
      }
    }
    num += 1;
    n = num-1;
    k+=1;
    
    /* we've searched enough. Set all remaining values to 'pare' */
    if(k==pare) {
      for (j = r1; j <= r2; j++) {
        for (i = c1; i <= c2; i++) {
          t1 = j * x + i;
          if (ol1[t1] == 0) ol1[t1] = pare;
          if (ol2[t1] == 0) ol2[t1] = pare;
        }
      }
      ct=0;
    }
  }

  /* loop through last time and create ramp */
  ramp = (float *) calloc(sizeof(float), x*y);
  
  for (j = 0; j < y; j++) {
    for (i = 0; i < x; i++) {
      t1 = j*x + i;
      if (ol1[t1] != -1 && ol2[t1] != -1) {
        ramp[t1] = (float)(ol2[t1]) / (float)(ol2[t1] + ol1[t1]);
      }
    }
  }
  
  free(ol1);
  free(ol2);
  
  out = newVal(BSQ, x, y, 1, FLOAT, ramp);
  return out;
}


/**
*** For positive angles, shift the right hand side of the image upwards.
*** When unshearing (using negative angles), trim will cut off the part
*** that gets left blank.
***
*** The only difference between trim/notrim is keeping track of the signs.
*** Keith claims it was easier to handle separately.
***
*** This routine uses direct array indexing, so will not be 64-bit compliant.
**/
Var *
thm_y_shear(vfuncptr func, Var * arg)
{
  /* last updated 07/06/2005 to change "null" arguments to accept "ignore". - kjn*/

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

  Alist alist[6];
  alist[0] = make_alist("picture",		ID_VAL,		NULL,	&pic_v);
  alist[1] = make_alist("angle",		FLOAT,		NULL,	&angle);
  alist[2] = make_alist("ignore",               FLOAT,          NULL,   &nullv);
  alist[3] = make_alist("trim",		        INT,		NULL,    &trim);
  alist[4] = make_alist("null",                 FLOAT,          NULL,   &nullv); // can be removed once all legacy programs are dead
  alist[5].name = NULL;

  if (parse_args(func, arg, alist) == 0) return(NULL);

  /* if no picture got passed to the function */
  if (pic_v == NULL){
    parse_error("y_shear() - 4/25/04");
    parse_error("It takes an input array and shears the y position of each pixel according to a shear angle in degrees.\n");
    parse_error("Syntax:  b = thm.y_shear(picture,angle,ignore,trim)");
    parse_error("example: b = thm.y_shear(picture=a,angle=8,ignore=-32768,trim=1)");
    parse_error("example: b = thm.y_shear(a,8,-32768,1)\n");
    parse_error("The shear angle may be between -80 and 80 degrees");
    parse_error("The null option fills in a specified value into all blank space");
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
