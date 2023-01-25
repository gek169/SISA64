#include "be_encoder.h"
#include <string.h>


/*
	system memory size in BYTES.

	configured for 4 gigabytes
*/
#define SYS_MEMORY_SIZE	0x100000000
#define SYS_MEMORY_MASK	0x0FFFFFFFF
/*memory for the implementation.
	Due to the new mask implementation, we don't need the extra bytes at the end.

	
*/
extern uint8_t* sisa64_mem;



#ifdef __clang__
#define NOINLINE_THIS __attribute__ ((noinline))
#endif

#ifdef __GNUC__
#define NOINLINE_THIS __attribute__ ((noinline))
#endif

#ifdef __TINYC__
#define NOINLINE_THIS /*a comment*/
#endif

#ifndef NOINLINE_THIS
#define NOINLINE_THIS /*a comment*/
#endif


/*run the emulator*/
NOINLINE_THIS void sisa64_emulate();

/*interact with the device*/
NOINLINE_THIS uint64_t dev_read(uint64_t addr);
NOINLINE_THIS void dev_write(uint64_t addr, uint64_t val);
NOINLINE_THIS int di();
NOINLINE_THIS void dcl();


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	ISA helper functions to interact with memory.
//	Note that in order for the mask to be effective,
// the mask must always be greater than 7.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


/*
#define ADDR_VALIDATOR(l_off)\
				addr = (((addr) & ~mask) == 0)*addr;\
				if(l_off)addr = (((addr+l_off) & ~mask) == 0)*addr;
*/
/*
#define ADDR_VALIDATOR(l_off)\
		if( (addr > mask) || ((addr + l_off) > mask)   ) addr = 0;
*/
static inline uint64_t addr_validator(uint64_t addr, const uint64_t l_off, uint64_t max){
	if( (addr > max) || ((addr + l_off) > max)   ) return 0;
	return addr;
}


/*
#define ADDR_VALIDATOR(l_off) (void);
*/
static inline uint8_t mread_u8(uint64_t addr, uint8_t* membase, uint64_t mask){
	addr = addr_validator(addr, 0, mask);
	return membase[addr];
}

static inline void mwrite_u8(uint64_t addr, uint8_t val,  uint8_t* membase, uint64_t mask){
	addr = addr_validator(addr, 0, mask);

	membase[addr] = val;
}


//multi-byte reads and writes
static inline uint16_t mread_u16(uint64_t addr, uint8_t* membase, uint64_t mask){
	uint16_t rv;
	addr = addr_validator(addr, 1, mask);


	memcpy(&rv, membase +  (addr), 2);
	return be_to_u16(rv);
}

static inline void mwrite_u16(uint64_t addr, uint16_t val,  uint8_t* membase, uint64_t mask){
	val = u16_to_be(val);
	addr = addr_validator(addr, 1, mask);

	memcpy(membase +  (addr), &val, 2);
}

static inline uint32_t mread_u32(uint64_t addr, uint8_t* membase, uint64_t mask){
	uint32_t rv;
	addr = addr_validator(addr, 3, mask);



	memcpy(&rv, membase +  (addr), 4);
	return be_to_u32(rv);
}

static inline void mwrite_u32(uint64_t addr, uint32_t val,  uint8_t* membase, uint64_t mask){
	val = u32_to_be(val);
	addr = addr_validator(addr, 3, mask);

	memcpy(membase +  (addr), &val, 4);
}

static inline uint64_t mread_u64(uint64_t addr, uint8_t* membase, uint64_t mask){
	uint64_t rv;
	addr = addr_validator(addr, 7, mask);
	memcpy(&rv, membase +  (addr), 8);
	return be_to_u64(rv);
}

static inline void mwrite_u64(uint64_t addr, uint64_t val,  uint8_t* membase, uint64_t mask){
	val = u64_to_be(val);
	addr = addr_validator(addr, 7, mask);
	memcpy(membase +  (addr), &val, 8);
}


