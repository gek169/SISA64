#include "sisa64.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
	return 0;
}

void dcl(){
	free(sisa64_mem);
}

uint64_t dev_read(uint64_t addr){
	if(addr == 0) return getchar();
	if(addr == 1) return SYS_MEMORY_SIZE;
	if(addr == 2) return clock() / (CLOCKS_PER_SEC/1000);
	if(addr == 0x1000000) return 1; /*Device Standard*/
	return 0;
}
void dev_write(uint64_t addr, uint64_t val){
	//printf("DEV_WRITE HAPPENED, %llu,%llu",(unsigned long long)addr,(unsigned long long)val);
	if(addr == 0) putchar(val);
}
