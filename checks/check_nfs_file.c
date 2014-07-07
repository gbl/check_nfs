/*
 *    Nagios plugin to check a file via NFS without having to mount.
 *
 *    Copyright (C) 2011 Guntram Blohm, <gbl@bso2001.com>.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <rpc/rpc.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <nfsclient.h>

fhandle3 mntfh;
fhandle3 lfh;

int retcode=0;
char *errmsg=NULL;

void nfs_read_cb(void *msg, int len, void *priv_ctx)
{
	READ3res *res = NULL;
	int read_len;	
	int i;

	res = xdr_to_READ3res(msg, len, NFS3_DATA_DEXDR);
	if (res == NULL) {
		retcode=2;
		errmsg="Read failed - permission error?";
		return;
	}
	if (res->status != NFS3_OK) {
		retcode=2;
		errmsg=strdup("Read failed - error XXXXXXX");
		sprintf(errmsg+20, "%d", res->status);
		return;
	}
	read_len = res->READ3res_u.resok.data.data_len;
	errmsg=malloc(read_len+1);
	memcpy(errmsg, res->READ3res_u.resok.data.data_val, read_len);
	for (i=0; i<read_len && errmsg[i]!='\n'; i++)
		;
	errmsg[i]='\0';
	return;
}


void nfs_lookup_cb(void *msg, int len, void *priv_ctx)
{
	LOOKUP3res *lookupres = NULL;
	int fh_length;
	char *fh;

	lookupres = xdr_to_LOOKUP3res(msg, len);
	if (lookupres == NULL) {
		retcode=2;
		errmsg="Lookup failed - no file handle returned";
		return;
	}

	if (lookupres->status != NFS3_OK) {
		retcode=2;
		errmsg=strdup("Lookup failed - error XXXXXXX");
		sprintf(errmsg+22, "%d", lookupres->status);
		mem_free(lookupres, sizeof(LOOKUP3res));
		return;
	}
	
	fh_length = lookupres->LOOKUP3res_u.resok.object.data.data_len;
	fh = lookupres->LOOKUP3res_u.resok.object.data.data_val;

	lfh.fhandle3_len = fh_length;
	lfh.fhandle3_val = (char *)mem_alloc(fh_length);
	if (lfh.fhandle3_val == NULL) {
		retcode=2;
		errmsg="Lookup failed - NULL file handle returned";
		mem_free(lookupres->LOOKUP3res_u.resok.object.data.data_val, fh_length);
		mem_free(lookupres, sizeof(LOOKUP3res));
		return;
	}

	memcpy(lfh.fhandle3_val, fh, fh_length);

	mem_free(lookupres->LOOKUP3res_u.resok.object.data.data_val, fh_length);
	mem_free(lookupres, sizeof(LOOKUP3res));
}

void nfs_mnt_cb(void *msg, int len, void *priv_ctx)
{
	mountres3 *mntres = NULL; 
	char *fh = NULL;
	u_long fh_length;

	mntres = xdr_to_mntres3(msg, len);

	if (mntres == NULL) {
		retcode=2;
		errmsg="Mount failed - no FS handle returned";
		return;
	}

	if (mntres->fhs_status != MNT3_OK) {
		retcode=2;
		errmsg=strdup("Mount failed - error XXXXXXX");
		sprintf(errmsg+21, "%d", mntres->fhs_status);
		mem_free(mntres, sizeof(mntres3));
		return;
	}

	fh_length = mntres->mountres3_u.mountinfo.fhandle.fhandle3_len;
	fh = mntres->mountres3_u.mountinfo.fhandle.fhandle3_val;
	mntfh.fhandle3_val = (char *)mem_alloc(fh_length);
	if (mntfh.fhandle3_val == NULL) {
		retcode=2;
		errmsg="Mount failed - NULL FS handle returned";
		mem_free(mntres, sizeof(mntres3));
		return;
	}

	memcpy(mntfh.fhandle3_val, fh, fh_length);
	mntfh.fhandle3_len = fh_length;
	mem_free(mntres->mountres3_u.mountinfo.fhandle.fhandle3_val,
			fh_length);
	mem_free(mntres, sizeof(mntres3));
	return;
}


int main(int argc, char *argv[])
{
	struct addrinfo *srv_addr, hints;
	int err;
	enum clnt_stat stat;
	nfs_ctx *ctx = NULL;

	LOOKUP3args largs;
	READ3args read;

	if(argc < 4) {
		printf("%s UNKNOWN: Not enough arguments\n"
			"Test if a file can be read via NFS\n"
			"USAGE: %s <server> <remote_dir> <filename>\n",
			argv[0], argv[0]);
		return 3;
	}

	/* First resolve server name */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;

	if ((err = getaddrinfo(argv[1], NULL, &hints, &srv_addr)) != 0) {
		printf("%s CRITICAL: Cannot resolve name: %s: %s\n",
				argv[0], argv[1], gai_strerror(err));
		exit(2);
	}

	ctx = nfs_init((struct sockaddr_in *)srv_addr->ai_addr, IPPROTO_TCP, 0);
	if (ctx == NULL) {
		printf("%s CRITICAL: Cant init nfs context\n", argv[0]);
		exit(2);
	}

	freeaddrinfo(srv_addr);
	mntfh.fhandle3_len = 0;
	stat = mount3_mnt(&argv[2], ctx, nfs_mnt_cb, NULL);
	if (stat == RPC_SUCCESS) {
	} else {
		printf("%s CRITICAL: Could not send NFS MOUNT call\n", argv[0]);
		exit(2);
	}
	if (retcode!=0) {
		printf("%s CRITICAL: %s\n", argv[0], errmsg);
		exit(retcode);
	}

	largs.what.dir.data.data_len = mntfh.fhandle3_len;
	largs.what.dir.data.data_val = mntfh.fhandle3_val;
	largs.what.name = argv[3];

	lfh.fhandle3_len = 0;
	stat = nfs3_lookup(&largs, ctx, nfs_lookup_cb, NULL);
	if (stat == RPC_SUCCESS) {
		// fprintf(stderr, "NFS LOOKUP Call sent successfully\n");
	} else {
		printf("%s CRITICAL: Could not send NFS LOOKUP call\n", argv[0]);
		exit(2);
	}

	mem_free(mntfh.fhandle3_val, mntfh.fhandle3_len);
	nfs_complete(ctx, RPC_BLOCKING_WAIT);

	if (retcode!=0) {
		printf("%s CRITICAL: %s\n", argv[0], errmsg);
		exit(retcode);
	}

	read.file.data.data_len = lfh.fhandle3_len;
	read.file.data.data_val = lfh.fhandle3_val;
	read.offset = 0;
	read.count = 256;

	stat = nfs3_read(&read, ctx, nfs_read_cb, NULL);
	if (stat == RPC_SUCCESS) {
		// fprintf(stderr, "NFS READ Call sent successfully\n");
	} else {
		printf("%s CRITICAL: Could not send NFS READ call\n", argv[0]);
		exit(2);
	}

	nfs_complete(ctx, RPC_BLOCKING_WAIT);
	if (retcode!=0) {
		printf("%s CRITICAL: %s\n", argv[0], errmsg);
		exit(retcode);
	}

	printf("%s OK: got %s\n", argv[0], errmsg);

	return 0;
}
