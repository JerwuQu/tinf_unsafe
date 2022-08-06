test: test.c ../tinf.c ../tinf.h test_compressed.h test_raw.h
	gcc -Wall -o $@ test.c ../tinf.c

test_compressed.h test_raw.h &: test.bmp
	./conv.sh test.bmp

.PHONY: clean
clean:
	rm -f test test_compressed.h test_raw.h
