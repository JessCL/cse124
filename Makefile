CC = c99

all: httpd

httpd: server.c
	$(CC) -o httpd server.c DieWithMessage.c rio.c util.c

clean:
	rm -f *.o httpd *~
