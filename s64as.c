/*
	Program written for the GNU99 dialect of C99
	to assemble 
*/

#include "stringutil.h"
#include <stdint.h>

#define LIBMIN_UINT uint64_t
#define LIBMIN_INT int64_t
#define LIBMIN_FLOAT double

#include "libmin.h"
#include "insns.h"
#include "be_encoder.h"

#include <stdio.h>
#include <stdlib.h>

static const char* fail_msg = "\r\n<ASM> Assembler Aborted.\r\n";
static const char* compil_fail_pref = "<ASM COMPILATION ERROR>";
static const char* syntax_fail_pref = "<ASM SYNTAX ERROR>";
static const char* general_fail_pref = "<ASM ERROR>";
static const char* internal_fail_pref = "<ASM INTERNAL ERROR>";
static const char* warn_pref = "<ASM WARNING>";

char* ifilename = NULL;
char* ofilename = "a.bin";
FILE* infile = NULL;
FILE* ofile = NULL;


static char was_macro = 0;
/**/	
static char using_asciz = 0;
/*Two C64's worth of memory per buffer*/
#define WKBUF_SZ 0x20000
static char line_copy[WKBUF_SZ] = {0}; /*line_copy*/
static char line[WKBUF_SZ] = {0}; /*line*/
static char buf1[WKBUF_SZ] = {0}; /*buffer for working with strings.*/
static char buf2[WKBUF_SZ] = {0}; /*another buffer for working with strings.*/
static char buf3[WKBUF_SZ] = {0}; /*another buffer for working with strings.*/
static char buf_repl[WKBUF_SZ] = {0}; /*another buffer for working with strings, specifically for the following function:*/
static uint64_t label_generator=0;
static uint64_t label_offset=0; /*used for usermode programs.*/


int perform_inplace_repl( /*returns whether or not it actually did a replacement. You can put it in a while loop.*/
	char* workbuf,
	const char* replaceme,
	const char* replacewith
){
	long loc;

	loc = strfind((char*)workbuf, (char*)replaceme);
	if(loc == -1) return 0;
	memcpy(buf_repl, workbuf, loc);
	mstrcpy(buf_repl + loc, replacewith);
	mstrcpy(
		buf_repl + loc + strlen((char*)replacewith),
		workbuf + loc + strlen((char*)replaceme)
	);
	mstrcpy(workbuf, buf_repl);
	return 1;
}


static char rut_append_to_me = 0; /*boolean value.*/
static char single_logical_line_mode = 0;
static const char* end_single_logical_line_mode = "$|";
static const unsigned long len_end_single_logical_line_mode = 2;

static char* read_line_from_file(FILE* f, unsigned long* lenout, char terminator){
	unsigned char c;
	unsigned long blen;
	unsigned long i;

	blen = 0;

	if(rut_append_to_me){
		blen = strlen((char*)line);
		rut_append_to_me = 0;
	}
	while(1)
	{while_looptop:;
		if(feof(f)){goto local_end_1;}
		c = fgetc(f);
		if(!single_logical_line_mode) if(c == terminator) goto local_end_1;


		if(c == '\r') continue; /*Lines read from file should eliminate carriage return.*/
		if(c > 0x80) continue; /*Invalid character.*/
		if(blen >= (WKBUF_SZ-1))	/*too big*/
			{
				puts(general_fail_pref);
				puts("Oversized line!");
				exit(1);
			}
		line[blen++] = (char)c;




		if(single_logical_line_mode){
			/*We need to check if the last three characters were the end of a single logical line.*/
			if(strlen(line) > len_end_single_logical_line_mode){
				for(i = 1; i <= len_end_single_logical_line_mode; i++){
					if(
						line[blen-i] 
						!= 
						end_single_logical_line_mode[len_end_single_logical_line_mode-i]
					) goto while_looptop;
				}
				/*match!*/
				single_logical_line_mode = 0;
				/*remove it from the string.*/
				line[blen - len_end_single_logical_line_mode] = '\0';
				goto local_end_1;
			}
		}
		continue;
	}
	local_end_1:;
	line[blen] = '\0';
	*lenout = blen;
	return line;
}

#ifndef SISA64_MAX_MACROS
#define SISA64_MAX_MACROS 0x10000
#endif
static char* variable_names[SISA64_MAX_MACROS] = {0};
static char* variable_expansions[SISA64_MAX_MACROS] = {0};
static char variable_is_redefining_flag[SISA64_MAX_MACROS] = {0};
static const unsigned long max_lines_disassembler = 0x1ffFFff;
static unsigned long nmacros;


static char int_checker(char* proc){
	unsigned long chars_read;
	char int_mode;

	chars_read = 0;
	int_mode = 0;
	if(!misdigit(proc[0])) return 1; /*Cannot be valid.*/
	if(proc[0] == '0') {
		int_mode = 1;
		proc++;
		chars_read++;
		if(proc[0] == ',' || proc[0] == ';' || proc[0] == ' ' || proc[0] == '\0' || proc[0] ==/*(*/ ')') return 0; /*zero.*/
		if(proc[0] == 'x') {
			int_mode = 2;proc++;
			chars_read = 0;
		} /*else return 0;*/ /*0 = valid integer */
		
	} /*octal*/
	
	for(;!(proc[0] == ',' || proc[0] == ';'|| proc[0] == ' ' || proc[0] ==/*(*/ ')' || proc[0] == '\0');proc++){
		/*Check hex*/
		if(int_mode==2 && 
		!( mishex(*proc))
		){
			return 1; /*they have 0x, but no numbers!*/
		}
		if(int_mode==0 && 
		!(misdigit(*proc))){
			return 1;
		}
		if(int_mode==1 && 
		!( misoct(*proc) )){
			return 1;
		}
		chars_read++;
	}
	if(chars_read == 0) return 1; /*There were no characters in the number. Invalid integer.*/
	return 0;
}


static uint64_t outputcounter = 0;
static unsigned long nbytes_written = 0;
static unsigned long npasses = 0;
static char printlines = 0;
static unsigned long linesize = 0;
static char region_restriction_mode = 0; /*0 = off, 1 = block, 2 = region*/

static uint8_t memimage[0x40000000] = {0}; /*1 gigabyte memory image.*/
static const uint64_t memimage_mask = 
						0x3fFFffFF;

static void fputbyte(uint8_t b){
	memimage[outputcounter]=b;
	outputcounter++; 
	
	if(outputcounter > nbytes_written)
		nbytes_written = outputcounter;
	outputcounter &= memimage_mask;
}

static void putshort(uint16_t sh){
	fputbyte(sh/256);
	fputbyte(sh);
}

static void putlong(uint32_t sh){
	fputbyte(sh/0x1000000);
	fputbyte(sh/0x10000);
	fputbyte(sh/0x100);
	fputbyte(sh);
}

static void putqword(uint64_t sh){
	fputbyte(sh/0x100000000000000);
	fputbyte(sh/0x1000000000000);
	fputbyte(sh/0x10000000000);
	fputbyte(sh/0x100000000);

	fputbyte(sh/0x1000000);
	fputbyte(sh/0x10000);
	fputbyte(sh/0x100);
	fputbyte(sh);
}

#define ASM_MAX_INCLUDE_LEVEL 20
static FILE* fstack[ASM_MAX_INCLUDE_LEVEL];

char* metaproc = NULL;
unsigned long include_level = 0;

int i_cli_args;
const unsigned long nbuiltin_macros = 4; 


/*
	Scope code.

*/
#define SCOPE_MAX_VARS 256
enum{
	TYPE_U8=0,
	TYPE_I8=1,	
	
	TYPE_U16=2,
	TYPE_I16=3,	

	TYPE_U32=4,
	TYPE_I32=5,	

	TYPE_F32=6,	

	TYPE_U64=7,
	TYPE_I64=8,

	TYPE_F64=9,
	TYPE_STRUCT=10
};

#define SCOPE_MAX_VARNAME_LEN 127

typedef struct{
	uint64_t basetype;
	uint64_t ptrlevel;
	uint8_t is_lvalue;
	uint64_t structid;
} type;

typedef struct{
	char name[SCOPE_MAX_VARNAME_LEN + 1];
	type t;	
} scopevar;

unsigned int scope_is_active = 0;
scopevar scopevars[SCOPE_MAX_VARS];
type scope_retval_type = {0};
char scope_has_retval = 0;
unsigned int scope_nvars = 0;

uint64_t type_get_ptrlvl(type t){
	return t.ptrlevel;
}

int type_is_ptr(type t){
	return type_get_ptrlvl(t) != 0;
}

type parse_type(char* where, char** out){
	uint64_t basetype = 7;
	uint64_t ptrlevel = 0;
	type t;
	while(isspace(*where))where++;

	if(strprefix("u8",where)){
		where += 2;
		basetype = 0;
		goto after_basetype;
	}	
	if(strprefix("byte",where)){
		where += 4;
		basetype = 0;
		goto after_basetype;
	}
	if(strprefix("char",where)){
		where += 4;
		basetype = 0;
		goto after_basetype;
	}	
	if(strprefix("uchar",where)){
		where += 5;
		basetype = 0;
		goto after_basetype;
	}	
	if(strprefix("ubyte",where)){
		where += 5;
		basetype = 0;
		goto after_basetype;
	}	


	if(strprefix("i8",where)){
		where += 2;
		basetype = 1;
		goto after_basetype;
	}
	if(strprefix("sbyte",where)){
		where += 5;
		basetype = 1;
		goto after_basetype;
	}	
	if(strprefix("schar",where)){
		where += 5;
		basetype = 1;
		goto after_basetype;
	}	



	if(strprefix("u16",where)){
		where += 3;
		basetype = 2;
		goto after_basetype;
	}	
	if(strprefix("ushort",where)){
		where += 6;
		basetype = 2;
		goto after_basetype;
	}	


	if(strprefix("i16",where)){
		where += 3;
		basetype = 3;
		goto after_basetype;
	}	
	if(strprefix("short",where)){
		where += 5;
		basetype = 3;
		goto after_basetype;
	}	
	if(strprefix("sshort",where)){
		where += 6;
		basetype = 3;
		goto after_basetype;
	}	


	if(strprefix("u32",where)){
		where += 3;
		basetype = 4;
		goto after_basetype;
	}	
	if(strprefix("uint",where)){
		where += 4;
		basetype = 4;
		goto after_basetype;
	}	
	if(strprefix("ulong",where)){
		where += 5;
		basetype = 4;
		goto after_basetype;
	}
	if(strprefix("unsigned",where)){
		where += 8;
		basetype = 4;
		goto after_basetype;
	}

	if(strprefix("i32",where)){
		where += 3;
		basetype = 5;
		goto after_basetype;
	}	
	if(strprefix("int",where)){
		where += 3;
		basetype = 5;
		goto after_basetype;
	}	
	if(strprefix("long",where)){
		where += 4;
		basetype = 5;
		goto after_basetype;
	}
	if(strprefix("signed",where)){
		where += 6;
		basetype = 5;
		goto after_basetype;
	}


	
	if(strprefix("u64",where)){
		where += 3;
		basetype = 7;
		goto after_basetype;
	}		
	if(strprefix("qword",where)){
		where += 5;
		basetype = 7;
		goto after_basetype;
	}
	if(strprefix("uqword",where)){
		where += 6;
		basetype = 7;
		goto after_basetype;
	}
	if(strprefix("ullong",where)){
		where += 6;
		basetype = 7;
		goto after_basetype;
	}


	
	if(strprefix("i64",where)){
		where += 3;
		basetype = 8;
		goto after_basetype;
	}		
	if(strprefix("sqword",where)){
		where += 6;
		basetype = 8;
		goto after_basetype;
	}
	if(strprefix("llong",where)){
		where += 5;
		basetype = 8;
		goto after_basetype;
	}
	if(strprefix("sllong",where)){
		where += 6;
		basetype = 8;
		goto after_basetype;
	}

	
	//floating point types.
	if(strprefix("float",where)){
		where += 5;
		basetype = 6;
		goto after_basetype;
	}	
	if(strprefix("f32",where)){
		where += 3;
		basetype = 6;
		goto after_basetype;
	}
	if(strprefix("f64",where)){
		where += 3;
		basetype = 9;
		goto after_basetype;
	}
	if(strprefix("double",where)){
		where += 6;
		basetype = 9;
		goto after_basetype;
	}

	puts(syntax_fail_pref);
	puts("Could not identify base type while parsing type.");
	puts("Line:");
	puts(line_copy);
	exit(1);
	
	after_basetype:;
	ptrlevel = 0;
	while(*where == '*'){
		ptrlevel++;
		where++;
	}

	t.basetype = basetype;
	t.is_lvalue = 1;
	t.ptrlevel = ptrlevel;

	if(out)
		*out = where;
	return t;
}


void parse_vardecl(char* where, char** out){
	type t;
	unsigned long len;
	char saved_character;
	scopevar* setme;
	unsigned long x; /*loop counter variable for checking matching identifiers.*/

	setme = scopevars + scope_nvars;

	if(scope_nvars == SCOPE_MAX_VARS){
		puts(general_fail_pref);
		puts("Too many scope variables are declared.");
		puts("Line:");
		puts(line_copy);
	}

	while(isspace(*where))where++;
	t = parse_type(where, &where);
	/*Require at least one space...*/
	if(!isspace(*where)){
		puts(syntax_fail_pref);
		puts("Expected space character during variable declaration parsing. Line:");
		puts(line_copy);
		exit(1);
	}
	while(isspace(*where))where++;
	/*walk the string until we hit a non-identifier character.*/
	for(len = 0;
		where[len]!='\0';
		len++
	){
		if(isalnum(where[len]) || where[len] == '_') continue;
		break;
	}
	if(len == 0){
		puts(syntax_fail_pref);
		puts("Invalid Local Variable Identifier.");
		puts("Line:");
		puts(line_copy);
		exit(1);
	}
	if(len > SCOPE_MAX_VARNAME_LEN){
		puts(syntax_fail_pref);
		puts("Variable name has too many characters:");
		saved_character = where[len];
			where[len] = '\0';
			puts(where);
		where[len] = saved_character;
		puts("Line:");
		puts(line_copy);
		exit(1);
	}

	if(isdigit(where[0])){
		puts(syntax_fail_pref);
		puts("No local variables may start with a number.");
		puts("Line:");
		puts(line_copy);
		exit(1);
	}
	/*write the variables name and type to scopevars.*/
	saved_character = where[len];
		where[len] = '\0';
		if(streq("return",where)){
			puts(syntax_fail_pref);
			puts("You may not name a variable 'return'.");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		/*check for duplicate identifiers.*/
		for(x = 0; x < scope_nvars;x++)
			if(streq(scopevars[x].name, where)){
				puts("Duplicate identifier declared.");
				puts("Identifier:");
				puts(where);
				puts("Line:");
				puts(line_copy);
				exit(1);
			}
	mstrcpy(setme->name, where);
	where[len] = saved_character;
	scope_nvars++;
	setme->t = t;
	/*Skip the identifier...*/
	where += len;
	if(out)
		*out = where;
	return;
}

void parse_arglist(char* where, char** out){
	while(isspace(*where))where++;
	/*Empty argument list?*/
	if(*where == /*(*/')'){
		where++;
		if(out) *out = where;
		return;
	}

	while(1){
		while(isspace(*where))where++;
		parse_vardecl(where, &where);
		while(isspace(*where))where++;
		if(*where == /*(*/')')
		{
			where++;
			if(out) *out = where;
			return;
		}
		if(*where != ','){
			puts(syntax_fail_pref);
			puts("Expected comma or closing parentheses in argument list.");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		where++;
	}
}



uint64_t type_getsz(type t){
	if(type_is_ptr(t)) return 8;
	if(t.basetype == TYPE_U8 || t.basetype == TYPE_I8) return 1;
	if(t.basetype == TYPE_U16 || t.basetype == TYPE_I16) return 2;
	if(t.basetype == TYPE_U32 || t.basetype == TYPE_I32|| t.basetype == TYPE_F32) return 4;
	if(t.basetype == TYPE_U64 || t.basetype == TYPE_I64|| t.basetype == TYPE_F64) return 8;

	puts(internal_fail_pref);
	puts("Tried to get the size of an unknown type!");
	puts("Line:");
	puts(line_copy);
	exit(1);
}



static void handle_dollar_open_ccb(){
	/*TODO: Handle previously malloc'd scope variable names.*/
	scope_nvars = 0;
	scope_is_active = 1;
	scope_has_retval = 0;
	char* s = line + 2;
	while(isspace(*s)) s++; /*skip whitespace.*/
	if(*s != '(' /*)*/){
		puts(syntax_fail_pref);
		puts("Scope syntax REQUIRES a function prototype, including OPEN PARENTHESES.");
		puts("Line:");
		puts(line_copy);
		exit(1);
	}
	s++; /*consume open parentheses.*/
	while(isspace(*s)) s++; /*skip whitespace.*/
	parse_arglist(s, &s);
	while(isspace(*s)) s++; /*skip whitespace.*/
	/*Check for the presence of ->*/
	if(strprefix("->",s)){
		s+=2;
		while(isspace(*s)) s++; /*skip whitespace.*/
		scope_retval_type = parse_type(s, &s);
		scope_has_retval = 1;
		while(isspace(*s)) s++; /*skip whitespace.*/
	}
	if(s[0] == '\0'){
		goto end; /*Done!*/
	}
	if(s[0] != ':'){
		puts(syntax_fail_pref);
		puts("Scope declaration requires colon separator before local variable list.");
		puts("Line:");
		puts(line_copy);
		exit(1);
	}
	s++; /*consume colon*/

	while(1){
		while(isspace(*s)) s++; /*skip whitespace.*/
		/*Check for possible end of string.*/
		if(
			*s == '\0'
		){
			goto end;
		}
		/*check for early semicolon.*/
		if(
			*s == ';'
		){
			s++;
			while(isspace(*s)) s++; /*skip whitespace.*/
			continue;
		}
		/*Parse a variable declaration.*/
		parse_vardecl(s, &s);
		/*skip whitespace*/
		while(isspace(*s)) s++; /*skip whitespace.*/
		/*require a semicolon*/
		if(*s != ';'){
			puts(syntax_fail_pref);
			puts("Expected semicolon in local variable list.");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		s++;
	}

	end:;
	line[0] = '\0';
	return;
}

static void handle_dollar_close_ccb(){
	if(!scope_is_active){
		puts(general_fail_pref);
		puts("Tried to close a scope when there was none active.");
		puts("Line:");
		puts(line_copy);
		exit(1);
	}
	scope_is_active = 0;
	return;
}

/*returns len_to_replace*/
static unsigned long handle_dollar_normal(char* loc_in, char recursed){
	char saved_character;
	long len = 0;
	char* loc_name = loc_in+1;
	long loc_eparen;
	unsigned long i;
	long len_to_replace;
	unsigned long val;
/*	if(loc_name[0] == '|'){
		return 2;
	}*/
	if(!recursed)
		if(strprefix("return",loc_name))
			if(!(isalnum(loc_name[6]) ||	 loc_name[6] == '_') )
			{
				/*return statement.*/
				len = 1;
				loc_name += 6; /*we have to eat the return.*/
				len += 6;
				while(isspace(loc_name[0])){loc_name++;len++;} /*eat the spaces, too!*/
				/*do we stop here? First, invalidate return if it has no semicolon.*/
				loc_eparen = strfind(loc_name, ";");
				if(loc_eparen == -1){
					return_no_semicolon:;
					puts(syntax_fail_pref);
					puts("the $return statement must end in a semicolon.");
					puts("Line:");
					puts(line_copy);
					puts("Line Internally:");
					puts(line);
					exit(1);
				}
				if(loc_name[0] == '\0' || loc_name[0] == ';'){
					if(scope_has_retval){
						puts(syntax_fail_pref);
						puts("Scope has a return value, but return has no register ID to return!");
						puts("Line:");
						puts(line_copy);
						exit(1);
					}
					/*Leave the semicolon!*/
					mstrcpy(buf2,"ret");
					return len;
				}
				if(!scope_has_retval){
					puts(syntax_fail_pref);
					puts("Scope does not have a return value, yet, there is something here...");
					puts("Line:");
					puts(line_copy);
					exit(1);
				}
				/*We have something to evaluate...*/
				if(loc_name[0] == '$'){ /*Perform a recursive call!*/
					len_to_replace = handle_dollar_normal(loc_name,1);
					/*The appropriate replacement is in buf2, and we don't want to rewrite the code, so....*/
					mstrcpy(buf3, loc_name + len_to_replace); /*The portion after the stuff we just figured out how to replace*/
					mstrcpy(loc_name, buf2); /*Overwrite with the replacement.*/
					strcat(loc_name, buf3);/*concatenate back the stuff we saved.*/
				}

				loc_eparen = strfind(loc_name, ";");
				if(loc_eparen == -1){ /*This is probably redundant.*/
					goto return_no_semicolon;
				}
				if(!misdigit(loc_name[0])){
					puts(syntax_fail_pref);
					puts("Return value takes a register id. Either use a $variable_name, $+13! or an integer literal.");
				}
				val = matou(loc_name);
				if(val == 0){ /*OPTIMIZATION! don't move a register into itself!*/
					mstrcpy(buf2, "ret");
					return len + loc_eparen;					
				}
				mstrcpy(buf2, "mov 0,");
				mutoa(buf2 + strlen(buf2),val);
				strcat(buf2, ";ret");
				return len + loc_eparen; /*We want to leave the semicolon.*/
			}
	if( (isalnum(loc_name[0])) || loc_name[0] == '_')
	{
		len = 0;
		while(1){
			if((!isalnum(loc_name[len])) && (loc_name[len] != '_') ){
				break;
			}
			len++;
		}
		if(isdigit(loc_name[0])){
			puts(syntax_fail_pref);
			puts("No local variables may start with a number.");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}

		saved_character = loc_name[len];
		loc_name[len] = '\0';
		for(i =0; i < scope_nvars; i++){
			if(streq(scopevars[i].name, loc_name)) break;
		}
		if(i >= scope_nvars){
			puts(syntax_fail_pref);
			puts("Unrecognized Local Variable Identifier:");
			puts(loc_name);
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		loc_name[len] = saved_character;
		//Now, we put our replacement in.
		mutoa(buf2, i);
		//return 1 + length of the identifier.
		return 1 + len;
	}

	if(loc_name[0] == '+'){
		len = 0;
		loc_eparen = strfind(loc_name, "!");
		if(loc_eparen == -1){
			puts(syntax_fail_pref);
			puts("In order to get an available register id, $+ must be ended with an exclamation mark, $+0! gets the first available.");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		len = 2 + loc_eparen; /*have to eat the dollar sign, plus sign, AND the exclamation mark...*/
		if(!isdigit(loc_name[1])){
			puts(syntax_fail_pref);
			puts("A valid positive integer MUST be provided to $+!");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		i = matou(loc_name+1); /*Skip the initial plus sign.*/
		i += scope_nvars;
		if(i > 255){
			puts(general_fail_pref);
			puts("The register ID generated from a $+! goes beyond the limits of S-ISA-64 standard.");
			puts("Line:");
			puts(line_copy);
			exit(1);
		}
		mutoa(buf2, i);
		return len;
	}
	puts(syntax_fail_pref);
	puts("Unknown $ command.");
	puts("Line:");
	puts(line_copy);
	exit(1);
	return 1;
}





static void mini_expander(unsigned long len_command){
	unsigned long i;
	unsigned long have_expanded;
	long len_name;
	long len_exp;
	mini_expander_looptop:;
	{
		i = 0;
		have_expanded = 0;
		i = nbuiltin_macros; 

		mini_inner_for_top:if(!(i<nmacros)) goto mini_beyond_inner_for; 
		{
			if(
				strfind((char*)line,(char*)variable_names[i]) == (long)len_command
				&&
				line[len_command+strlen((char*)variable_names[i])] == /*(*/')'
			)
			{
				len_name = strlen((const char*)variable_names[i]);
				len_exp = strlen((const char*)variable_expansions[i]);
				if( len_name < len_exp ){
				if(  (strlen((const char*)line) + (len_exp - len_name)) >= WKBUF_SZ){
						puts(general_fail_pref);
						puts("Macro expansion would cause line to overflow internal buffer. Line:");
						puts((char*)line_copy);
					}
				}
				perform_inplace_repl(line, variable_names[i], variable_expansions[i]);
				have_expanded = 1;
				goto mini_beyond_inner_for;
			}
			i++; goto mini_inner_for_top;
		}
		mini_beyond_inner_for:;
	} if(have_expanded == 1) goto mini_expander_looptop;
}

static void remove_comments(){
	long loc_backslashn;
	long loc_comment;
	top:;
	while(
		strprefix(" ",line) || 
		strprefix("\t",line) || 
		strprefix("\v",line) || 
		strprefix("\r",line) || 
		strprefix("\n",line)
	) mstrcpy(line, line+1);
	if(strprefix("!",line)) return;
	if(strprefix("..ascii:",line)) return;
	if(strprefix("..asciz:",line)) return;
	if(strprefix("VAR#",line)) return;
	loc_comment = strfind(line, "//");
	if( loc_comment != -1){
		/*there is a // comment.*/
		loc_backslashn = strfind(line + loc_comment,"\n");
		if(loc_backslashn != -1){
			loc_backslashn += loc_comment;
		
			mstrcpy(line+loc_comment, line+loc_backslashn);
			goto top;
		}
		line[loc_comment] = 0;
		goto top;
	}
	loc_comment = strfind(line, "#");
	if( loc_comment != -1){
		/*there is a # comment here*/
		loc_backslashn = strfind(line + loc_comment,"\n");
		if(loc_backslashn != -1){
			loc_backslashn += loc_comment;
			mstrcpy(line+loc_comment, line+loc_backslashn);
			goto top;
		}
		line[loc_comment] = 0;
		goto top;
	}
}

static void handle_var_syntax_sugar(){
	long loc_colon;
	loc_colon = strfind(line, ":");
	if(line[1] == '.'){
		puts(syntax_fail_pref);
		puts("Syntactic sugar has a second period but is recognized as a macro declaration. Most likely a spelling error. Line:");
		puts(line_copy);
		exit(1);
	}
	if(loc_colon != -1){
		perform_inplace_repl(line, ".", "VAR#");
		perform_inplace_repl(line, ":", "#");
	} else {
		puts(syntax_fail_pref);
		puts("Syntactic sugar macro declaration without colon! Line:");
		puts(line_copy);
		exit(1);
	}
}

static void handle_export(){
	char* variable_name;
	char found = 0;
	long loc_eparen;
	long len_export;
	unsigned long i;
	
	len_export = strlen("..export\"");
	variable_name = (char*)line + len_export;
	loc_eparen = strfind((char*)line + len_export, "\"");
	if(loc_eparen == -1){
		puts("<ASM SYNTAX ERROR> Syntactic sugar for file include is missing ending \"");
		puts("Line:");
		puts((char*)line_copy);
		exit(1);
	}
	loc_eparen += len_export;
	line[loc_eparen] = '\0';

	i = nbuiltin_macros;
	export_sugar_macro_loop:
	if(!(i < nmacros)) goto end_export_sugar_macro_loop;
	{
		if(streq((char*)(variable_names[i]), variable_name))
		{
			found = 1;
			variable_is_redefining_flag[i] |= 2;
			goto end_export_sugar_macro_loop;
		}

		i++;goto export_sugar_macro_loop;
	}
	end_export_sugar_macro_loop:;

	if(found == 0){
		puts(compil_fail_pref);
		puts("Unknown symbol to export:");
		puts(variable_name);
		exit(1);
	}
}

static void handle_dinclude(){
	long loc_eparen;
	long len_dinclude;
	len_dinclude = strlen("..dinclude\"");
	loc_eparen = strfind((char*)line + strlen("..dinclude\""), "\"");
	if(loc_eparen == -1){
		puts(syntax_fail_pref);
		puts("Syntactic sugar for data include is missing ending \"\r\nLine:\r\n");
		puts((char*)line_copy);
		exit(1);
	}

	buf2[0] = '\0';
	strcat((char*)buf2, "ASM_data_include ");
	line[len_dinclude + loc_eparen] = '\0';
	if((strlen(line + len_dinclude) + strlen(buf2) )>=WKBUF_SZ){
		puts(general_fail_pref);
		puts("dinclude syntax sugar expansion would overflow internal buffer. Line:");
		puts(line_copy);
		exit(1);
	}
	strcat((char*)buf2, (char*)line + len_dinclude);
	mstrcpy(line, buf2);
}

static void handle_include(){
	long loc_eparen;
	long len_include;
	len_include = strlen("..include\"");
	loc_eparen = strfind((char*)line + strlen("..include\""), "\"");
	if(loc_eparen == -1){
		puts(syntax_fail_pref);
		puts("Syntactic sugar for file include is missing ending \"\r\nLine:\r\n");
		puts((char*)line_copy);
		exit(1);
	}
	buf2[0] = '\0';
	strcat((char*)buf2, "ASM_header ");
	line[len_include + loc_eparen] = '\0';
	if((strlen(line + len_include) + strlen(buf2) )>=WKBUF_SZ){
		puts(general_fail_pref);
		puts("include syntax sugar expansion would overflow internal buffer. Line:");
		puts(line_copy);
		exit(1);
	}
	strcat((char*)buf2, (char*)line + len_include);
	mstrcpy(line, buf2);
}

static void handle_asciz(){
	perform_inplace_repl(
		line,
		"..asciz:",
		"!"
	);
	using_asciz = 1;
}

static void handle_ascii(){
	perform_inplace_repl(
		line,
		"..ascii:",
		"!"
	);
	using_asciz = 0;
}

static void handle_dotdotz(){
	if(strlen(line) + 6 >= WKBUF_SZ){
		puts(general_fail_pref);
		puts("..z syntax sugar expansion would overflow internal buffer. Line:");
		puts(line_copy);
		exit(1);
	}
	perform_inplace_repl(
		line,
		"..z:",
		"section 0;"
	);
}

static void handle_dotdotzero(){
	if(strlen(line) + 3 >= WKBUF_SZ){
		puts(general_fail_pref);
		puts("..zero syntax sugar expansion would overflow internal buffer. Line:");
		puts(line_copy);
		exit(1);
	}
	perform_inplace_repl(
		line,
		"..zero:",
		"section 0;"
	);
}

static void handle_standard_label_syntax(){
	long loc;
	long i;
	/*Alternate syntax for labels, more traditional to assemblers.*/
	loc = strfind(line, ":");
	if(loc <= 0 ) return;
	if(line[loc-1] == '\\') return;
	/*lex for invalid characters. If we find any, */
	{i = 0;
		for(;i<loc;i++)
			if(!(line[i] == '_' || isalnum(line[i])) )
				return;
	}
	{
		line[loc] = '\0';
		buf2[0] = 0;
		if(
			(strlen(line) + strlen("asm_label\\") + 1 + strlen(line+loc+1))
			>= WKBUF_SZ
		){
			puts(general_fail_pref);
			puts("Label definition line is TOO LONG! Line:");
			puts(line_copy);
			exit(1);
		}
		strcat(buf2, "asm_label\\");
		strcat(buf2, line);
		strcat(buf2, ";");
		
		line[loc] = '~'; /*if it gets in, it should throw an error.*/
		strcat(buf2, line + loc + 1);
		mstrcpy(line, buf2);
	}
}

static void handle_exclamation_mark_string_literal(){
	unsigned long i;
	/*We are writing out individual bytes for the rest of the line.*/
	for(i = 1; i < strlen(line);i++)
		fputbyte(line[i]);
	if(using_asciz) fputbyte(0);
	using_asciz = 0;
}

static void handle_asm_header(){
	FILE* tmp; 
	char* metaproc;
	metaproc = line + strlen("ASM_header ");
	if(include_level >= ASM_MAX_INCLUDE_LEVEL){
		puts(compil_fail_pref);
		puts("Include level maximum reached.");
		exit(1);
	}
	if(strlen(metaproc) >= ( (WKBUF_SZ)/2)){
		puts(general_fail_pref);
		puts("ASM_header path is TOO LARGE!, Line:");
		puts(line_copy);
		exit(1);
	}
	tmp = fopen(metaproc, "r");
	if(!tmp) {
		buf2[0] = '\0';
		strcat(buf2, "/usr/include/sisa64/");
		strcat(buf2, metaproc);
		tmp = fopen(buf2, "r");
	}
	if(!tmp) {
		buf2[0] = '\0';
		strcat(buf2, "C:\\SISA64\\");
		strcat(buf2, metaproc);
		tmp = fopen(buf2, "r");
	}
	if(!tmp) {
		puts(compil_fail_pref);
		puts("Unknown/unreachable header file:"); 
		puts(metaproc);
		exit(1);
	}
	fstack[include_level] = infile;
	include_level++;
	infile = tmp;
}

static void handle_asm_data_include(){
		/*data include! format is
	asm_data_include filename
	*/
	FILE* tmp; 
	char* metaproc; 
	unsigned long len;
	
	metaproc = line + strlen("ASM_data_include ");
	if(strlen(metaproc) >= ( (WKBUF_SZ)/2)){
		puts(general_fail_pref);
		puts("ASM_data_include path is TOO LARGE!, Line:");
		puts(line_copy);
		exit(1);
	}
	tmp = fopen(metaproc, "rb");
	if(!tmp) {
		buf2[0] = '\0';
		strcat(buf2, "/usr/include/sisa64/");
		strcat(buf2, metaproc);
		tmp = fopen(buf2, "rb");
	}
	if(!tmp) { 
		buf2[0] = '\0';
		strcat(buf2, "C:\\SISA64\\");
		strcat(buf2, metaproc);
		tmp = fopen(buf2, "rb");
	}
	if(!tmp) {
		puts(compil_fail_pref);
		puts("unreachable data file:"); 
		puts(metaproc);
		exit(1);
	}
	fseek(tmp, 0, SEEK_END);
	len = ftell(tmp);
	if(len > 0x1000000) {
		puts(compil_fail_pref);
		puts("data file too big:"); 
		puts(metaproc);
		exit(1);
	}
	if(len == 0){
		puts(compil_fail_pref);
		puts("data file empty:"); 
		puts(metaproc);
		exit(1);
	}
	fseek(tmp, 0, SEEK_SET);
	for(;len>0;len--)fputbyte(fgetc(tmp));
	fclose(tmp);
	if(printlines && npasses == 1)puts(line);
}


long find_first_occurrence(char* s, char* w, 
	char identifier_validation_rules, 
	char vbar_invalidation,
	char backslash_invalidation_rule,
	char brackets_invalidation_rule
){
	long loc;
	long loc_new;
	long loc_vbar;
	long invalid;
	long len_w;
	long walker;
	char q;
	loc_vbar = strfind(s, "|");
	loc = strfind(s,w);
	if(loc == -1) return -1; /*It is nowhere in the string.*/
	len_w = strlen(w);
	do{
		invalid = 0;
		if(loc == -1) return -1;
		if(identifier_validation_rules){
			q = s[loc + len_w];
			if(isalpha(q) || misdigit(q) || (q == '_')){ /*This cannot possibly be the correct macro.*/
				invalid=1;
			}
			if(loc > 0){
				q = *(s+loc-1);
				if(isalpha(q) || misdigit(q) || (q == '_')){ /*This cannot possibly be the correct macro.*/
					invalid=1;
				}
				/*do an additional test to see if this is inside a the colon syntax.*/
				if((q == ':') && (s[loc + len_w] == ':')){
					invalid=1;
				}
			}
		}
		if(backslash_invalidation_rule){
			if(loc > 1){
				q = s[loc-1];
				/*Is this escaped?*/
				if(q == '\\'){
					invalid=1;
				}
			}
		}
		if(vbar_invalidation){
			if(loc_vbar != -1)
			if(loc_vbar < loc)
				invalid = 1;
		}
		if(brackets_invalidation_rule){
			for(walker = loc+1; s[walker] != /*[*/']' && 
				s[walker]; walker++
			){
				if(!isalnum(s[walker]))
					if(s[walker] != '_')
						invalid = 1;
				if(walker == loc+1)
					if(!isalpha(s[walker]))
						if(s[walker] != '_')
							invalid = 1;
			}
		}
		if(invalid){
			loc_new = strfind(s+loc+1,w);
			if(loc_new == -1) return -1;
			loc_new += loc+1;
			loc = loc_new;
		}
	}while(invalid);
	return loc;
}

static const char* const asm_arg_names[10] = {
	"asm_arg0",
	"asm_arg1",
	"asm_arg2",
	"asm_arg3",
	"asm_arg4",
	"asm_arg5",
	"asm_arg6",
	"asm_arg7",
	"asm_arg8",
	"asm_arg9"
};

static unsigned long handle_colon_macro(long loc){
			long len_to_replace = 1;
			long loc_colon2;
			long colon_ident_walker;
			len_to_replace = 1;
							/*TODO change this to a for loop that walks every character.*/
			loc_colon2 = strfind(line+loc+1, ":");
	if(loc_colon2 == -1){
		puts(syntax_fail_pref);
		puts("(MACRO) Syntactic sugar label declaration without second colon! Line:");
		puts(line_copy);

		goto error;
	}
	if(loc_colon2 == 0){
		puts(syntax_fail_pref);
		puts("(MACRO) double colon syntax has empty label identifier! Line:");
		puts(line_copy);
		goto error;
	}

	len_to_replace += loc_colon2; /*Do not replace that character, we are going to write a semicolon there.*/
	loc_colon2 += loc + 1; /*Make this truly representative of what's happening here.*/


	line[loc_colon2] = '\0';
	for(colon_ident_walker = loc+1;colon_ident_walker < loc_colon2;colon_ident_walker++){
		if(colon_ident_walker == loc+1){
			if(
				line[colon_ident_walker] != '_' &&
				!isalpha(line[colon_ident_walker])
			){
				puts(syntax_fail_pref);
				puts("(MACRO) double colon syntax has INVALID identifier! Identifier:");
				puts(line+loc+1);
				puts("Line:");
				puts(line_copy);
				goto error;
			}
		} else if(
			line[colon_ident_walker] != '_' &&
			!isalnum(line[colon_ident_walker])
		){
			puts(syntax_fail_pref);
			puts("(MACRO) double colon syntax has INVALID identifier! Identifier:");
			puts(line+loc+1);
			puts("Line:");
			puts(line_copy);
			goto error;
		}
	}
	
	strcat(buf2, line + loc + 1);
	
	line[loc_colon2] = ';'; /*this is strcat'd onto buf1 later.*/
	return len_to_replace;

	error:
	puts(fail_msg);
	exit(1);
}


const unsigned MACRO_ID_PERCENT = 0;
const unsigned MACRO_ID_AT = 1;
const unsigned MACRO_ID_COLON = 2;
const unsigned MACRO_ID_DOLLAR = 3;




static void do_macro_expansion(){

	long loc_eparen=	-1;
	/*char* const expansion = (char*)buf3; Purely aesthetic.*/
	unsigned char have_expanded = 0;
	long i;
	long loc;
	long len_to_replace;
	unsigned char character_literal;
	long loc_obrack;
	long ident_is_invalid;
	long loc_after_args;
	long loc_curlybrace_1 = 0;
	long loc_curlybrace_2 = 0;
	long ncurlybraces = 0;
	long curlylevel = 0;
	long argnum = 0;
	char ignore_charlit;
	long last_found_location;
	long active_search_loc;
	char c;
	long len_argname;
	long len_buf3;
	uint64_t addval;

	/*split expansion*/
	unsigned char split_type;
	uint64_t split_uval;
	uint32_t split_u32val;
	double split_dval;
	float split_fval;

	if(sizeof(double) != 8){
		puts(internal_fail_pref);
		puts("Double is not 8 bytes.");
		puts("Change all uses of double into a compatible 64 bit floating type.");
		exit(1);
	}
	char* add_text;
		/*macro_expansion_greater_start:;*/
		{
			have_expanded = 0; 
			do{ /*do while*/
				top_of_macro_expansion_do_while:;
				have_expanded = 0;

				/*~~~~~~~~~~~~~~~~~~~~~~~~~*/
				/*~SPECIAL_MACRO_EXPANSION~*/
				/*~~~~~~~~~~~~~~~~~~~~~~~~~*/

				/*CHARACTER LITERAL SYNTAX*/
				if(!was_macro)
				{
					/*loc;*/
					len_to_replace = 1;
					/*loc = strfind(line, "'");*/
					loc = find_first_occurrence((char*)line,"'",0,1,1,0);
					
					if(loc != -1)
					{ /*CHAR_LITERAL_EXPANSION*/
						character_literal = ' '; /*It was probably the space character.*/
						buf1[0] = '\0';
						buf2[0] = '\0';
						memcpy(buf1, line, loc); /*buf1 modified here.*/
						buf1[loc] = '\0';
						if(strlen(line+loc) == 0){
							puts(syntax_fail_pref);
							puts("character literal is at end of line. Line:");
							puts(line_copy);
							goto error;
						}
						buf3[29] = '\0';
						for(;line[loc+len_to_replace] && line[loc+len_to_replace] != '\'';len_to_replace++){
							if(line[loc+len_to_replace] == '\\'){
								len_to_replace++; /*Skip evaluating this character.*/
								if(line[loc+len_to_replace] == '\0'){
									puts(syntax_fail_pref);
									puts("character literal escapes the end of line??? Line:");
									puts(line_copy);
								}
								character_literal = ((unsigned char*)line)[loc+len_to_replace];
								if(character_literal == 'n') character_literal = '\n';
								else if(character_literal == 'r') character_literal = '\r';
								else if(character_literal == 'a') character_literal = '\a';
								else if(character_literal == 'b') character_literal = '\b';
								else if(character_literal == 'f') character_literal = '\f';
								else if(character_literal == 'e') character_literal = '\e';
								else if(character_literal == 'v') character_literal = '\v';
								else if(character_literal == 't') character_literal = '\t';
								else if(character_literal == '0') character_literal = '\0';
								else if(character_literal == 'x') {
									puts(syntax_fail_pref);
									puts("Hex character literals not supported. Just use 0xXX (not inside a character literal) for your number. Line:");
									puts(line_copy);
									goto error;
								}
							} else {
								character_literal = ((unsigned char*)line)[loc+len_to_replace];
							}
						}
						if(line[loc+len_to_replace] != '\''){
							puts(syntax_fail_pref);
							puts("Character literal missing ending single quote! Line:");
							puts(line_copy);
							goto error;
						}
						len_to_replace++;
						mutoa(buf3, (unsigned long)character_literal);
						if( (strlen(buf1) + strlen(buf3)) >= WKBUF_SZ){
							puts(general_fail_pref);
							puts("Character literal expansion would result in overflowing a buffer. Line:");
							puts(line_copy);
							goto error;
						}
						strcat(buf1, buf3); /*buf1 was modified before this.*/

						/*overflow check*/
						if( (strlen(buf1) + strlen(line+loc+len_to_replace)) >= WKBUF_SZ)
						{
							puts(general_fail_pref);
							puts("Character Literal expansion would result in overflowing a buffer. Line:");
							puts(line_copy);
							goto error;
						}
						strcat(buf1, line+loc+len_to_replace);
						mstrcpy(line, buf1);
						goto top_of_macro_expansion_do_while;
					}
				}


				/*ASM_GEN_NAME*/
				if(!was_macro)
				{
					len_to_replace = 12;
					loc = find_first_occurrence((char*)line,"ASM_GEN_NAME",0,1,0,0);

					/**/
					if(loc != -1)
					{
						buf1[0] = '\0';
						buf2[0] = '\0';
						memcpy(buf1, line, loc); /*buf1 modified here.*/
						buf1[loc] = '\0';
						mstrcpy(buf3, "auto_");
						mutoa(buf3 + strlen(buf3),(unsigned long)label_generator++);
						strcat(buf3,"_");
						if( (strlen(buf1) + strlen(buf3)) >= WKBUF_SZ){
							puts(general_fail_pref);
							puts("ASM_GEN_NAME expansion would result in overflowing a buffer. Line:");
							puts(line_copy);
							goto error;
						}
						strcat(buf1, buf3); /*buf1 was modified before this.*/

						/*overflow check*/
						if( (strlen(buf1) + strlen(line+loc+len_to_replace)) >= WKBUF_SZ)
						{
							puts(general_fail_pref);
							puts("ASM_GEN_NAME expansion would result in overflowing a buffer. Line:");
							puts(line_copy);
							goto error;
						}
						strcat(buf1, line+loc+len_to_replace);
						mstrcpy(line, buf1);
						goto top_of_macro_expansion_do_while;
					}
				}
				
				/*BRACKET BRACKET MACROS*/

				if(!was_macro)
				if(nmacros > nbuiltin_macros){
					ident_is_invalid = 0;
					loc_obrack = find_first_occurrence((char*)line,"[",0,1,1,1);

	
					if(!ident_is_invalid)
					if(loc_obrack != -1)
					{
						for(i=nmacros-1; i>=(long)nbuiltin_macros; i--){
							/*loc;*/
							loc_after_args = 0;
							/*len_to_replace;*/ 
							loc = loc_obrack+1;
							if(!strprefix(variable_names[i], line+loc)) continue;
							len_to_replace = strlen(variable_names[i]);
							if(line[loc+len_to_replace] != /*[*/']') continue;

												
							buf1[0] = '\0';
							buf2[0] = '\0';
							memcpy(buf1, line, loc);
							mstrcpy(buf2, (variable_expansions[i]) );
							buf1[loc] = '\0';
							
									

							
							loc_curlybrace_1 = 0;
							loc_curlybrace_2 = 0;
							ncurlybraces = 0;
							curlylevel = 0;
							argnum = 0;
							
							loc_after_args = 0;
							have_expanded = 1; /*definitely expanding here!*/
							curlylevel = 0;
							line[loc-1] = ' ';
							line[loc+len_to_replace] = ' ';
							buf1[loc-1] = ' ';
							loc_curlybrace_1 = loc + len_to_replace+1;
							while(isspace(line[loc_curlybrace_1])) loc_curlybrace_1++;

							if(line[loc_curlybrace_1] != '{' /*}*/)
							{
								puts(syntax_fail_pref);
								puts("Macro with arguments found without opening curly brace. Line:");
								puts(line_copy);
								goto error;
							}
							loc_after_args = loc_curlybrace_1;
							while(1){ /*argnum from 0 to 9*/
								ignore_charlit = 0; /*Ignore character literals.*/
								last_found_location = -1;
								active_search_loc = -1;

								while(isspace(line[loc_curlybrace_1])) loc_curlybrace_1++; /*Skip whitespace to find the next curly brace.*/
								if(line[loc_curlybrace_1] != '{' /*}*/) break;
								/*After this point, loc_curlybrace_1 is the location on the line of an opening curly brace.*/

								if(argnum > 9) {
									puts(syntax_fail_pref);
									puts("Macro with arguments has too many arguments. Only 9 are supported. Line:");
									puts(line_copy);
									goto error;
								} /*unsupported argnum*/
								
								loc_curlybrace_2 = loc_curlybrace_1+1;
								loc_after_args = loc_curlybrace_2;
								/*Seek out the next curly brace.*/
								curlylevel = 1;
								ncurlybraces = 1;
								while(1){
									c = line[loc_curlybrace_2];
									if(c == '\0'){
										puts(syntax_fail_pref);
										puts("Incomplete curly braces. Line:");
										puts(line_copy);
										goto error;
									}
									if(c == '\'' )ignore_charlit = !ignore_charlit;
									if(!ignore_charlit){
										if(c == '{'/*}*/) {curlylevel++;ncurlybraces++;}
										if(c == /*{*/'}') {curlylevel--;ncurlybraces++;}
									}
									if(curlylevel == 0) break;
									loc_curlybrace_2++;
								}
								loc_after_args = loc_curlybrace_2+1;
								curlylevel = 0;

								ncurlybraces = 0;
								/*We now have an argument, wrapped from loc_curlybrace_1 to 2,
								and we need to find all instances of asm_argX (X=argnum)
								in buf2, and replace them. */
								/*Check for the special cases. Nothing in curly braces, and bugged assembler.*/
								if(loc_curlybrace_1 == (loc_curlybrace_2 - 1)) 
									goto next_argnum_bracket_syntax;
								if(loc_curlybrace_2 <= loc_curlybrace_1){
									puts("\nInternal error, curly brace parsing is bugged. Got incorrect locations. Line:");
									puts(line_copy);
									goto error;
								}
								memcpy(buf3, line+loc_curlybrace_1+1, (loc_curlybrace_2 - loc_curlybrace_1 - 1));
								buf3[(loc_curlybrace_2 - loc_curlybrace_1 - 1)] = '\0';
								/*Repeatedly perform the replacement.*/
								last_found_location = -1;
								active_search_loc = -1;
								while(1){
									active_search_loc = strfind(buf2, asm_arg_names[argnum]);
									if(active_search_loc <= last_found_location) break;

									last_found_location = active_search_loc;
									last_found_location += strlen((const char*)buf3);
									/*Check for buffer overflow.*/
									{
										/*len_argname;
										len_buf3;*/
										len_argname = strlen(asm_arg_names[argnum]);
										len_buf3 = strlen(buf3);
										/*if it is possible that this would create an overflow...*/
										if( len_argname < len_buf3){
											if( (len_buf3 - len_argname + strlen(buf2)) >= WKBUF_SZ){
												puts(general_fail_pref);
												puts("[Macro]{}, during expansion of arguments, "
														"Would overflow a buffer if it expanded one of its arguments."
														"\nLine:\n%s"
												);
												puts(line_copy);
												goto error;
											}
										}
									}
									perform_inplace_repl(buf2 + active_search_loc, asm_arg_names[argnum], buf3);

								}
								/*Skip to next argument portion.*/
								next_argnum_bracket_syntax:;
								loc_curlybrace_1 = loc_curlybrace_2 + 1;
								loc_after_args = loc_curlybrace_2+1;
								argnum++;
							} /*EOF loop over argnums.*/
							/*loc_start_args is the location of the opening curly brace, while
							loc_after_args is the location after the ending curly brace. They are
							fixed positions in the macro expansion.*/
							len_to_replace = loc_after_args - loc;/*Total amount to replace.*/
						goto bracket_macro_expansion_end;

							continue; /*This is not a valid macro to expand.*/

							bracket_macro_expansion_end:;
							if((strlen(buf1) + strlen(buf2)) > WKBUF_SZ)
							{
								puts("\nCannot perform macro expansion, macro expands to be TOO BIG! Line:");
								puts(line_copy);
								goto error;							
							}
							strcat(buf1, buf2);
													/*overflow check*/
							if( (strlen(buf1) + strlen(line+loc+len_to_replace)) >= WKBUF_SZ){
								puts(general_fail_pref);
								puts("Macro expansion would result in overflowing a buffer. Line:");
								puts(line_copy);
								goto error;
							}
						
							strcat(buf1, line+loc+len_to_replace);
							mstrcpy(line, buf1);
							goto top_of_macro_expansion_do_while;
						}

						/*We fell through the for loop, this might be part of an expression.*/
						puts(general_fail_pref);
						puts("\r\nCannot match macro inside of brackets! Line:");
						puts(line_copy);
						puts("\nLine Internally:\n");
						puts(line);
						goto error;
						
					}
				}





				/*~~~~~~~~~~~~~~~~~~~~~~~~*/
				/*~NORMAL_MACRO_EXPANSION~*/
				/*~~~~~~~~~~~~~~~~~~~~~~~~*/
				/*Where user-defined macros, 
				  @, character literals, 
				  and split%% are expanded.
				*/
				if(was_macro)
					i = MACRO_ID_AT;
				else
					i = nmacros-1;
				
				for(; i>=0; i--)
				{ /*Only check builtin macros when writing a macro. */
					
					if(was_macro)
					if(i != MACRO_ID_AT){ /*at sign only*/
						break; /*Do not parse any other macros excepts dollar sign and at inside of a macro.*/
					}
					/*
					buf1[0] = '\0';
					buf2[0] = '\0';*/
					/*loc = strfind(line, variable_names[i]);*/
					if(i < (long)nbuiltin_macros)
						loc = find_first_occurrence((char*)line, variable_names[i], 
							0,1,1,0
						);
					else
						loc = find_first_occurrence((char*)line, variable_names[i], 
							1,1,1,0
						);
					
					if(loc == -1) continue;
					have_expanded = 1; /*definitely expanding something*/
					len_to_replace = strlen(variable_names[i]);


					buf1[0] = '\0';
					buf2[0] = '\0';
					memcpy(buf1, line, loc);
					buf2[0] = '\0';
					mstrcpy(buf2, (variable_expansions[i]) );
					buf1[loc] = '\0';


					/*What type of macro is it?*/

					if(i==MACRO_ID_COLON){ /*colon*/
						if(loc > 0)if(line[loc-1] == '\'')continue;
						len_to_replace = handle_colon_macro(loc);
						strcat(buf1, buf2);
					}
					if((long)i >= (long)nbuiltin_macros){ /**/
						if((strlen(buf1) + strlen(buf2)) > WKBUF_SZ)
						{
							puts("\nCannot perform macro expansion, macro expands to be TOO BIG! Line:");
							puts(line_copy);
							goto error;							
						}
						strcat(buf1, buf2);
					}
					if(i == MACRO_ID_DOLLAR){
						len_to_replace = handle_dollar_normal(line+loc, 0);
						strcat(buf1, buf2);
					}
					if (i == MACRO_ID_AT){ /*SYNTAX: @+7+ or @ alone*/
						addval = 0;
						/*We need to check if there is a plus sign immediately after the at sign. */
						if(line[loc+1] == '+' /*strprefix("+",line+loc+1) */){
							add_text = line+loc+2;
							/*Find the next plus*/
							loc_eparen = strfind(line+loc+2,"+");
							if(loc_eparen == -1){
								puts(syntax_fail_pref);
								puts("@ with no ending plus. Line:");
								puts(line_copy);
								goto error;
							}
							if(loc_eparen == 0){
									puts(warn_pref);
									puts("@ with empty add section. Line:");
									puts(line_copy);
							}
							addval = strtoull(add_text,0,0);
							if(addval == 0 && npasses == 1)
							{
								puts(warn_pref);
								puts("@ with add evaluating to zero. Line:");
								puts(line_copy);
							}
							if(addval)
							len_to_replace += (loc_eparen-len_to_replace+3);
						}
						addval += outputcounter - label_offset;
						/*@& gets the region of the output counter.*/
						mutoa(buf3, addval);
						strcat(buf1, buf3);
					}
					if (i==MACRO_ID_PERCENT){ /*SPLIT_EXPANSION*/
						split_type = 0xFF; /*
							0 = 64 bit uint
							1 = 64 bit int
							2 = 32 bit uint
							3 = 32 bit int
							4 = 16 bit uint
							5 = 16 bit int
							6 = 8 bit uint
							7 = 8 bit int
							8 = 64 bit float
							9 = 32 bit float
						*/
						if(loc == 0){
							puts(syntax_fail_pref);
							puts("Cannot have split without a prefixing character.");
							puts("Line:");
							puts(line_copy);
							exit(1);
						}
						if(line[loc - 1] == 'q') split_type = 0;
						if(line[loc - 1] == '&') split_type = 0;
						if(line[loc - 1] == 'Q') split_type = 1;
						if(line[loc - 1] == 'd') split_type = 8;

						if(line[loc - 1] == 'l') split_type = 2;
						if(line[loc - 1] == 'L') split_type = 3;
						if(line[loc - 1] == 'f') split_type = 9;

						if(line[loc - 1] == 's') split_type = 4;
						if(line[loc - 1] == 'S') split_type = 5;

						if(line[loc - 1] == 'b') split_type = 6;
						if(line[loc - 1] == 'B') split_type = 7;
						line[loc-1] = ' ';
						buf1[loc-1] = ' ';

						if(split_type == 0xFF){
							puts(syntax_fail_pref);
							puts("Invalid SPLIT (%%) syntax.");
							puts("Example: jmp %q 300%");
							puts("Must be one of:");
							puts("q%0% - 64 bit uint");
							puts("&%0% - 64 bit uint (syntax sugar for address)");
							puts("Q%0% - 64 bit int");
							puts("l%0% - 32 bit uint");
							puts("L%0% - 32 bit int");
							puts("s%0% - 16 bit uint");
							puts("S%0% - 16 bit int");
							puts("b%0% - 8 bit uint");
							puts("B%0% - 8 bit int");
							puts("d%0% - 64 bit float");
							puts("f%0% - 32 bit float");
							puts("Line:");
							puts(line_copy);
							exit(1);
						}
						loc_eparen = strfind(line+loc+1,"%");
						if(loc_eparen == -1){
							puts(syntax_fail_pref);
							puts("SPLIT (%%) without ending %%. At:");
							puts(line + loc);
							puts("On this line:");
							puts(line_copy);
							exit(1);
						}
						/*dark magic from the sisa16 repository...*/
						len_to_replace += (loc_eparen-len_to_replace+2);

						if(
							split_type == 0 ||
							split_type == 2 ||
							split_type == 4 ||
							split_type == 6
						){
							split_uval = strtoull(line+loc+1, 0,0);
						}

						if(
							split_type == 1 ||
							split_type == 3 ||
							split_type == 5 ||
							split_type == 7
						){
							split_uval = strtoll(line+loc+1, 0,0);
						}


						if(split_type == 8)
						{
							split_dval = strtod(line+loc+1, 0);
							memcpy(&split_uval, &split_dval, 8);
						}

						if(split_type == 9)
						{
							split_fval = strtof(line+loc+1, 0);
							memcpy(&split_u32val, &split_fval, 4);
							split_uval = split_u32val;
						}

						//verify integrity of the scanned number.
						if(split_type < 8) /*Not a float!*/
						if(
							split_uval == 0  && 
							npasses == 1 && 
							line[loc+2] != '%' && 
							!misdigit(line[loc+2])
						)
						{
							puts(warn_pref);
							puts("Unusual Split (%%) evaluates to zero. Line:");
							puts(line_copy);
							puts("Line internally:");
							puts(line);
						}

						//based on split type, write out a comma
						//separated byte list.

						if(split_type == 6 || split_type == 7){
							mutoa(buf3, split_uval & 0xff);
						}

						//short
						if(split_type == 4 || split_type == 5){
							mutoa(buf3, (split_uval/256) & 0xff);
							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval) & 0xff
							);
						}
						//long
						if(split_type == 2 || split_type == 3 || split_type == 9){
							mutoa(buf3, (split_uval/0x1000000) & 0xff);
							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x10000) & 0xff
							);							
							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x100) & 0xff
							);
							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval) & 0xff
							);
						}
						//q
						if(split_type == 0 || split_type == 1 || split_type == 8){
							buf3[0] = 0;
							mutoa(buf3, 
								(split_uval/0x100000000000000) & 0xff

							);	
							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x1000000000000) & 0xff

							);							

							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x10000000000) & 0xff

							);

							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x100000000) & 0xff

							);							

							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x1000000) & 0xff

							);

							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x10000) & 0xff
							);							

							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval/0x100) & 0xff
							);

							strcat(buf3, ",");
							mutoa(buf3 + strlen(buf3), 
								(split_uval) & 0xff
							);
						}
						strcat(buf1, buf3);
					} //EOF SPLIT EXPANSION

					/*overflow check*/
					if( (strlen(buf1) + strlen(line+loc+len_to_replace)) >= WKBUF_SZ){
						puts(general_fail_pref);
						puts("Macro expansion would result in overflowing a buffer. Line:");
						puts(line_copy);
						exit(1);
					}
					strcat(buf1, line+loc+len_to_replace);
					mstrcpy(line, buf1);
					goto top_of_macro_expansion_do_while;
				} /*EOF loop over macros.*/

			}while(have_expanded);
		} /*EOF do while loop variables.*/
	return;
	error:
	puts(fail_msg);
	exit(1);
}


static void do_insn_expansion(){
	unsigned char have_expanded;
	long loc;
	unsigned long j;
	unsigned long i;
	unsigned long insn_len;
	int32_t num_commas_needed;

	have_expanded = 0; 
	do{
		have_expanded = 0;
		for(i = 0; i<n_insns; i++){
			/*num_commas_needed;*/
			/*loc = strfind(line, insns[i]);*/
			loc = find_first_occurrence(line, (char*)insn_names[i],1,1,0,0);
			if(loc == -1) continue;


			insn_len = strlen(insn_names[i]);


			
			if(loc == -1) continue;
			/*Search all other instructions to see if there is an equally valid solution.*/
			{
				char found_longer_match;
				found_longer_match = 0;
				for(j = i+1; j<n_insns; j++){
					if(j == i) continue;
					if(  strlen(insn_names[j]) > insn_len  ){
						long checkme = strfind(line, insn_names[j]);
						if(checkme == 0)
						{
							found_longer_match = 1;
							break;
						}
					}
				}
				if(found_longer_match) continue;
			}

			if(loc>0){
				/*Check to make sure this isn't prefixed by something which is obviously erroneous.*/
				if( (line[loc-1] != ';') && 
					(line[loc-1] != ' ')
				)
					{
						puts(syntax_fail_pref);
						puts("Insn parsing reached with unusual prefixing character:");
						putchar(*(line+loc-1));
						puts("Instruction to parse is ");
						puts(insn_names[i]);
						puts("Line:");
						puts(line_copy);
						puts("Line internally:");
						puts(line);
						goto error;
					}
				
			}
			if(
				*(line+loc+insn_len) != ';' &&  
				*(line+loc+insn_len) != '\0' && 
				!(
					*(line+loc+insn_len) >= '0' &&
					*(line+loc+insn_len) <= '9'
				) &&
				*(line+loc+insn_len) != ' '
			)
				{
					puts(warn_pref);
					puts(syntax_fail_pref);
					puts("Insn parsing reached with unusual postfixing character:");
					putchar(*(line+loc+insn_len));
					puts("Instruction to parse is ");
					puts(insn_names[i]);
					puts("Line:");
					puts(line_copy);
					puts("Line internally:");
					puts(line);
					goto error;
				}
			/*We know the location of an insn to be expanded and it is at loc.*/
			/*Crawl along the string. */
			num_commas_needed = insn_nargs[i]-1;
			/*If you have three arguments, you only need two commas.*/
			for(j = loc + insn_len; strlen(line+j)>0;j++){
				if(line[j] == ','){
					if(num_commas_needed <= 0) { /*Too many commas.*/
						puts(syntax_fail_pref);
						puts("\ntoo many arguments to insn");
						puts(insn_names[i]);
						puts("Line:");
						puts(line_copy);
						goto error;
					}
					num_commas_needed--;
				}
				if(line[j] == ';' || line[j] == '\n' || line[j] == '\0'){
					if(num_commas_needed > 0)
					{ /*Too many commas.*/
						puts(syntax_fail_pref);
						puts("Insn requires more arguments. Line:");
						puts(line_copy);
						puts("Loc on line internally:");
						puts(line + loc);
						goto error;
					}
					break; /*Out of this for loop.*/
				}
			}
			if(num_commas_needed > 0)
			{ /*Too many commas.*/
				puts(syntax_fail_pref);
				puts("insn:");
				puts(insn_names[i]);
				puts("requires more bytes of argument.");
				puts("it needs...");
				mutoa(buf3, insn_nargs[i]);
				puts(buf3);
				puts("arguments.");
				puts("Line:");
				puts(line_copy);
				goto error;
			}
			have_expanded = 1;
			{
				memcpy(buf1, line, loc);
				buf1[loc] = '\0';
				strcat((char*)buf1, insn_repl[i]);
				strcat(buf1, line+loc+insn_len);
				mstrcpy(line, buf1);
			}
		}
	}while(have_expanded);
 /*EOF INSN EXPANSION STAGE*/
 	return;
 	error:
 	puts(fail_msg);
 	exit(1);
}

static long incr;
static long incrdont;
static void do_bytes(char* metaproc){
	char* proc; /*VERY_BASIC_THINGS_IT_CAN_DO*/
	unsigned char byteval; 
	proc = metaproc + 5;
	do{

		while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
		if(int_checker(proc)){
			puts(syntax_fail_pref);
			puts("invalid integer literal for bytes. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
		byteval = strtoul(proc,NULL,0) & 255;
		fputbyte(byteval);
		/*Find the next comma.*/
		incr = strfind(proc, ",");
		incrdont = strfind(proc, ";");
		if(incr == -1) break;
		if(incrdont != -1 && incr > incrdont)break;
		proc += incr; 
		proc += 1; /*Skip the comma itself.*/
		if(strlen(proc) == 0){
			puts(syntax_fail_pref);
			puts("empty integer literal for bytes. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
	}while(strlen(proc) > 0);
	
}

static void do_shorts(char* metaproc){
	char* proc = metaproc + 6;
	unsigned short shortval;
	do{
		while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
		if(int_checker(proc)){
			puts(syntax_fail_pref);
			puts("invalid integer literal for shorts. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
		shortval = strtoul(proc,NULL,0);
		putshort(shortval);
		/*Find the next comma.*/
		incr = strfind(proc, ",");
		incrdont = strfind(proc, ";");
		if(incr == -1) break;
		if(incrdont != -1 &&
			incr > incrdont) break; /**/
		proc += incr; proc += 1; /*Skip the comma itself.*/
		if(strlen(proc) == 0){
			puts(syntax_fail_pref);
			puts("empty integer literal for shorts. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
	}while(strlen(proc) > 0);
}

static void do_longs(char* metaproc){
	char* proc = metaproc + 5;
	uint32_t shortval;
	do{
		while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
		if(int_checker(proc)){
			puts(syntax_fail_pref);
			puts("invalid integer literal for longs. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
		shortval = strtoul(proc,NULL,0);
		putlong(shortval);
		/*Find the next comma.*/
		incr = strfind(proc, ",");
		incrdont = strfind(proc, ";");
		if(incr == -1) break;
		if(incrdont != -1 &&
			incr > incrdont) break; /**/
		proc += incr; proc += 1; /*Skip the comma itself.*/
		if(strlen(proc) == 0){
			puts(syntax_fail_pref);
			puts("empty integer literal for longs. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
	}while(strlen(proc) > 0);
}

static void do_qwords(char* metaproc){
	char* proc = metaproc + 6;
	uint64_t shortval;
	do{
		while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
		if(int_checker(proc)){
			puts(syntax_fail_pref);
			puts("invalid integer literal for longs. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
		shortval = strtoull(proc,NULL,0);
		putqword(shortval);
		/*Find the next comma.*/
		incr = strfind(proc, ",");
		incrdont = strfind(proc, ";");
		if(incr == -1) break;
		if(incrdont != -1 &&
			incr > incrdont) break; /**/
		proc += incr; proc += 1; /*Skip the comma itself.*/

		if(strlen(proc) == 0){
			puts(syntax_fail_pref);
			puts("empty integer literal for qwords. Line:");
			puts(line_copy);
			puts("Line Internally:");
			puts(line);
			exit(1);
		}
	}while(strlen(proc) > 0);
}

static void do_section(char* metaproc){
	uint64_t dest;
	char* proc = metaproc + 7;
	while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
	if(strlen(proc) == 0){
		puts(syntax_fail_pref);
		puts("Cannot have empty SECTION tag.");
		exit(1);
	}
	
	if(int_checker(proc)){
		puts(syntax_fail_pref);
		puts("invalid integer literal for section. Line:");
		puts(line_copy);
		puts("Line Internally:");
		puts(line);
		exit(1);
	}
	dest = strtoull(proc, NULL, 0);
	if(dest == 0){
	/*Explicitly check to see if they actually typed zero.*/
		if(proc[0]!='0'  && npasses == 1)
		{
			puts(warn_pref);
			puts("Section tag at zero. Might be a bad number. Line:");
			puts(line_copy);
		}
	}
	outputcounter = dest;
}

static void do_fill(char* metaproc){
	uint64_t fillsize; 
	long next_comma; 
	unsigned char fillval;
	char* proc = metaproc + 4;
	while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
	if(strlen(proc) == 0){
		puts(syntax_fail_pref);
		puts("Cannot have empty fill tag.");
		exit(1);
	}
	if(int_checker(proc)){
		puts(syntax_fail_pref);
		puts("invalid integer literal for fill. Line:");
		puts(line_copy);
		puts("Line Internally:");
		puts(line);
		exit(1);
	}
	fillsize = strtoull(proc, NULL, 0);
	if(fillsize == 0){
		if(proc[0]!='0') /*Check if they actually typed zero.*/
		{
			puts(warn_pref);
			puts("fill tag size is zero. Might be a bad number. Line:");
			puts(line_copy);
		}
	}
	/**/
	next_comma = strfind(proc, ",");
	if(next_comma == -1) {
		puts(syntax_fail_pref);
		puts("Fill tag missing value. Line:");
		puts(line_copy);
		exit(1);
	}
	proc += next_comma + 1;
	while(proc[0] == ' ') proc++; /*Skip whitespace.*/ 
	if(int_checker(proc)){
		puts(syntax_fail_pref);
		puts("invalid integer literal for fill. Line:");
		puts(line_copy);
		puts("Line Internally:");
		puts(line);
		exit(1);
	}
	fillval = strtoul(proc, NULL, 0);
	if(fillval == 0) /*potential for a mistake*/
		if(proc[0]!='0') /*Did they actually MEAN zero?*/
			{
				puts(warn_pref);
				puts("fill tag value is zero. Might be a bad number. Line:");
				puts(line_copy);
			}
	for(;fillsize>0;fillsize--)
		fputbyte(fillval);
}



static void do_label(char* metaproc){
	/*Define a label with the text immediately after this.*/
	long next_semicolon;
	long begin_name;
	long end_name;
	char* macro_name; 
	char* macro_content;
	char is_overwriting;
	unsigned long index;
	unsigned long i;
	unsigned long q;
	is_overwriting = 0;
	macro_name = NULL;
	next_semicolon = -1;
	begin_name = 0;
	end_name = 0;
	if(nmacros >= SISA64_MAX_MACROS){
		puts(general_fail_pref);
		puts("asm_label cannot make a macro, there are too many. Line:");
		puts(line_copy);
		exit(1);
	}
	next_semicolon = strfind(metaproc, ";");
	begin_name = strlen("asm_label\\");
	if(next_semicolon == -1){
		puts("asm_label\\ requires a semicolon at the end! Line:");
		puts(line_copy);
		puts("Internal:");
		puts(line);
		puts("parsing position:");
		puts(metaproc);
		exit(1);
	}
	end_name = next_semicolon;
	if(begin_name - end_name == 0){ /*Generate a name dynamically.*/
		puts("asm_label\\ requires a name Line:");
		puts(line_copy);
		puts("Internal:");
		puts(line);
		puts("parsing position:");
		puts(metaproc);
		exit(1);
		exit(1);
	} else {
		macro_name= str_null_terminated_alloc(
						metaproc + begin_name,  
						end_name - begin_name
					);
	}
	mutoa(buf3, outputcounter - label_offset);
	if(!macro_name){
		puts(general_fail_pref); 
		puts("Failed Malloc."); exit(1);
	}
	/*
		Prevent macros from being defined which are illegal.
	*/

	if(strlen(macro_name) == 0){
		puts(syntax_fail_pref);
		puts("This asm_label macro has an EMPTY NAME. Invalid:");
		puts(line_copy);
		exit(1);
	}
	for(q = 0; q < strlen(macro_name); q++)
		if(!isalnum(macro_name[q]) && macro_name[q] != '_'){
			puts(syntax_fail_pref);
			puts("Illegal character in macro name:");
			putchar(macro_name[q]);
			puts("\nName:");
			puts(macro_name);
			puts("Line:");
			puts(line_copy);
			puts("Internally:");
			puts(line);
			
			exit(1);
		}
	if(!(isalpha(macro_name[0]) || macro_name[0] == '_')){
		puts(syntax_fail_pref);
		puts("This asm_label macro begins with a number!");
		puts(macro_name);
		puts("Line:");
		puts(line_copy);
		exit(1);
	}
	/*
		Prevent macros from over-writing instructions.
	*/
	for(i = 0; i < n_insns; i++){
		if(streq(insn_names[i], macro_name))
		{
			puts(syntax_fail_pref);
			puts("asm_label interferes with instruction");
			puts(insn_names[i]);
			puts("Its name is not a valid macro name.Line:");
			puts(line_copy);
			puts("Internally:");
			puts(line);
			exit(1);
		}
	}
	/*
		Check and make sure this is not a reserved name
	*/
	if(
		strprefix("asm_", macro_name) ||
		strprefix("ASM_", macro_name) ||
		streq("bytes", macro_name) ||
		streq("shorts", macro_name) ||
		streq("longs", macro_name) ||
		streq("qwords", macro_name) ||
		streq("section", macro_name) ||
		streq("fill", macro_name) ||
		streq("", macro_name)
	)
	{
		puts(syntax_fail_pref);

		puts("This macro deliberately attempts to define or trample a reserved name or character. You may not use this name:");
		puts(macro_name);
		exit(1);
	}

	macro_content = strcata(buf3,"");
	if(!macro_content){
		puts(general_fail_pref);
		puts("Failed Malloc");
		exit(1);
	}

	/*Search for identical macros.*/
	for(i = 0; i < nmacros; i++){
		if(streq(macro_name, variable_names[i])){
			is_overwriting = 1;
			if(npasses == 0)
			{
				if(!streq(macro_content, variable_expansions[i])){
					puts(general_fail_pref);
					puts("Cannot redefine already made label:");
					puts(macro_name);
					exit(1);
				}
			}
			if(i < nbuiltin_macros){
				puts(syntax_fail_pref);
				puts("attempted redefinition of critical macro, Line:");
				puts(line_copy);
				exit(1);
			}
			index = i;
		}
	}
	
	if(!is_overwriting){
		if(nmacros >= (SISA64_MAX_MACROS-1)) {
			puts(compil_fail_pref);
			puts("Too many macros. Cannot define another one. Line:");
			puts(line_copy);
			exit(1);
		}

		
		variable_names[nmacros] = macro_name;
		variable_expansions[nmacros++] =  macro_content;

		
		if(!variable_expansions[nmacros-1]){
			puts(general_fail_pref); puts("Failed Malloc."); exit(1);
		}
	} else {
		free(macro_name);
		free(macro_content);
		if(npasses == 1 && !(variable_is_redefining_flag[index]&1))
		{/*Ensure that the macro evaluates to the exact same piece of text as the last time.*/
			if(!streq(buf3, variable_expansions[index])){
				puts(compil_fail_pref);
				puts("Macro desync has occurred. Line:");
				puts(line_copy);
				puts("Internal:");
				puts(line);
				puts("Pass 1 value:");
				puts(variable_expansions[index]);
				puts("Pass 2 value:");
				puts(buf1);
				exit(1);
			}
		}
	}
/*End of asm_label definition portion*/
}


static int do_vardef(){
	long loc_pound, loc_pound2; 
	char* macro_name;
	char* macro_content;
	char is_overwriting; /*It's show time!*/
	unsigned long index;
	unsigned long i;
	unsigned long q;
	was_macro = 1;
	loc_pound = strfind(line, "#");
	loc_pound2 = 0;
	if(loc_pound == -1) {
		puts(syntax_fail_pref);
		puts("missing leading # in macro declaration. Line:"); 
		puts(line_copy); 
		goto error;
	}
	/*We have a macro.*/
	macro_name = line + loc_pound;
	if(strlen(macro_name) == 1){
		puts(syntax_fail_pref);
		puts("erroneous macro name length. Line:");
		puts(line_copy); 
		goto error;
	}
	macro_name += 1; loc_pound += 1; /*We use these again.*/
	loc_pound2 = strfind(macro_name, "#");
	if(loc_pound2 == -1) {
		puts(syntax_fail_pref);
		puts("missing second # in macro declaration. Line:");
		puts(line_copy); 
		goto error;
	}
	macro_name = str_null_terminated_alloc(macro_name, loc_pound2);
	if(!macro_name){
		puts(general_fail_pref); 
		puts("Failed Malloc."); 
		exit(1);
	}
	if(strlen(line + loc_pound + loc_pound2) < 2){
		puts(syntax_fail_pref);
		puts("macro without a definition. Line:"); 
		puts(line_copy);
		exit(1);
	}
	loc_pound2++;
	/*Search to see if we've already defined this macro*/
	is_overwriting = 0;
	index = 0;
	{for(i = 0; i < n_insns; i++){
		if(streq(insn_names[i], macro_name))
		{
			puts(syntax_fail_pref);
			puts("This macro would prevent instruction");
			puts(insn_names[i]); 
			puts("From being used. Line:");
			puts(line_copy);
			goto error;	
		}
	}}

	/*Conditional Declaration.*/
	if(macro_name[0] == '?'){
		mstrcpy(macro_name, macro_name+1);
		for(i = nbuiltin_macros; i < nmacros; i++)
			if(streq(macro_name, variable_names[i])){ /*Conditional declaration no longer accepted.*/
				free(macro_name);
				goto end;
			}
	}
	/*
		Prevent macros from being defined which are illegal.
	*/

	if(strlen(macro_name) == 0){
		puts(syntax_fail_pref);
		puts("This macro has an EMPTY NAME. Invalid:");
		puts(line_copy);
		goto error;
	}
	for(q = 0; q < strlen(macro_name); q++)
		if(!isalnum(macro_name[q]) && macro_name[q] != '_'){
			puts(syntax_fail_pref);
			puts("Illegal character in macro name:");
			putchar(macro_name[q]);
			puts("\nName:");
			puts(macro_name);
			puts("Line:");
			puts(line_copy);
			puts("Internally:");
			puts(line);
			goto error;
		}
	
	if(!(isalpha(macro_name[0]) || macro_name[0] == '_')){
		puts(syntax_fail_pref);
		puts("This macro begins with a number!");
		puts(macro_name);
		puts("Line:");
		puts(line_copy);
		exit(1);
		goto error;
	}

	/*
		Check and make sure this is not a reserved name
	*/
	if(
		strprefix("asm_", macro_name) ||
		strprefix("ASM_", macro_name) ||
		streq("bytes", macro_name) ||
		streq("shorts", macro_name) ||
		streq("longs", macro_name) ||
		streq("qwords", macro_name) ||
		streq("section", macro_name) ||
		streq("fill", macro_name) ||
		streq("", macro_name)
	)
	{
		puts(syntax_fail_pref);
		puts("This macro deliberately attempts to define or trample a reserved name or character. You may not use this name:");
		puts(macro_name);
		goto error;	
	}

	macro_content =	str_null_terminated_alloc(
					line+loc_pound+loc_pound2,
					strlen(line+loc_pound+loc_pound2)
			);
	if(!macro_content){
		puts(general_fail_pref);
		puts("Failed malloc");
		exit(1);
	}
	/*Search for identical macros.*/
	/*Search for identical macros.*/
	for(i = 0; i < nmacros; i++){
		if(streq(macro_name, variable_names[i])){
			is_overwriting = 1;
			if(npasses == 0)
			{
				if(!streq(macro_content, variable_expansions[i])){
					puts(general_fail_pref);
					puts("Cannot redefine already made macro:");
					puts(macro_name);
					exit(1);
				}
			}
			if(i < nbuiltin_macros){
				puts(syntax_fail_pref);
				puts("attempted redefinition of critical macro, Line:");
				puts(line_copy);
				exit(1);
			}
			index = i;
		}
	}


	
	if(!is_overwriting){
		if(nmacros >= (SISA64_MAX_MACROS-1)) {
			puts(compil_fail_pref);
			puts("Too many macros. Cannot define another one. Line:");
			puts(line_copy);
			exit(1);
		}
		variable_names[nmacros] = macro_name;
		variable_expansions[nmacros++] = macro_content;
		if(!variable_expansions[nmacros-1]){
			puts(general_fail_pref); 
			puts("Failed Malloc."); 
			exit(1);
		}
	} else { /*ELSE_EASY*/
		free(macro_name);
		mstrcpy(buf1, macro_content);
		free(macro_content);
		if(
			/*Second pass?*/
			npasses == 1 && 
			/*This variable is not marked already as redefining.*/
			!(variable_is_redefining_flag[index]&1)
		)
		{/*Ensure that the macro evaluates to the exact same piece of text as the last time.*/
			if(!streq(buf1, variable_expansions[index])){
				puts(compil_fail_pref);
				puts("Macro desync has occurred. Line:");
				puts(line_copy);
				puts("Internal:");
				puts(line);
				puts("Pass 1 value:");
				puts(variable_expansions[index]);
				puts("Pass 2 value:");
				puts(buf1);
				goto error;
			}
		}
	}
	return 0;
	end:
	return 1;
	error:
	puts(fail_msg);
	exit(1);
}


static void init_vars(){
	variable_names[0] = "%";
	variable_expansions[0] = "";
	variable_names[1] = "@";
	variable_expansions[1] = variable_expansions[0];
	variable_names[2] = ":";
	variable_expansions[2] = "asm_label\\";
	variable_names[3] = "$";
	variable_expansions[3] = variable_expansions[0];
	nmacros = 4;
	if(nbuiltin_macros != 4) exit(1);
}

static void handle_cli_args(int argc, char** argv){
	unsigned long i;
	if(argc < 2) goto ASSEMBLER_SHOW_HELP;
	/*for(i_cli_args = 2; i_cli_args < argc; i_cli_args++)*/

	
	i_cli_args = 2;
	/*if(!(i_cli_args < argc)) goto beyond_cli_arg_parse_loop;*/
	for(i_cli_args = 2; i_cli_args< argc; i_cli_args++)
	{
		if(strprefix("-o",argv[i_cli_args-1]))ofilename = argv[i_cli_args];
		if(strprefix("-i",argv[i_cli_args-1]))ifilename = argv[i_cli_args];
	}
	/*beyond_cli_arg_parse_loop:;*/



	/*for(i_cli_args = 1; i_cli_args < argc; i_cli_args++)*/
	i_cli_args = 1;
	cli_arg_parse_loop2:if(!(i_cli_args < argc)) goto beyond_cli_arg_parse_loop2;
	{
		if(strprefix("-C",argv[i_cli_args])){
			puts("SISA-64 Assembler and Emulator written by D[MHS]W for the Public Domain");
			puts("This program is Free Software that respects your freedom, you may trade it as you wish.");
			puts("Developer's original repository: https://github.com/gek169/SISA64");
			puts("Programmer Documentation for this software is provided in a .odt manual");
			puts("~~COMPILETIME ENVIRONMENT INFORMATION~~");
#ifdef __STDC_IEC_559__

#if __STDC_IEC_559__ == 0
		puts("Floating point compatibility of the environment is specifically not guaranteed.");
#else
		puts("Floating point compatibility of the environment is specifically guaranteed.");
#endif

#else
		puts("the STDC_IEC_559 define is not defined. Standard behavior is not guaranteed.");
#endif
		if(sizeof(void*) == 4) puts("Compiled for a 32 bit architecture.");
		if(sizeof(void*) == 8) puts("Compiled for a 64 bit architecture.");
		{i = 0;
			for(i=0; i<n_insns; i++)
			if(
				(insn_repl[i][strlen(insn_repl[i])-1] == ',' &&
				insn_nargs[i] == 0) ||
				(insn_repl[i][strlen(insn_repl[i])-1] == ';' &&
				insn_nargs[i] > 0)
			)
			{
				puts(internal_fail_pref);
				puts("insns.h corrupted. Check insn: \n");
				puts(insn_names[i]);
				puts(fail_msg);
				exit(1);
			}
		}
#if defined(__i386__) || defined(_M_X86) || defined(__i486__) || defined(__i586__) || defined(__i686__) || defined(__IA32__) || defined(__INTEL__) || defined(__386)
		puts("Compiled for x86.");
#endif
#if defined(__ia64__) || defined(_IA64) || defined(__ia64) || defined(_M_IA64) || defined(__itanium__)
		puts("Compiled for Itanium.")
#endif
#if defined(__m68k__) || defined(M68000) || defined(__MC68K__)
		puts("Compiled for the M68k.");
#endif
#if defined(__mips__) || defined(__mips) || defined(_MIPS_ISA) || defined(__MIPS__)
		puts("Compiled for MIPS.");
#endif
#if defined(__powerpc__) || defined(__powerpc) || defined(__powerpc64__) || defined(__POWERPC__) || defined(__ppc__) || defined(__ppc64__) || defined(_M_PPC) || defined(_ARCH_PPC) || defined(__PPCGECKO__) || defined(__PPCBROADWAY__) || defined(__ppc)
		puts("Compiled for PowerPC.");
#endif
#if defined(__THW_RS6000)
		puts("Compiled for RS/6000.");
#endif
#if defined(__sparc__) || defined(__sparc) || defined(__sparc_v8__) || defined(__sparc_v9__) || defined(__sparcv8) || defined(__sparcv9)
		puts("Compiled for sparc.");
#endif
#if defined(__sh__)
		puts("Compiled for SuperH.");
#endif
#if defined(__370__) || defined(__s390__) || defined(__s390x__) || defined(__zarch__) || defined(__SYSC_ZARCH__)
		puts("Compiled for SystemZ.");
#endif
#if defined(_TMS320C2XX) || defined(_TMS320C5X) || defined(_TMS320C6X)
		puts("Compiled for TMS320.");
#endif
#if defined(__TMS470__)
		puts("Compiled for TMS470.");
#endif
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
		puts("Compiled for x86_64.");
#endif
#if defined(__alpha__) || defined(_M_ALPHA)
		puts("Compiled for Alpha.");
#endif
#if defined(__arm__) || defined(_ARM) || defined(_M_ARM)|| defined(_M_ARMT) || defined(__thumb__) || defined(__TARGET_ARCH_ARM)|| defined(__TARGET_ARCH_THUMB)
		puts("Compiled for arm.");
#endif
#if defined(__aarch64__)
		puts("Compiled for aarch64.");
#endif
#if defined(__bfin)
		puts("Compiled for blackfin.");
#endif
#if defined(__convex__)
		puts("Compiled for convex.");
#endif
#if defined(__epiphany__)
		puts("Compiled for epiphany.");
#endif
#if defined(__hppa__) || defined(__HPPA__)
		puts("Compiled for hppa.");
#endif
#if defined(WIN32) || defined(_WIN32)
		puts("Compiled for Macrohard Doors.");
#endif
#if defined(__unix__)
		puts("Targetting *nix");
#endif
#if defined(linux) || defined(__linux__) || defined(__linux) || defined (_linux) || defined(_LINUX) || defined(__LINUX__)
			puts("Targetting Linux. Free Software Is Freedom.");
#endif
#if defined(__MINGW32__)
			puts("Compiled with MinGW");
#endif
#if defined(__TenDRA__)
			puts("Compiled with TenDRA.");
			exit(0);
#endif
#ifdef __INTEL_LLVM_COMPILER
			puts("Compiled with Intel LLVM.");
			exit(0);
#endif
#ifdef __cilk
			puts("Compiled with Cilk.");
			exit(0);
#endif
#ifdef __TINYC__
			puts("Compiled with TinyCC.");
			exit(0);
#endif
#ifdef _MSVC_VER
			puts("Compiled with MSVC.");
			exit(0);
#endif
#ifdef __SDCC
			puts("Compiled with SDCC.");
			exit(0);
#endif
#ifdef __clang__
			puts("Compiled with Clang.");
			exit(0);
#endif
#ifdef __GNUC__
			puts("Compiled with GCC. Free Software Is Freedom.");
			exit(0);
#endif
			puts("[Unknown C Compiler]");
			exit(0);
		}

		if(
			strprefix("-h",argv[i_cli_args]) ||
			strprefix("-v",argv[i_cli_args]) ||
			strprefix("--help",argv[i_cli_args]) ||
			strprefix("--version",argv[i_cli_args])
		){
			ASSEMBLER_SHOW_HELP:;
			{
				fputs("Usage: ",stdout);
				fputs(argv[0],stdout);
				fputs(" [ARGS...]\n",stdout);
				
			}
			puts("required argument: -i: specify input file.");
			puts("Optional argument: -o: specify output file. If not specified it is: a.bin");
			puts("Optional argument: -C: display compiletime environment information (What C compiler you used) as well as Author.");
			puts("Optional argument: -v, -h, --help, --version: This printout.");
			puts("\n\nSISA64 Macro Assembler");
			puts("Jesus of Nazareth is the Lord");
			puts("Authored by DMHSW for the Public Domain. Enjoy.\n\n");
			puts("Language information:");
			mutoa(buf3, n_insns);
			fputs("There are ",stdout);
			fputs(buf3, stdout);
			fputs(" instructions in the instruction set for this version.\n",stdout);

			fputs("The The last instruction added to the instruction set is ",stdout);
			fputs(insn_names[n_insns-1],stdout);
			putchar('\n');
			exit(0);
		}
		i_cli_args++;goto cli_arg_parse_loop2;
	}
	beyond_cli_arg_parse_loop2:;
	
}





int main(int argc, char** argv){
	long loc_vbar;
	unsigned long i;
	long comment_find_newline;
	long next_semicolon;
	long next_vbar;
	FILE* ftmp;
	init_vars();
	handle_cli_args(argc, argv);


	if(ifilename){
		infile = fopen(ifilename, "rb");
		if(!infile) {
			puts("Cannot open input file");
			puts(ifilename);
			return 1;
		}
	} else {
		puts(general_fail_pref);
		puts("No input files.");
		puts(fail_msg);
		exit(1);
	}
	ofile = NULL;


	for(
			npasses = 0; /*init*/
			npasses < 2; /*test*/
			/*increment, uses comma operator to make sequence points in an expression*/
			npasses++, 
			fseek(infile, 0, SEEK_SET),
		 	(outputcounter=0),
		 	(label_generator = 0),
		 	(label_offset = 0),
		 	(scope_is_active = 0)
		)
		{/*For loop, 2 passes*/
		while(1){ /*While loop fetching lines*/
			was_macro = 0;	/*Is this a VAR line or one of its syntactic variants?*/
			using_asciz = 0; /*Is this a string literal line which requires writing a zero at the end?*/
			if(feof(infile)){
				/*try popping from the fstack*/
				single_logical_line_mode = 0;
				
				if(include_level > 0){
					fclose(infile); infile = NULL;
					include_level -= 1;
					infile = fstack[include_level];
					continue;
				}
				/*else, break. End of pass.*/
				break;
			}
	
			/*LINE_FETCH_STAGE*/
			read_line_from_file(infile, &linesize, '\n'); /*Always suceeds.*/
			while(
				strprefix(" ",(char*)line)
				|| strprefix("\t",(char*)line)
				|| (isspace(line[0]) && line[0] != '\0')
			){mstrcpy(line, line+1);}
			
			top_line_fetch:;
			{
				/*if this is not a string literal (!) or directive (..) then
					search the line for the sequence "\//" and replace the first forward slash with a backslash zero.
				*/
				if(!strprefix("!",(char*)line) &&
					!strprefix("..",(char*)line)
				){
					long l;
					/*first, do an in-place replacement of double spaces and whatnot.*/
					while(perform_inplace_repl(line,"\t", " "));
					while(perform_inplace_repl(line,"\r", " "));
					while(perform_inplace_repl(line,"  ", " "));
					l = strfind(line, "\\//");
					if(l != -1){
						line[l] = '\0'; /*Keep the backslash.*/
						rut_append_to_me = 1;
						read_line_from_file(infile, &linesize, '\n');
						goto top_line_fetch;
					}
				}
	
				/*if this line ends in a backslash...*/
				if(
					!feof(infile) 
					&& strlen((char*)line) > 0 
					&& !strprefix("!",(char*)line) 
					&& line[strlen((char*)line)-1] == '\\'
				)
				{
					line[strlen((char*)line)-1] = '\0';
					rut_append_to_me = 1;
					read_line_from_file(infile, &linesize, '\n');
					goto top_line_fetch;
				}
				/*Preserve a copy of the line for displaying error messages.*/
				
				mstrcpy(line_copy, line);
	
				/*END_OF_LINE_FETCH_STAGE*/
			}
	
	
	
	
	
	
			/*handle comments...*/
			if(strprefix("#!",(char*)line)) goto end;

			if(strprefix("$}", line)){
				handle_dollar_close_ccb();
				goto end;
			}

			if(strprefix("${", line)){
				handle_dollar_open_ccb();
				goto end_of_syntax_sugar_eval;
			}
	
			remove_comments();
	
					/*SYNTAX_SUGAR_EVALUATION_STAGE_START*/
			if(strprefix("..zero:", (char*)line)){
				handle_dotdotzero();
				goto end_of_syntax_sugar_eval;
			}
	
			if(strprefix("..z:", (char*)line)){
	
				handle_dotdotz();
				goto end_of_syntax_sugar_eval;
			}

			if(strprefix("..usr:", line)){
				label_offset = outputcounter;
				mstrcpy(line, line + 6); 
				goto end_of_syntax_sugar_eval;
			}

			if(strprefix("..priv:", line)){
				label_offset = 0;
				mstrcpy(line, line + 7); 
				goto end_of_syntax_sugar_eval;
			}
	
			
			if(strprefix("..ascii:", (char*)line)){
				handle_ascii();
				goto end_of_syntax_sugar_eval;
			}
			if(strprefix("..asciz:", (char*)line)){
				handle_asciz();
				goto end_of_syntax_sugar_eval;
			}

	
			if(strprefix("..include\"", (char*)line)){
				handle_include();
				goto end_of_syntax_sugar_eval;
			}
			if(strprefix("..dinclude\"", (char*)line)){
	
				handle_dinclude();
				goto end_of_syntax_sugar_eval;
			}
			if(strprefix("..export\"", (char*)line)){
	
				handle_export();
				goto end;
			}
		
			/*syntactic sugar for VAR*/
			if(line[0] == '.'){
			
				handle_var_syntax_sugar();
				goto end_of_syntax_sugar_eval;
			} 
			/*Handle the standard assembly label syntax sugar, myLabel:*/
			if(
				!strprefix("!",line) && 
				!strprefix("VAR#",line) && 
				!strprefix("ASM_",line) && 
				!strprefix("asm_", line) &&
				!strprefix(".",line) &&
				!strprefix(":",line)
			){ /*Additional syntactic sugar for labels.*/
	
				handle_standard_label_syntax();
				goto end_of_syntax_sugar_eval;
			}
	
	
			end_of_syntax_sugar_eval:;
			
			/*SPECIAL_LINE_TYPE_EVALUATION_STAGE*/
			
			if(strprefix(end_single_logical_line_mode, line)){
				/*enable or disable single logical line mode.*/
				single_logical_line_mode = !single_logical_line_mode;
				/*Remove it from the string.*/
				mstrcpy(line, line + len_end_single_logical_line_mode);
			}
			

	
			if(strprefix("!",line)) {
				handle_exclamation_mark_string_literal();
				goto end;
			}
			if(strprefix("ASM_header ", line) || strprefix("asm_header ", line)){
	
				handle_asm_header();
				goto end;
			}
			if(strprefix("ASM_data_include ", line) || strprefix("asm_data_include ", line)){
				handle_asm_data_include();
				goto end;
			}
			/*EOF SPECIAL LINE TYPE EVALUATION STAGE*/
	
			
			/*Step 0: PRE-PRE PROCESSING. Yes, this is a thing.*/
			pre_pre_processing:
			while(
					strprefix(" ",line) 
					|| strprefix("\t",line)
					|| strprefix("\n",line)
					|| strprefix("\v",line)
					|| strprefix("\r",line)
					|| (isspace(line[0]) && line[0] != '\0')
			){ /*Remove preceding whitespace... we do this twice, actually...*/
				mstrcpy(line, line+1);
			}
			/*Find line comments... but only on non-VAR lines!*/
			/*Note that even though we already did this, we must do it again!*/
				/*TODO skip comments*/
			remove_comments();
	
			if(strlen(line) < 1) goto end; /*the line is empty*/
			/*TODO: make it so that the end of file character is not parsed. values above 0x7F are not permitted.*/
			if(strlen(line) == 1 && !isalpha(line[0])) goto end; /*Cannot possibly be a macro, it's the end of file thing.*/
			if(!isalpha(line[0]) && 
				line[0] != ' ' && 
				line[0] != ':' && 
				line[0] != '_' && 
				line[0] != ';' && line[0] != '\t'
				&& line[0] != '\\' && line[0] != '|' && 
				line[0] != '[' && line[0] != '=' && line[0] != '$'
				&& line[0] != '{' && line[0] != '}' ){
				{
					puts(syntax_fail_pref);
					puts("Line beginning with illegal character... Line:");
					puts(line_copy);
					goto error;
				}
				goto end;
			}
			/*Change all whitespace to normal spaces.*/
			{size_t i = 0;
				for(;i<strlen(line);i++)
					if(isspace(line[i]) || line[i] == '\r') line[i] = ' ';
			}
			
			if(strprefix("VAR#",line))
				was_macro=1; 
			else 
				was_macro = 0;
	
	
			/*MACRO_EXPANSION*/
			do_macro_expansion();
	
	
	
	
	
			/*Check again to see if this is a comment line.*/
			/*if(strprefix("#",line)) goto end;*/
				remove_comments();
				if(!strprefix("VAR#",line))
					while(strfind("#",(char*)line) != -1) {
						comment_find_newline = strfind(line,"\n");
						if(comment_find_newline == -1) goto end;
						/*this is a single logical line mode comment with a line comment in it.*/
						mstrcpy(
							line+strfind("#",line),
							line+comment_find_newline
						);
					}
			if(strprefix("!",line)) {
				puts("Invalid string literal. Line:");
				puts(line_copy);
				goto error;
			}
	
	
	
	
	
	
			
			/*Step 2: Check to see if this is a macro*/
			/*MACRO_DEFINITION_STAGE*/
			if(strprefix("VAR#",line)){
				was_macro = 1;
				if(do_vardef()) goto end;;
				
			}
			if(was_macro) goto end;
			/*
				Turn whitespace into single spaces.
			*/
	
			/*TODO: remove this*/
			
			while(perform_inplace_repl(line, "\t", " "));
			while(perform_inplace_repl(line, "\n", ";"));
			while(perform_inplace_repl(line, "\r", " "));
			while(perform_inplace_repl(line, "\v", " "));
			/*while(perform_inplace_repl(line, " ", ""));*/
			
			
			/*
			*/
			/*INSN_EXPANSION_STAGE*/
			do_insn_expansion();
			/*
				Put out bytes.
			*/
			/*Inch along*/
			if(printlines && npasses==1){
				loc_vbar = 0;
				loc_vbar = strfind(line, "|");
				if(loc_vbar != -1) line[loc_vbar] = '\0';
				puts(line);
				if(loc_vbar != -1) line[loc_vbar] = '|';
			}
	
	
			metaproc = line; /*declaration at beginning of main.*/
			/*ASSEMBLER_RAW_COMMANDS_PASS*/
			do{
				 /*Used to walk comma arguments.*/
				incr = 0;
				incrdont = 0;
				/*TODO rewrite to not use elses... in the assembly language port*/
				if(strlen(metaproc) == 0) break; /*Did the line just end right here?*/
				while(metaproc[0] == ' ') metaproc++; /*Skip whitespace.*/
				if(metaproc[0] == ';'){metaproc++; continue;} /*Skip empty command*/
				
				if(strprefix("bytes", metaproc)){
					do_bytes(metaproc);
				} else if(strprefix("shorts", metaproc)){
					do_shorts(metaproc);
				} else if(strprefix("longs", metaproc)){
					do_longs(metaproc);
				} else if(strprefix("qwords", metaproc)){
					do_qwords(metaproc);
				} else if(strprefix("section", metaproc)){
					do_section(metaproc);
				} else if(strprefix("fill", metaproc)){
					do_fill(metaproc);
				} else if(strprefix("asm_label\\", metaproc)){
					do_label(metaproc);
	
				} else if(strprefix("$|", metaproc)){
					/*do nothing, this is a leftover...*/
				} else if(strprefix("#", metaproc)){
					/*do nothing.*/
				} else if(strprefix("asm_print", metaproc)){
					if(npasses == 1 && !printlines)
					{
						puts("\nRequest to print status at this insn. STATUS:");
						puts("\nLine:");
						puts(line_copy);
						puts("\nLine Internally:");
						puts(line);
						mutoa_hex(buf3,outputcounter);
						puts("\nOutput Counter (hex)");
						puts(buf3);
					}
					
				} else if(strprefix("asm_vars", metaproc)){
					{
						puts("\n~~~~~~STATUS~~~~~~\n");
						puts("\nLine:");
						puts(line_copy);
						puts("\nLine Internally:");
						puts(line);
						mutoa_hex(buf3,outputcounter);
						puts("\nOutput Counter (hex)");
						puts(buf3);
						puts("\n<DUMPING SYMBOL TABLE>");
						for(i=nbuiltin_macros;i<nmacros;i++){
							if(i==nmacros) puts("Showing User Macros");
							mstrcpy(buf3, "VAR#");
							strcat(buf3, variable_names[i]);
							strcat(buf3,"#");
							strcat(buf3,variable_expansions[i]);
							strcat(buf3,"\n");
							puts(buf3);
						}
					}
				} else if (
					strprefix("ASM_EXPORT_HEADER", metaproc)
				){
					/*
						Create a header file for this compilation unit. That means exporting all macros
						that are not redefining.
					*/
					if(npasses == 1){
						
						mstrcpy(buf1, ofilename);
						strcat(buf1, ".s64sym");
						ftmp = fopen(buf1, "w");
						if(ftmp){
							for(i = nbuiltin_macros; i < nmacros; i++){
								if( variable_is_redefining_flag[i]&2 ){
									if(!(variable_is_redefining_flag[i]&1) ){
										mstrcpy(buf3, "VAR#");
										strcat(buf3, variable_names[i]);
										strcat(buf3,"#");
										strcat(buf3,variable_expansions[i]);
										strcat(buf3,"\n");
										fputs((char*)buf3,ftmp);
									} else {
										puts(compil_fail_pref);
										puts("Cannot Export a redefining symbol.\n");
										goto error;
									}
								}
							}
						}else{
							puts(general_fail_pref);
							puts("Cannot open file:");
							puts(buf1);
							exit(1);
						}
					}
				} else if(
					strprefix("asm_quit", metaproc) ||
					strprefix("asm_halt", metaproc)
				){
					if(npasses == 1){
						puts("\nHalting assembly. STATUS:");
						puts("\nLine:");
						puts(line_copy);
						puts("\nLine Internally:");
						puts(line);
						mutoa_hex(buf3,outputcounter);
						puts("\nOutput Counter (hex)");
						puts(buf3);
					}
					goto error;
				}else if(strprefix("!", metaproc)){
					puts(syntax_fail_pref);
	
					puts("Cannot put string literal here:");
					puts(line_copy);
					puts("\n\nSpecifically, here:");
					puts(metaproc);
					goto error;
				} else if( /*Invalid statement detected!*/
						strlen(metaproc) > 0 && 
						!(
							strfind(metaproc,";") == 0 ||
							strfind(metaproc,"|") == 0
						)
				){
						puts(syntax_fail_pref);
						puts("\ninvalid statement, Line:");
						puts(line_copy);
						puts("\nstatement:");
						puts(metaproc);
						puts("\nInternal representation:");
						puts(line);
						goto error;
				}
	
				
	
				/*Break out of the do while?*/
				{
					next_semicolon = strfind(metaproc, ";");
					next_vbar = strfind(metaproc, "|");
					if(next_semicolon == -1) break; /*We have handled all sublines.*/
					if(next_vbar!= -1 && next_vbar < next_semicolon) break; /*respect sequence point operator.*/
					metaproc += next_semicolon + 1;
					if(strlen(metaproc) == 0) break; /*Did the line just end right here?*/
				}
			} while(1); 
			/*END_OF_ASSEMBLER_RAW_COMMANDS_PASS*/
	
	
	
			
			/*if this is a line with vertical bars, start processing the stuff after the next vertical bar. */
			{
				next_vbar = strfind(line,"|");
				if(next_vbar != -1){
					next_vbar++;
					mstrcpy(line_copy, line + next_vbar);
					mstrcpy(line, line_copy); /*Added.*/
					goto pre_pre_processing;
				}
			}
	
	
			end:continue;
	
			
			error:
			puts(fail_msg);
			return 1;
		} /*While loop fetching lines*/
		} /*For loop, 2 passes*/
	
	
		/*
			TODO: do file writing here.
		*/
		if(infile)	{fclose(infile);infile = NULL;}
		{
			ofile=fopen(ofilename, "wb");
			if(!ofile){
				puts(general_fail_pref);
				puts("UNABLE TO OPEN OUTPUT FILE:");
				puts(ofilename);
				return 1;
			}
			fwrite(memimage, 1, nbytes_written, ofile);
			fflush(ofile);
			fclose(ofile);ofile = NULL;
			fputs("<ASM> Successfully assembled ", stdout);
			fputs(ofilename,stdout);
			fputs("\n",stdout);
		}
}
