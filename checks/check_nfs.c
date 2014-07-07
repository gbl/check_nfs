/*
 *    Nagios plugin to check space on an NFS filesystem without having to mount.
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
#include <sys/types.h>

fhandle3 mntfh;

int exitcode=0;
char *errmsg=NULL;

long long tbytes, fbytes, abytes;
long long argtonum(char *str, long long ref);

void nfs_fsstat_cb(void *msg, int len, void *priv_ctx)
{
	FSSTAT3res *res = NULL;

	res = xdr_to_FSSTAT3res(msg, len);
	if(res == NULL) {
		exitcode=2;
		errmsg="fsstat failed";
		return;
	}

	if(res->status != NFS3_OK) {
		exitcode=2;
		errmsg=malloc(128);
		strcpy(errmsg, "Fsstat failed - error ");
		sprintf(errmsg+strlen(errmsg), "%d (%s)\n",
			res->status, strerror(res->status));
		free_FSSTAT3res(res);
		return;
	}

	tbytes= (long long) res->FSSTAT3res_u.resok.tbytes;
	fbytes= (long long) res->FSSTAT3res_u.resok.fbytes;
	abytes= (long long) res->FSSTAT3res_u.resok.abytes;
	free_FSSTAT3res(res);
	return;
}

void nfs_mnt_cb(void *msg, int len, void *priv_ctx)
{
	mountres3 *mntres = NULL; 
	char *fh = NULL;
	u_long fh_length;

	mntres = xdr_to_mntres3(msg, len);
	if(mntres == NULL)
		return;
	
	if(mntres->fhs_status != MNT3_OK) {
		exitcode=2;
		errmsg=malloc(128);
		strcpy(errmsg, "Mount failed - error ");
		sprintf(errmsg+strlen(errmsg), "%d (%s)\n",
			mntres->fhs_status, strerror(mntres->fhs_status));
		free_mntres3(mntres);
		return;
	}
	

	/* Prepare to copy mounted file handle. */
	fh_length = mntres->mountres3_u.mountinfo.fhandle.fhandle3_len;
	fh = mntres->mountres3_u.mountinfo.fhandle.fhandle3_val;
	mntfh.fhandle3_val = (char *)mem_alloc(fh_length);
	mntfh.fhandle3_len = fh_length;
	if(mntfh.fhandle3_val == NULL) {
		free_mntres3(mntres);
		return;
	}

	memcpy(mntfh.fhandle3_val, fh, fh_length);
	free_mntres3(mntres);
	return;
}


int main(int argc, char *argv[])
{
	struct addrinfo *srv_addr, hints;
	int err;
	enum clnt_stat stat;
	nfs_ctx *ctx = NULL;
	long long warn, crit;

	FSSTAT3args fs;

	if(argc < 4) {
		printf("%s UNKNOWN: Not enough arguments\n"
			"Check free space on an NFS directory\n"
			"USAGE: %s <server> <remote_mountpoint> <w> <c>\n",
			argv[0], argv[0]);
		return 3;
	}

	/* First resolve server name */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;

	if((err = getaddrinfo(argv[1], NULL, &hints, &srv_addr)) != 0) {
		printf("%s CRITICAL: Cannot resolve name: %s: %s\n",
				argv[0], argv[1], gai_strerror(err));
		exit(2);
	}
	
	ctx = nfs_init((struct sockaddr_in *)srv_addr->ai_addr, IPPROTO_TCP, 0);
	if(ctx == NULL) {
		printf("%s CRITICAL:  Cant init nfs context\n", argv[0]);
		exit(2);
	}

	freeaddrinfo(srv_addr);
	mntfh.fhandle3_len = 0;
	stat = mount3_mnt(&argv[2], ctx, nfs_mnt_cb, NULL);
	if (stat == RPC_SUCCESS && exitcode==0)
		;
	else {
		printf("%s CRITICAL: %s\n", argv[0], errmsg);
		exit(2);
	}

	fs.fsroot.data.data_len = mntfh.fhandle3_len;
	fs.fsroot.data.data_val = mntfh.fhandle3_val;
	stat = nfs3_fsstat(&fs, ctx, nfs_fsstat_cb, NULL);
	if (stat == RPC_SUCCESS && exitcode==0)
		;
	else {
		printf("%s CRITICAL: Could not send NFS FSSTAT call\n", argv[0]);
		exit(2);
	}

	nfs_complete(ctx, RPC_BLOCKING_WAIT);
	if (exitcode) {
		printf("%s CRITICAL: %s\n", argv[0], errmsg);
		exit(2);
	}

	warn=argtonum(argv[3], tbytes);
	crit=argtonum(argv[4], tbytes);

	if (abytes<crit) {
		printf("%s CRITICAL: only %ld of %ld bytes free (%ld%%)"
			"|free=%ld,%ld,%ld,%ld,%ld\n",
			argv[0],
			(long long)abytes, (long long)tbytes, (long long)100*abytes/tbytes,
			(long long)abytes, (long long)warn, (long long)crit, 0L, (long long)tbytes
			);
		exit(2);
	} else if (abytes<warn) {
		printf("%s WARNING: only %ld of %ld bytes free (%ld%%)"
			"|free=%ld,%ld,%ld,%ld,%ld\n",
			argv[0],
			(long long)abytes, (long long)tbytes, (long long)100*abytes/tbytes,
			(long long)abytes, (long long)warn, (long long)crit, 0L, (long long)tbytes
			);
		exit(1);
	} else {
		printf("%s OK: %lld of %lld bytes free (%lld%%)"
			"|free=%lld,%lld,%lld,%ld,%lld\n",
			argv[0],
			(long long)abytes, (long long)tbytes, (long long)100*abytes/tbytes,
			(long long)abytes, (long long)warn, (long long)crit, 0L, (long long)tbytes
			);
		exit(0);
	}
}

long long argtonum(char *str, long long ref) {
	long long result=0;

	while (*str) {
		if (isdigit(*str)) {
			result=result*10+*str-'0';
		} else if (*str=='K' || *str=='k') {
			result*=1024; break;
		} else if (*str=='M' || *str=='m') {
			result*=1024*1024; break;
		} else if (*str=='G' || *str=='g') {
			result*=1024L*1024*1024; break;
		} else if (*str=='T' || *str=='t') {
			result*=1024L*1024*1024*1024; break;
		} else if (*str=='P' || *str=='p') {
			result*=1024L*1024*1024*1024*1024; break;
		} else if (*str=='%') {
			result=result*ref/100; break;
		} else {
			printf("UNKNOWN: can't parse %s\n", str);
			exit(3);
		}
		str++;
	}
	return result;
}
