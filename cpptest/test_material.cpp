/*------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

Journal:
2021-09-22:
        1. Create test_material.cpp

Midas Zhou
midaszhou@yahoo.com
https://github.com/widora/wegi
------------------------------------------------------------------*/
#include "egi_debug.h"
#include "egi_fbdev.h"
#include "egi_math.h"
#include "egi_FTsymbol.h"
#include "e3d_trimesh.h"  //E3D_Material


int main(int argc, char **argv)
{

/* <<<<<  EGI general init  >>>>>> */
#if 0
        /* Start sys tick */
        printf("tm_start_egitick()...\n");
        tm_start_egitick();

        /* Start EGI log */
        printf("egi_init_log()...\n");
        if(egi_init_log("/mmc/log_wifiscan") != 0) {
                printf("Fail to init logger,quit.\n");
                return -1;
        }

        /* Load symbol pages */
        printf("symbol_load_allpages()...\n");
        if(symbol_load_allpages() !=0 ) {
                printf("Fail to load sym pages,quit.\n");
                return -2;
        }
        /* Load freetype sysfonts */
        printf("FTsymbol_load_sysfonts()...\n");
        if(FTsymbol_load_sysfonts() !=0) {
                printf("Fail to load FT sysfonts, quit.\n");
                return -1;
        }
#endif
        /* Load freetype appfonts */
        if(FTsymbol_load_appfonts() !=0 ) {     /* load FT fonts LIBS */
                printf("Fail to load FT appfonts, quit.\n");
                return -2;
        }

        //FTsymbol_set_SpaceWidth(1);

        /* Initilize sys FBDEV */
        printf("init_fbdev()...\n");
        if(init_fbdev(&gv_fb_dev))
                return -1;

        /* Start touch read thread */
#if 0
        printf("Start touchread thread...\n");
        if(egi_start_touchread() !=0)
                return -1;
#endif

        /* Set sys FB mode */
        fb_set_directFB(&gv_fb_dev,false);
        fb_position_rotate(&gv_fb_dev,0);

/* <<<<<  End of EGI general init  >>>>>> */


	E3D_Material material;

	material.name = "Head Skin";
	material.map_kd = argv[1]==NULL?"":argv[1]; //"/mmc/linuxpet.jpg";
	material.print();

	/* To display the texture map_kd */
	EGI_IMGBUF *imgbuf_kd = egi_imgbuf_readfile(material.map_kd.c_str());

#if 1
	if(imgbuf_kd) {
		if(imgbuf_kd->width>320 || imgbuf_kd->height>240) {
			if( 1.0*imgbuf_kd->width/imgbuf_kd->height > 1.0*320/240 )
			    egi_imgbuf_resize_update(&imgbuf_kd, true, 320, -1); /* proportional width to FB width */
			else
			    egi_imgbuf_resize_update(&imgbuf_kd, true, -1, 240); /* proportional height to FB height */
		}
		clear_screen(&gv_fb_dev, WEGI_COLOR_GRAY2);
		egi_subimg_writeFB(imgbuf_kd, &gv_fb_dev, 0, -1, 0,0);
		fb_render(&gv_fb_dev);
	}
#endif



	/* Free */
	egi_imgbuf_free2(&imgbuf_kd);

/* <<<<<  EGI general release >>>>> */

        //printf("FTsymbol_release_allfonts()...\n");
        //FTsymbol_release_allfonts();
        //printf("symbol_release_allpages()...\n");
        //symbol_release_allpages();
        printf("release_fbdev()...\n");
        fb_filo_flush(&gv_fb_dev);
        release_fbdev(&gv_fb_dev);
        //printf("egi_quit_log()...\n");
        //egi_quit_log();
        printf("<-------  END  ------>\n");

	return 0;
}
