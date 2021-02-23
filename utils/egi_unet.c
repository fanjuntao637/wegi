/*---------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

SURFMAN, SURFUSER

Note:
1. A simple AF_UNIX local communication module.
2. All functions based on UNIX domain sockets!


----- Struct -----

1. struct sockaddr_un
        #define UNIX_PATH_MAX   108
        struct sockaddr_un {
                __kernel_sa_family_t sun_family; // AF_UNIX
                char sun_path[UNIX_PATH_MAX];    //pathname
        };

2. struct msghdr
        struct msghdr {
               void         *msg_name;       // optional address
	       //used on an unconnected socket to specify the target address for a datagram
               socklen_t     msg_namelen;    // size of address
               struct iovec *msg_iov;        // scatter/gather array, writev(), readv()
               size_t        msg_iovlen;     // # elements in msg_iov
               void         *msg_control;    // ancillary data, see below
               size_t        msg_controllen; // ancillary data buffer len
               int           msg_flags;      // flags (unused)
           };

3. struct cmsghdr and CMSG_xxx
        struct cmsghdr {
           socklen_t cmsg_len;    // data byte count, including header
           int       cmsg_level;  // originating protocol
           int       cmsg_type;   // protocol-specific type
           // followed by unsigned char cmsg_data[];
       };

       struct cmsghdr *CMSG_FIRSTHDR(struct msghdr *msgh);
       struct cmsghdr *CMSG_NXTHDR(struct msghdr *msgh, struct cmsghdr *cmsg);
       size_t CMSG_ALIGN(size_t length);
       size_t CMSG_SPACE(size_t length);
       size_t CMSG_LEN(size_t length);
       unsigned char *CMSG_DATA(struct cmsghdr *cmsg);


Jurnal:
2021-02-17/18:
	1. Add EGI_RING, EGI_SURFACE concept test functions.
2021-02-21:
	1. Spin off EGI_SURFACE functions.

Midas Zhou
midaszhou@yahoo.com
---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <pthread.h>

/* EGI_SURFACE */
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/select.h>

#include "egi_unet.h"
#include "egi_timer.h"
#include "egi_debug.h"

static void* userv_listen_thread(void *arg);

/*-----------------------------------------------------
1. Create an AF_UNIX socket sever for local communication.
and type of socket is SOCK_STREAM.
2. Start a thread to listen and accept cliens.

@svrpath:	userver path. (Must be full path!)

Return:
	Poiter to an EGI_USERV	  OK
	NULL			  Fails
------------------------------------------------------*/
EGI_USERV* unet_create_Userver(const char *svrpath)
{
	EGI_USERV *userv=NULL;

        /* 1. Calloc EGI_USERV */
        userv=calloc(1,sizeof(EGI_USERV));
        if(userv==NULL)
                return NULL;

	/* 2. Create socket  */
	userv->sockfd = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if(userv->sockfd <0) {
		//printf("%s: Fail to create socket! Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to create socket!");
		free(userv);
		return NULL;
	}

	/* 3. If old path exists, fail OR remove it? */
	if( access(svrpath,F_OK) ==0 ) {
	   #if 0  /* TODO: */
		//printf("%s: svrpath already exists!\n", __func__);
		egi_dpstd("svrpath already exists!\n");
		/* DO NOT goto END_FUNC, it will try to unlink svrpath again! */
		close(userv->sockfd);
		free(userv);
		return NULL;
	   #else
		//printf("%s: svrpath already exists! try to unlink it...\n", __func__);
		egi_dpstd("svrpath already exists! try to unlink it...\n");
		if( unlink(svrpath)!=0 ) {
			//printf("%s: Fail to remove old svrpath! Err'%s'.\n", __func__, strerror(errno));
			egi_dperr("Fail to remove old svrpath!");
			goto END_FUNC;
		}
	   #endif
	}

	/* 4. Prepare address struct */
	memset(&userv->addrSERV, 0, sizeof(typeof(userv->addrSERV)));
	userv->addrSERV.sun_family = AF_UNIX;
	strncpy(userv->addrSERV.sun_path, svrpath, sizeof(userv->addrSERV.sun_path)-1);

	/* 5. Bind address */
	if( bind(userv->sockfd, (struct sockaddr *)&userv->addrSERV, sizeof(typeof(userv->addrSERV))) !=0 ) {
		//printf("%s: Fail to bind address for sockfd! Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to bind address for sockfd!");
		goto END_FUNC;
	}

	/* 6. Set backlog */
	userv->backlog=USERV_MAX_BACKLOG;

	/* 7. Start listen/accept thread */
	if( pthread_create(&userv->acpthread, NULL, (void *)userv_listen_thread, userv) !=0 ) {
		//printf("%s: Fail to launch userv->acpthread! Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to launch userv->acpthread!");
		goto END_FUNC;
	}

#if 0	/* DO NOT!!! Detach the thread */
	if(pthread_detach(userv->acpthread)!=0) {
        	//printf("%s: Fail to detach userv->acpthread! Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to detach userv->acpthread!");
	        //go on...
	} else {
		egi_dpstd("OK! Userv->acpthread detached!\n");
	}
#endif

	/* 8. OK */
	return userv;

END_FUNC: /* Fails */
	if( access(svrpath,F_OK) ==0 ) {
		if( unlink(svrpath)!=0 )
			//printf("%s: Fail to remove old svrpath! Err'%s'.\n", __func__, strerror(errno));
			egi_dperr("Fail to remove old svrpath!");
	}
	close(userv->sockfd);
        free(userv);
        return NULL;
}


/*---------------- A thread function -------------
Start EGI_USERV listen and accept thread.
Disconnected clients will NOT be detected here!

@arg	Pointer to an EGI_USERV.
------------------------------------------------*/
static void* userv_listen_thread(void *arg)
{
	int i;
	int index;
	int csfd=0;
	struct sockaddr_un addrCLIT={0};
	socklen_t addrLen;

	EGI_USERV* userv=(EGI_USERV *)arg;
	if(userv==NULL || userv->sockfd<=0 )
                return (void *)-1;

        /* 1. Start listening */
        if( listen(userv->sockfd, userv->backlog) <0 ) {
                //printf("%s: Fail to listen() sockfd, Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to listen() sockfd.");
                return (void *)-2;
        }

	/* 2. Accepting clients ... */
	userv->acpthread_on=true;
	while( userv->cmd !=1 ) {

		/* 2.1 Re_count clients, and get an available userv->sessions[] for next new client. */
		userv->ccnt=0;
		index=-1;
		for(i=0; i<USERV_MAX_CLIENTS; i++) {
			if( index<0 && userv->sessions[i].alive==false )
				index=i;  /* availble index for next session */
			if( userv->sessions[i].alive==true )
				userv->ccnt++;
		}
		//printf("%s: Recount active sessions, userv->ccnt=%d\n", __func__, userv->ccnt);

		/* 2.1 Check Max. clients */
		if(index<0) {
			//printf("%s: Clients number hits Max. limit of %d!\n",__func__, USERV_MAX_CLIENTS);
			egi_dpstd("Clients number hits Max. limit of %d!\n", USERV_MAX_CLIENTS);
			tm_delayms(500);
			continue;
		}

		/* 2.3 Accept clients */
		//printf("%s: accept() waiting...\n", __func__);
		addrLen=sizeof(addrCLIT);
	//csfd=accept4(userv->sockfd, (struct sockaddr *)&addrCLIT, &addrLen, SOCK_NONBLOCK|SOCK_CLOEXEC); /* flags for the new open file!!! */
		csfd=accept(userv->sockfd, (struct sockaddr *)&addrCLIT, &addrLen);
		if(csfd<0) {
			switch(errno) {
                                #if(EWOULDBLOCK!=EAGAIN)
                                case EWOULDBLOCK:
                                #endif
				case EAGAIN:
					tm_delayms(10); /* if NONBLOCKING */
					continue;  /* ---> continue to while() */
					break;
				case EINTR:
					//printf("%s: accept() interrupted! errno=%d Err'%s'.  continue...\n",
					//					__func__, errno, strerror(errno));
					egi_dperr("accept() interrupted! errno=%d", errno);
					continue;
					break;
				default:
					//printf("%s: Fail to accept a new client, errno=%d Err'%s'.  continue...\n",
					//							__func__, errno, strerror(errno));
					egi_dperr("Fail to accept a new client, errno=%d", errno);

					//tm_delayms(20); /* if NONBLOCKING */
					/* TODO: End routine if it's a deadly failure!  */
					continue;  /* ---> whatever, continue to while() */
			}
		}

	/*** NOTE:
	 * "On  Linux, the new socket returned by accept() does not inherit file status flags such as O_NONBLOCK and O_ASYNC
	 * from the listening socket.  This behavior differs from the canonical BSD sockets implementation.
 	 * Portable programs should not rely on inheritance or noninheritance of file status flags and always explicitly
	 * set all required flags on the socket returned from accept(). " --- man accept
	 */
		/* 2.4 Proceed to add a new client ...  */
		userv->sessions[index].sessionID=index;
		userv->sessions[index].csFD=csfd;
		userv->sessions[index].addrCLIT=addrCLIT;
		userv->sessions[index].alive=true;
		userv->ccnt++;

		//egi_dpstd("session[%d] starts with csFD=%d\n", index, csfd);
       		egi_dpstd("\t\t\t----- (+)userv->sessions[%d], ccnt=%d -----\n", index, userv->ccnt);
	}

	/* End thread */
	userv->acpthread_on=false;
	return (void *)0;
}


/*-------------------------------------------
Destroy an EGI_USERV.

@userv	Pointer to a pointer to an EGI_USERV.

Return:
	0	OK
	<0	Fails
-------------------------------------------*/
int unet_destroy_Userver(EGI_USERV **userv )
{
	int i;

	if(userv==NULL || *userv==NULL)
                return 0;

	/* Join acpthread */
	(*userv)->cmd=1;  /* CMD to end thread */
	//printf("%s: Join acpthread...\n", __func__);
	if( pthread_join((*userv)->acpthread, NULL)!=0 ) {
		//printf("%s: Fail to join acpthread! Err'%s'\n", __func__, strerror(errno));
		egi_dperr("Fail to join acpthread!");
		// Go on...
	}

        /* Make sure all sessions/clients have been safely closed/disconnected! */

	/* Close session fd */
        for(i=0; i<USERV_MAX_CLIENTS; i++) {
	        if( (*userv)->sessions[i].alive==true ) {
			close((*userv)->sessions[i].csFD);
			/* Reset token */
			(*userv)->sessions[i].csFD=0;
			(*userv)->sessions[i].alive=false;
		}
	}

        /* Close main sockfd */
        if(close((*userv)->sockfd)<0) {
                //printf("%s: Fail to close sockfd, Err'%s'\n", __func__, strerror(errno));
		egi_dperr("Fail to close sockfd.");
                return -1;
        }

        /* Free mem */
        free(*userv);
        *userv=NULL;

        return 0;
}


/*-----------------------------------------------------
Create an AF_UNIX socket sever for local communication.
and type of socket is SOCK_STREAM.

@svrpath:	userver path. (Must be full path!)

Return:
	0	OK
	<0	Fails
------------------------------------------------------*/
EGI_UCLIT* unet_create_Uclient(const char *svrpath)
{
	EGI_UCLIT *uclit=NULL;

        /* Calloc EGI_USERV */
        uclit=calloc(1,sizeof(EGI_UCLIT));
        if(uclit==NULL)
                return NULL;

	/* Create socket  */
	uclit->sockfd = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
	if(uclit->sockfd <0) {
		//printf("%s: Fail to create socket! Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to create socket!");
		free(uclit);
		return NULL;
	}

	/* Prepare address struct */
	memset(&uclit->addrSERV, 0, sizeof(typeof(uclit->addrSERV)));
	uclit->addrSERV.sun_family = AF_LOCAL;
	strncpy(uclit->addrSERV.sun_path, svrpath, sizeof(uclit->addrSERV.sun_path)-1);

	/* Connect to the server */
	if( connect(uclit->sockfd, (struct sockaddr *)&uclit->addrSERV, sizeof(typeof(uclit->addrSERV))) !=0 ) {
		//printf("%s: Fail to connect to the server! Err'%s'.\n", __func__, strerror(errno));
		egi_dperr("Fail to connect to the server!");
		close(uclit->sockfd);
		free(uclit);
		return NULL;
	}

	/* Assign other members */


	return uclit;
}


/*-------------------------------------------
Destroy an EGI_UCLIT.

@uclit	Pointer to a pointer to an EGI_UCLIT.

Return:
	0	OK
	<0	Fails
-------------------------------------------*/
int unet_destroy_Uclient(EGI_UCLIT **uclit )
{
        if(uclit==NULL || *uclit==NULL)
                return 0;

        /* Make sure all sessions/clients have been safely closed/disconnected! */

        /* Close main sockfd */
        if(close((*uclit)->sockfd)<0) {
                //printf("%s: Fail to close sockfd, Err'%s'\n", __func__, strerror(errno));
		egi_dperr("Fail to close sockfd!");
                return -1;
        }

        /* Free mem */
        free(*uclit);
        *uclit=NULL;

        return 0;
}


/*----------------------------------------------------------------
Send out msg through a AF_UNIX socket.

@sockfd  A valid UNIX SOCK_STREAM type socket.
	 Assume to be BLOCKING type.
@msg	 Pointer to MSG.

Return:
	>0	OK, the number of bytes sent.
		DO NOT include ancillary(msg_control) data length!
	<0	Fails
----------------------------------------------------------------*/
int unet_sendmsg(int sockfd,  struct msghdr *msg)
{
	int nsnd;
	if(msg==NULL)
		return -1;

	/* Send MSG */
	nsnd=sendmsg(sockfd, msg, MSG_NOSIGNAL); /* MSG_NOWAIT */
	if(nsnd>0) {
		//printf("%s: OK, sendmsg() %d bytes!\n", __func__, nsnd);
//		egi_dpstd("OK, sendmsg() %d bytes!\n",nsnd);
	}
	else if(nsnd==0) {
		//printf("%s: nsnd=0! for sendmsg()!\n", __func__);
		egi_dpstd("nsnd=0! for sendmsg()!\n");
	}
	else { /* nsnd < 0 */
	        switch(errno) {
        		#if(EWOULDBLOCK!=EAGAIN)
                	case EWOULDBLOCK:
                        #endif
                        case EAGAIN:  //EWOULDBLOCK, for it would block */
                        	//printf("%s\n", strerror(errno));
				egi_dperr("Eagain/EwouldBlock");
				break;

			/* If  the  message  is  too  long  to pass atomically through the underlying protocol,
			 *  the error EMSGSIZE is returned, and the message is not transmitted.
       			 */
			case EMSGSIZE:
				//printf("%s: Message too long! Err'%s'.\n", __func__, strerror(errno));
				egi_dperr("Message too long!");
				break;

                        case EPIPE:
				//printf("%s: Err'%s'.\n",__func__, strerror(errno));
				egi_dperr("Epipe");
				break;
			default:
				//printf("%s: Err'%s'.\n",__func__, strerror(errno));
				egi_dperr("sendmsg fails.");
		}
	}

	return nsnd;
}


/*-----------------------------------------------
Receive a msg through a AF_UNIX socket.
The Caller should also check msg->msg_flags.

@sockfd  A valid UNIX SOCK_STREAM type socket.
	 Assume to be BLOCKING type.
@msg	 Pointer to MSG.
	 The Caller must ensure enough space!

Return:
	>0	OK, the number of bytes received.
		DO NOT include ancillary(msg_control) data length!
	=0	Counter peer quits.
	<0	Fails
------------------------------------------------*/
int unet_recvmsg(int sockfd,  struct msghdr *msg)
{
	int nrcv;
	if(msg==NULL)
		return -1;

	/* MSG_WAITALL: Wait all data, but still may fail? see man */
	nrcv=recvmsg(sockfd, msg, MSG_WAITALL); /* MSG_CMSG_CLOEXEC,  MSG_NOWAIT */
	if(nrcv>0) {
		//printf("%s: OK, recvmsg() %d bytes!\n", __func__, nrcv);
//		egi_dpstd("OK, recvmsg() %d bytes!\n", nrcv);
	}
	else if(nrcv==0) {
		//printf("%s: nrcv=0! counter peer quits the session!\n", __func__);
//		egi_dpstd("nrcv=0! counter peer quits the session!\n");
	}
	else { /* nsnd < 0 */
	        switch(errno) {
        		#if(EWOULDBLOCK!=EAGAIN)
                	case EWOULDBLOCK:
                        #endif
                        case EAGAIN:  //EWOULDBLOCK, for datastream it woudl block */
                        	//printf("%s: Err'%s'.\n", __func__, strerror(errno));
				egi_dperr("Eagain/EwouldBlock");
				break;
			default:
				//printf("%s: Err'%s'.\n", __func__, strerror(errno));
				egi_dperr("recvmsg fails");
		}
	}

	/* Check msg->msg_flags */
	if( msg->msg_flags != 0 )
		//printf("%s: msg_flags=%d, the MSG is incomplete!\n",__func__, msg->msg_flags);
		egi_dpstd("msg_flags=%d, the MSG is incomplete!\n", msg->msg_flags);

	return nrcv;
}


