#include "header.h"
#include "parser.h"
#include "io_lablib3.h"

#include <ctype.h>


void Set_Col_Var(Var **,FIELD **,LABEL * ,int *size, char **);
void ProcessGroupIntoLabel(FILE *fp,int record_bytes, Var *v, char *name);
void ProcessObjectIntoLabel(FILE *fp,int record_bytes, Var *v, char *name,unsigned char **obj_ptr, int *size);
Var * ProcessIntoLabel(FILE *fp,int record_bytes, Var *v, int depth,int *label_ptr,unsigned char **obj_ptr,int *size);


static char keyword_prefix[]="PTR_TO_";
static int keyword_prefix_length=8;

typedef struct _dataKeys
{
	char *Name; /*Name Entry in Table; Used for searching*/
	Var  *Obj;  /*Pointer to Parent Var obj to which the data will be assigned*/
	char *KeyValue; /*PTR_TO_<OBJECT> keyword entry's value*/
	char *FileName; /*Name of the file the PDS object is from, 
							could be different than the one originally given */
	int  dptr;		/*offset into file where data begins*/
}dataKey;

/*To add dataKey words for more readable types do the following 3 steps:*/

/*Step 1: Add READ function/wrapper declaration here*/
int rf_QUBE(char *, Var *,char *, int);
int rf_TABLE(char *, Var *,char *, int);
int rf_IMAGE(char *, Var *,char *, int);
int rf_HISTOGRAM_IMAGE(char *, Var *,char *, int);
int rf_HISTORY(char *, Var *,char *, int);

static dataKey	*dK;
typedef int (*PFI)(char *, Var *, char *, int);

/*Step2: Add the name of the READ function/wrapper here*/
PFI VrF[]={rf_QUBE,rf_TABLE,rf_IMAGE,rf_HISTOGRAM_IMAGE,rf_HISTORY};


/*Step 3: Add the dataKey word to this here (before the NULL!)*/
static char *keyName[]={"QUBE","TABLE","IMAGE","HISTOGRAM_IMAGE","HISTORY",NULL};
static int num_data_Keys;


/*
** We want to note the Record_Bytes.  
** If we have multiple objects in this file
** this is value which everything is aligned to.
** Therefore, if we have a qube and a table, while the table
** maybe have rows of size N, the record bytes might be of
** size M.  Therefore, any pointers in the file will
** be in units of M (ie ^TABLE = Q ==> the table is at
** address Q*M.  However, after reaching the table, we'll
** be reading it in chunks of N.  So....
*/
void
Add_Key_Offset(char *Val,int Index, int record_bytes)
{
   if (Index < 1) {
      printf("Error: Table Underflow\n");
      exit(0);
   }     
         
   if (Index > num_data_Keys) {
      printf("Error: Table Overflow\n");
      exit(0);
   }  
      
   /*Need to subtract 1 (we added one when we sent it the first time)*/
            
   Index--;

//I don't understand, but when record_bytes was N and the object Point M dptr was always N*(M-1)...oh well
	dK[Index].dptr=record_bytes*(atoi(Val)-1);
}
	
char *
fix_name(char *input_name)
{
	char *name=strdup(input_name);
	int len=strlen(name);
	int i;
	int val;

	if (len < 1)
		return("Foo!");

	i=0;
	while(i<len){
		val=(int)(name[i]) & 0xff;
		if (isupper(val)){
			val=tolower(val) & 0xff;
			name[i]=(char)val;
		}
		i++;
	}

	return(name);
}
				

void
Init_Obj_Table(void)
{
	int i=0;
	while(keyName[i]!=NULL)
		i++;

	num_data_Keys=i;

	if (dK!=NULL){
		free(dK);
	}

	dK=(dataKey *)calloc(num_data_Keys,sizeof(dataKey));

	for (i=0;i<num_data_Keys;i++){
		dK[i].Name=keyName[i];
		dK[i].Obj=NULL;
		dK[i].KeyValue=NULL;
		dK[i].dptr=0;
	}
}	

int
Match_Key_Obj(char *obj)
{
	/*Want to know if this Object is in our table*/
	
	int i;

	for (i=0;i<num_data_Keys;i++){
		if(!(strcmp(dK[i].Name,obj)))

			/*If it is, then return it's index(+1)
				By adding one, we can use 0 to indicate no match.
				Just need to remember and subtract 1 on it's return 	
				below...this should be transperent to the caller. */

			return(i+1);
	}

	return(0);
}

void
Add_Key_Word_Value(char *Val,int Index)
{
	if (Index < 1) {
		printf("Error: Table Underflow\n");
		exit(0);
	}

	if (Index > num_data_Keys) {
		printf("Error: Table Overflow\n");
		exit(0);
	}

	/*Need to subtract 1 (we added one when we sent it the first time)*/

	Index--;

	if (dK[Index].KeyValue!=NULL) {
		printf("Error: Redudant Value Entry at Index:%d\n",Index);
		exit(0);
	}

	dK[Index].KeyValue=strdup(Val);
}

void Add_Key_Obj(Var *Ob,char *fn,int Index)
{
	if (Index < 1) {
		printf("Error: Table Underflow\n");
		exit(0);
	}

	if (Index > num_data_Keys) {
		printf("Error: Table Overflow\n");
		exit(0);
	}

	/*Need to subtract 1 (we added one when we sent it the first time)*/
	Index--;

	if (dK[Index].Obj!=NULL) {
		printf("Error: Redudant Obj Entry at Index:%d\n",Index);
		exit(0);
	}

	dK[Index].Obj=Ob;
	dK[Index].FileName=strdup(fn);
}


void
Read_Object(Var *v)
{
	int i;
	for (i=0;i<num_data_Keys;i++){
		if (dK[i].Obj!=NULL && dK[i].Name!=NULL && dK[i].KeyValue!=NULL)
/*			printf("%p %s %s %s\n",dK[i].Obj,dK[i].FileName,dK[i].Name,dK[i].KeyValue);*/
			(VrF[i])(dK[i].FileName,dK[i].Obj,dK[i].KeyValue,dK[i].dptr);
	}
}

int
make_int(char *number)
{
	char *base;
	char *radix;
	int r_flag=0;
	int len;
	int i=0;
	int count=0;
	int offset;
	len=strlen(number);

	/*Looking for # which signifies a Base notation integer*/

	while (i<len) {
		if (number[i]=='#'){
			r_flag=1;
			break;
		}
		i++;
	}

	if(!(r_flag)) /*Didn't find it, regular int*/
		return(atoi(number));
	
	/*Gotta convert it!*/
	
	number[i]='\0'; /*Null it at first #*/
	radix=strdup(number);
	i++;
	offset=i; /*Start string here now*/

	while (i<len) {
		if (number[i]=='#') { /*Other one*/
			number[i]='\0'; 
			base=strdup((number+offset));
			return((int) strtol(base,NULL,atoi(radix)));
		}
		i++;
	}

	return(0); /*No 2nd #? Then it's junk*/
}
/*
void  
do_obj(OBJDESC *ob,Var *v)
{
	char *name;
	KEYWORD *kwd;
	Var *o=new_struct(0);

	if (ob->class != NULL)
		name=strdup(ob->class);

	else if((kwd = OdlFindKwd(ob, "NAME", NULL,1, ODL_THIS_OBJECT))!=NULL)
		name=strdup(kwd->name);

	else{
		parse_error("Object with no name is unusable");
		return(NULL);
	}
	add_struct(v,name,o);
}
*/
		

Var * 
do_key(OBJDESC *ob, KEYWORD* key)
{

	unsigned short kwv;
	Var *o=NULL;
	int	*i;
	float *f;

			kwv=OdlGetKwdValueType(key);
	
			switch (kwv) {
	
			case ODL_INTEGER:
				i=(int *)calloc(1,sizeof(int));
				*i=make_int(key->value);
				o=newVal(BSQ,1,1,1,INT,i);
				break;
			case ODL_REAL:
				f=(float *)calloc(1,sizeof(float));
				*f=atof(key->value);
				o=newVal(BSQ,1,1,1,FLOAT,f);
				break;
			case ODL_SYMBOL:
			case ODL_DATE:
			case ODL_DATE_TIME:
				o=newVar();
				V_TYPE(o)=ID_STRING;
				V_STRING(o)=strdup(key->value);
				break;	
			case ODL_TEXT:

            o=newVar();
            V_TYPE(o)=ID_STRING;
            V_STRING(o)=strdup(key->value);
					
				break;

			case ODL_SEQUENCE:
			case ODL_SET:
				{
					char **stuff;
					int num;
					int i;
					num=OdlGetAllKwdValuesArray(key,&stuff);
					if (num){
						o=newVar();
						V_TYPE(o)=ID_TEXT;
						V_TEXT(o).Row=num;
						V_TEXT(o).text=(char **)calloc(num,sizeof(char *));
						for (i=0;i<num;i++){
							V_TEXT(o).text[i]=strdup(stuff[i]);
						}
					}
					else {
						o=newVar();
						V_TYPE(o)=ID_STRING;
						V_STRING(o)=(char *)calloc(1,sizeof(char));
						V_STRING(o)[0]='\0';
					}
				}
				break;

			default:
					parse_error("Unknown PDS value type...Setting as string");
					o=newVar();
					V_TYPE(o)=ID_STRING;
					if (key->value!=NULL)
						V_STRING(o)=strdup(key->value);
					else {
						V_STRING(o)=(char *)calloc(1,sizeof(char));
						V_STRING(o)[0]='\0';
					}

			}	
	return (o);
}

char *
mod_name_if_necessary(char *name)
{
	char *new_name;
	if (name[0]!='^')
		return(name);

	new_name=(char *)calloc(strlen(&name[1])+
							keyword_prefix_length,sizeof(char));

	strcpy(new_name,keyword_prefix);
	strcat(new_name,&name[1]);
	return(new_name);
}

void
ProcessGroup(Var *v, KEYWORD **key,OBJDESC * ob)
{
	Var *next_var;
	Var *tmp_var;
	char *keyname;

	*key=OdlGetNextKwd(*key);

	while (*key!=NULL ) {

		if (!(strcmp("GROUP",(*key)->name))) {
			next_var=new_struct(0);
			add_struct(v,fix_name((*key)->value),next_var);
         tmp_var=newVar();
         V_TYPE(tmp_var)=ID_STRING;
         V_STRING(tmp_var)=strdup("GROUP");
			add_struct(next_var,"Object",tmp_var);	
			ProcessGroup(next_var,key,ob);
			(*key)=OdlGetNextKwd(*key);
			continue;
		}

		else if (!(strcmp("END_GROUP",(*key)->name))) 
			return;

		else {
			keyname=mod_name_if_necessary((*key)->name);
			add_struct(v,fix_name(keyname),do_key(ob,*key));
			*key=OdlGetNextKwd(*key);
		}
	}
}

void
Traverse_Tree(OBJDESC * ob,Var *v, int record_bytes)
{
	KEYWORD * key;
	int offset=strlen(keyword_prefix);
	OBJDESC *next_ob=NULL;
	unsigned short scope=ODL_CHILDREN_ONLY;
	int count=0;
	Var *next_var;
	Var *tmp_var;
	char *name;
	char *keyname;
	KEYWORD *tmp_key;
	int idx;


	/*count the number of child OBJECTS of ob */
	next_ob=OdlNextObjDesc(ob,0,&scope);
	if (next_ob!=NULL)
			count=1;
	while (next_ob != NULL){
		next_ob=OdlNextObjDesc(next_ob,0,&scope);
		if (next_ob != NULL)
			count++;
	}

	/*Reset next_ob back to the first child (if there is one)*/
	scope=ODL_CHILDREN_ONLY;
	next_ob=OdlNextObjDesc(ob,0,&scope);


	/*Iterate through keywords and objects until we run out of one or the other*/

	key=OdlGetFirstKwd(ob);

	while (key!=NULL && count) {


		/*If Key and Next_ob are from different files, keyword takes precedent*/
		if ((strcmp(key->file_name,next_ob->file_name))){
			keyname=mod_name_if_necessary(key->name);

			/*Check to see if this is a pointer*/
			if (key->is_a_pointer){/* then check to see if it's an object we want*/
				if ((idx=Match_Key_Obj(&key->name[1]))) {/* It is */
					Add_Key_Word_Value(key->value,idx);
					Add_Key_Offset(key->value,idx,record_bytes);
				}
			}

			/*Now add the name to the structure*/
			add_struct(v,fix_name(keyname),do_key(ob,key));
         key=OdlGetNextKwd(key);
      }

		/*Same filename, so check line numbers*/
		/*Keyword before object*/
		else if (key->line_number < next_ob->line_number) {
				keyname=mod_name_if_necessary(key->name);

/*
** Okay, here we break from the previous smooth transitioning
*/
				if (!(strcmp("GROUP",key->name))) { /* we have the begining of a group! */
					next_var=new_struct(0);
					add_struct(v,fix_name(key->value),next_var);
            	tmp_var=newVar();
            	V_TYPE(tmp_var)=ID_STRING;
            	V_STRING(tmp_var)=strdup("GROUP");
					add_struct(next_var,"Object",tmp_var);	
					ProcessGroup(next_var,&key,ob);
					key=OdlGetNextKwd(key);
					continue;
				}
						
			
				/*Check to see if this is a pointer*/
				if (key->is_a_pointer){/* then check to see if it's an object we want*/
					if ((idx=Match_Key_Obj(&key->name[1]))) {/* It is */
						Add_Key_Word_Value(key->value,idx);
						Add_Key_Offset(key->value,idx,record_bytes);
					}
				}

				add_struct(v,fix_name(keyname),do_key(ob,key));
				key=OdlGetNextKwd(key);
		}


		/*object before keyword*/
		else {
			name=NULL;
			tmp_key=(OdlFindKwd(next_ob, "NAME", NULL,1, ODL_THIS_OBJECT));
			if(tmp_key!=NULL)
				if(tmp_key->value!=NULL) 
					name=tmp_key->value;

			if (name==NULL)/*Still no name*/
				name=next_ob->class;

			next_var=new_struct(0);
			add_struct(v,fix_name(name),next_var);
			/*NEW: Add variable Object with value "class" right under our structure */
         tmp_var=newVar();
         V_TYPE(tmp_var)=ID_STRING;
         V_STRING(tmp_var)=strdup(next_ob->class);
			add_struct(next_var,"Object",tmp_var);	

			/*Check to see if this is an object we want*/
			if ((idx=Match_Key_Obj(next_ob->class))) {/* It is */
				Add_Key_Obj(next_var,next_ob->file_name,idx);

			}
			
			Traverse_Tree(next_ob,next_var,record_bytes);

			next_ob= OdlNextObjDesc(next_ob,0,&scope);

			count--;
		}
	}

	/*Okay, somebody is depleted; keyword or children of ob */

	if (key==NULL) {/*Only children of ob left on this level*/
		while(count){
			name=NULL;
			tmp_key=(OdlFindKwd(next_ob, "NAME", NULL,1, ODL_THIS_OBJECT));
			if(tmp_key!=NULL)
				if(tmp_key->value!=NULL) 
					name=tmp_key->value;
			
			if(name==NULL)
				name=next_ob->class;

			next_var=new_struct(0);
			add_struct(v,fix_name(name),next_var);
			/*NEW: Add variable Object with value "class" right under our structure */
         tmp_var=newVar();
         V_TYPE(tmp_var)=ID_STRING;
         V_STRING(tmp_var)=strdup(next_ob->class);
			add_struct(next_var,"Object",tmp_var);	

			/*Check to see if this is an object we want*/
			if ((idx=Match_Key_Obj(next_ob->class))) {/* It is */
				Add_Key_Obj(next_var,next_ob->file_name,idx);

			}

			Traverse_Tree(next_ob,next_var,record_bytes);
			next_ob=OdlNextObjDesc(next_ob,0,&scope);
			count--;
		}
	}

	else {/*Only keywords left*/
		while (key!=NULL){
			keyname=mod_name_if_necessary(key->name);
/*
** Okay, here we break from the previous smooth transitioning
*/
			if (!(strcmp("GROUP",key->name))) { /* we have the begining of a group! */
				next_var=new_struct(0);
				add_struct(v,fix_name(key->value),next_var);
            tmp_var=newVar();
            V_TYPE(tmp_var)=ID_STRING;
            V_STRING(tmp_var)=strdup("GROUP");
				add_struct(next_var,"Object",tmp_var);	
				ProcessGroup(next_var,&key,ob);
				key=OdlGetNextKwd(key);
				continue;
			}
						

			/*Check to see if this is a pointer*/
			if (key->is_a_pointer){/* then check to see if it's an object we want*/
				if ((idx=Match_Key_Obj(&key->name[1]))) {/* It is */
					Add_Key_Word_Value(key->value,idx);
					Add_Key_Offset(key->value,idx,record_bytes);
				}
			}

			add_struct(v,fix_name(keyname),do_key(ob,key));
			key=OdlGetNextKwd(key);
		}
	}
/*everything finished for this ob so return*/
}
char *
remangle_name(char *filename)
{
	return filename;
}

char *
correct_name(char *name)
{
	char *newname=strdup(name);
	int i=strlen(newname)-1;
	while (i>=0) {
		newname[i]=(char)toupper((int)(0xff & newname[i]));
		i--;
	}

	return(newname);
}
	
void
ProcessGroupIntoLabel(FILE *fp,int record_bytes, Var *v, char *name)
{
	Var *tmpvar;
	Var *v2;
	int i;
	int count;
	char *struct_name;
	char *tmpname;
	char *tmpname2;

	tmpname2=correct_name(name);
	fprintf(fp,"GROUP = %s\r\n", tmpname2);

   get_struct_element(v, 0, &struct_name, &tmpvar); /*Get the OBJECT Tag and temporarily NULLify it */
	tmpname=strdup(struct_name);
	*struct_name='\0';

	ProcessIntoLabel(fp,record_bytes,v, 0,NULL,NULL,NULL);

	memcpy(struct_name,tmpname,strlen(tmpname));
	free(tmpname);

	fprintf(fp,"END_GROUP = %s\r\n", tmpname2);
	free(tmpname2);
	
}

void
ProcessObjectIntoLabel(FILE *fp,int record_bytes, Var *v,char *name,unsigned char **obj_ptr, int *size)
{

	Var *tmpvar;
	int i;
	int count;
	char *struct_name;

	char *tmpname;
	char *tmpname2;
	fprintf(fp,"OBJECT = %s\r\n", name);

	if (!(strcasecmp("table",name))){ /*We're processing a table!*/
   	get_struct_element(v, 0, &struct_name, &tmpvar); /*Get the OBJECT Tag and temporarily NULLify it */
		tmpname=strdup(struct_name);
		*struct_name='\0';

		ProcessIntoLabel(fp,record_bytes,v, 0,NULL,NULL,NULL);
		memcpy(struct_name,tmpname,strlen(tmpname));
		free(tmpname);
		/*Need to make use of the data object here*/
	}

	else if ((!(strcasecmp("qube",name))) || (!(strcasecmp("image",name)))){ /*We're processing a qube*/
		count = get_struct_count(v);
		for (i=0;i<count;i++){ /*We could assume the DATA object is last, but that could lead to trouble */
	 		get_struct_element(v,i, &struct_name, &tmpvar);
			if (!(strcasecmp(struct_name,"data"))) {/*Found it!*/
				*obj_ptr=(unsigned char *)V_DATA(tmpvar); /*Now we should have a pointer to the data*/
				*size=V_SIZE(tmpvar)[0]*V_SIZE(tmpvar)[1]*V_SIZE(tmpvar)[2]*NBYTES(V_FORMAT(tmpvar));
				break;
			}
		}
		/*finish processing the object label */
			/*We send in nulls because at this point....we SHOULD NOT find any labels or objects that we want! */	

   	get_struct_element(v, 0, &struct_name, &tmpvar); /*Get the OBJECT Tag and temporarily NULLify it */
		tmpname=strdup(struct_name);
		*struct_name='\0';
	
		ProcessIntoLabel(fp,record_bytes,v, 0,NULL,NULL,NULL);

		memcpy(struct_name,tmpname,strlen(tmpname));
		free(tmpname);

	}

	else {
		ProcessIntoLabel(fp,record_bytes,v, 0,NULL,NULL,NULL);
	}

	fprintf(fp,"END_OBJECT = %s\r\n", name);
}

/*Any Var which has more than a single value must be "iterated" */
void
output_big_var(FILE *out,Var *data,char *inset,char *name)
{
	int numelements=V_SIZE(data)[0]*V_SIZE(data)[1]*V_SIZE(data)[2];
	int i;
	char 	*cp;
	short *sp;
	int	*ip;
	float	*fp;
	double *dp;
	char dmrk[10];
	
	

	fprintf(out,"%s%s = (",inset,name);
	strcpy(dmrk,", ");
	for(i=0;i<numelements;i++){
		if ((i+1) == numelements)
			strcpy(dmrk,")\r\n");

		switch(V_FORMAT(data)) {

		case BYTE:
						cp=((char *)V_DATA(data));
						fprintf(out,"%d%s",cp[i],dmrk);	
						break;

		case SHORT:
						sp=((short *)V_DATA(data));
						fprintf(out,"%d%s",sp[i],dmrk);	
						break;


		case INT:
						ip=((int *)V_DATA(data));
						fprintf(out,"%d%s",ip[i],dmrk);	
						break;


		case FLOAT:
						fp=((float *)V_DATA(data));
						fprintf(out,"%f%s",fp[i],dmrk);	
						break;


		case DOUBLE:
						dp=((double *)V_DATA(data));
						fprintf(out,"%lf%s",dp[i],dmrk);	
						break;
		}
	}
}

Var *
ProcessIntoLabel(FILE *fp,int record_bytes, Var *v, int depth, int *label_ptrs, unsigned char **obj_ptr, int *size)
{
	int i;
	/* int label_ptrs[3]; 0=FILE_RECORDS; 1=LABEL_RECORDS 2=PDS_OBJECT (ie ^table, ^qube, etc)*/
	
	int count;
	Var *data=newVar();
	Var *tmp_var=newVar();
	char *name;
	char pad[26]={0};
	char *pds_filename;
	char *inset;
	char *tmpname;

	depth++;

	if (depth > 0) {
		inset = malloc(sizeof(char) * depth + 1);
		memset(inset,0x0,depth+1);
		memset(inset,'\t',depth);
	}
	else
		inset=strdup(" ");



	memset(pad,0x20,25);


	count=get_struct_count(v);
   i=0;

	while (count > 0) {
		get_struct_element(v,i++,&name,&data);

		if (name == NULL) {
			parse_error("Found a NULL element...skipping");
			count--;
			continue;
		}
		else if (*name == '\0') {
			parse_error("Found an element with no name...skipping");
			count--;
			continue;
		}

		/* There are a set of FIXED label items we look for */

		/*Don't want the first label indented...screws up OUR readers */
		if (!(strcasecmp((name),"PDS_VERSION_ID"))){
			fprintf(fp,"PDS_VERSION_ID = \"PDS3\"\r\n");
		}


		else if (!(strcasecmp((name),"FILE_RECORDS"))){
			label_ptrs[0]=ftell(fp)+strlen("FILE_RECORDS = ");	
			fprintf(fp,"%sFILE_RECORDS = %s\r\n",inset,pad);
		}

		else if (!(strcasecmp(name,"LABEL_RECORDS"))){
         label_ptrs[1]=ftell(fp)+strlen("LABEL_RECORDS = ");
         fprintf(fp,"%sLABEL_RECORDS = %s\r\n",inset,pad);
      }

/*	
		else if (!(strcasecmp(name,"PRODUCT_ID"))){
			pds_filename=remangle_name(V_STRING(data));	
         fprintf(fp,"%sPRODUCT_ID = %s\r\n",inset,pds_filename);
      }
*/


		else if (!(strncasecmp(name,"PTR_TO_",7))){
			label_ptrs[2]=ftell(fp)+strlen((name+7))+4; /* +4 for the space and ='s*/
			tmpname=correct_name((name+7));
			fprintf(fp,"%s^%s = %s\r\n",inset,tmpname,pad);
			free(tmpname);
		}

		else if (!(strncasecmp(name,"END",3))){
			return; /*This function is used recusively from the group/object functions*/
		}	

		else if (!(strcasecmp(name,"DATA"))){
			count--;
         continue; /*Skip over the data object*/
      }

		else if (V_TYPE(data)==ID_STRUCT) {
			char *newname;
			get_struct_element(data,0,&newname,&tmp_var);
			if (strcmp("Object",(newname))) {
				parse_error("Parsing unknown structure");
				ProcessIntoLabel(fp,record_bytes,data,depth,label_ptrs, obj_ptr, size);
			}
			else {
			
				if (!(strcmp("GROUP",(V_STRING(tmp_var))))) 
					ProcessGroupIntoLabel(fp,record_bytes,data,name);

				else
					ProcessObjectIntoLabel(fp,record_bytes,data,V_STRING(tmp_var),obj_ptr,size);
			}
		}	


		else if (V_TYPE(data)==ID_TEXT) {
//			char **text;
//			int row;
			int ti;
//			text=V_TEXT(data).text;
//			row=V_TEXT(data).Row;
			tmpname=correct_name(name);
			fprintf(fp,"%s%s = (",inset,tmpname);
			free(tmpname);
			for (ti=0;ti<V_TEXT(data).Row-1;ti++)
				fprintf(fp,"%s, ",V_TEXT(data).text[ti]);
			fprintf(fp,"%s)\r\n",V_TEXT(data).text[ti]);
		}
			



		else if (V_TYPE(data) == ID_STRING) {
			tmpname=correct_name(name);
			fprintf(fp,"%s%s = %s\r\n",inset,tmpname,V_STRING(data));
			free(tmpname);

		}

		else if (V_TYPE(data) == ID_VAL) {

			if (V_SIZE(data)[0] > 1 ||
				 V_SIZE(data)[1] > 1 ||
				 V_SIZE(data)[2] > 1   ) {
					int axis_count=0;
					if (V_SIZE(data)[0] > 1 ) axis_count++;
					if (V_SIZE(data)[1] > 1 ) axis_count++;
					if (V_SIZE(data)[2] > 1 ) axis_count++;
					if (axis_count > 1) {
						parse_error("I'm not writing out anything larger than a vector (ie no planes or qubes OTHER than data)");
						count--;
						continue;
					}
					output_big_var(fp,data,inset,correct_name(name));
			}

			else {

				tmpname=correct_name(name);
				switch (V_FORMAT(data)) {

				case BYTE:
						fprintf(fp,"%s%s = %d\r\n",inset,tmpname,(0xff & V_INT(data)));
						break;

				case SHORT:
						fprintf(fp,"%s%s = %d\r\n",inset,tmpname,(0xffff & V_INT(data)));
						break;

				case INT:
						fprintf(fp,"%s%s = %d\r\n",inset,tmpname,V_INT(data));
						break;

				case FLOAT:
						fprintf(fp,"%s%s = %9.3f\r\n",inset,tmpname,V_FLOAT(data));
						break;

				case DOUBLE:
						fprintf(fp,"%s%s = %9.3lf\r\n",inset,tmpname,(*((double *)V_DATA(data))));
						break;
				}/*Switch*/

				free(tmpname);
			}/*else*/

		} /*else if*/

		else {
			parse_error("What the hell is this: %d",V_TYPE(data));
		}

		count--;
		fflush(fp);
	}
}




void
Fix_Label(FILE *fp,int record_bytes,int *label_ptr, int obj_size)
{
	int total,rem;
	char *pad;	
	int size;
	int size_in_records;

	rewind(fp);
/*
** label_ptr[0] is the #-bytes into fp where the FILE_RECORDS=
** label_ptr[1] is the #-bytes into fp where the LABEL_RECORDS=
** label_ptr[2] is the #-bytes into fp where the ^OBJECT=
** label_ptr[3] is the #-bytes into fp where the label ends.
*/

	size=label_ptr[3];
	fseek(fp,size,SEEK_SET); /*back to end!*/
	size_in_records=size/record_bytes;
	rem = (size % record_bytes);
	if (rem) { /*there is always the possibility that we're on the money!*/
		size_in_records++;
		rem = (size_in_records* record_bytes) - size;
		pad = (char *)malloc(rem);
		memset(pad,0x20 /*space*/,rem);
		fwrite(pad,sizeof(char),rem,fp); /*Okay! now our label is padded appropriately*/
		fflush(fp);
	}

	total=size_in_records+obj_size/record_bytes;
	

/*Now to go back and fix the 3 label spots*/

	rewind(fp);
	fseek(fp,label_ptr[0]+1,SEEK_SET);
	fprintf(fp,"%d",total);
	rewind(fp);
	fseek(fp,label_ptr[1]+1,SEEK_SET);
	fprintf(fp,"%d",size_in_records);
	rewind(fp);
	fseek(fp,label_ptr[2]+1,SEEK_SET);
	fprintf(fp,"%d",size_in_records+1);

}

Var *
WritePDS(vfuncptr func, Var *arg)
{
	Alist alist[4];
	Var *v=NULL;
	FILE *fp;
	int force=0;
	char output[1024];
	char *name;
	Var *data=newVar();
	int flag;
	int count;
	int i;
	char *fn;
	int record_bytes; /*		Gotta keep track of this baby!*/
	Var *result;
	int label_ptr[4];
	unsigned char *obj_ptr;
	int size;



   alist[0] = make_alist( "object", ID_STRUCT,   NULL,     &v);
   alist[1] = make_alist( "filename", ID_STRING,   NULL,     &fn);
   alist[2] = make_alist( "force", INT,   NULL,     &force);
   alist[3].name = NULL;

	if (parse_args(func, arg, alist) == 0) return(NULL);


	if (v==NULL) {
		parse_error("Can't write out an empty file...please supply an object");
		return(NULL);
	}

	if ((fp=fopen(fn,"r"))!=NULL){
		if (!(force)){
			parse_error("File already exists (use force=1 to overwite)...aborting");
			fclose(fp);
			return(NULL);
		}
		fclose(fp);
	}

	if (strstr(fn,"foo")!=NULL)
		parse_error("You're naming your file %s...how original.",fn);

	count=get_struct_count(v);
	i=0;
	while(count >= 0)  {
		get_struct_element(v,i++,&name,&data); /* check and see if this is what it should be*/
		if (!(strcasecmp("record_bytes",name))){
			record_bytes=V_INT(data); /*Contains the integer value for the size of a record*/
			break;
		}
		count--;
	}
	if (count < 0) {
		parse_error("Your object doesn't contain the necessary elements for a PDS label");
		return(NULL);
	}

	fp=fopen(fn,"w");

	ProcessIntoLabel(fp,record_bytes,v,-1,label_ptr,&obj_ptr,&size);
	fprintf(fp,"END\r\n");
	label_ptr[3]=ftell(fp);
	Fix_Label(fp,record_bytes,label_ptr,size);
	fseek(fp,0,SEEK_END);
	fwrite(obj_ptr,sizeof(char),size,fp);
	fclose(fp);
	return(result);

}

Var *
ff_load_many_pds(vfuncptr func, Var * arg)
{  
   int i;
   char *filename; 
   Var *s, *t;
   if (arg == NULL || V_TYPE(arg) != ID_TEXT) { 
        parse_error("No filenames specified to %s()", func->name);
        return (NULL);
   }
   
   s = new_struct(V_TEXT(arg).Row);    
   for (i = 0 ; i < V_TEXT(arg).Row ; i++) {    
      filename = strdup(V_TEXT(arg).text[i]);   
      t = ReadPDS(func, newString(filename));   
      if (t) add_struct(s, filename, t);
   }
   if (get_struct_count(s)) {
      return(s);
   }  else {
      return(NULL);
   }    
}
	


Var *
ReadPDS(vfuncptr func, Var *arg)
{

   Alist alist[2];
   int ac;
   Var **av;

	OBJDESC * ob;
	KEYWORD *key;
	char *err_file=NULL;
	int	obn=1;
	Var *fn;
	int record_bytes;
	char *filename;

	FILE *fp;

	Var *v = new_struct(0);

   alist[0] = make_alist( "filename", ID_UNK,   NULL,     &fn);
   alist[1].name = NULL;

	if (parse_args(func, arg, alist) == 0) return(NULL);


   if (V_TYPE(fn) == ID_TEXT) {
      return(ff_load_many_pds(func, fn));
   } else if (V_TYPE(fn) == ID_STRING) {
      filename = V_STRING(fn);
   } else {
        parse_error("Illegal argument to function %s(%s), expected STRING",
         func->name, "filename");
        return (NULL);
   }


	if((fp=fopen(filename,"r"))==NULL){
		parse_error("Can't find file: %s",filename);
		return(NULL);
	}

	fclose(fp);


	Init_Obj_Table();

	ob = (OBJDESC *)OdlParseLabelFile(filename, err_file,ODL_EXPAND_STRUCTURE, 0);
	if ((key = OdlFindKwd(ob, "RECORD_BYTES", NULL, 0, ODL_THIS_OBJECT))==NULL){
		parse_error("Hey! This doesn't look like a PDS file");
		return(NULL);
	}
	record_bytes=atoi(key->value);
	
	Traverse_Tree(ob,v,record_bytes);

	Read_Object(v);

	return(v);
}


int rf_QUBE(char *fn, Var *ob,char * val, int dptr)
{
	FILE *fp;
	Var *data=NULL;
	fp=fopen(fn,"r");
	data=(Var *)dv_LoadISISFromPDS(fp,fn,dptr);
	if (data!=NULL){
		add_struct(ob,fix_name("DATA"),data);
		fclose(fp);
		return(0);
	}

	fclose(fp);
	return(1);
}
int rf_TABLE(char *fn, Var *ob,char * val, int dptr)
{
	LABEL *label;
	Var *Data;
	char **Bufs;
	char *tmpbuf;
	int i,j;
	int fp;
	int Offset;
	FIELD **f;
	int step;
	int *size;
	int err;

	label=LoadLabel(fn);
	if (label == NULL){
		fprintf(stderr, "Unable to load label from \"%s\".\n", ((fn == NULL)?"(null)":fn));
		return 1;
	}

	f = (FIELD **) label->fields->ptr;

/*Add new structure to parent ob*/
	Data=new_struct(0);

	/*Initialize a set of buffers to read in the data*/
	
	Bufs=(char **)calloc(label->nfields,sizeof(char *));
	tmpbuf=(char *)calloc(label->reclen,sizeof(char));
	size=(int *)calloc(label->nfields,sizeof(int));
	for (j=0;j<label->nfields;j++){

		if (f[j]->dimension)
			size[j]=f[j]->size*f[j]->dimension;
		else
			size[j]=f[j]->size;

		Bufs[j]=(char *)calloc((label->nrows*size[j]),sizeof(char));
	}

	Offset=dptr;

	fp=open(fn,O_RDONLY,0);
	lseek(fp,Offset,SEEK_SET);
	for (i=0;i<label->nrows;i++){

		/*Read each row as a block*/
		if((err=read(fp,tmpbuf,label->reclen))!=label->reclen){
			fprintf(stderr,"Bad data in file:%s\n Read %d byte(s), should be %d\n",
								fn,err,label->reclen);
			return(1);
		}

		step=0;

		for(j=0;j<label->nfields;j++){

			/*Place in the approiate buffer*/
			memcpy((Bufs[j]+i*size[j]),(tmpbuf+step),size[j]);
			step+=size[j];
		}
	}
	close(fp);

	/*Set each field Var's data and parameter information*/
	Set_Col_Var(&Data,f,label,size,Bufs);

	add_struct(ob,fix_name("DATA"),Data);

	free(tmpbuf);
	free(size);
	for (j=0;j<label->nfields;j++){	
		free(Bufs[j]);
	}
	free(Bufs);
	return(0);
}
double
Scale(int size, void *ptr, FIELD *f)
{
	 unsigned char	*cp;
	 unsigned int	*ip;
	 unsigned short *sp;
	float *fp;
	double *dp;
	char num[9];

/*Set up pointer casts for our data type*/

	cp=ptr;
	ip=ptr;
	sp=ptr;
	fp=ptr;
	dp=ptr;



	switch(f->eformat) {

		case MSB_INTEGER:
      case MSB_UNSIGNED_INTEGER:
			switch(f->size) {
			case 4:
					return((double)ip[0]*f->scale+f->offset);
			case 2:
					return((double)sp[0]*f->scale+f->offset);
			case 1:
					return((double)cp[0]*f->scale+f->offset);
			}
			break;
		
		case IEEE_REAL:
			switch(f->size) {
			case 8:
					return(dp[0]*f->scale+f->offset);
			case 4:
					return((double)fp[0]*f->scale+f->offset);
			}
       break;
		case ASCII_INTEGER:
			memcpy(num,cp,f->size);
			num[f->size]='\0';
			return((double)(atoi(num))*f->scale+f->offset);
        
         break;
      case ASCII_REAL:
			memcpy(num,cp,f->size);
			num[f->size]='\0';
			return((double)(atof(num))*f->scale+f->offset);
		}
	return (0);
}

	
char *
DoScale(FIELD **f,LABEL *label,char *ob)
{
	char *Bufs;
	int rows;
	int size;
	int dim;
	void *ptr;
	
	double Val;
	int index=0;
	int i,j;


	rows=label->nrows;
	size=f[0]->size;
	dim=(f[0]->dimension ? f[0]->dimension : 1);

	Bufs=(char *)calloc(rows*8*dim,sizeof(char));
	ptr=ob;
	for(i=0;i<rows;i++){
		for (j=0;j<dim;j++){	
			Val=Scale(size,ptr,f[0]);
			memcpy((Bufs+index),&Val,8);
			index+=8;
			ptr = (unsigned char *)ptr + size;
		}
	}
	f[0]->eformat=IEEE_REAL;
	f[0]->size=8;
	free(ob);
	return(Bufs);
}

void
Set_Col_Var(Var **Data,FIELD **f,LABEL *label,int *size, char **Bufs)
{
	int j,i;
	void *data;
	char **text;
	Var *v;
	char num[9];
	int inum;
	double fnum;
	int step;
	int dim;
	

	for (j=0;j<label->nfields;j++){
		dim=(f[j]->dimension ? f[j]->dimension : 1);
		step=0;
		if (f[j]->scale)
			Bufs[j]=DoScale(&f[j],label,Bufs[j]);
		switch(f[j]->eformat) {
		case CHARACTER:
			text=(char **)calloc(label->nrows,sizeof(char *));
			for (i=0;i<label->nrows;i++){
				text[i]=(char *)calloc(size[j]+1,sizeof(char));
				memcpy(text[i],(Bufs[j]+i*size[j]),size[j]);
				text[i][size[j]]='\0';
			}
			v=newText(label->nrows,text);
			break;

		case MSB_INTEGER:
		case MSB_UNSIGNED_INTEGER:
			data=calloc(size[j]*label->nrows,sizeof(char));
			memcpy(data,Bufs[j],size[j]*label->nrows);
			switch (f[j]->size) {
				case 4:
					v=newVal(BSQ,dim,label->nrows,1,INT,data);
					break;
				case 2:
					v=newVal(BSQ,dim,label->nrows,1,SHORT,data);
					break;
				case 1:
					v=newVal(BSQ,dim,label->nrows,1,BYTE,data);
			 }
			break;

		case IEEE_REAL:
			/*Easier to make a newVal, so free current instance*/
/*			data=calloc(size[j]*label->nrows,sizeof(char)); */
			data=calloc(f[j]->size*label->nrows,sizeof(char));
/*			memcpy(data,Bufs[j],size[j]*label->nrows); */
			memcpy(data,Bufs[j],f[j]->size*label->nrows);
			switch (f[j]->size) {
				case 8:
					v=newVal(BSQ,dim,label->nrows,1,DOUBLE,data);
					break;
				case 4:
					v=newVal(BSQ,dim,label->nrows,1,FLOAT,data);
					break;
			}
			break;

		case ASCII_INTEGER:
			data=calloc(sizeof(int)*label->nrows,sizeof(char));
			for (i=0;i<(label->nrows*dim);i++){
				memcpy(num,Bufs[j]+f[j]->size*i,f[j]->size);
				num[f[j]->size]='\0';
				inum=atoi(num);
				memcpy((char *)data+step,&inum,sizeof(int));
				step+=sizeof(int);
			}

			v=newVal(BSQ,dim,label->nrows,1,INT,data);
			break;

		case ASCII_REAL:
			data=calloc(sizeof(double)*label->nrows,sizeof(char));
			for (i=0;i<(label->nrows*dim);i++){
				memcpy(num,Bufs[j]+f[j]->size*i,f[j]->size);
				num[f[j]->size]='\0';
				fnum=atof(num);
				memcpy((char *)data+step,&fnum,sizeof(double));
				step+=sizeof(double);
			}

			v=newVal(BSQ,dim,label->nrows,1,DOUBLE,data);
			break;

		case BYTE_OFFSET:
			data=calloc(size[j]*label->nrows,sizeof(char));
			memcpy(data,Bufs[j],size[j]*label->nrows);
			v=newVal(BSQ,dim,label->nrows,1,INT,data);
			break;

		case MSB_BIT_FIELD:
			data=calloc(size[j]*label->nrows,sizeof(char));
			memcpy(data,Bufs[j],size[j]*label->nrows);
			v=newVal(BSQ,dim,label->nrows,1,INT,data);
			break;

		}
		add_struct(*Data,fix_name(f[j]->name),v);
	}	
}

int rf_IMAGE(char *fn, Var *ob,char * val, int dptr)
{

	FILE *fp;
	Var *data=NULL;
	fp=fopen(fn,"r");
	data=(Var *)dv_LoadISIS(fp,fn,NULL);
	if (data!=NULL){
		add_struct(ob,fix_name("DATA"),data);
		fclose(fp);
		return(0);
	}

	fclose(fp);
	return(1);
}
int rf_HISTOGRAM_IMAGE(char *fn, Var *ob,char * val, int dptr)
{
	return(0);
}
int rf_HISTORY(char *fn, Var *ob,char * val, int dptr)
{
	return(0);
}

