/*
 * Loader for SISA64
 * Loads binaries from disk into memory.

 Written by Escapechar
*/

#include <stdio.h>
#include "sisa64.h"
#include "stringutil.h"
#include <stdlib.h>
int main(int argc, char **argv) {
    FILE   *fhandle;
    char   *image_path = "a.bin";
    unsigned long long image_length;
   /* 
    if (argc == 1) {
        fprintf(stderr, "sisa64_loader: a binary image must be specified.\n");
        return -1;
    }
    */
    if(argc > 1){
    	if(streq(argv[1], "-v")){
    		puts("SISA64 emulator frontend, written originally by Escapechar-dev");
    		puts("He has agreed to place it into the public domain.");
    		puts("\nUsage: s64e /path/to/image.bin");
    		exit(0);
    	}
    }
    if(argc > 1)
    	image_path = argv[1];
    fhandle = fopen(image_path, "rb");
    if (!fhandle) {
        fprintf(stderr, "sisa64_loader: binary image '%s' cannot be found or cannot be accessed.\n",
            image_path);
        return -1;
    }
    
    fseek(fhandle, 0, SEEK_END);
    image_length = ftell(fhandle);
    rewind(fhandle);
    
    if (image_length > SYS_MEMORY_SIZE) {
        fprintf(stderr, "sisa64_loader: binary image is too large (max is %llu)\n",
            (unsigned long long)SYS_MEMORY_SIZE);
        return -1;
    }

    
    if(!sisa64_mem) sisa64_mem = malloc((int64_t)SYS_MEMORY_SIZE);
	if(!sisa64_mem) {
		puts("Initial memory allocation failed.");
		exit(1);
	}
    
    if (fread(sisa64_mem, 1, image_length, fhandle) < image_length) {
        fprintf(stderr, "sisa64_loader: failed to read byte(s) from image.\n");
        return -1;
    }

    di();
    sisa64_emulate();
	dcl();
    
    fclose(fhandle);

    return 0;
}
