

static const char* insn_names[89] = {
	"hlt",
	"nop",
	"become_user",
	"dread",
	"dwrite",
	"syscall",

	"im64",
	"im32",
	"im16",
	"im8",

	"ld64",
	"ld32",
	"ld16",
	"ld8",

	"st64",
	"st32",
	"st16",
	"st8",

	"ild64",
	"ild32",
	"ild16",
	"ild8",

	"ist64",
	"ist32",
	"ist16",
	"ist8",

	"mov",
	
	"lsh",
	"rsh",
	"and",
	"or",
	"xor",
	"compl",
	"neg",
	"bool",
	"not",
	"iadd",
	"isub",
	"imul",
	"udiv",
	"umod",
	"idiv",
	"imod",
	"se32",
	"se16",
	"se8",
	"ucmp",
	"icmp",
	"jmp",
	"jnz",
	"ret",
	"call",
	"getstp",
	"setstp",
	"push64",
	"push32",
	"push16",
	"push8",
	"pop64",
	"pop32",
	"pop16",
	"pop8",
	//float
	"itod",
	"itof",
	"dtoi",
	"ftoi",

	"dadd",
	"dsub",
	"dmul",
	"ddiv",

	"fadd",
	"fsub",
	"fmul",
	"fdiv",
	//float cmp
	"dcmp",
	"fcmp",
	//increment, decrement
	"inc",
	"dec",
	//stack-relative load and store
	"ldsp64",
	"ldsp32",
	"ldsp16",
	"ldsp8",
	"stsp64",
	"stsp32",
	"stsp16",
	"stsp8",
	"bswap64",
	"bswap32",
	"bswap16"
};


//how many bytes of arguments does the instruction take?
static const unsigned insn_nargs[89] = {
	//halt and nop,
	0,
	0,
	//become user
	4,
	//dread
	1,
	//dwrite
	2,
	//syscall
	0,
	//immediate loads
	9,
	5,
	3,
	2,
	//static loads all take a regid and a qword address.
	9,9,
	9,9,
	//static stores are the same
	9,9,
	9,9,
	//iloads and istores all take 2 regids
	2,2,2,2,
	2,2,2,2,
	//mov takes 2 regids
	2,
	//lsh,rsh,and,or,xor
		2,  2,  2, 2,  2,
	//compl, neg, bool, not,
		  1,   1,    1,   1,
	//iadd-umod
	2,2,2, //iadd, isub, imul
	2,2, //udiv,umod,
	2,2, //idiv,imod,

	//sign extend
	1,
	1,
	1,
	//ucmp and icmp take 3
	3,3,

	//jmp takes just a qword
	8,
	//jnz needs a regid and a qword...
	9,
	//ret needs nothing
	0,
	//call needs a qword
	8,

	//stack pointer manip needs a regid
	1,1,

	//all pushes and pops need a regid
	1,1,1,1,
	1,1,1,1,
	//floating point <-> int conversions need 1 regid
	1,1,1,1,
	//double and float ops need 2 regids
	2,2,2,2,
	2,2,2,2,
	//float compares need 3 regids
	3,3,

	//increment decrement both take a regid
	1,1,
	//stack relative load/store
	5,5,5,5,
	5,5,5,5,
	//bswap
	1,1,1
	//dummy
	//,0
};

static const char* insn_repl[89] = {
	"bytes0;",
	"bytes1;",
	//become user has arguments
	"bytes2,",
	//dread and dwrite have arguments
	"bytes3,",
	"bytes4,",
	//syscall has no arguments.
	"bytes5;",
	//imXX has args
	"bytes6,",
	"bytes7,",
	"bytes8,",
	"bytes9,",
	//ldXX has args
	"bytes10,",
	"bytes11,",
	"bytes12,",
	"bytes13,",	
	//stXX has args
	"bytes14,",
	"bytes15,",
	"bytes16,",
	"bytes17,",	

	//ildXX has args
	"bytes18,",
	"bytes19,",
	"bytes20,",
	"bytes21,",	
	//istXX has args
	"bytes22,",
	"bytes23,",
	"bytes24,",
	"bytes25,",
	//mov has args
	"bytes26,",
	//lsh,rsh have args
	"bytes27,",
	"bytes28,",
	//and,or,xor
	"bytes29,",
	"bytes30,",
	"bytes31,",
	//compl, neg, bool, not have args
	"bytes32,",
	"bytes33,",
	"bytes34,",
	"bytes35,",
	//iadd,isub,imul have args
	"bytes36,",
	"bytes37,",
	"bytes38,",
	//udiv,umod
	"bytes39,",
	"bytes40,",
	//idiv, imod
	"bytes41,",
	"bytes42,",
	//sign extend
	"bytes43,",
	"bytes44,",
	"bytes45,",
	//int compares
	"bytes46,",
	"bytes47,",
	//jumps
	"bytes48,",
	"bytes49,", //JNZ
	//ret
	"bytes50;",
	//call
	"bytes51,",
	//getstp/setstp
	"bytes52,",
	"bytes53,",
	//pushXX
	"bytes54,",
	"bytes55,",
	"bytes56,",
	"bytes57,",
	//popXX
	"bytes58,",
	"bytes59,",
	"bytes60,",
	"bytes61,",
	//float <-> int
	"bytes62,",
	"bytes63,",
	"bytes64,",
	"bytes65,",
	//double ops
	"bytes66,",
	"bytes67,",
	"bytes68,",
	"bytes69,",
	//float ops
	"bytes70,",
	"bytes71,",
	"bytes72,",
	"bytes73,",
	//float compares
	"bytes74,",
	"bytes75,",
	//inc dec
	"bytes76,",
	"bytes77,",
	//,"dummy"
	//stack relative load
	"bytes78,",
	"bytes79,",
	"bytes80,",
	"bytes81,",
	//stack relative store
	"bytes82,",
	"bytes83,",
	"bytes84,",
	"bytes85,",
	//bswap64,32,16
	"bytes86,",
	"bytes87,",
	"bytes88,"
};
static const unsigned n_insns = 89;
