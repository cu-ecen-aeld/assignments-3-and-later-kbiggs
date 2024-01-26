# Content of the makefile was based upon content found at below links. 
# I used these pages as a guide for how to write a make clean command and incorporate command line arguments.
# https://makefiletutorial.com/#command-line-arguments-and-override
# https://stackoverflow.com/questions/2826029/passing-additional-variables-from-command-line-to-make

writer: writer.o
	$(CROSS_COMPILE)gcc -o writer writer.o

writer.o: writer.c
	$(CROSS_COMPILE)gcc -o writer.o -c writer.c

clean:
	rm -f writer
	rm -f *.o