CFLAGS  += -std=gnu99 -Wall -Werror -W -Wundef -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Wsign-compare -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -Wold-style-definition -O -g
LDFLAGS += -lcrypto

dng: dng.o encode.o decode.o

dng.o: qimport.h
encode.o: qimport.h
dencode.o: qimport.h
