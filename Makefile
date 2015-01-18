CC = c99

all: tiny

tiny: tiny.c
	$(CC) -o tiny tiny.c DieWithMessage.c rio.c util.c

clean:
	rm -f *.o tiny *~
