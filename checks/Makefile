CFLAGS=-g
CC=gcc
# Linux only:
# CFLAGS=-g -m32
# Solaris only:
# LDFLAGS=-lnsl -lsocket
all:	check_nfs check_nfs_file

check_nfs:	check_nfs.c
	${CC} $(CFLAGS) -I ../include -o $@ $< ../src/libnfs.a $(LDFLAGS)

check_nfs_file:	check_nfs_file.c
	${CC} $(CFLAGS) -I ../include -o $@ $< ../src/libnfs.a $(LDFLAGS)


clean:
	rm -f check_nfs check_nfs_file
