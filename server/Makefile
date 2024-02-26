default : aesdsocket

all : aesdsocket

aesdsocket : aesdsocket.c
	$(CC) $(LDFLAGS) -pthread -Wall -Werror -g -o aesdsocket aesdsocket.c

clean:
	rm -f aesdoscket aesdsocket.o