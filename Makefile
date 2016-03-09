CC=gcc

all:
	cd src; ${MAKE}
	cd checks; ${MAKE}

clean:
	cd src; ${MAKE} clean
	cd checks; ${MAKE} clean

PROGRAM=check_nfs
HOME=/home/gbl
PUBLISHDIR=${HOME}/www.guntram.de/nagiosbinaries
VERSION=0.03

publish: clean
	mkdir -p ${PUBLISHDIR}/${PROGRAM}

	cp ${HOME}/COPYING.gbl checks/
	cd ..; tar czf  ${PUBLISHDIR}/${PROGRAM}/${PROGRAM}-${VERSION}-src.tgz \
		${PROGRAM}/src \
		${PROGRAM}/checks \
		${PROGRAM}/include \
		${PROGRAM}/COPYRIGHT.nfsreplay \
		${PROGRAM}/README \
		${PROGRAM}/CHANGELOG \
		${PROGRAM}/Makefile
	ln -sf ${PROGRAM}-${VERSION}-src.tgz ${PUBLISHDIR}/${PROGRAM}/${PROGRAM}-src.tgz
	md5sum ${PUBLISHDIR}/${PROGRAM}/${PROGRAM}-src.tgz

binpublish: all
	bindir=`uname -s`-`uname -p` && \
	mkdir -p $$bindir && \
	cp checks/check_nfs checks/check_nfs_file README $$bindir && \
	strip $$bindir/check_nfs $$bindir/check_nfs_file && \
	gtar czf ${PUBLISHDIR}/${PROGRAM}/${PROGRAM}-${VERSION}-$$bindir-bin.tgz $$bindir
