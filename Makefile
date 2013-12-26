CFLAGS  += -std=c99 -Wall -Werror -W -Wundef -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Wsign-compare -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -Wold-style-definition -O -g
LDFLAGS += -lcrypto

qimport: qimport.o dng.o encode.o decode.o

*.o: qimport.h

clean:
	rm -f *.o qimport
