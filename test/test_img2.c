/*------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

Test image blur/resize/update functions.

Usage:
	./test_img2  dir

Journal:
2022_04_05:
1. Test  egi_imgbuf_resize_nolock().

Midas Zhou
midaszhou@yahoo.com(Not in use since 2022_03_01)
------------------------------------------------------------------*/
#include <stdio.h>
#include "egi_common.h"
#include "egi_pcm.h"
#include "egi_FTsymbol.h"
#include "egi_utils.h"

int main(int argc, char** argv)
{
	int i,j,k;
	int ret;

	struct timeval tm_start;
	struct timeval tm_end;

        /* <<<<<  EGI general init  >>>>>> */
#if 1
        printf("tm_start_egitick()...\n");
        tm_start_egitick();		   	/* start sys tick */
        printf("egi_init_log()...\n");
        if(egi_init_log("/mmc/log_test") != 0) {	/* start logger */
                printf("Fail to init logger,quit.\n");
                return -1;
        }
#endif

#if 0
        printf("symbol_load_allpages()...\n");
        if(symbol_load_allpages() !=0 ) {   	/* load sys fonts */
                printf("Fail to load sym pages,quit.\n");
                return -2;
        }
        if(FTsymbol_load_appfonts() !=0 ) {  	/* load FT fonts LIBS */
                printf("Fail to load FT appfonts, quit.\n");
                return -2;
        }
#endif

        printf("init_fbdev()...\n");
        if( init_fbdev(&gv_fb_dev) )		/* init sys FB */
                return -1;

        /* Set sys FB mode */
        fb_set_directFB(&gv_fb_dev, false); //true);//false);
        fb_position_rotate(&gv_fb_dev,0);
        //gv_fb_dev.pixcolor_on=true;             /* Pixcolor ON */
        //gv_fb_dev.zbuff_on = true;              /* Zbuff ON */
        //fb_init_zbuff(&gv_fb_dev, INT_MIN);

	/* <<<<<  End EGI Init  >>>>> */


int blur_size;
int xsize=240;
int ysize=320;
EGI_IMGBUF* eimg=NULL;
EGI_IMGBUF* tmpimg=NULL;
EGI_IMGBUF* tmpimg2=NULL;

char**	fpaths=NULL;	/* File paths */
int	ftotal=0; 	/* File counts */
int	num=0;		/* File index */


#if 1 /* -------------- TEST:  egi_imgbuf_resize_nolock() ----------------------- */
	float scales;

	eimg=egi_imgbuf_readfile(argv[1]);
	if(eimg==NULL) {
        	EGI_PLOG(LOGLV_ERROR, "%s: Fail to read and load file '%s'!", __func__, argv[1]);
		return -1;
	}
        fb_clear_workBuff(&gv_fb_dev, WEGI_COLOR_DARKGRAY);
        egi_subimg_writeFB(eimg, &gv_fb_dev, 0, -1, 0,0);   /* imgbuf, fbdev, subnum, subcolor, x0, y0 */
        fb_render(&gv_fb_dev);
	sleep(1);

	bool scale_down=false;
	scales=1.0;
while(1) {
		/* Resize image */
		tmpimg=egi_imgbuf_resize_nolock(eimg, false, roundf(1.0*eimg->width*scales), 0.5*roundf(1.0*eimg->height*scales));
		if(tmpimg==NULL) {
			printf("egi_imgbuf_resize_nolock fails!\n");
		}
		printf("scales=%f, resize to: W%dxH%d, Ratio:%f\n", scales, tmpimg->width, tmpimg->height, 1.0*tmpimg->width/tmpimg->height);

		/* Display */
                fb_clear_workBuff(&gv_fb_dev, WEGI_COLOR_DARKGRAY);

	#if 1 //////////////////////////
	        egi_imgbuf_windisplay( tmpimg, &gv_fb_dev, -1,
        	                       0,0,(320-tmpimg->width)/2, (240-tmpimg->height)/2,
                	               tmpimg->width, tmpimg->height);

	#else /////////// Fit tmpimg to screen ////////////////
		tmpimg2=egi_imgbuf_resize_nolock(tmpimg, false, 320,240);
                egi_subimg_writeFB(tmpimg2, &gv_fb_dev, 0, -1, (320-tmpimg2->width)/2, (240-tmpimg2->height)/2);   /* imgbuf, fbdev, subnum, subcolor, x0, y0 */

	#endif
		usleep(50000);
                fb_render(&gv_fb_dev);

		/* Check size and scale factor */
		if(scale_down && tmpimg->width<10)
			scale_down=false;
		else if(!scale_down && tmpimg->width>640)
			scale_down=true;

		if(scale_down)
			scales *=0.9; //0.95
		else
			scales *=1.111; //1.01

		/* free */
		egi_imgbuf_free2(&tmpimg);
		egi_imgbuf_free2(&tmpimg2);
}
	exit(0);
#endif


    /* search for files and put to ffCtx->fpath */
    fpaths=egi_alloc_search_files(argv[1], "jpg,png", &ftotal);
    if(fpaths==NULL) {
		printf("No recognizable image file found!\n");
		exit(-1);
    }

do {    ////////////////////////////   1.  LOOP TEST   /////////////////////////////////

	/* 0. Check/reset index */
	if(num>ftotal-1)
		num=0;

        /* 1. Load pic to imgbuf */
	eimg=egi_imgbuf_readfile(fpaths[num]);
	if(eimg==NULL) {
        	EGI_PLOG(LOGLV_ERROR, "%s: Fail to read and load file '%s'!", __func__, fpaths[num]);
		return -1;
	}

        /* 2. Copy a Block from the image */
        /* If original image ratio is H4:W3, just same as SCREEN's. */
        if( eimg->height*3 == (eimg->width<<2) ) {
        	printf("Picture ratio of eimg is 4:3\n");
                tmpimg=egi_imgbuf_blockCopy( eimg,  0, 0,     /* eimg, xp,yp */
                	eimg->height, eimg->width );       /* H, W*/
        }
        /* Else if original image is square, or whatever else....
         * then copy a H4:W3 block from it. */
        else {
                printf("Picture ratio of eimg is NOT 4:3\n");
                tmpimg=egi_imgbuf_blockCopy(eimg,  eimg->width>>3, 0,   /* eimg, xp,yp */
                	eimg->height, eimg->width*3/4 );       		/* H, W*/
        }
	if(tmpimg==NULL) {
		printf("Fail to block copy eimg!\n");
		return -2;
	}

	/* Free eimg */
	egi_imgbuf_free(eimg); eimg=NULL;

        /* 2. blur and resize the imgbuf  */
        blur_size=tmpimg->height/100;
        if( tmpimg->height <240 ) {     /* A. If a small size image, blur then resize */
                egi_imgbuf_blur_update( &tmpimg, blur_size, false);
             ret=egi_imgbuf_resize_update( &tmpimg, false, xsize,ysize);
        }
        else  {                         /* B. If a big size image, resize then blur */
                egi_imgbuf_resize_update( &tmpimg, false, xsize, ysize);
            ret=egi_imgbuf_blur_update( &tmpimg, blur_size, false);
        }
        if(ret !=0 )
                EGI_PLOG(LOGLV_ERROR, "%s: Fail to blur and resize eimg!", __func__);

        /* 3. Adjust Luminance */
        egi_imgbuf_avgLuma(tmpimg, 255/3);

	/* 4. Display the image */
        egi_imgbuf_windisplay( tmpimg, &gv_fb_dev, -1,
                               0, 0, 0, 0,
                               tmpimg->width, tmpimg->height);

        /* 4.1 Refresh FB by memcpying back buffer to FB */
        fb_page_refresh(&gv_fb_dev, FBDEV_BKG_BUFF);

	/* 5. Free tmpimg */
	egi_imgbuf_free(tmpimg);
	tmpimg=NULL;

	/* 6. Clear screen and increase num */
	tm_delayms(500);
	clear_screen(&gv_fb_dev, WEGI_COLOR_BLACK);
	num++;

}while(1); ///////////////////////////   END LOOP TEST   ///////////////////////////////


        /* <<<<<  EGI general release >>>>> */
        printf("FTsymbol_release_allfonts()...\n");
        FTsymbol_release_allfonts();
        printf("symbol_release_allpages()...\n");
        symbol_release_allpages();
	printf("release_fbdev()...\n");
        fb_filo_flush(&gv_fb_dev);
        release_fbdev(&gv_fb_dev);
        printf("egi_quit_log()...\n");
        egi_quit_log();
        printf("<-------  END  ------>\n");

return 0;
}


