#include "parser.h"
/**
 ** This is a simple ASCII output function
 **/

int
WriteAscii(Var *s, char *filename, int force)
{
    size_t dsize;
    int format;
    int ival;
    double dval;
    int i, j, k;
    int x, y, z;
    size_t pos;
    FILE *fp;

    if (!force && access(filename, F_OK) == 0) {
		parse_error("File %s already exists.", filename);
        return 0;
    }

	if ((fp = fopen(filename, "w")) == NULL) {
        parse_error("Unable to open file: %s\n", filename);
		return 0;
	}

	if (V_TYPE(s)==ID_STRING) {
		fwrite(V_STRING(s), strlen(V_STRING(s)), 1, fp);
		fclose(fp);
		return(1);
	} else if (V_TYPE(s)==ID_TEXT){
		char cr='\n';
		for (i=0;i<V_TEXT(s).Row;i++){
			fwrite(V_TEXT(s).text[i],sizeof(char),strlen(V_TEXT(s).text[i]),fp);
			fwrite(&cr,sizeof(char),1,fp);
			
		}
		fclose(fp);
	 	return(1);
	}	

    dsize = V_DSIZE(s);
    format = V_FORMAT(s);

	x = GetX(s);
	y = GetY(s);
	z = GetZ(s);

    for (k = 0 ; k < z ; k++) {
		if (k) fputc('\n', fp);
		for (j = 0 ; j < y ; j++) {
			for (i = 0 ; i < x ; i++) {
				if (i) fputc('\t', fp);
				pos = cpos(i, j, k, s);
				switch (format) {
				  case BYTE:
				  case SHORT:
				  case INT:
					ival = extract_int(s, pos);
					fprintf(fp, "%d", ival);
					break;
				  case FLOAT:
				  case DOUBLE:
					dval = extract_double(s, pos);
					fprintf(fp, "%.10g", dval);
					break;
				}
			}
			fputc('\n', fp);
		}
	}
    fclose(fp);
    return(1);
}
