CFLAGS=-g
CC=gcc
OBJECTS= clnt_tcp_nb.o hash_functions.o hash_table.o mount3.o nfs3.o \
	nfs3_xdr.o nfsclient.o 


.c.o:	$(OBJECTS)
	$(CC) $(CFLAGS) -c -I../include $<

libnfs.a:	$(OBJECTS)
	rm -f libnfs.a
	ar cq libnfs.a $(OBJECTS)

clean:
	rm -f $(OBJECTS) libnfs.a
