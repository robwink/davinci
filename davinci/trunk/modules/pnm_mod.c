// pnm_module.c : Gary Rigg : January 2002
//
// Port of the PNM suite of utilities for DaVinci
// 
#include "help.h"
#include "parser.h"
#include "ff_modules.h"

// Main functions - Static so can't be extern'd
static Var *ff_pnmcut(vfuncptr f, Var *args);
static Var *ff_pnmscale(vfuncptr f, Var *args);
static Var *ff_pnmcrop(vfuncptr f, Var *args);
static Var *ff_pnmpad(vfuncptr f, Var *args);
static Var *ff_destripe(vfuncptr f, Var *args);

// Main Algorithms - None static so is reusable
Var *pnmcut(Var* obj, int iLeft, int iTop, int iWidth, int iHeight);
Var *pnmscale(Var* obj, int xsize, int ysize, float xscale, float yscale); 
Var *pnmcrop(Var *obj, int left, int right, int top, int bottom);
Var *pnmpad(Var *obj, int color, int left, int right, int top, int bottom);
Var *destripe(Var *obj, int detectors);

// Internal support functions - ARE ALL STATIC
static int  cut_convert_and_check_ranges(int *iLeft, int *iTop, 
		                         int iWidth, int iHeight, 
				         int x, int y);
static Var *scale_doit(Var* obj, int x, int y, int z, int newcols, int newrows);
static int fill_object_with_pad_color(int nx, int ny, int z, 
		                      Var* output, void* data, int color);
static int map_image(int x, int y, int z, int left, int top, 
		     Var* obj,Var* output, void* data);
static int size_object(Var *output, int nx, int ny, int z);
static double GetPixel(Var *obj, int width, int height, float orgX, float orgY, int z);
static void fix_stripe(void* data, Var* obj, 
		       int x, int k1, int k2, int k3, int detectors);

// Usage functions
static void print_cut_usage(void);
static void print_scale_usage(void);
static void print_crop_usage(void);
static void print_pad_usage(void);
static void print_destripe_usage(void);

// Initialization
static dvModuleFuncDesc exported_list[] = {
    {"cut", (void *)ff_pnmcut},
    {"scale", (void *)ff_pnmscale},
    {"crop", (void *)ff_pnmcrop},
    {"pad", (void *)ff_pnmpad}, 
    {"destripe", (void *)ff_destripe} /*,*/
};

static dvModuleInitStuff is = {
    exported_list, 5,
    NULL, 0
};

DV_MOD_EXPORT int
dv_module_init(
    const char *name,
    dvModuleInitStuff *init_stuff
    )
{
	parse_error("*******************************************************");
	parse_error("*******   pnm_mod.c initialized with %-10s   ********", name);
	parse_error("*******************************************************");

    *init_stuff = is;
    
    return 1; /* return initialization success */
}

// Tidy up
DV_MOD_EXPORT void
dv_module_fini(
    const char *name
)
{
	parse_error("*******************************************************");
	parse_error("********** pnm_mod.c finalized                ************");
	parse_error("*******************************************************");
}

/////////////////////////////////////////////////////////////////
//       SECTION ONE - MAIN FUNCTIONS CALLABLE BY USER         //
/////////////////////////////////////////////////////////////////


// CUT - Extracts a rectangle from either X,Y or X,Y,Z images.
// 
// INPUTS:  OBJECT - The image (matrix) to be cut
//          LEFT   - The leftmost column to be included in output
//                   Negative values imply from the RIGHT
//          TOP    - The topmost row to be included in the output
//                   Negative values imply from the BOTTOM
//          WIDTH  - Number of columns in the output (must be >0)
//          HEIGHT - Number of rows in the output (must be >0)
//
// RETURNS: The cut image as output.
//
static Var *
ff_pnmcut(vfuncptr f, Var *args)
{
    Var *obj   = NULL;
    int left   = 0;
    int top    = 0;
    int width  = 0;
    int height = 0;

    Alist alist[6];
    alist[0] = make_alist("object", ID_VAL, NULL, &obj);
    alist[1] = make_alist("left", INT, NULL, &left);
    alist[2] = make_alist("top", INT, NULL, &top);
    alist[3] = make_alist("width", INT, NULL, &width);
    alist[4] = make_alist("height", INT, NULL, &height);
    alist[5].name = NULL;

    // Check arguments are valid
    if (0 == parse_args(f, args, alist) || NULL == obj)
    {
	    print_cut_usage();
	    return(NULL);
    }

    // Echo to user
    printf("Left:     %d\t", left);
    printf("Top:      %d\t", top);
    printf("Width:    %d\t", width);
    printf("Height:   %d\n", height);

    return pnmcut(obj, left, top, width, height);
}

// SCALE - Scales an image according to sizes/factors passed in
//         As with CUT, does nothing on the Z axis
//
// INPUTS:  OBJECT - The image (matrix) to be scaled
//          XSIZE  - New X dimension
//          YSIZE  - New Y dimension
//          XSCALE - New X Factor
//          YSCALE - New Y Factor
//          (inputs are optional/combinations are possible - 
//           see the print_scale_usage function below for info)
//
// RETURNS: The scaled image as output
// 
static Var *
ff_pnmscale(vfuncptr f, Var *args)
{
    Var *obj = NULL;
    int xsize = MAXINT;
    int ysize = MAXINT;
    float xscale = MAXFLOAT;
    float yscale = MAXFLOAT;

    Alist alist[6];
    alist[0] = make_alist("object", ID_VAL, NULL, &obj);
    alist[1] = make_alist("xsize", INT, NULL, &xsize);
    alist[2] = make_alist("ysize", INT, NULL, &ysize);
    alist[3] = make_alist("xscale", FLOAT, NULL, &xscale);
    alist[4] = make_alist("yscale", FLOAT, NULL, &yscale);
    alist[5].name = NULL;

    // Check arguments are valid
    if (0 == parse_args(f, args, alist) || NULL == obj)
    {
	    print_scale_usage();
	    return(NULL);
    }

    // Simple argument checking
    if ( (MAXINT != xsize && MAXFLOAT != xscale) ||
		   (xscale < 0.0 || xsize < 0)   ||
         (MAXINT != ysize && MAXFLOAT != yscale) ||
	           (yscale < 0.0 || ysize < 0) )
    {
	  printf("\n\nError in input arguments.\n\n");
	  print_scale_usage();
	  return(NULL);
    }

    return pnmscale(obj, xsize, ysize, xscale, yscale); 
}

// CROP  - Remove edges that are the background color
//
// INPUTS:  OBJECT - The image (matrix) to be cropped 
//          LEFT   - 0 or 1 ==> Crop left False/True
//          RIGHT  - 0 or 1 ==> Crop right False/True
//          TOP    - 0 or 1 ==> Crop top False/True
//          BOTTOM - 0 or 1 ==> Crop bottom False/True
//
//          If no sides are specified, default is ALL
//
// RETURNS: The cropped image as output
// 
static Var *
ff_pnmcrop(vfuncptr f, Var *args)
{
    Var *obj = NULL;

    // By default crop all edges (unless options override)
    int cLeft   = 1;
    int cRight  = 1;
    int cTop    = 1;
    int cBottom = 1;

    Alist alist[6];
    alist[0] = make_alist("object", ID_VAL, NULL, &obj);
    alist[1] = make_alist("left", INT, NULL, &cLeft);
    alist[2] = make_alist("right", INT, NULL, &cRight);
    alist[3] = make_alist("top", INT, NULL, &cTop);
    alist[4] = make_alist("bottom", INT, NULL, &cBottom);
    alist[5].name = NULL;

    if (0 == parse_args(f, args, alist) || NULL == obj)
    {
        print_crop_usage();
	return(NULL);
    }

    // Check arguments
    if (cLeft > 1 || cLeft < 0 || cRight  > 1 || cRight  < 0 ||
	cTop  > 1 || cTop  < 0 || cBottom > 1 || cBottom < 0) 
    {
	    print_crop_usage();
	    return(NULL);
    }

    return(pnmcrop(obj,cLeft,cRight,cTop,cBottom));
}

// PAD - Adds borders to an image
//
// INPUTS:  OBJECT - The image (matrix) to be padded 
//          COLOR  - 0 or 1 ==> Black or White 
//          LEFT   - Size of left border (>= 0) 
//          RIGHT  - Size of right border (>= 0)
//          TOP    - Size of top border (>= 0)
//          BOTTOM - Size of top border (>= 0)
//
// RETURNS: The padded image as output
// 
static Var *
ff_pnmpad(vfuncptr f, Var *args)
{
    Var *obj = NULL;

    // Default is to pad nothing on each side
    int pLeft   = 0; 
    int pRight  = 0;
    int pTop    = 0;
    int pBottom = 0;

    // Default color is black
    int pColor = 0;

    Alist alist[7];
    alist[0] = make_alist("object", ID_VAL, NULL, &obj);
    alist[1] = make_alist("color", INT, NULL, &pColor);
    alist[2] = make_alist("left", INT, NULL, &pLeft);
    alist[3] = make_alist("right", INT, NULL, &pRight);
    alist[4] = make_alist("top", INT, NULL, &pTop);
    alist[5] = make_alist("bottom", INT, NULL, &pBottom);
    alist[6].name = NULL;

    if (0 == parse_args(f, args, alist) || NULL == obj)
    {
        print_pad_usage();
	return(NULL);
    }

    // Check arguments
    if (pLeft < 0 || pRight < 0 || pTop  < 0 || pBottom < 0 || 
        (pColor < 0 || pColor > 1) ) 
    {
	    print_crop_usage();
	    return(NULL);
    }

    return(pnmpad(obj,pColor,pLeft,pRight,pTop,pBottom));
}

// DESTRIPE - Remove sensor miscalibration lines
//
// INPUTS:  OBJECT     - The image (matrix) to be padded 
//          DETECTORS  - See print_destripe_usage below
//
// RETURNS: The destriped image
// 
static Var *
ff_destripe(vfuncptr f, Var *args)
{
    Var *obj = NULL;

    int detectors = 0;

    Alist alist[3];
    alist[0] = make_alist("object", ID_VAL, NULL, &obj);
    alist[1] = make_alist("detectors", INT, NULL, &detectors);
    alist[2].name = NULL;

    if (0 == parse_args(f, args, alist) || NULL == obj)
    {
        print_destripe_usage();
	return(NULL);
    }

    // Check arguments
    if (detectors <= 0 || obj == NULL)
    {
	    print_detectors_usage();
	    return(NULL);
    }

    return(destripe(obj,detectors));
}

/////////////////////////////////////////////////////////////////
//       SECTION TWO - MAIN ALGORITHMS CALLED INTERNALLY       //
/////////////////////////////////////////////////////////////////

Var* pnmcut(Var* obj, int iLeft, int iTop, int iWidth, int iHeight)
{
    int x = 0;
    int y = 0;
    int z = 0;
    Range r;

    // Read our Matrix
    x = GetX(obj); 
    y = GetY(obj);
    z = GetZ(obj);
    
    // SENG RULE #1 - Assume the user is stupid. ;-D
    // So, lets check that their left/top and width/height values
    // are at least compatible!
    if (!cut_convert_and_check_ranges(&iLeft, &iTop, iWidth, iHeight, x, y))
	    return (NULL);

    // Ok, all being well, we're good to go...
    // Remember, USER input is ONE based, but in code we're ZERO based
    // We're assuming NO changes on the Z plane
    // Create new object and cut...
    
    // X, Y, Z - But note ranges ONE based
    r.lo[0]   = iLeft;
    r.hi[0]   = (iLeft+iWidth)-1;
    r.step[0] = 0;
    r.lo[1]   = iTop;
    r.hi[1]   = (iTop+iHeight)-1;
    r.step[1] = 0;
    r.lo[2]   = 1;
    r.hi[2]   = z;
    r.step[2] = 0;

    return (extract_array(obj, &r));
} 

Var *pnmscale(Var* obj, int xsize, int ysize, float xscale, float yscale)
{
    int x = 0;
    int y = 0;
    int z = 0;
    int newcols = 0;
    int newrows = 0;

    float aratio = MAXFLOAT;

    // Read our Matrix
    x = GetX(obj); 
    y = GetY(obj);
    z = GetZ(obj);
    
    // Calculate output image size
    if (xscale != MAXFLOAT)
	   newcols = x * xscale + 0.999; // Rounding
    else if (xsize != MAXINT)
	   newcols = xsize;
    else
	   newcols = x; // No change

    if (yscale != MAXFLOAT)
	   newrows = y * yscale + 0.999; // Rounding
    else if (ysize != MAXINT)
	   newrows = ysize;
    else
	   newrows = y; // No change

    // In the case where only ONE dimension given, calculate the
    // other maintaining the ASPECT RATIO
    if (xsize == MAXINT && xscale == MAXFLOAT)
    {
	if (yscale != MAXFLOAT)
		aratio = yscale;
	else
		aratio = ysize/y;

	newcols = x * aratio + 0.999;
    }
    else if (ysize == MAXINT && yscale == MAXFLOAT)
    {
	if (xscale != MAXFLOAT)
		aratio = xscale;
	else
		aratio = xsize/x;

	newrows = y * aratio + 0.999;
    }

    return(scale_doit(obj,x,y,z, newcols, newrows));
}

Var *pnmcrop(Var *obj, int left, int right, int top, int bottom)
{
    // Get far edges...
    int x = GetX(obj);
    int y = GetY(obj);

    Var *topRow    = pnmcut(obj,1,1,x,1);
    Var *bottomRow = pnmcut(obj,1,y,x,1);
    Var *leftRow   = pnmcut(obj,1,1,1,y);
    Var *rightRow  = pnmcut(obj,x,1,1,y);

    // Calculate top if option set 
    if (top)
    {
	top = 1;
	while(pp_compare(topRow, pnmcut(obj,1,top,x,1)) 
			 && top < y) 
		{ top++; }

	if (top >= y) 
	    top = 1; // e.g., entirely black image	
    }
    else 
	top = 1; // No cutting of top edge (ONE based)

    // Calculate left if option set	
    if (left)
    {
	left = 1;
	while(pp_compare(leftRow, pnmcut(obj,left,1,1,y)) 
			&& left < x)
		{ left++; }
			
	if (left >= x)
		left = 1;

    }
    else
    	left = 1; // No cutting (remember, ONE based)

    // Right
    if (right)
    {
	right = x;
	while(pp_compare(rightRow, pnmcut(obj,right,1,1,y))
		      && right > 1) 
		{ right--; }

	if (right <= 1) 
		right = x;
    }
    else
    	right = x; // Go all the way to the right

    // Bottom
    if (bottom)
    { 
	bottom = y;
	while(pp_compare(bottomRow, pnmcut(obj,1,bottom,x,1))
		       	&& bottom > 1) 
		{ bottom--; }

	if (bottom <= 1)
		bottom = y;
    }
    else
    	bottom = y; // Go all the way to the bottom 

    return(pnmcut(obj, left, top, (right-left), (bottom-top)));
}

Var *pnmpad(Var *obj, int color, int left, int right, int top, int bottom)
{
    int x = 0;
    int y = 0;
    int z = 0;
    int nx = 0;
    int ny = 0;
    Var* output  = NULL;
    void* data   = NULL;

    x  = GetX(obj);
    y  = GetY(obj);
    z  = GetZ(obj);
    nx = x + (right+left);
    ny = y + (top+bottom);

    output            = newVar();
    V_TYPE(output)    = V_TYPE(obj);   
    V_DSIZE(output)   = (nx * ny * z);
    V_ORG(output)     = V_ORG(obj);
    V_FORMAT(output)  = V_FORMAT(obj);
    V_DATA(output)    = calloc(NBYTES(V_FORMAT(obj)), (nx * ny * z) ); 
    data              = calloc(NBYTES(V_FORMAT(obj)), (nx*ny*z));

    // Size according to format
    if (!size_object(output, nx, ny, z))
    	    return(NULL);

    // Phase 1: Fill new object will PAD color
    if (!fill_object_with_pad_color(nx,ny,z,output,data,color))
	    return(NULL);

    // Phase 2: Map old image onto new image
    if (!map_image(x,y,z,left,top,obj,output,data))
	    return(NULL);

    free(V_DATA(output));
    V_DATA(output) = data;

    return(output);
}

Var *destripe(Var *obj, int detectors)
{
    int i  = 0;
    int j  = 0;
    int k  = 0;
    int k1 = 0;
    int k2 = 0;
    int k3 = 0;
    int x = GetX(obj);
    int y = GetY(obj);
    int z = GetZ(obj);

    void* data = NULL;
    Var* output = newVar();

    V_TYPE(output)    = V_TYPE(obj);   
    V_DSIZE(output)   = (x * y * z);
    V_ORG(output)     = V_ORG(obj);
    V_FORMAT(output)  = V_FORMAT(obj);
    V_DATA(output)    = calloc(NBYTES(V_FORMAT(obj)), (x * y * z) ); 
    data              = calloc(NBYTES(V_FORMAT(obj)), (x * y * z) );

    if (!size_object(output, x, y, z))
    	    return(NULL);

    // Loop through all X (horizontal)
    // If we're on a detector line, go through all Y / Z.
    // For each pixel, take the average of the lines either side.
    for (i = 0; i < x; i++)
    {
	    for (j = 0; j < y; j++)
	    {
		    for (k = 0; k < z; k++)
		    {
		        k1 = cpos(i,j,k, obj);

			// Get pixels either side of target
			// If 'side' is not possible (e.g. image
			// edge) get the other side just so average
			// works out reasonable.
			if (i > 0)
				k2 = cpos((i-1),j,k,obj);
			else
				k2 = cpos((i+1),j,k,obj);

			if (i < (x-1))
				k3 = cpos((i+1),j,k,obj);
			else
				k3 = cpos((i-1),j,k,obj);
		
			fix_stripe(data,obj,i,k1,k2,k3,detectors);
       		     }
	    }
    }

    free(V_DATA(output));
    V_DATA(output) = data;

    return output;
}

/////////////////////////////////////////////////////////////////
//       SECTION THREE - INTERNAL SUPPORT ROUTINES             // 
/////////////////////////////////////////////////////////////////

int cut_convert_and_check_ranges(int *iLeft, int *iTop, int iWidth, int iHeight, int x, int y)
{
    // Convert NEGATIVES to equivalent POSITIVES
    if (*iLeft < 0)
	   *iLeft = x - (*iLeft*-1);
    if (*iTop < 0)
	   *iTop = y - (*iTop*-1);

    // Check values in RANGE.
    if (*iLeft > x || *iLeft <= 0)
    {
	    parse_error("\nBeginning Column number %d out of range (1 to %d)", *iLeft, x);
	    return 0;
    }
    if (*iTop > y || *iTop <= 0)
    {
	    parse_error("\nBeginning Row number %d out of range (1 to %d)", *iTop, y);
	    return 0;
    }

    if (iWidth > x  || iWidth <= 0)
    {
	    parse_error("Width %d is invalid (%d)", iWidth, x);
	    return 0;
    }
    if ( iHeight > y || iHeight <= 0 )
    {
	    parse_error("Height %d is invalid (%d)", iHeight, y);
	    return 0;
    }
    return 1; // Good job!
}

static Var *scale_doit(Var* obj, int x, int y, int z, int newcols, int newrows)
{
    // Ratios
    float colratio = (float)newcols/(float)x;
    float rowratio = (float)newrows/(float)y;
    float orgX     = 0.0;
    float orgY     = 0.0;
    int   k1       = 0;
    Var*  output   = NULL;
    void* data     = NULL;
    int   i,j,k    = 0;
    output         = newVar();

    V_TYPE(output)   = V_TYPE(obj);   
    V_DSIZE(output)  = (newcols * newrows * z);
    V_ORG(output)    = V_ORG(obj);
    V_FORMAT(output) = V_FORMAT(obj);
    V_DATA(output)   = calloc(NBYTES(V_FORMAT(obj)), (newcols * newrows * z));
    data             = calloc(NBYTES(V_FORMAT(obj)), (newcols * newrows * z));

    if (!size_object(output, newcols, newrows, z))
	    return(NULL);

    for (i = 0; i < newcols; i++)
    {
	    for (j = 0; j < newrows; j++)
	    {
		for (k = 0; k < z; k++)
		{
		    orgX = (float)i/colratio; // Relative pos in original
		    orgY = (float)j/rowratio; // Relative pos in original
		    k1   = cpos(i,j,k,output);  // Writing position

		    switch(V_FORMAT(output)) {
			case BYTE: 
	                  ((u_char*)data)[k1]
				  = (u_char)GetPixel(obj,x,y,orgX,orgY,k); 
			  break;
			case SHORT:
			  ((short*)data)[k1] 
				  = (short)GetPixel(obj,orgX,x,y,orgY,k);
			  break;
			case INT:
			  ((int*)data)[k1] 
				  = (int)GetPixel(obj,orgX,x,y,orgY,k);
			  break;
			case FLOAT:
		          ((float*)data)[k1] 
				  = (float)GetPixel(obj,x,y,orgX,orgY,k);
			  break;
			case DOUBLE:
		          ((double*)data)[k1] 
				  = GetPixel(obj,x,y,orgX,orgY,k);
			  break;
		 	default:
		          printf("\n\nUnsupported type for Scale\n\n");
			  return(NULL);
			  break;
		    }
    	        }
     	    }
    }
    free(V_DATA(output));
    V_DATA(output) = data;
    return(output);
}

static int fill_object_with_pad_color(int nx, int ny, int z, 
		                      Var* output, void* data, int color)
{
    int i = 0;
    int j = 0;
    int k = 0;
    int k1 = 0;

    if (1 == color) // White
	    color = 255;
    // Else color is black and zero is fine

    for (i = 0; i < nx; i++)
    {
	    for (j = 0; j < ny; j++)
	    {
		    for (k = 0; k < z; k++)
		    {
		        k1 = cpos(i,j,k, output);
			switch(V_FORMAT(output)) {
				case BYTE: 
			            ((u_char*)data)[k1] = color; 
				    break;
				case SHORT:
				    ((short*)data)[k1] = color; 
				    break;
				case INT:
				    ((int*)data)[k1] = color;
				    break;
				case FLOAT:
			            ((float*)data)[k1] = color; 
				    break;
				case DOUBLE:
			            ((double*)data)[k1] = color; 
				    break;
			 	default:
			            printf("\n\nUnsupported type for pad\n\n");
				    return(0);
				    break;
			}
		    }
            }
    }
    return 1;
}

static int map_image(int x, int y, int z, int left, int top,
		     Var* obj,Var* output, void* data)
{
    int i = 0;
    int j = 0;
    int k = 0;
    int k1 = 0;
    int k2 = 0;

    for (i = 0; i < x; i++)
    {
	    for (j = 0; j < y; j++)
	    {
		for (k = 0; k < z; k++)
		{
		    k1 = cpos(i+left, j+top, k, output);	
		    k2 = cpos(i,j,k, obj);
		    switch(V_FORMAT(obj)) {
			case BYTE: 
		            ((u_char*)data)[k1] = (u_char)extract_int(obj, k2); 
			    break;
			case SHORT:
			    ((short*)data)[k1] = (short)extract_int(obj,k2); 
			    break;
			case INT:
			    ((int*)data)[k1] = extract_int(obj,k2);
			    break;
			case FLOAT:
		            ((float*)data)[k1] = extract_float(obj,k2); 
			    break;
			case DOUBLE:
		            ((double*)data)[k1] = extract_double(obj,k2); 
			    break;
		 	default:
		            printf("\n\nUnsupported type for pad\n\n");
			    return(0);
			    break;
		    }
    	        }
     	    }
    }
    return(1);
}

static int size_object(Var *output, int nx, int ny, int z)
{
    switch(V_ORG(output)) {
	    case BSQ:
		    V_SIZE(output)[0] = nx;
		    V_SIZE(output)[1] = ny;
		    V_SIZE(output)[2] = z;
		    break;
	    case BIP:
		    V_SIZE(output)[0] = z;
		    V_SIZE(output)[1] = nx;
		    V_SIZE(output)[2] = ny;
		    break;
	    case BIL:
		    V_SIZE(output)[0] = nx;
		    V_SIZE(output)[1] = z;
		    V_SIZE(output)[2] = ny;
		    break;
    	    default:
		    printf("\n\nUnsupported format - not BSQ/BIP or BIL.\n\n");
		    return(0);
		    break;
    }
    return(1);
}

static double GetPixel(Var *obj, int width, int height, float orgX, float orgY, int z)
{
    float XPan = orgX-(int)orgX; // i.e., floating point part for
    float YPan = orgY-(int)orgY; // when pixel not in original
	
    double color    = 0.0;

    int k1 = cpos((int)orgX, (int)orgY, z, obj);

    color = extract_double(obj,k1);

    if (XPan && YPan) // Pixel non-existent on both planes
    {
	    // So, we need to do some averaging
	    if (orgX < (width-1))
	    {
		    k1 = cpos(orgX+1, orgY, z, obj);
		    color += extract_double(obj,k1); color /= 2.0;
	    }
	    if (orgY < (height-1) && orgX < (width-1))
	    {
		    k1 = cpos(orgX, orgY+1, z, obj);
		    color += extract_double(obj,k1); color /= 2.0;

		    k1 = cpos(orgX+1, orgY+1, z, obj);
		    color += extract_double(obj,k1); color /= 2.0;
	    }
	    else if (orgY < (height-1))
	    {
		    k1 = cpos(orgX,orgY+1, z, obj);
		    color += extract_double(obj,k1); color /= 2.0;
	    }
    }
    else if (XPan && orgX < width)
    {
	    k1 = cpos(orgX+1, orgY, z, obj);
	    color += extract_double(obj,k1); color /= 2.0;
    }
    else if (YPan && orgY < height)
    {
	    k1 = cpos(orgX,orgY+1,z,obj);
	    color += extract_double(obj,k1); color /= 2.0;
    }
    return color;
}

static void fix_stripe(void* data, Var* obj, 
		       int x, int k1, int k2, int k3, int detectors)
{
	// One based
	// If we're on an error (detector) line, do the whole averaging thing
	// Otherwise, just take a straight copy
	if ((x+1) % detectors == 0)
	{
		switch(V_FORMAT(obj)) {
			case BYTE: 
	       		     ((u_char*)data)[k1] = ( 
				   extract_int(obj,k2) +
				   extract_int(obj,k3) 
				                  ) / 2;
			    break;
			case SHORT:
	       		     ((short*)data)[k1] = ( 
				   extract_int(obj,k2) +
				   extract_int(obj,k3) 
				                  ) / 2;
			    break;
			case INT:
	       		     ((int*)data)[k1] = ( 
				   extract_int(obj,k2) +
				   extract_int(obj,k3) 
				                  ) / 2;
			    break;
			case FLOAT:
	       		     ((float*)data)[k1] = ( 
				   extract_float(obj,k2) +
				   extract_float(obj,k3) 
				                  ) / 2;
			    break;
			case DOUBLE:
	       		     ((double*)data)[k1] = ( 
				   extract_double(obj,k2) +
				   extract_double(obj,k3) 
				                  ) / 2;
				    break;
	 		default:
			    break;
		}
	}
	else
	{
		switch(V_FORMAT(obj)) {
			case BYTE: 
	       		     ((u_char*)data)[k1] = extract_int(obj,k1); 
			    break;
			case SHORT:
	       		     ((short*)data)[k1] = extract_int(obj,k1);
			    break;
			case INT:
	       		     ((int*)data)[k1] = extract_int(obj,k1); 
			    break;
			case FLOAT:
	       		     ((float*)data)[k1] = extract_float(obj,k1); 
			    break;
			case DOUBLE:
	       		     ((double*)data)[k1] = extract_double(obj,k1);
			    break;
	 		default:
			    break;
		}
	}
}

///////////////////////////////////////////////
//       SECTION FOUR - USAGE FUNCTIONS      // 
///////////////////////////////////////////////

void print_cut_usage(void)
{
    printf("\nUsage: cut(object, left, top, width, height) where:\n\n");
    printf("LEFT is the leftmost column (negative implies from right)\n");
    printf("TOP is the topmost row (negative implies from bottom)\n");
    printf("WIDTH is the number of columns in the output.\n");
    printf("HEIGHT is the number of rows in the output.\n\n");
}

void print_scale_usage(void)
{
    printf("\nUsage: scale(object, xsize, ysize, xscale, yscale)\n\n");
    printf("You may specify a SINGLE axis change.  For example, xsize OR\n");
    printf("xscale. The aspect ratio will be maintained.\n\n");
    printf("You may also specify BOTH axis. For example, xscale & yscale.\n\n");
    printf("SIZE values are literal (i.e., pixels) whereas SCALE values are\n");
    printf("factors.  I.e., < 1 implies SHRINK, > 1 implies GROW.\n\n");
    printf("Function uses BILINEAR FILTERING for Smoothing.\n\n");
}

void print_crop_usage(void)
{
    printf("\nUsage: crop(object, left, right, top, bottom)\n\n");
    printf("Left/Right/Top and Bottom relate to image sides.  Specify 1\n");
    printf("to crop the side, 0 to leave it alone.  Crop automatically\n");
    printf("determines the border color by simple image analysis.\n\n");
}

void print_pad_usage(void)
{
    printf("Usage: pad(object, color, left, right, top, bottom)\n\n");
    printf("Color can be zero (black) or one (white)\n");
    printf("Left/Right/Top and Bottom values dictate the size (in pixels)\n");
    printf("of the border on each side of the image.\n\n");
}

void print_destripe_usage(void)
{
    printf("Usage: destripe(object, detectors)\n\n");
    printf("Destripe removes periodic scan lines introduced by sensor\n");
    printf("mis-calibration.  This type of striping is often seen in\n");
    printf("Landsat MSS data (every 6th line) and Landsat TM data\n");
    printf("(every 16th line).  This function will calculate the mean\n");
    printf("of lines either side of the Nth line and add the standard\n");
    printf("deviation of that mean.\n\n");
    printf("The number of detectors is the periodicity of the striping\n");
    printf("(i.e. 6 for MSS and 16 for TM). The algorithm assumes\n");
    printf("VERTICAL striping - i.e., the data should be in the aquired\n");
    printf("format (horizontal strips).\n\n");
}

