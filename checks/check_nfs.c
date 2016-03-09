/*
 *    Nagios plugin to check space on an NFS filesystem without having to mount.
 *
 *    Copyright (C) 2011-2016 Guntram Blohm, <nagios1@guntram.de>.
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
#include <sys/signal.h>
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
char *progname;

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

void timeout(int signal) {
	printf("%s CRITICAL: timeout\n", progname);
	exit(2);
}

int main(int argc, char *argv[])
{
	struct addrinfo *srv_addr, hints;
	int err;
	enum clnt_stat stat;
	nfs_ctx *ctx = NULL;
	long long warn, crit;
	char *unitstr="", *perfunitstr="";
	unsigned long long divisor=1;
	unsigned long long perfdivisor=1;
	int fullpath=0;
	progname=argv[0];
	char option;
	int alarmtime=5;

	FSSTAT3args fs;

	while (argc>1 && argv[1][0] == '-') {
		if (tolower(argv[1][1]) == 'u') {
			option=argv[1][1];
			if (argv[1][2]) {
				unitstr=argv[1]+2;
				argc--; argv++;
			} else if (argc>2) {
				unitstr=argv[2];
				argc-=2; argv+=2;
			}
			if (!strncasecmp(unitstr, "ki", 2)) {
				divisor=1024LL;
			} else if (*unitstr=='k' || *unitstr=='K') {
				divisor=1000LL;
			} else if (!strncasecmp(unitstr, "mi", 2)) {
				divisor=1024LL*1024;
			} else if (*unitstr=='m' || *unitstr=='M') {
				divisor=1000LL*1000;
			} else if (!strncasecmp(unitstr, "gi", 2)) {
				divisor=1024LL*1024LL*1024LL;
			} else if (*unitstr=='g' || *unitstr=='G') {
				divisor=1000LL*1000LL*1000LL;
			} else if (!strncasecmp(unitstr, "ti", 2)) {
				divisor=1024LL*1024LL*1024LL*1024LL;
			} else if (*unitstr=='t' || *unitstr=='T') {
				divisor=1000LL*1000LL*1000LL*1000LL;
			} else if (!strncasecmp(unitstr, "pi", 2)) {
				divisor=1024LL*1024LL*1024LL*1024LL*1024LL;
			} else if (*unitstr=='p' || *unitstr=='P') {
				divisor=1000LL*1000LL*1000LL*1000LL*1000LL;
			}
			if (option=='U') {
				perfdivisor=divisor;
				perfunitstr=unitstr;
			}
		} else if (argv[1][1] == 'p') {
			fullpath=1;
			argc--;
			argv++;
		} else if (argv[1][1] == 'a') {
			alarmtime=atoi(argv[2]);
			argc-=2;
			argv+=2;
		} else {
			printf("%s UNKNOWN: bad argument: %c\n",
				progname, argv[1][1]);
			exit(3);
		}

	}
	if (fullpath==0 &&  strrchr(progname, '/')!=NULL)
		progname=strrchr(progname, '/')+1;

	if(argc < 4) {
		printf("%s UNKNOWN: Not enough arguments\n"
			"Check free space on an NFS directory\n"
			"USAGE: %s [-U perfunit] [-u unit] <server> <remote_mountpoint> <w> <c>\n",
			progname, progname);
		return 3;
	}

	if (alarmtime!=0) {
		signal(SIGALRM, timeout);
		alarm(alarmtime);
	}

	/* First resolve server name */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;

	if((err = getaddrinfo(argv[1], NULL, &hints, &srv_addr)) != 0) {
		printf("%s CRITICAL: Cannot resolve name: %s: %s\n",
				progname, argv[1], gai_strerror(err));
		exit(2);
	}
	
	ctx = nfs_init((struct sockaddr_in *)srv_addr->ai_addr, IPPROTO_TCP, 0);
	if(ctx == NULL) {
		printf("%s CRITICAL:  Cant init nfs context\n", progname);
		exit(2);
	}

	freeaddrinfo(srv_addr);
	mntfh.fhandle3_len = 0;
	stat = mount3_mnt(&argv[2], ctx, nfs_mnt_cb, NULL);
	if (stat == RPC_SUCCESS && exitcode==0)
		;
	else {
		printf("%s CRITICAL: %s\n", progname, errmsg);
		exit(2);
	}

	fs.fsroot.data.data_len = mntfh.fhandle3_len;
	fs.fsroot.data.data_val = mntfh.fhandle3_val;
	stat = nfs3_fsstat(&fs, ctx, nfs_fsstat_cb, NULL);
	if (stat == RPC_SUCCESS && exitcode==0)
		;
	else {
		printf("%s CRITICAL: Could not send NFS FSSTAT call\n", progname);
		exit(2);
	}

	nfs_complete(ctx, RPC_BLOCKING_WAIT);
	if (exitcode) {
		printf("%s CRITICAL: %s\n", progname, errmsg);
		exit(2);
	}

	warn=argtonum(argv[3], tbytes);
	crit=argtonum(argv[4], tbytes);

	if (abytes<crit) {
		printf("%s CRITICAL: only %lld%s of %lld%s bytes free (%lld%%)"
			"|free=%lld%s,%lld,%lld,%lld,%lld\n",
			progname,
			(long long)abytes/divisor, unitstr, (long long)tbytes/divisor, unitstr, (long long)100*abytes/tbytes,
			(long long)abytes/perfdivisor, perfunitstr, (long long)warn/perfdivisor, (long long)crit/perfdivisor, 0LL, (long long)tbytes/perfdivisor
			);
		exit(2);
	} else if (abytes<warn) {
		printf("%s WARNING: only %lld%s of %lld%s bytes free (%lld%%)"
			"|free=%lld%s,%lld,%lld,%lld,%lld\n",
			progname,
			(long long)abytes/divisor, unitstr, (long long)tbytes/divisor, unitstr, (long long)100*abytes/tbytes,
			(long long)abytes/perfdivisor, perfunitstr, (long long)warn/perfdivisor, (long long)crit/perfdivisor, 0LL, (long long)tbytes/perfdivisor
			);
		exit(1);
	} else {
		printf("%s OK: %lld%s of %lld%s bytes free (%lld%%)"
			"|free=%lld%s,%lld,%lld,%lld,%lld\n",
			progname,
			(long long)abytes/divisor, unitstr, (long long)tbytes/divisor, unitstr, (long long)100*abytes/tbytes,
			(long long)abytes/perfdivisor, perfunitstr, (long long)warn/perfdivisor, (long long)crit/perfdivisor, 0LL, (long long)tbytes/perfdivisor
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
			result*=1024LL*1024*1024*1024; break;
		} else if (*str=='P' || *str=='p') {
			result*=1024LL*1024*1024*1024*1024; break;
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
