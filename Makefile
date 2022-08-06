libtinf.a: tinf.o
	ar rcs $@ $^

tinf.o: tinf.c tinf.h
	gcc -Wall -Wextra -Werror -pedantic -std=c89 -c -o $@ $<

.PHONY: clean
clean:
	rm -f libtinf.a tinf.o
