/*-------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

An example of making a HTTP request to www.heweather.com and get weather
information for the specified location, then transfrom it to an image and
save as a PNG file.

Midas Zhou
midaszhou@yahoo.com
---------------------------------------------------------------------*/
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "egi_common.h"
#include "he_weather.h"
#include "egi_FTsymbol.h"


#define HE_UPDATE_INTERVAL	180    /* 90, in seconds */

int main(int argc, char **argv) {

	int index;
        int n=0;
	int nday=1; /* forecast day: 1-today, 2-tommorrow ,3-the day aft. tommorrow */
	int pixlen;
	int icon_width;
	char strtemp[16];
	int  temp;
	char strhum[16];
	int  hum;
	char strbuff[256]={0};

	int nloc=3;
	int locindex;
	char *location_names[3]=
	{
		"shanghai",
		"lasa",
		"zhoushan",
	};
	int  subnum=nloc*2;	/* total sub imges, today+tommoror */
	int  subindex;
  	int  i,j;
	int  pos;
	int  xs,ys;
	int  hs,ws;

        FBDEV vfb;
        EGI_IMGBUF *host_eimg=NULL;
        EGI_IMGBUF *icon_eimg=NULL;

        /* --- init log --- */
        if(egi_init_log("/mmc/log_weather") !=0 )
        {
           printf("egi_init_log() fails! \n");
           return -1;
        }

	/* display png cond image */
   	init_fbdev(&gv_fb_dev); /* init FB dev */

        /* --- load all symbol pages --- */
        if(symbol_load_allpages() !=0 ) {
		return -1;
	}
        if(FTsymbol_load_appfonts() !=0 ) {  /* and FT fonts LIBS */
                printf("Fail to load FT appfonts, quit.\n");
                return -2;
        }

	/* get request index */
	if(argc>1) {
		index=atoi(argv[1]);
		if(index<0 || index>3)
			index=0;
	}
	else	index=0;

	/* create dir for heweather */
	egi_util_mkdir("/tmp/.egi/heweather",0755);

	/* prepare host_eimg and virt FB */
        host_eimg=egi_imgbuf_alloc();
        egi_imgbuf_init(host_eimg, 60*subnum, 240); /* 3 rowss */

        /* set subimg */
        host_eimg->submax=subnum-1; /* 3 image data */
        host_eimg->subimgs=calloc(subnum,sizeof(EGI_IMGBOX));

	/* subimgs box */
	for(i=0; i< subnum; i++)
	        host_eimg->subimgs[i]=(EGI_IMGBOX){0,60*i,240,60};

	/* init Vrit FB */
        init_virt_fbdev(&vfb,host_eimg);

//	subindex=0;
	locindex=0;		/* location index */
//////////////////////////////    LOOP TEST    /////////////////////////////
while(1) {

	/* reset location index */
	locindex++;
	locindex=locindex%nloc;

	/* get NOW weather data */
	if(heweather_httpget_data(data_forecast, location_names[locindex]) !=0 ) {
		locindex--;
		goto SLEEP_WAIT;
	}
//	sleep(10);
//	continue;


    /*------------- 1-today, 2-tommorrow ---------*/
    for(nday=1; nday<3; nday++)
    {
	subindex=2*locindex+nday-1;

	/* reset alpha value for the subimg of host_eimg, which is a Virt FB.
	 * 1. Color value other than 0: When host_eimg works as vfb, its color value
	 *    will be counted as bk color, without weighing its alpha value!!!!
	 * 2. The reset alpha value will be blended with front alpha value later when call
	 *    writeFB() to vfb, So it will emphasize the front color, and weaken/darken the
	 *    bk color.
	 */
	printf(">>>>>> reset subindex=%d <<<<<<<\n", subindex);
	egi_imgbuf_reset(host_eimg, subindex, WEGI_COLOR_WHITE, 90);

	/* Write location name to VIRT FB */
        FTsymbol_uft8strings_writeFB(&vfb, egi_appfonts.regular,   	    /* FBdev, fontface */
                                           20, 20, weather_data[nday].location,    /* fw,fh, pstr */
                                           240, 1, 0,           	    /* pixpl, lines, gap */
                                           0, 8+subindex*60,                /* x0,y0, */
                                           WEGI_COLOR_GRAYC, -1, -1);   /* fontcolor, stranscolor,opaque */
	/* Write, today, tommorro */
        FTsymbol_uft8strings_writeFB(&vfb, egi_appfonts.regular,   	    /* FBdev, fontface */
                                           16, 16, nday==1?"(???)":"(???)",    /* fw,fh, pstr */
                                           240, 1, 0,           	    /* pixpl, lines, gap */
                                           10, 5+28+subindex*60,                /* x0,y0, */
                                           WEGI_COLOR_GRAYC, -1, -1);   /* fontcolor, stranscolor,opaque */


	/* Load icon to VIRT FB */
	pixlen=FTsymbol_uft8strings_pixlen(egi_appfonts.regular,20, 20, weather_data[nday].location);
	printf("---------- pxilen=%d ----------\n",pixlen);
	egi_imgbuf_windisplay(weather_data[nday].eimg, &vfb, WEGI_COLOR_GRAYC, 0, 0, pixlen, subindex*60, 60, 60);

        /* Write cond_txt to VIRT FB */
        sprintf(strbuff,"%s  %s%s???",weather_data[nday].cond_txt,
					weather_data[nday].wind_dir, weather_data[nday].wind_scale);

        FTsymbol_uft8strings_writeFB(&vfb, egi_appfonts.regular,   	/* FBdev, fontface */
                                           14, 14, strbuff,    		/* fw,fh, pstr */
                                           240, 1, 0,           	/* pixpl, lines, gap */
                                           pixlen+60, 8+subindex*60,     /* x0,y0, */
                                           WEGI_COLOR_GRAYC, -1, -1);   /* fontcolor, stranscolor,opaque */

        /* write temp & hum to VIRT FB */
        sprintf(strbuff, "??????%d/%dC  ??????%d%%",weather_data[nday].temp_max, weather_data[nday].temp_min,
						weather_data[nday].hum);
        FTsymbol_uft8strings_writeFB(&vfb, egi_appfonts.regular,   	/* FBdev, fontface */
                                           14, 14, strbuff,    		/* fw,fh, pstr */
                                           240, 1, 0,           	/* pixpl, lines, gap */
                                           pixlen+60, 30+subindex*60,   /* x0,y0, */
                                           WEGI_COLOR_GRAYC, -1, -1);   /* fontcolor, stranscolor,opaque */

        /* reset color value for all pixels in host_img */
//        egi_imgbuf_reset(host_eimg, -1, WEGI_COLOR_WHITE, -1);
#if 1	/* Rest alpha<91 to 0, ignore this if you prefer a little darker back ground. */
	xs=host_eimg->subimgs[subindex].x0;
	ys=host_eimg->subimgs[subindex].y0;
	hs=host_eimg->subimgs[subindex].h;
	ws=host_eimg->subimgs[subindex].w;
        for(i=ys; i<ys+hs; i++) {           /* transverse subimg Y */
        	if(i < 0 ) continue;
                if(i > host_eimg->height-1) break;
                for(j=xs; j<xs+ws; j++) {   /* transverse subimg X */
                	if(j < 0) continue;
                        if(j > host_eimg->width -1) break;
                        pos=i*host_eimg->width+j;
                        /* reset alpha < 90 */
                        if( host_eimg->alpha ) {
				if(host_eimg->alpha[pos]<(90+1))
					host_eimg->alpha[pos]=0;
			}
                }
	}
#endif
   } /* end of today, tommorrow */

        /* save to PNG file */
        if(egi_imgbuf_savepng("/tmp/.egi/heweather/now.png", host_eimg)) {
                printf("Fail to save imgbuf to PNG file!\n");
        }

SLEEP_WAIT:
	printf(" --------------------- N:%d ---------------\n", n);
	n++;

	if(n>nloc) /* fill up one round for all locations first */
		sleep(HE_UPDATE_INTERVAL); //90); /* Limit 1000 per day, 90s per call */


} ////////////////////////////    LOOP TEST END    ///////////////////////////////////


	printf("release host_eimg and virt FB...\n");
        egi_imgbuf_free(host_eimg);
        release_virt_fbdev(&vfb);

        printf("FTsymbol_release_allfonts()...\n");
        FTsymbol_release_allfonts();

        printf("symbol_release_allpages()...\n");
        symbol_release_allpages();

        printf("release_fbdev()...\n");
   	release_fbdev(&gv_fb_dev);

        printf("egi_quit_log()...\n");
        egi_quit_log();
        printf("---------- END -----------\n");


  return 0;
}
