#First target is the default target
#Runs when called if writer.c is newer than the generated writer file
writer: writer.o
	cc writer.o -o writer

writer.o: writer.c
	cc -c writer.c -o writer.o

#Alternatively, the 2 above can be combined into like:
#writer: writer.c
#cc writer.c -o writer

clean: 
	rm -f writer writer.o