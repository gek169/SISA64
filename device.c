
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "avdevice.h"
//uint8_t static_buffer[SYS_MEMORY_SIZE];

int di(){
	if(!sisa64_mem)
		sisa64_mem = malloc(SYS_MEMORY_SIZE);
	if(!sisa64_mem){
		puts("Initial memory allocation failed.");
		exit(1);
	}
	if(sizeof(f32) != 4){
		puts("f32 is not 4 bytes...");
		exit(1);
	} 
	if(sizeof(f64) != 8) {
		puts("f64 is not 8 bytes...");
		exit(1);
	}
	av_init();
	return 0;
}

void dcl(){
	//Don't bother!
	/*
	free(sisa64_mem);
	*/
}

uint64_t dev_read(uint64_t addr){
	const char* vstring = "S-ISA-64 Emulator: Default Device.";
	const uint64_t vstring_loc = 0x2000000;
	uint64_t periph = (addr >> 44);
	addr &= 0xfFFffFFffFF;
	if(periph == 0){
		if(addr == 0) return getchar();
		if(addr == 1) return SYS_MEMORY_SIZE;
		if(addr == 2) return clock() / (CLOCKS_PER_SEC/1000);
		if(addr == 8) return 1; /*only 1- the AV device.*/
		if(addr == 12) return 0x2000000;
		if(addr == 0x1000000) return 1; /*Device Standard*/
		if(addr == 0x1000001) return 1; /*Device Standard*/
		if(addr >= vstring_loc && ((addr - vstring_loc) < strlen(vstring))){
			return vstring[addr - vstring_loc];
		}
		return 0;
	}
	if(periph == 1){
		return av_device_read(addr);
	}
	return 0;
}
void dev_write(uint64_t addr, uint64_t val){
	uint64_t periph = (addr >> 44);
	addr &= 0xfFFffFFffFF;
	//printf("DEV_WRITE HAPPENED, %llu,%llu",(unsigned long long)addr,(unsigned long long)val);
	if(periph == 0){
		if(addr == 0) putchar(val);
	}
	if(periph == 1){
		av_device_write(addr, val);
	}
}
