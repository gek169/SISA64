#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef double f64;
typedef float f32;

/*This will get inlined away on an optimizing compiler.*/
static inline int is_be(){
    union{
        uint32_t i;
        char c[4];
    } b;
    b.i = 0x01020304;
    if(b.c[0] == 1) return 1;
    return 0;
}

/**/
static inline uint16_t u16_to_be(uint16_t s){
	uint16_t targ;
    if(is_be()) return s;
	/*if this is a big-endian architecture, do nothing.*/
    targ = 0;
    targ |= (s & 0x00FF)<<8;
    targ |= (s & 0xFF00)>>8;
    return targ;
}



static inline uint32_t u32_to_be(uint32_t s){
    uint32_t targ;
    if(is_be()) return s;
	/*if this is a big-endian architecture, do nothing.*/
    targ = 0;
    targ |= (s & 0x000000FF)<<24;
    targ |= (s & 0x0000FF00)<<8;
    targ |= (s & 0x00FF0000)>>8;
    targ |= (s & 0xFF000000)>>24;
    return targ;
}

static inline uint64_t u64_to_be(uint64_t s){
    uint64_t targ;
    if(is_be()) return s;
	/*if this is a big-endian architecture, do nothing.*/
    targ = 0;
    targ |= (s & 0x00000000000000FF)<<56;
    targ |= (s & 0x000000000000FF00)<<40;
    targ |= (s & 0x0000000000FF0000)<<24;
    targ |= (s & 0x00000000FF000000)<<8;
    targ |= (s & 0x000000FF00000000)>>8;
    targ |= (s & 0x0000FF0000000000)>>24;
    targ |= (s & 0x00FF000000000000)>>40;
    targ |= (s & 0xFF00000000000000)>>56;
    return targ;
}

/*These versions are more explicit.*/
static inline uint16_t be_to_u16(uint16_t s){
    return u16_to_be(s);
}

static inline uint32_t be_to_u32(uint32_t s){
    return u32_to_be(s);
}

static inline uint64_t be_to_u64(uint64_t s){
    return u64_to_be(s);
}


/*Without the check.*/

static inline uint16_t u16_bswap(uint16_t s){
	uint16_t targ;
	/*if this is a big-endian architecture, do nothing.*/
    targ = 0;
    targ |= (s & 0x00FF)<<8;
    targ |= (s & 0xFF00)>>8;
    return targ;
}



static inline uint32_t u32_bswap(uint32_t s){
    uint32_t targ;
	/*if this is a big-endian architecture, do nothing.*/
    targ = 0;
    targ |= (s & 0x000000FF)<<24;
    targ |= (s & 0x0000FF00)<<8;
    targ |= (s & 0x00FF0000)>>8;
    targ |= (s & 0xFF000000)>>24;
    return targ;
}

static inline uint64_t u64_bswap(uint64_t s){
    uint64_t targ;
	/*if this is a big-endian architecture, do nothing.*/
    targ = 0;
    targ |= (s & 0x00000000000000FF)<<56;
    targ |= (s & 0x000000000000FF00)<<40;
    targ |= (s & 0x0000000000FF0000)<<24;
    targ |= (s & 0x00000000FF000000)<<8;
    targ |= (s & 0x000000FF00000000)>>8;
    targ |= (s & 0x0000FF0000000000)>>24;
    targ |= (s & 0x00FF000000000000)>>40;
    targ |= (s & 0xFF00000000000000)>>56;
    return targ;
}



/*convert uint to float*/

static inline f64 u64_rto_f64(uint64_t s){
	f64 retval;
	memcpy(&retval, &s, 8);
	return retval;
}

static inline f32 u32_rto_f32(uint32_t s){
	f32 retval;
	memcpy(&retval, &s, 4);
	return retval;
}

/*convert float to uint*/

static inline int64_t f64_rto_i64(f64 s){
	int64_t retval;
	memcpy(&retval, &s, 8);
	return retval;
}

static inline int32_t f32_rto_i32(f32 s){
	int32_t retval;
	memcpy(&retval, &s, 4);
	return retval;
}


