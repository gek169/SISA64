CC=gcc
OPTIMIZE=-O3 -march=native

all: clean s64as s64e

s64as:
	$(CC) $(OPTIMIZE) s64as.c -o s64as

s64e:
	$(CC) $(OPTIMIZE) device.c emulator.c emu_frontend.c -o s64e -lm -lSDL2

install: all
	cp s64as /usr/local/bin/
	cp s64e /usr/local/bin/

install_micro_syntax_file:
	mkdir -p ~/.config/micro/syntax/
	cp ./micro_syntax_highlight/s64.yaml ~/.config/micro/syntax/

q: example install_micro_syntax_file
	admin make install
	make ap

test: example test_usermode rxincrmark

uninstall:
	rm -f /usr/local/bin/s64as
	rm -f /usr/local/bin/s64e

ap: clean
	git add .; git commit -m "Automatic Commit, make ap"; git push -f;

pull:
	git pull -f;

clean:
	rm -f *.bin *.out *.s64sym *.exe s64as s64e asm/*.bin asm/*.out asm/*.s64sym asm/*.exe

example: all
	./s64as -i asm/example.s64 -o example.bin
	./s64e example.bin

video: all
	./s64as -i asm/video.s64 -o video.bin
	./s64e video.bin
	
example2: all
	./s64as -i asm/example2.s64 -o example2.bin
	./s64e example2.bin

test_usermode: all
	./s64as -i asm/test_usermode.s64 -o test_usermode.bin
	./s64e test_usermode.bin

rxincrmark: all
	./s64as -i asm/rxincrmark.s64 -o rxincrmark.bin
	#time ./s64e rxincrmark.bin

bench_rxincr: rxincrmark
	time ./s64e rxincrmark.bin
