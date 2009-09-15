#include "parser.h"

#if defined(HAVE_LIBQMV) && defined(HAVE_QMV_HVECTOR_H)

char *
createThemisGhostImage(int ghostDown,
					   int ghostRight,
					   const char *msg,
					   const char *stamp_id,
					   int imageW,
					   int imageH,
					   int bytesPerPixel,
					   void *imageDataBuff,
					   void *ghostDataBuff,
					   int useTrackServer);
Var * make_band(Var *in, int band);


Var *
ff_deghost(vfuncptr func, Var * arg)
{

	Var *obj = NULL,
	    *down = NULL,
	    *right = NULL,
	    *bands = NULL,
	    *v = NULL,
	    *out = NULL;
	char *id;
	int z, y, x, nbytes, i;
	size_t xy;
	void *out_data;
	char *msg;
	char prompt[256];
	int useTrackServer = 0;

	Alist alist[7];
	alist[0] = make_alist("object",		ID_VAL,		NULL,	&obj);
	alist[1] = make_alist("bands",  	ID_VAL,	    NULL,	&bands);
	alist[2] = make_alist("down",  		ID_VAL,	    NULL,	&down);
	alist[3] = make_alist("right",  	ID_VAL,	    NULL,	&right);
	alist[4] = make_alist("id",  	    ID_STRING,	NULL,	&id);
	alist[5] = make_alist("usetrackserver", INT,    NULL,   &useTrackServer);
	alist[6].name = NULL;

	if (parse_args(func, arg, alist) == 0) return(NULL);

	if (obj == NULL) {
		parse_error("%s: No object specified\n", func->name);
		return(NULL);
	}
	if (down == NULL) {
		parse_error("%s: No downward offsets specified\n", func->name);
		return(NULL);
	}
	if (bands == NULL) {
		parse_error("%s: Bands not specified\n", func->name);
		return(NULL);
	}

	x = GetX(obj);
	y = GetY(obj);
	z = GetZ(obj);
	xy = (size_t)x*(size_t)y;
	nbytes = NBYTES(V_FORMAT(obj));
	
	if (z != V_DSIZE(down)) {
		parse_error("%s: downward offsets don't match input image size\n", func->name);
		return(NULL);
	}
	if (z != V_DSIZE(bands)) {
		parse_error("%s: band numbers don't match input image size\n", func->name);
		return(NULL);
	}


	out = newVal(BSQ, x, y, z, V_FORMAT(obj), calloc(xy*z,nbytes));

	for (i = 0 ; i < z ; i++) {
		v = make_band(obj, i);
		out_data = V_DATA(out)+cpos(0,0,i,out)*nbytes;
		if (extract_int(down,i) == 0 && (right == NULL || extract_int(right,i) == 0)) {
			memcpy(out_data, V_DATA(v), xy*nbytes);
		} else  {
			sprintf(prompt, "Band %d: ", i+1);
			msg = 
				createThemisGhostImage(extract_int(down, i),		/* down */
								(right ? extract_int(right,i) : 0), /* right */
								prompt, 						/* msg */
								id, 						/* imageID */
								x, 							/* width */
								y, 							/* height */
								nbytes, 					/* nbytes */
								V_DATA(v), 		/* imageDataBuff */
								out_data,       /* ghostDataBuff */
								useTrackServer);/* database vs trackserver */
			if (msg != NULL) {
				parse_error(msg);
				return(NULL);
			}
		}
	}
	return(out);
}

#endif

Var *
make_band(Var *in, int band)
{
	int x, y, z;
	int i, j;
	int nbytes;
	Var *out;
	size_t p1, p2;
	size_t xy;

	x = GetX(in);
	y = GetY(in);
	z = GetZ(in);
	xy = (size_t)x*(size_t)y;
	nbytes = NBYTES(V_FORMAT(in));

	out = newVal(BSQ, x, y, 1, V_FORMAT(in), calloc(xy, nbytes));

	if (V_ORG(in) == BSQ) {
		p1 = cpos(0,0,band,in);
		memcpy(V_DATA(out), (char *)V_DATA(in)+p1*nbytes, nbytes*xy);
	} else {
		for (i = 0 ; i< x ; i++) {
			for (j = 0 ; j< y ; j++) {
				p1 = cpos(x,y,band,in);
				p2 = cpos(x,y,0,out);
				memcpy((char *)V_DATA(out)+p2*nbytes, (char *)V_DATA(in)+p1*nbytes, nbytes);
			}
		}
	}
	return(out);
}
