writer: writer.o
	$(CROSS_COMPILE)gcc -o writer writer.o

writer.o: writer.c
	$(CROSS_COMPILE)gcc -o writer.o -c writer.c

clean:
	rm -f writer
	rm -f *.o