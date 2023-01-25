

#include "sisa64.h"
//this is used only for exit, which itself, is only used for checking size of float types.


#define EAT_BYTE() mread_u8(pc++, membase, mem_max)
#define EAT_SHORT() mread_u16((pc=pc+2)-2, membase, mem_max)
#define EAT_LONG() mread_u32((pc=pc+4)-4, membase, mem_max)
#define EAT_QWORD() mread_u64((pc=pc+8)-8, membase, mem_max)


/*
	ISA helper functions for reading and writing memory
*/

#define READ8(addr) mread_u8(addr, membase, mem_max)
#define READ16(addr) mread_u16(addr, membase, mem_max)
#define READ32(addr) mread_u32(addr, membase, mem_max)
#define READ64(addr) mread_u64(addr, membase, mem_max)

#define WRITE8(addr,val)  mwrite_u8(addr, val,membase, mem_max)
#define WRITE16(addr,val) mwrite_u16(addr, val,membase, mem_max)
#define WRITE32(addr,val) mwrite_u32(addr, val,membase, mem_max)
#define WRITE64(addr,val) mwrite_u64(addr, val,membase, mem_max)

enum{
	ERROK=0,
	ERR_PRIV_CHANGE=1,
	ERR_PREEMPTED=2,
	ERR_DEVICE=3,
	ERR_SYSCALL=4,
	ERR_INTMATH=0x10, //dividing by zero?
	ERR_FLOATMATH=0x20, //dividing by zero?
};


#define PREEMPT_MAX 0x1000000

uint8_t* sisa64_mem = NULL;


void sisa64_emulate(){
	uint64_t gpregs[256];
	register uint64_t pc = 0; 
	register uint64_t stp = 0;
	register uint8_t is_user = 0; /*privileged or not?*/
	uint8_t Rcode = 0; /*error code- used for */
	register uint8_t* membase = NULL;
	register uint64_t mem_max = SYS_MEMORY_MASK;
	register uint64_t instruction_counter = 0;
	uint64_t saved_pc;
	uint64_t saved_stp;
	uint64_t regfile_loc; /*saved*/
	uint8_t maxsaved_regs;	
	memset(gpregs, 0, sizeof(gpregs));

	//256 possible values in a byte, 256 opcodes (illegal or otherwise), 256 entries in the opcode table.
	const void* opcodes[256] = {

		/*PRIVELEGE AND TECHNICAL*/

		&&J_HLT,
		&&J_NOP, //0 width
		&&J_BECOME_USER, //4 regids
		
		&&J_DEVREAD,&&J_DEVWRITE,
		&&J_SYSCALL, //effectively just "become not user" for user mode.


		&&J_LITERAL64,&&J_LITERAL32,&&J_LITERAL16,
		&&J_LITERAL8,

		&&J_LOAD64,&&J_LOAD32,&&J_LOAD16,
		&&J_LOAD8,

		&&J_STORE64,&&J_STORE32,&&J_STORE16,
		&&J_STORE8,


		&&J_ILOAD64,&&J_ILOAD32,&&J_ILOAD16,
		&&J_ILOAD8,

		&&J_ISTORE64,&&J_ISTORE32,&&J_ISTORE16,
		&&J_ISTORE8,

		//integer ops
		&&J_MOV,
		&&J_LSH,&&J_RSH,&&J_AND,
		&&J_OR,&&J_XOR,&&J_COMPL,&&J_NEG,
		&&J_BOOL,&&J_NOT,

		&&J_IADD,
		&&J_ISUB,
		&&J_IMUL,
		&&J_UDIV,
		&&J_UMOD,
		&&J_IDIV,
		&&J_IMOD,
		//sign extension
		&&J_SE32,
		&&J_SE16,
		&&J_SE8,

		//compare
		&&J_UCMP,
		&&J_ICMP,

		&&J_JUMP,
		&&J_JNZ,
		&&J_RET,
		&&J_CALL,

		//stack
		//getstp, setstp
		&&J_GETSTP,&&J_SETSTP,

		&&J_PUSH64,
		&&J_PUSH32,
		&&J_PUSH16,
		&&J_PUSH8,

		&&J_POP64,
		&&J_POP32,
		&&J_POP16,
		&&J_POP8,

		//TODO: implement floating point code.


		//conversion to and from floating point types
		&&J_ITOD,
		&&J_ITOF,
		&&J_DTOI,
		&&J_FTOI,
		//the 4 double
		&&J_DADD,
		&&J_DSUB,
		&&J_DMUL,
		&&J_DDIV,
		//the 4 float
		&&J_FADD,
		&&J_FSUB,
		&&J_FMUL,
		&&J_FDIV,
		//compares
		&&J_DCMP,
		&&J_FCMP,
		//inc dec
		&&J_INC,
		&&J_DEC,

		&&J_LDSP64,
		&&J_LDSP32,
		&&J_LDSP16,
		&&J_LDSP8,

		&&J_STSP64,
		&&J_STSP32,
		&&J_STSP16,
		&&J_STSP8,

		&&J_BSWAP64,
		&&J_BSWAP32,
		&&J_BSWAP16,

		&&J_MNZ,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,

		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,

		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,
		&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT,&&J_HLT

		//,&&J_HLT
	};

	membase = sisa64_mem;
	
	goto J_NOP;

	#define DISPATCH()  instruction_counter++;\
						if(is_user)\
							if(instruction_counter > PREEMPT_MAX)\
								goto PREEMPTED;\
						goto *opcodes[EAT_BYTE()]

	
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//PRIVILEGE HANDLING
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


	PREEMPTED:;
		Rcode = ERR_PREEMPTED; /*preempted!*/
		goto BECOME_PRIVILEGED;

	J_SYSCALL:;
		Rcode = ERR_SYSCALL;
		//goto BECOME_PRIVILEGED;
	BECOME_PRIVILEGED:;
		if(is_user == 0) goto J_HLT;
		is_user = 0;
		membase = sisa64_mem;
		mem_max = SYS_MEMORY_MASK;
		/*store the registerfile*/
		for(int i = 0; i <= maxsaved_regs; i++) WRITE64(regfile_loc+i*8,gpregs[i]);
		WRITE64(regfile_loc+((uint32_t)maxsaved_regs+1)*8,pc);
		WRITE64(regfile_loc+((uint32_t)maxsaved_regs+2)*8,stp);
		/*Prepare for continued privileged execution*/
		gpregs[0] = Rcode;
		Rcode = 0;
		//instruction_counter = 0;
		pc = saved_pc; //restore privileged program counter.
		stp = saved_stp; //restore privileged stack pointer
	DISPATCH();



	/*launch the user process with offset and mask given in first two registers, and register file stored at address in third register.
		the instruction counter value is given in a third register.
	*/
	J_BECOME_USER:;
	if(is_user){
		Rcode = ERR_PRIV_CHANGE; /*privilege change fault*/
		goto BECOME_PRIVILEGED;
	}
	{
		uint64_t new_offset;
		uint64_t new_max;
		new_offset = gpregs[EAT_BYTE()];
		new_max = gpregs[EAT_BYTE()];
		regfile_loc = gpregs[EAT_BYTE()];
		instruction_counter = PREEMPT_MAX - gpregs[EAT_BYTE()];
		maxsaved_regs = EAT_BYTE();

		for(int i = 0; i <= maxsaved_regs; i++) gpregs[i] = READ64(regfile_loc+i*8);
		saved_pc = pc;
		saved_stp = stp;
		pc = READ64(regfile_loc+((uint32_t)maxsaved_regs + 1)*8);
		stp = READ64(regfile_loc+((uint32_t)maxsaved_regs + 2)*8);

		membase = sisa64_mem + new_offset;
		mem_max = new_max;

		is_user = 1;
		Rcode = 0;
		instruction_counter = 0;
	}
	DISPATCH();

	J_DEVREAD:;
	if(is_user){
		Rcode = ERR_DEVICE; /*attempted dread/write in user mode*/
		goto BECOME_PRIVILEGED;
	}
	{
		uint64_t addr;
		uint8_t regid;
		regid = EAT_BYTE();
		addr = gpregs[regid];
		gpregs[regid] = dev_read(addr);
	}
	DISPATCH();

	J_DEVWRITE:;
	if(is_user){
		Rcode = ERR_DEVICE; /*attempted dread/write in user mode*/
		goto BECOME_PRIVILEGED;
	}
	{
		uint64_t addr;
		uint64_t val;
		uint8_t regid;
		uint8_t regid_val;
		regid = EAT_BYTE();
		regid_val = EAT_BYTE();
		addr = gpregs[regid];
		val = gpregs[regid_val];
		dev_write(addr,val);
	}
	DISPATCH();


	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//LITERALS, LOADS, AND STORES
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	J_LITERAL64:;
	{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = EAT_QWORD();
	}
	DISPATCH();

	J_LITERAL32:;
	{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = EAT_LONG();
	}
	DISPATCH();

	J_LITERAL16:;
	{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = EAT_SHORT();
	}
	DISPATCH();

	J_LITERAL8:;
	{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = EAT_BYTE();
	}
	DISPATCH();


	/*loads*/

	
	J_LOAD64:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = READ64(EAT_QWORD());
	}
	DISPATCH();

	J_LOAD32:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = READ32(EAT_QWORD());
	}
	DISPATCH();

	J_LOAD16:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = READ16(EAT_QWORD());
	}
	DISPATCH();

	J_LOAD8:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = READ8(EAT_QWORD());
	}
	DISPATCH();

	/*stores*/

	J_STORE8:{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE8(EAT_QWORD(),gpregs[regid]);
	}
	DISPATCH();

	J_STORE16:{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE16(EAT_QWORD(),gpregs[regid]);
	}
	DISPATCH();

	J_STORE32:{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE32(EAT_QWORD(),gpregs[regid]);
	}
	DISPATCH();

	J_STORE64:{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE64(EAT_QWORD(),gpregs[regid]);
	}
	DISPATCH();


	/*indirect loads*/

	J_ILOAD8:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] = READ8(gpregs[regid_src]);
	}
	DISPATCH();
	
	J_ILOAD16:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] = READ16(gpregs[regid_src]);
	}
	DISPATCH();

	J_ILOAD32:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] = READ32(gpregs[regid_src]);
	}
	DISPATCH();
	
	J_ILOAD64:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] = READ64(gpregs[regid_src]);
	}
	DISPATCH();

	/*indirect stores.*/

	J_ISTORE8:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		WRITE8(gpregs[regid_dest],gpregs[regid_src]);
	}
	DISPATCH();

	J_ISTORE16:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		WRITE16(gpregs[regid_dest],gpregs[regid_src]);
	}
	DISPATCH();	

	J_ISTORE32:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		WRITE32(gpregs[regid_dest],gpregs[regid_src]);
	}
	DISPATCH();

	J_ISTORE64:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		WRITE64(gpregs[regid_dest],gpregs[regid_src]);
	}
	DISPATCH();


	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//INTEGER MATH
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	/*integer math and register-to-register transfers.*/
	J_MOV:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] = gpregs[regid_src];
	}
	DISPATCH();	


	
	J_LSH:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] <<= gpregs[regid_src];
	}
	DISPATCH();	

	J_RSH:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] >>= gpregs[regid_src];
	}
	DISPATCH();

	J_AND:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] &= gpregs[regid_src];
	}
	DISPATCH();	

	J_OR:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] |= gpregs[regid_src];
	}
	DISPATCH();

	J_XOR:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] ^= gpregs[regid_src];
	}
	DISPATCH();

	J_COMPL:;
	{
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		gpregs[regid_dest] = ~(uint64_t)gpregs[regid_dest];
	}
	DISPATCH();

	J_NEG:;
	{
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		gpregs[regid_dest] = -1 * (int64_t)gpregs[regid_dest];
	}
	DISPATCH();

	J_BOOL:;
	{
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		gpregs[regid_dest] = !!gpregs[regid_dest];
	}
	DISPATCH();	

	J_NOT:;
	{
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		gpregs[regid_dest] = !gpregs[regid_dest];
	}
	DISPATCH();

	J_IADD:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] += gpregs[regid_src];
	}
	DISPATCH();

	J_ISUB:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] -= gpregs[regid_src];
	}
	DISPATCH();


	J_IMUL:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		gpregs[regid_dest] *= gpregs[regid_src];
	}
	DISPATCH();

	J_UDIV:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		uint64_t divisee;
		uint64_t divisor;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();

		divisee = gpregs[regid_dest];
		divisor = gpregs[regid_src];
		
		if(divisor == 0){
			Rcode = ERR_INTMATH; /*Integer math error*/
			goto BECOME_PRIVILEGED;
		}
		gpregs[regid_dest] = divisor / divisee;
	}
	DISPATCH();

	J_IDIV:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		int64_t divisee;
		int64_t divisor;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();

		divisee = gpregs[regid_dest];
		divisor = gpregs[regid_src];
		
		if(divisor == 0){
			Rcode = ERR_INTMATH; /*Integer math error*/
			goto BECOME_PRIVILEGED;
		}
		gpregs[regid_dest] = divisor / divisee;
	}
	DISPATCH();


	J_UMOD:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		uint64_t divisee;
		uint64_t divisor;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();

		divisee = gpregs[regid_dest];
		divisor = gpregs[regid_src];
		
		if(divisor == 0){
			Rcode = ERR_INTMATH; /*Integer math error*/
			goto BECOME_PRIVILEGED;
		}
		gpregs[regid_dest] = divisor % divisee;
	}
	DISPATCH();

	J_IMOD:;
	{
		uint8_t regid_src;
		uint8_t regid_dest;
		int64_t divisee;
		int64_t divisor;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();

		divisee = gpregs[regid_dest];
		divisor = gpregs[regid_src];
		
		if(divisor == 0){
			Rcode = ERR_INTMATH; /*Integer math error*/
			goto BECOME_PRIVILEGED;
		}
		gpregs[regid_dest] = divisor % divisee;
	}
	DISPATCH();

	J_SE8:;{
		uint8_t regid;
		int8_t val;
		int64_t val64;
		regid = EAT_BYTE();
		val = gpregs[regid];
		val64 = val;
		gpregs[regid] = val64;
	}
	DISPATCH();
	
	J_SE16:;{
		uint8_t regid;
		int16_t val;
		int64_t val64;
		regid = EAT_BYTE();
		val = gpregs[regid];
		val64 = val;
		gpregs[regid] = val64;
	}
	DISPATCH();	

	J_SE32:;{
		uint8_t regid;
		int32_t val;
		int64_t val64;
		regid = EAT_BYTE();
		val = gpregs[regid];
		val64 = val;
		gpregs[regid] = val64;
	}
	DISPATCH();

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//COMPARE
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	J_UCMP:;{
		uint8_t targ_reg;
		uint64_t val1;
		uint64_t val2;
		targ_reg = EAT_BYTE();

		val1 = gpregs[EAT_BYTE()];
		val2 = gpregs[EAT_BYTE()];
		gpregs[targ_reg] = (val1 > val2) * 1 +
							(val1 < val2) * (int64_t)-1;
	}
	DISPATCH();

	J_ICMP:;{
		uint8_t targ_reg;
		int64_t val1;
		int64_t val2;
		targ_reg = EAT_BYTE();
		val1 = gpregs[EAT_BYTE()];
		val2 = gpregs[EAT_BYTE()];
		gpregs[targ_reg] = (val1 > val2) * 1 +
							(val1 < val2) * (int64_t)-1;
	}
	DISPATCH();
	

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	/*
		JUMPS, BRANCHES, CALLS
	*/
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	J_JUMP:;
	{
		uint64_t val;
		val = EAT_QWORD();
		pc = val;
	}
	DISPATCH();

	J_JNZ:;
	{
		uint64_t dest;
		uint8_t regid;
		regid = EAT_BYTE();
		dest = EAT_QWORD();
		if(gpregs[regid] != 0)
			pc = dest;
	}
	DISPATCH();



	J_RET:;
	{
		stp -= 8;
		pc = READ64(stp);
	}
	DISPATCH();



	J_CALL:;{
		uint64_t dest;
		dest = EAT_QWORD();
		WRITE64(stp,pc);
		stp += 8;
		pc = dest;
	}
	DISPATCH();

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//PUSH, POP
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	J_GETSTP:;{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = stp;
	}
	DISPATCH();
	
	J_SETSTP:;{
		uint8_t regid;
		regid = EAT_BYTE();
		stp = gpregs[regid];
	}
	DISPATCH();
	
	J_PUSH64:;{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE64(stp,gpregs[regid]);
		stp += 8;
	}
	DISPATCH();

	J_PUSH32:;{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE32(stp,gpregs[regid]);
		stp += 4;
	}
	DISPATCH();	

	J_PUSH16:;{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE16(stp,gpregs[regid]);
		stp += 2;
	}
	DISPATCH();

	J_PUSH8:;{
		uint8_t regid;
		regid = EAT_BYTE();
		WRITE8(stp,gpregs[regid]);
		stp += 1;
	}
	DISPATCH();

	J_POP64:;{
		uint8_t regid;
		regid = EAT_BYTE();
		stp -= 8;
		gpregs[regid] = READ64(stp);
	}
	DISPATCH();

	J_POP32:;{
		uint8_t regid;
		regid = EAT_BYTE();
		stp -= 4;
		gpregs[regid] = READ32(stp);
	}
	DISPATCH();

	J_POP16:;{
		uint8_t regid;
		regid = EAT_BYTE();
		stp -= 2;
		gpregs[regid] = READ16(stp);
	}
	DISPATCH();	

	J_POP8:;{
		uint8_t regid;
		regid = EAT_BYTE();
		stp -= 1;
		gpregs[regid] = READ8(stp);
	}
	DISPATCH();

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//FLOATING POINT
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	J_ITOD:;{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = (int64_t)f64_rto_i64((double) ((int64_t)gpregs[regid]) );
	}
	DISPATCH();

	J_ITOF:;{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = (int64_t)f32_rto_i32((float) ((int32_t)gpregs[regid]));
	}
	DISPATCH();

	J_DTOI:;{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = (int64_t)u64_rto_f64(gpregs[regid]);
	}
	DISPATCH();

	J_FTOI:;{ /*FTOI sign extends...*/
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = (int64_t)u32_rto_f32(gpregs[regid]);
	}
	DISPATCH();

	J_DADD:;{
		f64 a;
		f64 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u64_rto_f64(gpregs[regid_dest]);
		b = u64_rto_f64(gpregs[regid_src]);
		a = a + b;
		gpregs[regid_dest] = f64_rto_i64(a);
	}
	DISPATCH();

	J_DSUB:;{
		f64 a;
		f64 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u64_rto_f64(gpregs[regid_dest]);
		b = u64_rto_f64(gpregs[regid_src]);
		a = a - b;
		gpregs[regid_dest] = f64_rto_i64(a);
	}
	DISPATCH();

	J_DMUL:;{
		f64 a;
		f64 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u64_rto_f64(gpregs[regid_dest]);
		b = u64_rto_f64(gpregs[regid_src]);
		a = a * b;
		gpregs[regid_dest] = f64_rto_i64(a);
	}
	DISPATCH();

	J_DDIV:;{
		f64 a;
		f64 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u64_rto_f64(gpregs[regid_dest]);
		b = u64_rto_f64(gpregs[regid_src]);
		if(b == 0.0 || b == -0.0){
			Rcode = ERR_FLOATMATH;
			goto BECOME_PRIVILEGED;
		}
		a = a / b;
		gpregs[regid_dest] = f64_rto_i64(a);
	}
	DISPATCH();

	J_FADD:;{
		f32 a;
		f32 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u32_rto_f32(gpregs[regid_dest]);
		b = u32_rto_f32(gpregs[regid_src]);
		a = a + b;
		gpregs[regid_dest] = (int64_t)f32_rto_i32(a);
	}
	DISPATCH();	

	J_FSUB:;{
		f32 a;
		f32 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u32_rto_f32(gpregs[regid_dest]);
		b = u32_rto_f32(gpregs[regid_src]);
		a = a - b;
		gpregs[regid_dest] = (int64_t)f32_rto_i32(a);
	}
	DISPATCH();	

	J_FMUL:;{
		f32 a;
		f32 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u32_rto_f32(gpregs[regid_dest]);
		b = u32_rto_f32(gpregs[regid_src]);
		a = a * b;
		gpregs[regid_dest] = (int64_t)f32_rto_i32(a);
	}
	DISPATCH();

	J_FDIV:;{
		f32 a;
		f32 b;
		uint8_t regid_dest;
		uint8_t regid_src;
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		a = u32_rto_f32(gpregs[regid_dest]);
		b = u32_rto_f32(gpregs[regid_src]);
		if(b == 0.0 || b == -0.0){
			Rcode = ERR_FLOATMATH;
			goto BECOME_PRIVILEGED;
		}
		a = a / b;
		gpregs[regid_dest] = (int64_t)f32_rto_i32(a);
	}
	DISPATCH();

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//FLOATING POINT COMPARE
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	J_DCMP:;{
		f64 val1;
		f64 val2;
		uint8_t regid_a;
		uint8_t regid_b;
		uint8_t regid_d;
		regid_d = EAT_BYTE();
		regid_a = EAT_BYTE();
		regid_b = EAT_BYTE();
		val1 = u64_rto_f64(gpregs[regid_a]);
		val2 = u64_rto_f64(gpregs[regid_b]);
		
		gpregs[regid_d] = (val1 > val2) * 1 +
									(val1 < val2) * (int64_t)-1;
	}
	DISPATCH();	

	J_FCMP:;{
		f32 val1;
		f32 val2;
		uint8_t regid_a;
		uint8_t regid_b;
		uint8_t regid_d;
		regid_d = EAT_BYTE();
		regid_a = EAT_BYTE();
		regid_b = EAT_BYTE();
		val1 = u32_rto_f32(gpregs[regid_a]);
		val2 = u32_rto_f32(gpregs[regid_b]);
		
		gpregs[regid_d] = (val1 > val2) * 1 +
									(val1 < val2) * (int64_t)-1;
	}
	DISPATCH();


	J_INC:;{
		uint8_t regid_d;
		regid_d = EAT_BYTE();
		gpregs[regid_d]++;
	}
	DISPATCH();


	J_DEC:;{
		uint8_t regid_d;
		regid_d = EAT_BYTE();
		gpregs[regid_d]--;
	}
	DISPATCH();

	J_LDSP64:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		gpregs[regid] = READ64(stp - off);
	}
	DISPATCH();
	J_LDSP32:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		gpregs[regid] = READ32(stp - off);
	}
	DISPATCH();
	J_LDSP16:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		gpregs[regid] = READ16(stp - off);
	}
	DISPATCH();
	J_LDSP8:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		gpregs[regid] = READ8(stp - off);
	}
	DISPATCH();

	J_STSP64:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		WRITE64(stp - off, gpregs[regid]);
	}
	DISPATCH();
	J_STSP32:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		WRITE32(stp - off, gpregs[regid]);
	}
	DISPATCH();
	J_STSP16:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		WRITE16(stp - off, gpregs[regid]);
	}
	DISPATCH();
	J_STSP8:{
		uint8_t regid;
		uint32_t off;
		regid = EAT_BYTE();
		off = EAT_LONG();
		WRITE8(stp - off, gpregs[regid]);
	}
	DISPATCH();
	J_BSWAP64:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = u64_bswap(gpregs[regid]);
	}
	DISPATCH();
	J_BSWAP32:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = u32_bswap(gpregs[regid]);
	}
	DISPATCH();
	J_BSWAP16:{
		uint8_t regid;
		regid = EAT_BYTE();
		gpregs[regid] = u16_bswap(gpregs[regid]);
	}
	DISPATCH();

	J_MNZ:; //move if not zero
	{
/*
		uint8_t regid_dest;
		uint8_t regid_src;
		uint8_t regid_testme;
		regid_testme = EAT_BYTE();
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		if(gpregs[regid_testme] != 0)
			gpregs[regid_dest] = gpregs[regid_src];
*/
		uint8_t regid_dest;
		uint8_t regid_src;
		uint8_t regid_testme;
        uint64_t test;
		regid_testme = EAT_BYTE();
		regid_dest = EAT_BYTE();
		regid_src = EAT_BYTE();
		test = (gpregs[regid_testme] != 0) ;
		gpregs[regid_dest] = 
			test * 
            	gpregs[regid_src]
            +
            (!test) * 
            	gpregs[regid_dest]
        
        ;
	}
	DISPATCH();

	
	J_NOP:DISPATCH();

	J_HLT:;
	Rcode = ERROK;
	if(is_user) 
		goto BECOME_PRIVILEGED;


	{
		return;
	}
}
