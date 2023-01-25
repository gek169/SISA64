CC=gcc


all: clean s64as s64e

s64as:
	$(CC) -O3 -march=native s64as.c -o s64as

s64e:
	$(CC) -O3 -march=native device.c emulator.c emu_frontend.c -o s64e

install: all
	cp s64as /usr/local/bin/
	cp s64e /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/s64as
	rm -f /usr/local/bin/s64e

clean:
	rm -f *.bin *.out *.s64sym *.exe s64as s64e asm/*.bin asm/*.out asm/*.s64sym asm/*.exe

example: all
	cd asm && ../s64as -i example.s64 -o example.bin && ../s64e example.bin

example2: all
	cd asm && ../s64as -i example2.s64 -o example2.bin && ../s64e example2.bin

test_usermode: all
	cd asm && ../s64as -i test_usermode.s64 -o test_usermode.bin && ../s64e test_usermode.bin

rxincrmark: all
	cd asm && ../s64as -i rxincrmark.s64 -o rxincrmark.bin
