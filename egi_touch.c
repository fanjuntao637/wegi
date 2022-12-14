/* ------------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.


NOTE:
1. egi_touch_loopread() will wait until live_touch_data.updated is fals,
   so discard first egi_touch_getdata() before loop call it.


TODO:
1. The speed of updating live_touch_data depends on egi_touch_loopread()
   however, other threads may not be able to keep up with it.
   try adjusting tm_delayms()...

Journal:
2022-03-03:
	1. Add egi_touch_waitPress() and egi_touch_readXYP()


Midas Zhou
midaszhou@yahoo.com(Not in use since 2022_03_01)
-----------------------------------------------------------------------*/
#include "egi.h"
#include "egi_fbgeom.h"
#include "spi.h"
#include "egi_debug.h"
#include "egi_timer.h"
#include "xpt2046.h"
#include "egi_touch.h"
#include <errno.h>
#include <stdbool.h>

/* typedef struct egi_touch_data EGI_TOUCH_DATA in "egi.h" */
static EGI_TOUCH_DATA live_touch_data;  /* !!! TODO & Warning --- Not mutex locked */

static bool tok_loopread_running;       /* token for loopread is running if true */
static bool cmd_end_loopread;	        /* command to end loopread if true */
static pthread_t thread_loopread;


/* timeWait for a touch: mutex protected data */
static EGI_POINT 	wpxy;		/* touching point coord.*/
static EGI_TOUCH_DATA  	wtouch_data;	/* timewait event touch data */
static pthread_cond_t	cond_touch;	/* To indicate that touch_status 'pressing' is detected.
					 * XXX or 'pressed_hold' is detected */
static pthread_mutex_t	mutex_lockCond; /* mutex lock for pthread cond */
static int		flag_cond=-1;	/* predicate for pthread cond, set in egi_touch_loopread()
					 * flag_cond:  == 0, untouched,  ==1 touched
					 * flag_cond:  == -1, idle.
					 */
static bool tok_loopread_nowait;	/* If ture, touch_loopread will run nonstop,
					 * otherwise it will wait until live_touch_data.updated becomes false,
					 * after last data is read out.
					 */


/*--------------------------------------------------------------
To check whether it touchs/moves on an EGI_RECTBTN


Return:
	true	Yes
	false	No OR Fails
---------------------------------------------------------------*/
bool egi_touch_on_rectBTN(EGI_TOUCH_DATA *touch_data, EGI_RECTBTN *btn)
{
	if(touch_data==NULL || btn==NULL)
		return false;

	if(touch_data->status==pressing || touch_data->status==pressed_hold ) {
	       return pxy_inbox( touch_data->coord.x, touch_data->coord.y, btn->x0, btn->y0,
				 		btn->x0+btn->width, btn->y0+btn->height );
	}

	return false;
}


/*----------------------------------------------------------------------------------
Wait for a touch 'pressing' event, return on the event or
when time is out.

@s:	      Timeout in secondes.
	      If s<0, persistent wait.
@ms:	      Timeout in Millisecond.
@touch_data   Touch data on the event
	      If NULL, ignored.

Retrun:
	0	Ok
	<0	Fails / Errors
	>0	Time out.
-------------------------------------------------------------------------------------*/
int egi_touch_timeWait_press( int s, unsigned int ms, EGI_TOUCH_DATA *touch_data) //EGI_POINT *pxy)
{
	int ret=0;
	int wait_ret=0;
	struct timeval now;
	struct timespec outtime;
	unsigned int ns;

	/* Assert thread loop_read is running */
	if(!tok_loopread_running)
		return -1;

	/* wait flag_cond AND cond signal */
	if( pthread_mutex_lock(&mutex_lockCond)==0 ) {
//		printf("%s: Enter cond_timewait zone ...\n",__func__);
/*  --- >>>  Critical Zone  */
		/* Must make loopread unstop */
		tok_loopread_nowait=true;

		/* reset flag_cond */
		flag_cond=0;

		/* wait flag and timeout:  flag_cond and cond_signal to be received simutaneously */
		while(flag_cond==0) {
			if( s<0 ) { 	/* --- persistent wait --- */
				if(pthread_cond_wait(&cond_touch, &mutex_lockCond)==0 && flag_cond==1)
					break;
			}
			else {		/* --- Time wait --- */
				/* ??? If invoked by incorrect signal, then outtime will reset ?!!! */
				gettimeofday(&now, NULL);
				ns=now.tv_usec*1000+ms*1000000;
				if(ms ==0 ) {
					outtime.tv_sec = now.tv_sec + s;
					outtime.tv_nsec = ns; //now.tv_usec*1000+ms*1000000;
				} else {
					outtime.tv_sec = now.tv_sec+s+ns/1000000000;
					outtime.tv_nsec = ns%1000000000;
				}
				/* timedwait, wait_ret==0 if cond_touch is received.*/
				wait_ret=pthread_cond_timedwait(&cond_touch, &mutex_lockCond, &outtime);
				if(wait_ret==ETIMEDOUT)
					break;
			}
		}

		/* Reset loopread to wait mode */
		tok_loopread_nowait=false;

		/* Reset updated token, as we read out pxy */
		live_touch_data.updated=false;

		pthread_mutex_unlock(&mutex_lockCond);
/*  --- <<<   Critical Zone  */

		/* check return value*/
		if(wait_ret==0) {		/* OK, signal received. */
			ret=0;
			//printf("%s: Cond signal received!\n",__func__);
			if( touch_data != NULL )
				*touch_data=wtouch_data;
			/* pass touched pointer coord */
//			if( pxy != NULL )
//				*pxy=wpxy;	/* Maybe a litte later */
		}
		else if(wait_ret==ETIMEDOUT) {	/* Time Out */
			ret=1;
			//printf("%s: Time out!\n",__func__);
		}
		else {
			ret=-2;			/* Error */
			printf("%s: Error!\n",__func__);
		}

	}
	else {
                printf("%s: Fail to lock mutex_lockCond!\n",__func__);
		return -2;
	}

	/* reset flag_cond to -1 as idle */
	flag_cond=-1;

	return ret;
}


/*------------------------------------------------------------------------
Wait for a touch 'releasing' event, return on the event or when time is out.

@s:	      Timeout in secondes.
	      If s<0, persistent wait.
@ms:	      Timeout in Millisecond.
@touch_data   Touch data on the event
	      If NULL, ignored.
Retrun:
	0	Ok
	<0	Fails / Errors
	>0	Time out.
------------------------------------------------------------------------*/
int egi_touch_timeWait_release(int s, unsigned int ms, EGI_TOUCH_DATA *touch_data)
{
	int ret=0;
	int wait_ret=0;
	struct timeval now;
	struct timespec outtime;
	unsigned int ns;

	/* Assert thread loop_read is running */
	if(!tok_loopread_running)
		return -1;

	/* wait flag_cond AND cond signal */
	if( pthread_mutex_lock(&mutex_lockCond)==0 ) {
//		printf("%s: Enter cond_timewait zone ...\n",__func__);
/*  --- >>>  Critical Zone  */
		/* Must make loopread unstop */
		tok_loopread_nowait=true;

		/* reset flag_cond to 1, as touched. */
		flag_cond=1;

		/* wait flag and timeout:  flag_cond and cond_signal to be received simutaneously */
		while(flag_cond==1) {
			if( s<0 ) { /* --- persistent wait --- */
				if(pthread_cond_wait(&cond_touch, &mutex_lockCond)==0 && flag_cond==0)
					break;
			}
			else {	/* --- Time wait --- */
				/* ??? If invoked by incorrect signal, then outtime will reset ?!!! */
				gettimeofday(&now, NULL);
				ns=now.tv_usec*1000+ms*1000000;
				if(ms ==0 ) {
					outtime.tv_sec = now.tv_sec + s;
					outtime.tv_nsec = ns; //now.tv_usec*1000+ms*1000000;
				} else {
					outtime.tv_sec = now.tv_sec+s+ns/1000000000;
					outtime.tv_nsec = ns%1000000000;
				}
				/* timedwait, wait_ret==0 if cond_touch is received.*/
				wait_ret=pthread_cond_timedwait(&cond_touch, &mutex_lockCond, &outtime);
				if(wait_ret==ETIMEDOUT)
					break;
			}
		}

		/* Reset loopread to wait mode */
		tok_loopread_nowait=false;

		/* Reset updated token, as we read out pxy */
		live_touch_data.updated=false;

		pthread_mutex_unlock(&mutex_lockCond);
/*  --- <<<   Critical Zone  */

		/* check return value*/
		if(wait_ret==0) {		/* OK, signal received. */
			ret=0;
			//printf("%s: Cond signal received!\n",__func__);
			/* pass touch data */
			if( touch_data != NULL )
				*touch_data=wtouch_data;
		}
		else if(wait_ret==ETIMEDOUT) {	/* Time Out */
			ret=1;
			//printf("%s: Time out!\n",__func__);
		}
		else {
			ret=-2;			/* Error */
			printf("%s: Error!\n",__func__);
		}

	}
	else {
                printf("%s: Fail to lock mutex_lockCond!\n",__func__);
		return -2;
	}

	/* reset flag_cond to -1 as idle */
	flag_cond=-1;

	return ret;
}


/*-----------------------------------------------------------------
@nowati
Ture:	Touch_loopread thread will not check live_touch_data.updated
	it will update live_touch_data continously.
False:  Touch loopread thread will wait until live_touch_data.updated
	is reset to FALSE after read out.

------------------------------------------------------------------*/
void egi_touchread_nowait(bool nowait)
{
	tok_loopread_nowait=nowait;
}

/*-----------------------------------
Start touch_loopread thread.

TODO: Make it a pthread_once_t function.

Return:
	0	Ok
	<0	Fails
------------------------------------*/
int egi_start_touchread(void)
{
	/* open touch_spi dev  */
        if( SPI_Open()<0 ) {
                printf("%s: Fail to open spi device '%s' for touch screen reading!\n", __func__, spi_fdev);
		return -1;
	}

	/* initiliaze pthread mutex cond */
	if( pthread_mutex_init(&mutex_lockCond,NULL) != 0 ) {
                printf("%s: Fail to initialize mutex_lockCond!\n", __func__);
		return -2;
	}
	if( pthread_cond_init(&cond_touch, NULL) !=0 ) {
                printf("%s: Fail to initialize cond_touch!\n", __func__);
		return -3;
	}
	/* reset cond flag */
	flag_cond=0;

	/* start touch_read thread */
        if( pthread_create(&thread_loopread, NULL, (void *)egi_touch_loopread, NULL) !=0 )
        {
                printf("%s: Fail to create touch_read thread!\n", __func__);
		return -4;
        }

	/* reset token */
	tok_loopread_running=true;

	return 0;
}


/*-----------------------------------
Stop touch read thread.
Return:
	0	Ok
	<0	Fails
	>0 	Thread is not running.
------------------------------------*/
int egi_end_touchread(void)
{
	int ret=0;

	if(tok_loopread_running==false)
		return 1;

	/* Set indicator to end loopread */
	cmd_end_loopread=true;

	/* Wait to join touch_loopread thread */
	if( pthread_join(thread_loopread, NULL) !=0 ) {
		printf("%s:Fail to join thread_loopread.\n", __func__);
		ret-=1;
	}

	/* destroy mutex cond */
        if( pthread_cond_destroy(&cond_touch) !=0 ) {
		printf("%s:Fail to destroy cond_touch.\n", __func__);
		ret-=2;
	}

	/* destroy mutex lock */
        if( pthread_mutex_destroy(&mutex_lockCond) !=0 ) {
		printf("%s:Fail to destroy mutex_lockCond.\n", __func__);
		ret-=4;
	}

	/* close SPI dev */
	SPI_Close();

	return ret;
}

/*------------------------------------------
Return touch_loopread thread status.
------------------------------------------*/
inline bool egi_touchread_is_running(void)
{
	return tok_loopread_running;
}


/*------------------------------------------
pass touch data to the caller

return:
	true	get updated data
	false	obsolet data
------------------------------------------*/
bool egi_touch_getdata(EGI_TOUCH_DATA *data)
{
	if(!live_touch_data.updated) {
		if(data != NULL)
			data->updated=false;
		return false;
	}

	/* pass data, directly assign */
	if(data!=NULL)
		*data=live_touch_data;

	/* reset update flag */
	live_touch_data.updated=false;

	//printf("--------- touch get data -----------\n");

	return true;
}


/*---------------------------------------------------------------
Map touch data(coord,dx,dy) to the same coord sys as current
FBDEV's pos_rotate set.

Note: We suppose that default/HW_set coord sys of FB and TOUCH PAD
      were set as the same!

(screen's)

@fbdev:		FBDEV
@touch_data:	Pointer to touch_data, which will be modified.
//@spang:		Rotation angle of screen coord. relative to touch pad coord.
rpos:		Rotation position of screen  coord. relative to touch pad coord.
		0:	Default position
		1:      Clockwise 90 deg
		2:	Clockwise 180 deg
		3:	Clockwise 270 deg
return:
	0	OK
	<0	Fails
----------------------------------------------------------------*/
int egi_touch_fbpos_data(FBDEV *fbdev, EGI_TOUCH_DATA *touch_data, int rpos)
{
	int pxres,pyres;  /* upright screen resolutions */
	int tx,ty;
	int dx,dy;

	if(fbdev==NULL || touch_data==NULL)
		return -1;

	/* Screen coord. rotation relative to touch pad coord. */
	//rpos=(spang/90)%4;
	rpos = rpos%4;
	//if(rpos<0)rpos+=4;

	/* Resolution for X and Y direction, as per pos_rotate */
        pxres=fbdev->pos_xres;
        pyres=fbdev->pos_yres;

	/* get original/raw touch data */
	tx=touch_data->coord.x;
	ty=touch_data->coord.y;
	dx=touch_data->dx;
	dy=touch_data->dy;

        /* check FB.pos_rotate, and map touch_data to FB pos_rotate coord.
         * IF FB 90 Deg rotated: touch_Y maps to POS_FB.X,  touch_X maps to POS_FB.Y
         */
        switch( rpos ) {
                case 0:                 /* FB defaul position */
                        break;
                case 1:                 /* Clockwise 90 deg */
			touch_data->dx = dy;
			touch_data->dy = -dx;
			touch_data->coord.x = ty;
			touch_data->coord.y = (pyres-1)-tx;
                        break;
                case 2:                 /* Clockwise 180 deg */
			touch_data->dx = -dx;
                        touch_data->dy = -dy;
			touch_data->coord.x = (pxres-1)-tx;
			touch_data->coord.y = (pyres-1)-ty;
                        break;
                case 3:                 /* Clockwise 270 deg */
                        touch_data->dx = -dy;
                        touch_data->dy = dx;
			touch_data->coord.x = (pxres-1)-ty;
			touch_data->coord.y =  tx;
                        break;
        }

	return 0;
}


/*------------------------------------------
Check the touch data, but do NOT read out.
------------------------------------------*/
EGI_TOUCH_DATA egi_touch_peekdata(void)
{
	while(!live_touch_data.updated) {
        	tm_delayms(2);
	}

	return live_touch_data;
}

/*--------------------------------------------
Check dx but do NOT read out or reset token.
Usually use it to check touch_slide condition.
---------------------------------------------*/
inline int egi_touch_peekdx(void)
{
	while(!live_touch_data.updated) {
        	tm_delayms(2);
	}

	return live_touch_data.dx;
}

/*--------------------------------------------
Check dY but do NOT read out or reset token.
Usually use it to check touch_slide condition.
---------------------------------------------*/
inline int egi_touch_peekdy(void)
{
	while(!live_touch_data.updated) {
        	tm_delayms(2);
	}

	return live_touch_data.dy;
}


/*---------------------------------------------
Check dx,dy  but do NOT read out or reset token.
Usually use it to check touch_slide condition.
-----------------------------------------------*/
inline void egi_touch_peekdxdy(int *dx, int *dy)
{
	while(!live_touch_data.updated) {
        	tm_delayms(2);
	}

	if(dx != NULL)
		*dx=live_touch_data.dx;
	if(dy != NULL)
		*dy=live_touch_data.dy;
}


/*------------------------------------------
Check the touch status, but do NOT read out.
------------------------------------------*/
enum egi_touch_status egi_touch_peekstatus(void)
{
	while(!live_touch_data.updated) {
        	tm_delayms(2);
	}

	return live_touch_data.status;
}



/* ------------------     A Thread Function    ----------------------
loop in reading touch data and updating live_touch_data

NOTE:
	1. tm_timer(): start tick timer before run this function.
--------------------------------------------------------------------*/
void *egi_touch_loopread(void)
{
	int ret;
	uint16_t sx,sy; /* last x,y */
	struct egi_point_coord sxy;
	int last_x=0,last_y=0; /* last recorded x,y */

        /* for time struct */
        struct timeval t_start,t_end; /* record two pressing_down time */
        long tus; /* time	 in us */

	/* reset status */
	enum egi_touch_status last_status;

	last_status=released_hold;
	live_touch_data.coord.x=0;
	live_touch_data.coord.y=0;
	live_touch_data.dx=0;
	live_touch_data.dy=0;
	live_touch_data.status=released_hold;

	/* Init. calibrated factors */
	xpt_init_factors();

	/* Loop touch reading... */
	while(1)
 	{
/*
enum egi_touch_status 	 !!! --- TO see lateset in egi.h --- !!!
{
        unkown=-1,
        releasing=0,
        pressing=1,
        released_hold=2,
        pressed_hold=3,
        db_releasing=4,
        db_pressing=5,
};
*/
		/* Check ending indicator */
		if(cmd_end_loopread) {
			break;
		}

	        /* 1. Necessary wait,just for XPT to prepare data */
        	tm_delayms(2);

		/* Wait .... until read out,  AND NOT nowait mode */
		if( live_touch_data.updated==true && !tok_loopread_nowait )
			continue;

		/* 2. Read XPT to get avg tft-LCD coordinate */
        	//printf("start xpt_getavt_xy() \n");
        	ret=xpt_getavg_xy(&sx,&sy); /* if fail to get touched tft-LCD xy */
		sxy.x=sx; sxy.y=sy;

        	/* 3. touch reading is going on... */
        	if(ret == XPT_READ_STATUS_GOING )
        	{
			//printf("XPT READ STATUS GOING ON....\n");
               	 	/* DO NOT assign last_status=unkown here!!! because it'll always happen!!!
                           and you will never get pressed_hold status if you do so. */

			continue; /* continue to loop to finish reading touch data */
		}

               	/* 4. put PEN-UP status events here */
                else if(ret == XPT_READ_STATUS_PENUP )
                {
                        if(last_status==pressing || last_status==db_pressing || last_status==pressed_hold)
                        {
                                last_status=releasing; /* or db_releasing */
                                EGI_PDEBUG(DBG_TOUCH,": ... pen releasing ... \n");

				/* update touch data after wtouch_data update!*/
				live_touch_data.coord=sxy; /* record the last point coord */
				live_touch_data.status=releasing;
				live_touch_data.updated=true;

				/* reset flag_cond AND send cond signal simultaneously */
				if(flag_cond==1)
				{
				   printf("%s: 'releasing' reset flag_cond to 0...\n",__func__);
				   if( pthread_mutex_lock(&mutex_lockCond) ==0 ) {
					flag_cond=0;
					wtouch_data=live_touch_data; //wpxy=sxy; /* assign touching data */
					pthread_cond_signal(&cond_touch);
					pthread_mutex_unlock(&mutex_lockCond);
				   }
				   else {
			                printf("%s: Fail to lock mutex_lockCond!\n",__func__);
				   }
				}
				else {
				   printf("%s: 'releasing', BUT flag_cond already 0...\n",__func__);
				}

				/* reset sliding deviation ---- After wtouch_data update! ---- */
				live_touch_data.dx=0;
				live_touch_data.dy=0;
				last_x=sx;
				last_y=sy;

                        }
                        else /* last_status also released_hold */
			{
                                last_status=released_hold;
				/* update touch data */
				live_touch_data.updated=true;
				live_touch_data.status=released_hold;

				/* reset last_x,y */
				last_x=0;
				last_y=0;
			}

                        tm_delayms(40); //100);/* hold on for a while to relive CPU load, or the screen will be ...heheheheheh... */
                }

		/* 5. get touch coordinates and trigger actions for the hit button if any */
		else if(ret == XPT_READ_STATUS_COMPLETE) /* touch action detected */
		{
                        /* CASE HOLD_ON: check if hold on */
                        if( last_status==pressing || last_status==db_pressing || last_status==pressed_hold )
                        {
                                last_status=pressed_hold;
                                EGI_PDEBUG(DBG_TOUCH," ... pen hold down ...\n");

				/* update touch data */
				live_touch_data.coord=sxy;
				live_touch_data.updated=true;
				live_touch_data.status=pressed_hold;

				/* sliding deviation */
				live_touch_data.dx += (sx-last_x);
				last_x=sx;
				live_touch_data.dy += (sy-last_y);
				last_y=sy;

				EGI_PDEBUG(DBG_TOUCH,"egi_touch_loopread(): ...... dx=%d, dy=%d ......\n",
								live_touch_data.dx,live_touch_data.dy );

                        }
                        else /* CASE PRESSING: it's a pressing action */
                        {
                                last_status=pressing;
				/* update touch data */
				live_touch_data.dx = 0;
				live_touch_data.dy = 0;
				live_touch_data.coord=sxy;
				live_touch_data.updated=true;
				live_touch_data.status=pressing;

				last_x=sx;
				last_y=sy;
                      		EGI_PDEBUG(DBG_TOUCH,"... pen pressing ...\n");

				/* set flag_cond AND send cond signal simultaneously */
				if(flag_cond==0)
				{
				   printf("%s: 'Pressing' set flag_cond to 1...\n",__func__);
				   if( pthread_mutex_lock(&mutex_lockCond) ==0 ) {
					flag_cond=1;
					wtouch_data=live_touch_data; //wpxy=sxy; /* assign touching data */
					pthread_cond_signal(&cond_touch);
					pthread_mutex_unlock(&mutex_lockCond);
				   }
				   else {
			                printf("%s: Fail to lock mutex_lockCond!\n",__func__);
				   }
				}

                                /* check if it's a double-click   */
                                t_start=t_end;
                                gettimeofday(&t_end,NULL);
                                tus=tm_diffus(t_end,t_start);
                                //printf("------- diff us=%ld  ---------\n",tus);
                                if( tus < TM_DBCLICK_INTERVAL )
                                {
                                        EGI_PDEBUG(DBG_TOUCH,"egi_touch_loopread(): ... ... ... double click,tus=%ld    \
											  ... ... ...\n",tus);
                                        live_touch_data.status=db_pressing;
                                }
                        }
                        //eig_pdebug(DBG_TOUCH,"egi_touch_loopread(): --- XPT_READ_STATUS_COMPLETE ---\n");

		}

	}/* while() end */


 	return (void *)0;
}


//////////////////////  touch functions for ADS7846 based touchscreen and sensor  ///////////////////////
/* NOTE:
 *	1. Touch sensor as one of input devices, its functions SHOULD be classified in "egi_input.c".
	   you CAN ALSO call egi_read_kbdcode() to get ABS_X/Y values. HOWEVER, abskey/absvalue in kstat
	   are NOT buffered! and it ONLY returns the last stauts.
 *	2. Here, the touch sensor input device is assumed to be /dev/input/event0,
	   while egi_read_kbdcode() scans/monitors all /dev/input/eventX instead.
	   OR you can read '/proc/bus/input/devices' to pick the right event device in /dev/input/event*.
	   OR mouseX/mice if it also has mouse handler registered.
	3. Following functions is especially for testing ADS7846 touch screen.
 */


/*-----------------------------------------------------------
Wait for touch press in BLOCK mode.

Note:
1. !!! ----- CAUTION ----- !!!
   fd_touch MUST be open each time before calliing select()!
   If the touch device has other file descriptor refering
   to it, then it will affect select() behavour! ???
2. TODO&TBD
   If fd_touch is close after select, then all data in file buffer
   will BE LOST!

@timeout:  Time interval that select() should block waiting for
  	   a file descriptor to become ready, the call will block
	   until either:
       		*  a file descriptor becomes ready;
       		*  the call is interrupted by a signal handler; or
       		*  the timeout expires.

	   If is NULL, then it can block indefinitely.
Return:
	0	OK
	<0	Fails
-----------------------------------------------------------*/
int egi_touch_waitPress(const struct timeval *timeout)
{
	static int fd_touch=-1; 	/* File descriptor for touch event device */
	struct timeval tmout;
        fd_set rfds;
	int res;
	int ret=0;

	/* Open input touch device each time before calling select() */
	if(fd_touch<0) {
           if((fd_touch = open(INPUT_TOUCH_DEVNAME, O_RDONLY|O_CLOEXEC, 0))<0)
		return -1;
	}

        /* Select and read fd[] */
        FD_ZERO(&rfds);
	FD_SET(fd_touch, &rfds);

	/* Select fd_touch, with consideration of blocking mode. */
	if(timeout==NULL)
	        res=select(fd_touch+1, &rfds, NULL, NULL, NULL); /* Block indefinitely */
	else {
		/* Reset timeout value each time before calling select,
		 * On Linux, select() modifies timeout to reflect the amount of time not slept!
		 */
		tmout=*timeout;
        	res=select(fd_touch+1, &rfds, NULL, NULL, &tmout);
	}

	if(res<0) {
		egi_dperr("Fail to select fd_touch");
		ret=-2;
	}
	else if(res==0) { /* Impossible when timeout set as NULL ?? */
		egi_dpstd("Select res=0!\n");
		ret=-3;
	}
	//else
	//	egi_dpstd("res=%d\n", res);

	/* Close fd_touch. IF close, then data in file buffer will LOST! */
	//close(fd_touch);

	return ret;
}

/*---------------------------------------------------------------------------

	<<< --- Touchpad 5_points calibration --- >>>

                X--- Calibration refrence points
    ( A 320x240 LCD + touchpad, with common ORIGIN(0,0) )

   X <-------------------  Max. 320 ------------------> X (0,0)
   ^  x[2]y[2]                                x[0]y[0]  ^
   |                                                    |
   |                                                    |
   |                                                    |
  Max. 240                 X x[4]y[4]                Max. 240
   |                                                    |
   |                                                    |
   |                                                    |
   V   x[3]y[3]                               x[1]y[1]  V
   X <-------------------  Max. 320 ------------------> X

Tpxy --- Touch pad XY coordinate data read from input device adsXXX.
Lpxy --- LCD XY coordinates in pixels.

T1. Touch point data corresponding to LCD coordinates.
 Tpxy[0]:(349,209) Tpxy[1]:(3522,210) Tpxy[2]:(542,3459) Tpxy[3]:(3482,3687) Tpxy[4]:(2113,2074)
 Lpxy[]={ (333,188), (3750,188), (295,3695), (3830,3675), (2080,1965) }

T2. Computation for factX/Y and center Base point XY.
 factX= 1.0*((Lpx[1]-Lpx[0] + Lpx[3]-Lpx[2])/2) / ((Tpx[1]-Tpx[0] + Tpx[3]-Tpx[2])/2);
 factY= 1.0*((Lpy[2]-Lpy[0] + Lpy[3]-Lpy[1])/2) / ((Tpy[2]-Tpy[0] + Tpy[3]-Tpy[1])/2);

 factX=1.0*240/(((3750-333)+(3830-295))/2)=0.0690;
 factY=1.0*320/(((3695-188)+(3675-188))/2)=0.0915;
 Center base_XY: (2080,1965)  (touch data)

T3. Formula for mapping touch data to LCD coordinate:

   Lpx = (Tpx-baseX)*factX + 240/2;
   Lpy = (Tpy-baseY)*faceY + 320/2;


Note:
1. The function opens touch input device in nonblocking mode (fd is static here!).
   and read ABS_X/Y samplings, then map to get corresponding LCD coordinates.

		!!! --- CAUTION --- !!!
2. File descriptor fd is a static variable here, so this function does NOT support multip-thread!

3. Notice event report sequence in adsxxx driver: BTN_TOUCH, ABS_X, ABS_Y, PRESSURE.
4. If 'TOUCH preess': then initiate/clear all vars, (XXX in case
   there are old data still remained in file buffer.)
5. If 'TOUCH release': then close(fd) and return 1;
6. If any error happens(except EAGAIN), close(fd) and set fd=-1;

TODO:
1. A select() operation in egi_touch_waitPress() will take away the first BTN_TOUCH event!?

@pt:       Pointer to pass out touch point in LCD coordinates.
	   (Driver set in MAX 12bits)
	   If null, ignore.
@pressure: Pointer to pass out pressure value.
	   (Driver set in MAX 8bits, Normally return value: ~120 - ~230)
	   If null, ignore.

Return:
	0	OK
	1	Touch release
	        then *pressure reset to be 0! *pt keeps unchanged!
	<0 	Fails
---------------------------------------------------------------------------*/
int egi_touch_readXYP(EGI_POINT *pt, int *pressure)
{
	static int fd=-1;   /* This  */
	int 	   rc;
	struct input_event event;
	const int     np=2;	  /* Exponent for samples */
	const int     ns=(1<<np); /* samples */

	/* ni, sumTX/TY, tmpX/Y to init/clear in case BTN_TOUCH */
	int 	      ni=0;		/* sample index */
	int	      sumTX=0, sumTY=0, sumPress=0; /* sum up all raw X/Y as per ns */
	int           tmpX=-1,tmpY=-1, tmpP=-1;     /* <0 as invalid */
//	static bool   Tpressing=false;   /* the first press point */

	/* Conversion factors */
	const float factX=0.0690;
	const int   factX_num=690, factX_div=10000; /* For fixed point */
	const float factY=0.0915;
	const int   factY_num=915, factY_div=10000;
	const int   baseX=2080, baseY=1965; /* TouchPad Center BASE */

	/* Open input touch device */
	if(fd<0) {
            if((fd = open(INPUT_TOUCH_DEVNAME, O_RDWR|O_NONBLOCK|O_CLOEXEC, 0))<0)
		return -1;
	}

	/* Read ns samples */
	while(1) {
		rc=read(fd, &event, sizeof(event));

		/* Break if read error */
		if( rc<0 ) {
			/* Close fd is errno is NOT EAGAIN */
			if(errno!=EAGAIN) {
			   printf("Read ERROR! rc=%d\n", rc);
			   close(fd); fd=-1;
			   break;
			}
			else {   /* errno==EAGAIN */
			   //printf("read EAGAIN!\n");
			   //usleep(5000);
			   continue;
			}
		}

#if 0 /* TEST: ------------------------- */
            printf("%-24.24s.%06lu type 0x%04x; code 0x%04x;"
                " value 0x%08x; ",
        	ctime(&event.time.tv_sec),
                event.time.tv_usec,
                event.type, event.code, event.value);
#endif

           switch (event.type) {
		/* S1. Touch Press/release event */
		case EV_KEY:
		    switch( event.code ) {
		       case BTN_TOUCH:    /* BTN_TOUCH: 0x14a */
			  egi_dpstd("BTN_TOUCH, event.value=%d\n", event.value);
			  if( event.value ) {
			        egi_dpstd(" >>>>> TOUCH press \n");
				//Tpressing=true;

				/* Init and clear. In case there are old data in event buffer before the TOUCH... */
				ni=0; tmpX=-1; tmpY=-1; tmpP=-1;
				sumTX=0; sumTY=0; sumPress=0;
			  }
			  else {
			        egi_dpstd(" TOUCH release <<<<<\n");
				//Tpressing=false;

				/* Empty all data */
				//read(fd, &event, sizeof(event)); //NO WAY! nonblocking mode!

				/* Clear pressure */
				if(pressure) *pressure=0;

				/* Note:  */
				close(fd); fd=-1;

				return 1;
			  }
		          break;

		       default:
			  egi_dpstd("event.code: 0x%x, .value: %d\n", event.code, event.value);

    		    } /* END switch( event.code ) */
		    break;

		/* S2. Touch position/pressure abs. value */
                case EV_ABS:
                    switch (event.code) {
                       case ABS_X:
				/* Update tmpX */
				tmpX=event.value;
				break;
                       case ABS_Y:
				/* Update tmpY */
				tmpY=event.value;
			        break;
                       case ABS_PRESSURE:
				/* Update tmpP */
				tmpP=event.value;
				break;
                       default:            break;
                    }

		    /* Update touch status */
		    //touch.status=pressed_hold;
                    break;
	   } /* END switch(evetn.type) */

	   /* NOTE: Event report sequence in adsxxx driver: BTN_TOUCH, ABS_X, ABS_Y, PRESSURE */

	   /* Check caller's read choice: 'pt' and/or/neither 'pressure'  */
	   /* C1. Only 'pt' */
	   if( pt && !pressure ) {
	   	   /* Assume ABS_X ALWAYS preceds ABS_Y and come as a pair!!!  Ignore invalid data. */
		   if(tmpY>=0 && tmpX<0 ) {
			printf(" ---- X Missing ----\n");
			tmpY = -1; /* Reset Y */
			continue;
		   }
		   /* Sum up XYP. ONLYABS_X,ABS_Y and PRESSURE events all have been received.  */
		   else if(ni<ns && tmpX>=0 && tmpY>=0 ) {
			sumTX += tmpX;
			sumTY += tmpY;

			egi_dpstd("tmpXYP[%d]: (%d, %d)\n", ni, tmpX, tmpY);

			tmpX=-1; tmpY=-1; /* reset */
			ni++; /* Sampling increment */
//		   	printf(": ni=%d, sumTX=%d, sumTY=%d\n", ni, sumTX, sumTY);
	   	}
	   }
	   /* C2. Both 'pt' and 'pressure' */
	   else if( pt && pressure ) { /* pressure!=NULL */
		   /* Check sequence and missing events */
		   if(tmpY>=0 && tmpX<0 ) {
			egi_dpstd(" ---- X Missing ----\n");
			tmpY = -1; /* Reset Y */
			tmpP = -1; /* Reset P */
			continue;
		   }
		   else if( tmpP>=0 && tmpY<0 ) {
			egi_dpstd(" ---- Y Missing ----\n");
			tmpX = -1;
			tmpP = -1;
		   }
		   /* Sum up XYP. ONLYABS_X,ABS_Y and PRESSURE events all have been received.  */
		   else if(ni<ns && tmpX>=0 && tmpY>=0 && tmpP>=0 ) {
			sumTX += tmpX;
			sumTY += tmpY;
			sumPress +=tmpP;

			egi_dpstd("tmpXYP[%d]: (%d, %d), %d \n", ni, tmpX, tmpY, tmpP);

			tmpX=-1; tmpY=-1; tmpP=-1; /* reset */
			ni++; /* Sampling increment */
//		   	printf(": ni=%d, sumTX=%d, sumTY=%d\n", ni, sumTX, sumTY);
	   	}
	   }
	   /* C3. Only 'pressure' */
	   else if(pressure) {
		if(ni<ns && tmpP>=0) {
			sumPress +=tmpP;
			printf("pressure[%d]: %d \n", ni, tmpP);

			tmpP=-1; /* reset */
			ni++; /* Sampling increment */
		}
	   }

	   /* Checking (ns) samplings */
	   if(ni==ns)
		break;

	} /* END While() */

	/* If while() breaks unexpectedly, ni!=ns then. */
	if(rc<0) {
//		printf("Read touch fails! rc=%d\n", rc);
		close(fd); fd=-1;
		return -3;
	}

	/* Average samplings, and map to get LCD coordinates. */
	if(pt) {
	   sumTX >>=np;  /* Average TX/TY */
	   sumTY >>=np;

	   /* Convert to get Pxy */
	   #if 0 /* Facts in float point */
           pt->x=(sumTX-baseX)*factX + 240/2;
           pt->y=(sumTY-baseY)*factY + 320/2;
	   #else /* Facts in fixed point */
           pt->x=(sumTX-baseX)*factX_num/factX_div + 240/2;
           pt->y=(sumTY-baseY)*factY_num/factY_div + 320/2;
	   #endif
	}
	if(pressure) {
	   sumPress >>=np;
	   *pressure=sumPress;
	}

/* TEST: ------------- */
       	//close(fd);

	return 0;
}
