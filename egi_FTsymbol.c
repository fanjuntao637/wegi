/*------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

Note:
1. FreeType 2 is licensed under FTL/GPLv3 or GPLv2.
   Based on freetype-2.5.5
2. Unicode emoji characters and sequences: https://www.unicode.org/emoji/charts/emoji-list.html

TODO:
1.Font size class, ratio of fw/fh to be fixed!
	NOW: font width(fw) is regarded as font size.
2. FTsymbol_unicode_writeFB(): Check if font data is corrupted! it
   MAY return error advanceX/bitmap.row.width ???  Set a limit
   to avoid it return a too big/samll value!
3. Remote_font v.s. Local_font
   Local: A set of English font
   Remote: Various types of fonts

Journal:
2021-01-27:
	1. Add  FTsymbol_create_fontBuffer() and  FTsymbol_free_fontBuffer().
2021-01-28:
	1.FTsymbol_unicode_writeFB(), add EGI_FONT_BUFFER to speed up.
2021-05-25:
	1. Add: FTsymbol_uft8strings_writeIMG().
2021-05-26:
	1. Add member 'lib_mutex' for EGI_FONTS.
	2. Apply lib_mutex for following functions:
		FTsymbol_load_library()
		FTsymbol_release_library()
		FTsymbol_unicode_writeFB()
2021-07-16:
	1. Add module private member 'enable_SplitWord' and functions of
	   FTsymbol_enable_SplitWord()
	   FTsymbol_disable_SplitWord()
	   FTsymbol_status_SplitWord()
2021-07-17:
	1. Add FTsymbol_eword_pixlen()
	2. FTsymbol_uft8strings_writeFB(): SplitWord contorl.
2021-07-20:
	1. FTsymbol_eword_pixlen(): Count in any SPACEs/TABs.
2021-09-14:
	1. Add FTsymbol_uft8strings_writeIMG2(), with rotation angle.
2021-11-26:
	1. FTsymbol_uft8strings_writeFB(). If *p gets to the end of the string
	   with just xleft==0, then NO need for increment of ln/xleft/py.
2022-05-26:
	1. Add egi_sysemojis.
	2. FTsymbol_load_sysemojis()
	3. Modify functions for Emojis font:
	   FTsymbol_load_library(), FTsymbol_load_sysfonts(), FTsymbol_load_appfonts()
2022-05-27:
	1. FTsymbol_unicode_writeFB(): Add case FT_HAS_COLOR.
2022-06-16:
	1. FTsymbol_unicode_writeFB(): TEST___ NO mutexlock for EGI_FONT_BUFFER.
2022-06-18:
	1. Add FTsymbol_save_fontBuffer()
	2. Add FTsymbol_load_fontBuffer()
2022-06-19:
	1. EGI_FONT_BUFFER: Add member 'family_name' and 'style_name'.
	   modify FTsymbol_save_fontBuffer(), FTsymbol_load_fontBuffer()

Midas Zhou
midaszhou@yahoo.com(Not in use since 2022_03_01)
-------------------------------------------------------------------*/
#include "egi_log.h"
#include "egi_FTsymbol.h"
#include "egi_symbol.h"
#include "egi_cstring.h"
#include "egi_utils.h"
#include "egi_debug.h"
#include "sys_list.h"  /* container_of() */
#include <arpa/inet.h>
#include <ctype.h>
#include <freetype2/ft2build.h>
#include <freetype2/ftglyph.h>
#include <freetype2/ftadvanc.h>
#include FT_FREETYPE_H

/* <<<<<<<<<<<<<<<<<<   FreeType Fonts  >>>>>>>>>>>>>>>>>>>>>>*/

EGI_SYMPAGE sympg_ascii={0}; 	/* default  LiberationMono-Regular */

EGI_FONTS  egi_sysfonts = {.ftname="sysfonts",};
EGI_FONTS  egi_sysemojis = {.ftname="sysemojis",};
EGI_FONTS  egi_appfonts = {.ftname="appfonts",};
EGI_FONT_BUFFER *egi_fontbuffer;

/*--------------------------------
   Module Prative Parameters

       --- Caution! ---
       NOT thread safe!
---------------------------------*/
static float  factor_TabWidth=3.0;	/* TabWidth/Font_Width */
static float  factor_SpaceWidth=1.0;	/* SpaceWidth/FontWidth */
static bool   enable_SplitWord=true;    /* Enable a word splited into two lines
					 * NOW affect FTsymbol_uft8strings_writeFB() ONLY!
					 */


/*--------------------------------------
Set TAB and SPACE width facotrs.
---------------------------------------*/
void FTsymbol_set_TabWidth( float factor)
{
	factor_TabWidth=factor;
}
void FTsymbol_set_SpaceWidth( float factor)
{
	factor_SpaceWidth=factor;
}

/*---------------------------------
Set enable_SplitWord.
----------------------------------*/
void FTsymbol_enable_SplitWord(void)
{
	enable_SplitWord=true;
}
void FTsymbol_disable_SplitWord(void)
{
	enable_SplitWord=false;
}
bool FTsymbol_status_SplitWord(void)
{
	return enable_SplitWord;
}

/*--------------------------------------
Load FreeType2 EGI_FONT egi_sysfonts
for main process, as small as possible.

return:
	0	OK
	<0	Fails
---------------------------------------*/
int  FTsymbol_load_sysfonts(void)
{
	int ret=0;
	char fpath_regular[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_light[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_bold[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_special[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_emojis[EGI_PATH_MAX+EGI_NAME_MAX];

	/* Read egi.conf and get fonts paths */
	egi_get_config_value("SYS_FONTS","regular",fpath_regular);
	egi_get_config_value("SYS_FONTS","light",fpath_light);
	egi_get_config_value("SYS_FONTS","bold",fpath_bold);
	egi_get_config_value("SYS_FONTS","special",fpath_special);
	egi_get_config_value("SYS_FONTS","emojis",fpath_emojis);

	egi_sysfonts.fpath_regular=fpath_regular;
	egi_sysfonts.fpath_light=fpath_light;
	egi_sysfonts.fpath_bold=fpath_bold;
	egi_sysfonts.fpath_special=fpath_special;
	egi_sysfonts.fpath_emojis=fpath_emojis;

	/* Load FT library with default fonts */
	ret=FTsymbol_load_library( &egi_sysfonts );
	if(ret) {
		//return -1;
		// Go on....
		EGI_PLOG(LOGLV_ERROR,"%s: Some sysfonts missed or NOT configured!\n", __func__ );
	}

	return 0;
}


#if 1 ////////////////////////////////////////
/*--------------------------------------
Load FreeType2 EGI_FONT egi_sysemojis
for main process, as small as possible.

return:
	0	OK
	<0	Fails
---------------------------------------*/
int  FTsymbol_load_sysemojis(void)
{
	int ret=0;
	char fpath_regular[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_light[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_bold[EGI_PATH_MAX+EGI_NAME_MAX];
	char fpath_special[EGI_PATH_MAX+EGI_NAME_MAX];

	/* Read egi.conf and get fonts paths */
	egi_get_config_value("SYS_EMOJIS","regular",fpath_regular);
	egi_get_config_value("SYS_EMOJIS","light",fpath_light);
	egi_get_config_value("SYS_EMOJIS","bold",fpath_bold);
	egi_get_config_value("SYS_EMOJIS","special",fpath_special);

	egi_sysemojis.fpath_regular=fpath_regular;
	egi_sysemojis.fpath_light=fpath_light;
	egi_sysemojis.fpath_bold=fpath_bold;
	egi_sysemojis.fpath_special=fpath_special;

	/* Load FT library with default fonts */
	ret=FTsymbol_load_library( &egi_sysemojis );
	if(ret) {
		//return -1;
		// Go on....
		EGI_PLOG(LOGLV_ERROR,"%s: Some sysEmojis missed or NOT configured!\n", __func__ );
	}

	return 0;
}
#endif ////////////////////////////////////////////

/*--------------------------------------
Load FreeType2 fonts for all APPs.

return:
	0	OK
	<0	Fails
---------------------------------------*/
int  FTsymbol_load_appfonts(void)
{
	int ret=0;
	char fpath_regular[EGI_PATH_MAX+EGI_NAME_MAX]={0};
	char fpath_light[EGI_PATH_MAX+EGI_NAME_MAX]={0};
	char fpath_bold[EGI_PATH_MAX+EGI_NAME_MAX]={0};
	char fpath_special[EGI_PATH_MAX+EGI_NAME_MAX]={0};
	char fpath_emojis[EGI_PATH_MAX+EGI_NAME_MAX];

	/* Read egi.conf and get fonts paths */
	egi_get_config_value("APP_FONTS","regular",fpath_regular);
	egi_get_config_value("APP_FONTS","light",fpath_light);
	egi_get_config_value("APP_FONTS","bold",fpath_bold);
	egi_get_config_value("APP_FONTS","special",fpath_special);
	egi_get_config_value("APP_FONTS","emojis",fpath_emojis);

	egi_appfonts.fpath_regular=fpath_regular;
	egi_appfonts.fpath_light=fpath_light;
	egi_appfonts.fpath_bold=fpath_bold;
	egi_appfonts.fpath_special=fpath_special;
	egi_appfonts.fpath_emojis=fpath_emojis;

	/* Load FT library with default fonts */
	ret=FTsymbol_load_library( &egi_appfonts );
	if(ret) {
		EGI_PLOG(LOGLV_WARN,"%s: Some appfonts missed or not configured!", __func__ );
		//return -1;
	}

	return 0;
}

void FTsymbol_release_allfonts(void)
{
	/* release FT fonts libraries */
	FTsymbol_release_library(&egi_sysfonts);
	FTsymbol_release_library(&egi_appfonts);
}


/*-----------------------------------------
Load all FT type symbol pages.

1. sympg_ascii

return:
	0	OK
	<0	Fails
-----------------------------------------*/
int  FTsymbol_load_allpages(void)
{
	char fpath_ascii[EGI_PATH_MAX+EGI_NAME_MAX]={0};

	/* read egi.conf and get fonts paths */
	if ( egi_get_config_value("FTSYMBOL_PAGE","ascii",fpath_ascii) == 0 &&
        	/* load ASCII font, bitmap size 18x18 in pixels, each line abt.18x2 pixels in height */
        	FTsymbol_load_asciis_from_fontfile( &sympg_ascii, fpath_ascii, 18, 18) ==0 )
        {

		EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to load FTsymbol page sympg_ascii!", __func__ );
		return 0;
	}
	else {
		EGI_PLOG( LOGLV_CRITICAL,"%s: Fail to load FTsymbol page sympg_ascii!", __func__ );
		return -1;
	}
}


/* --------------------------------------
     Release all TF type symbol pages
----------------------------------------*/
void FTsymbol_release_allpages(void)
{
        /* release FreeType2 symbol page */
      	symbol_release_page(&sympg_ascii);
	if(sympg_ascii.symwidth!=NULL)		/* !!!Not static */
		free(sympg_ascii.symwidth);

}


/*---------------------------------------------------------------
Initialize FT library and load faces.


Note:
1. You shall avoid to load all fonts in main process,
   it will slow down fork() process significantly.
2. When a new face is created (FT_New_Face or FT_Open_Face), the
   the library looks for a Unicode charmap within the list and
   automatically activates it. Or use FT_Select_Charmap to manually
   change encoding type.

@symlib:	A pointer to EGI_FONTS.

return:
        0       OK
        !0      Fails, or not all faces are loaded successfully.
----------------------------------------------------------------*/
int FTsymbol_load_library( EGI_FONTS *symlib )
{
	FT_Error	error;
	int ret=0;

	/* check input data */
//	if( symlib==NULL || symlib->fpath_regular==NULL
//			 || symlib->fpath_light==NULL
//			 || symlib->fpath_bold==NULL	)

	if( symlib == NULL )
	{
		egi_dpstd("Fail to check integrity of the input FTsymbol_library!\n");
		return -1;
	}

	/* 0. Init lib_mutex mutex */
        if(pthread_mutex_init(&symlib->lib_mutex, NULL) != 0)
        {
                egi_dpstd("Fail to init lib_mutex.\n");
                return -1;
        }

        /* 1. initialize FT library */
        error = FT_Init_FreeType( &symlib->library );
        if(error) {
                EGI_PLOG(LOGLV_ERROR, "%s: An error occured during FreeType library [%s] initialization.",
									__func__, symlib->ftname);
                return error;
        }

	/* 2. create face object: Regular */
        error = FT_New_Face( symlib->library, symlib->fpath_regular, 0, &symlib->regular );
        if(error==FT_Err_Unknown_File_Format) {
                EGI_PLOG(LOGLV_WARN,"%s: [%s] font file '%s' opens, but its font format is unsupported!",
								__func__, symlib->ftname,symlib->fpath_regular);
		ret=1;
        }
        else if ( error ) {
                EGI_PLOG(LOGLV_WARN,"%s: Fail to open or read [%s] REGULAR font file '%s'.",
 							__func__, symlib->ftname, symlib->fpath_regular);
		ret=2;
        }
	else {
        	EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to open and read [%s] REGULAR font file '%s', type face has %s kerning value.",
					__func__, symlib->ftname, symlib->fpath_regular, FT_HAS_KERNING(symlib->regular) ? " ":"NO" );
	}

	/* 3. Create face object: Light */
        error = FT_New_Face( symlib->library, symlib->fpath_light, 0, &symlib->light );
        if(error==FT_Err_Unknown_File_Format) {
                EGI_PLOG(LOGLV_WARN,"%s: [%s] font file '%s' opens, but its font format is unsupported!",
						     	__func__, symlib->ftname, symlib->fpath_light);
		ret=3;
        }
        else if ( error ) {
                EGI_PLOG(LOGLV_WARN,"%s: Fail to open or read [%s] LIGHT font file '%s'.",
						 	__func__, symlib->ftname, symlib->fpath_light);
		ret=4;
        }
	else {
        	EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to open and read [%s] LIGHT font file '%s', type face has %s kerning value.",
				 	__func__, symlib->ftname, symlib->fpath_light, FT_HAS_KERNING(symlib->light) ? " ":"NO" );
	}

	/* 4. Create face object: Bold */
        error = FT_New_Face( symlib->library, symlib->fpath_bold, 0, &symlib->bold );
        if(error==FT_Err_Unknown_File_Format) {
                EGI_PLOG(LOGLV_WARN,"%s: [%s] font file '%s' opens, but its font format is unsupported!",
							__func__, symlib->ftname, symlib->fpath_bold);
		ret=5;
        }
        else if ( error ) {
                EGI_PLOG(LOGLV_WARN,"%s: Fail to open or read [%s] BOLD font file '%s'.",
							__func__, symlib->ftname,symlib->fpath_bold);
		ret=6;
        }
	else {
        	EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to open and read [%s] BOLD font file '%s', type face has %s kerning value.",
						__func__, symlib->ftname,symlib->fpath_bold, FT_HAS_KERNING(symlib->bold) ? " ":"NO");
	}

	/* 5. Create face object: Special */
        error = FT_New_Face( symlib->library, symlib->fpath_special, 0, &symlib->special );
        if(error==FT_Err_Unknown_File_Format) {
                EGI_PLOG(LOGLV_WARN,"%s: [%s] font file '%s' opens, but its font format is unsupported!",
							__func__, symlib->ftname, symlib->fpath_special);
		ret=7;
        }
        else if ( error ) {
                EGI_PLOG(LOGLV_WARN,"%s: Fail to open or read [%s] SPECIAL font file '%s'.\n",
							__func__, symlib->ftname, symlib->fpath_special);
		ret=8;
        }
	else {
        	EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to open and read [%s] SPECIAL font file '%s', type face has %s kerning value.",
					__func__, symlib->ftname, symlib->fpath_special, FT_HAS_KERNING(symlib->special) ? " ":"NO");
	}

	/* 6. Create face object: Emojis */
        error = FT_New_Face( symlib->library, symlib->fpath_emojis, 0, &symlib->emojis );
        if(error==FT_Err_Unknown_File_Format) {
                EGI_PLOG(LOGLV_WARN,"%s: [%s] font file '%s' opens, but its font format is unsupported!",
							__func__, symlib->ftname, symlib->fpath_emojis);
		ret=9;
        }
        else if ( error ) {
                EGI_PLOG(LOGLV_WARN,"%s: Fail to open or read [%s] EMOJI font file '%s'.\n",
							__func__, symlib->ftname, symlib->fpath_emojis);
		ret=10;
        }
	else {
        	EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to open and read [%s] EMOJI font file '%s', type face has %s kerning value.",
					__func__, symlib->ftname, symlib->fpath_emojis, FT_HAS_KERNING(symlib->emojis) ? " ":"NO");
	}


	return ret;


//FT_FAIL:
 	FT_Done_Face    ( symlib->regular );
  	FT_Done_Face    ( symlib->light );
  	FT_Done_Face    ( symlib->bold );
  	FT_Done_Face    ( symlib->special );
  	FT_Done_Face    ( symlib->emojis );
  	FT_Done_FreeType( symlib->library );

	return error;
}


/*------------------------------------------------------------------
Create a new FT_Face in the FT_library of given EGI_FONTS.

@symlib:  An EGI_FONTS with a FT_Library to hold the FT Face.
@ftpath:  Font file path for the new FT Face.

Return:
	FT_Face		OK
	NULL		Fails
--------------------------------------------------------------------*/
FT_Face FTsymbol_create_newFace( EGI_FONTS *symlib, const char *ftpath)
{
	FT_Error	error;
	FT_Face		face;

	/* Initialize library if NULL */
	if(symlib->library==NULL) {
		printf("%s: Input EGI_FONTS has an empty FT_library! try to initialize it...\n",__func__);
	        error = FT_Init_FreeType( &symlib->library );
        	if(error) {
                     EGI_PLOG(LOGLV_ERROR, "%s: An error occured during FreeType library [%s] initialization.",
									__func__, symlib->ftname);
                	return NULL;
		}
        }

	/* Create a new face according to the input file path */
        error = FT_New_Face( symlib->library, ftpath, 0, &face);
        if(error==FT_Err_Unknown_File_Format) {
                EGI_PLOG(LOGLV_WARN,"%s: font file '%s' opens, but its font format is unsupported!",
							__func__, ftpath);
		return NULL;
        }
        else if ( error ) {
                EGI_PLOG(LOGLV_WARN,"%s: Fail to open or read font file '%s'.", __func__, ftpath);
		return NULL;
        }
        EGI_PLOG(LOGLV_CRITICAL,"%s: Succeed to open and read font file '%s'.", __func__, ftpath);

	return face;
}



/*--------------------------------------------------
	Rlease FT library.
---------------------------------------------------*/
void FTsymbol_release_library( EGI_FONTS *symlib )
{
	if(symlib==NULL || symlib->library==NULL)
		return;

	/* 1. Destroy lib_mutex */
        /* Hope there is no other user */
        if(pthread_mutex_lock(&symlib->lib_mutex) !=0 )
                EGI_PLOG(LOGLV_TEST,"%s:Fail to lib_mutex!\n",__func__);

        if( pthread_mutex_unlock(&symlib->lib_mutex) != 0)
                EGI_PLOG(LOGLV_TEST,"%s:Fail to unlock lib_mutex!\n",__func__);

        /* Destroy thread mutex lock for page resource access */
        if(pthread_mutex_destroy(&symlib->lib_mutex) !=0 )
                EGI_PLOG(LOGLV_TEST,"%s:Fail to destroy lib_mutex!.\n",__func__);

	/* 2. Free FT faces and library */
 	FT_Done_Face    ( symlib->regular );
  	FT_Done_Face    ( symlib->light );
  	FT_Done_Face    ( symlib->bold );
	FT_Done_Face	( symlib->special );
  	FT_Done_FreeType( symlib->library );
}


/*--------------------------------------------------------------------------------------------------------
(Note: FreeType 2 is licensed under FTL/GPLv3 or GPLv2 ).

1. This program is ONLY for horizontal text layout !!!

2. To get the MAX boudary box heigh: symheight, which is capable of containing each character in a font set.
   (symheight as for EGI_SYMPAGE)

   1.1	base_uppmost + base_lowmost +1    --->  symheight  (same height for all chars)
	base_uppmost = MAX( slot->bitmap_top )			    ( pen.y set to 0 as for baseline )
	base_lowmost = MAX( slot->bitmap.rows - slot->bitmap_top )  ( pen.y set to 0 as for baseline )

   	!!! Note: Though  W*H in pixels as in FT_Set_Pixel_Sizes(face,W,H) is capable to hold each
        char images, but it is NOT aligned with the common baseline !!!

   1.2 MAX(slot->advance.x/64, slot->bitmap.width)	--->  symwidth  ( for each charachter )

       Each ascii symbol has different value of its FT bitmap.width.

3. 0-31 ASCII control chars, 32-126 ASCII printable symbols.

4. For ASCII charaters, then height defined in FT_Set_Pixel_Sizes() or FT_Set_Char_Size() is NOT the true
   hight of a character bitmap, it is a norminal value including upper and/or low bearing gaps, depending
   on differenct font glyphs.

5. Following parameters all affect the final position of a character.
   5.1 slot->advance.x ,.y:  	Defines the current cursor/pen shift vector after loading/rendering a character.
				!!! This will affect the position of the next character. !!!
   5.2 HBearX, HBearY:	 	position adjust for a character, relative to the current cursor position(local
				origin).
   5.3 slot->bitmap_left, slot->bitmap_top:  Defines the left top point coordinate of a character bitmap,
				             relative to the global drawing system origin.


LiberationSans-Regular.ttf

Midas Zhou
midaszhou@yahoo.com(Not in use since 2022_03_01)
----------------------------------------------------------------------------------------------------------*/


/* -------------------------------------------------------------------------------------------
Load a ASCII symbol_page struct from a font file by calling FreeType2 libs.

Note:
0. This is for pure western ASCII alphabets font set.
1. Load ASCII characters from 0 - 127, bitmap data of all nonprintable ASCII symbols( 0-31,127)
   will not be loaded to symfont_page, and their symwidths are set to 0 accordingly.

2. Take default face_index=0 when call FT_New_Face(), you can modify it if
   the font file contains more than one glphy face.

3. Inclination angle for each single character is set to be 0. If you want an oblique effect (for
   single character ), just assign a certain value to 'deg'.

4. !!! symfont_page->symwidth MUST be freed by calling free() separately,for symwidth is statically
   allocated in most case, and symbol_release_page() will NOT free it.

TODO: Apply character functions in <ctype.h> to rule out chars, with consideration of locale setting?

@symfont_page:   pointer to a font symbol_page.
@font_path:	 font file path.
@Wp, Hp:	 in pixels, as for FT_Set_Pixel_Sizes(face, Wp, Hp )

 !!!! NOTE: Hp is nominal font height and NOT symheight of symbole_page,
	    the later one shall be set to be the same for all symbols in the page.

Return:
        0       ok
        <0      fails
---------------------------------------------------------------------------------------------*/
int  FTsymbol_load_asciis_from_fontfile(  EGI_SYMPAGE *symfont_page, const char *font_path,
				       	   int Wp, int Hp )
{
	FT_Error      	error;
	FT_Library    	library;
	FT_Face       	face;		/* NOTE: typedef struct FT_FaceRec_*  FT_Face */
	FT_GlyphSlot  	slot;
	FT_Matrix     	matrix;         /* transformation matrix */
	FT_Vector	origin;

	int bbox_W; // bbox_H; 	/* Width and Height of the boundary box */
	int base_uppmost; 	/* in pixels, from baseline to the uppermost scanline */
	int base_lowmost; 	/* in pixels, from baseline to the lowermost scanline */
	int symheight;	  	/* in pixels, =base_uppmost+base_lowmost+1, symbol height for EGI_SYMPAGE */
	int symwidth_sum; 	/* sum of all charachter widths, as all symheight are the same,
			      	 * so symwidth_total*symheight is required mem space */
	int sympix_total;	/* total pixel number of all symbols in a sympage */
	int hi,wi;
	int pos_symdata, pos_bitmap;

 	FT_CharMap*   pcharmaps=NULL;
  	char          tag_charmap[6]={0};
  	uint32_t      tag_num;

  	int		deg;    /* angle in degree */
  	double          angle;  /* angle in double */
  	int 		i,n; //j,k,m,n;
	int		ret=0;

	/* A1. initialize FT library */
	error = FT_Init_FreeType( &library );
	if(error) {
		printf("%s: An error occured during FreeType library initialization.\n",__func__);
		return -1;
	}

	/* A2. create face object, face_index=0 */
 	error = FT_New_Face( library, font_path, 0, &face );
	if(error==FT_Err_Unknown_File_Format) {
		printf("%s: Font file opens, but its font format is unsupported!\n",__func__);
		FT_Done_FreeType( library );
		return -2;
	}
	else if ( error ) {
		printf("%s: Fail to open or read font file '%s'.\n",__func__, font_path);
		FT_Done_FreeType( library );
		return -3;
	}

  	/* A3. get pointer to the glyph slot */
  	slot = face->glyph;

	/* A4. print font face[0] parameters */
#if 1  /////////////////////////     PRINT FONT INFO     /////////////////////////////
	printf("   FreeTypes load font file '%s' :: face_index[0] \n", font_path);
	printf("   num_faces:		%d\n",	(int)face->num_faces);
	printf("   face_index:		%d\n",	(int)face->face_index);
	printf("   family name:		%s\n",	face->family_name);
	printf("   style name:		%s\n",	face->style_name);
	printf("   num_glyphs:		%d\n",	(int)face->num_glyphs);
	printf("   face_flags: 		0x%08X\n",(int)face->face_flags);
	printf("   units_per_EM:	%d\n",	face->units_per_EM);
	printf("   num_fixed_sizes:	%d\n",	face->num_fixed_sizes);
	if(face->num_fixed_sizes !=0 ) {
		for(i=0; i< face->num_fixed_sizes; i++) {
			printf("[%d]: H%d x W%d\n",i, face->available_sizes[i].height,
							    face->available_sizes[i].width);
		}
	}
		/* print available charmaps */
	printf("   num_charmaps:	%d\n",	face->num_charmaps);
	for(i=0; i< face->num_charmaps; i++) { /* print all charmap tags */
		pcharmaps=face->charmaps;
		tag_num=htonl((uint32_t)(*pcharmaps)->encoding );
		memcpy( tag_charmap, &tag_num, 4); /* 4 bytes TAG */
		printf("      			[%d] %s\n", i,tag_charmap ); /* 'unic' as for Unicode */
		pcharmaps++;
	}
	tag_num=htonl((uint32_t)( face->charmap->encoding));
	memcpy( tag_charmap, &tag_num, 4); /* 4 bytes TAG */
	printf("   charmap in use:	%s\n", tag_charmap ); /* 'unic' as for Unicode */
		/* vertical distance between two consective lines */
	printf("   height(V dist, in font units):	%d\n",	face->height);
#endif ////////////////////////////   END PRINT   ///////////////////////////////////


   	/* A5. set character size in pixels */
   	error = FT_Set_Pixel_Sizes(face, Wp, Hp); /* width,height */
	/* OR set character size in 26.6 fractional points, and resolution in dpi */
   	//error = FT_Set_Char_Size( face, 32*32, 0, 100, 0 );

  	/* A6. set char inclination angle and matrix for transformation */
	deg=0.0;
  	angle     = ( deg / 360.0 ) * 3.14159 * 2;
  	printf("   Font inclination angle: %d, %f \n", deg, angle);
  	matrix.xx = (FT_Fixed)( cos( angle ) * 0x10000L );
  	matrix.xy = (FT_Fixed)(-sin( angle ) * 0x10000L );
	matrix.yx = (FT_Fixed)( sin( angle ) * 0x10000L );
	matrix.yy = (FT_Fixed)( cos( angle ) * 0x10000L );

	/* --------------------------- NOTE -----------------------------------------------------
         * 	1. Define a boundary box with bbox_W and bbox_H to contain each character.
	 *	2. The BBoxes are NOT the same size, and neither top_lines nor bottom_lines
	 *	   are aligned at the same level.
	 *	3. The symbol_page boundary boxes are all with the same height, and their top_lines
	 *	   and bottum_lines are aligned at the same level!
	 -----------------------------------------------------------------------------------------*/

	/* ------- Get MAX. base_uppmost, base_lowmost ------- */
	base_uppmost=0;  /* baseline to top line of [symheight * bbox_W] */
	base_lowmost=0;  /* baseline to top line of [symheight * bbox_W] */

	/* set origin here, so that all character bitmaps are aligned to the same origin/baseline */
	origin.x=0;
	origin.y=0;
    	FT_Set_Transform( face, &matrix, &origin);

	/* 1. Tranvser all chars to get MAX height for the boundary box, base line all aligned. */
	symheight=0;
	symwidth_sum=0;
	for( n=0; n<128; n++) {  /* 0-NULL, 32-SPACE, 127-DEL */
	    	error = FT_Load_Char( face, n, FT_LOAD_RENDER );
	    	if ( error ) {
			printf("%s: FT_Load_Char() error!\n",__func__);
			exit(1);
		}

		/* 1.1 base_uppmost: dist from base_line to top */
		if(base_uppmost < slot->bitmap_top)
			base_uppmost=slot->bitmap_top;

		/* 1.2 base_lowmost: dist from base_line to bottom */
		/* !!!! unsigned int - signed int */
		if( base_lowmost < (int)(slot->bitmap.rows) - (int)(slot->bitmap_top) ) {
				base_lowmost=(int)slot->bitmap.rows-(int)slot->bitmap_top;
		}

		/* 1.3 get symwidth, and sum it up */
			/* rule out all unpritable characters, except TAB  */
		if( n<32 || n==127 ) {
			/* symwidth set to be 0, no mem space for it. */
			continue;
		}

		bbox_W=slot->advance.x>>6;
		if(bbox_W < slot->bitmap.width) /* adjust bbox_W */
			bbox_W =slot->bitmap.width;

		symwidth_sum += bbox_W;
	}

	/* 2. get symheight at last */
	symheight=base_uppmost+base_lowmost+1;
	printf("   Input Hp=%d; symheight=%d; base_uppmost=%d; base_lowmost=%d  (all in pixels)\n",
							Hp, symheight, base_uppmost, base_lowmost);
	symfont_page->symheight=symheight;

	/* 3. Set Maxnum for symbol_page*/
	symfont_page->maxnum=128-1; /* 0-127 */

	/* 4. allocate EGI_SYMPAGE .data, .alpha, .symwidth, .symoffset */
	sympix_total=symheight*symwidth_sum; /* total pixel number in the symbol page */

	/* release all data before allocate it */
	symbol_release_page(symfont_page);

	symfont_page->data=calloc(1, sympix_total*sizeof(uint16_t));
	if(symfont_page->data==NULL) { ret=-4; goto FT_FAILS; }

	symfont_page->alpha=calloc(1, sympix_total*sizeof(unsigned char));
	if(symfont_page->alpha==NULL) { ret=-4; goto FT_FAILS; }

	symfont_page->symwidth=calloc(1, (symfont_page->maxnum+1)*sizeof(int) );
	if(symfont_page->symwidth==NULL) { ret=-4; goto FT_FAILS; }

	symfont_page->symoffset=calloc(1, (symfont_page->maxnum+1)*sizeof(int));
	if(symfont_page->symoffset==NULL) { ret=-4; goto FT_FAILS; }


	/* 5. Tranverseall ASCIIs to get symfont_page.alpha, ensure that all base_lines are aligned. */
	/* NOTE:
	 *	1. TODO: nonprintable ASCII characters will be presented by FT as a box with a cross inside.
	 *	   It is not necessary to be allocated in mem????
	 *	2. For font sympage, only .aplha is available, .data is useless!!!
	 *	3. Symbol font boundary box [ symheight*bbox_W ] is bigger than each slot->bitmap box !
	 */

	symfont_page->symoffset[0]=0; 	/* default for first char */

	for( n=0; n<128; n++) {		/* 0-NULL, 32-SPACE, 127-DEL */


		/* 5.0 rule out all unpritable characters */
		if( n<32 || n==127 ) {
			symfont_page->symwidth[n]=0;
		        /* Not necessary to change symfont_page->symoffset[x], as its symwidth is 0. */
			continue;
		}

		/* 5.1 load N to ASCII symbol bitmap */
	    	error = FT_Load_Char( face, n, FT_LOAD_RENDER );
	    	if ( error ) {
			printf("%s: Fail to FT_Load_Char() n=%d as ASCII symbol!\n", __func__, n);
			exit(1);
		}

		/* 5.2 get symwidth for each symbol, =MAX(advance.x>>6, bitmap.width) */
		bbox_W=slot->advance.x>>6;
		if(bbox_W < slot->bitmap.width) /* adjust bbox_W */
			bbox_W =slot->bitmap.width;
		symfont_page->symwidth[n]=bbox_W;

		/* 5.3    <<<<<<<<<	get symfont_page.alpha	 >>>>>>>>>
			As sympage boundary box [ symheight*bbox_W ] is big enough to hold each
			slot->bitmap image, we divide symheight into 3 parts, with the mid part
			equals bitmap.rows, and get alpha value accordingly.

			ASSUME: bitmap start from wi=0 !!!

			variables range:: { hi: 0-symheight,   wi: 0-bbox_W }
		*/

		/* 1. From base_uppmost to bitmap_top:
		 *    All blank lines, and alpha values are 0 as default.
		 *
		 *   <------ So This Part Can be Ignored ------>
		 */

#if 0 /*   <------  This Part Can be Ignored ------> */
		for( hi=0; hi < base_uppmost-slot->bitmap_top; hi++) {
			for( wi=0; wi < bbox_W; wi++ ) {
				pos_symdata=symfont_page->symoffset[n] + hi*bbox_W + wi;
				/* all default value */
				symfont_page->alpha[pos_symdata] = 0;
			}
		}
#endif

		/* 2. From bitmap_top to bitmap bottom,
		 *    Actual data here.
		 */
	     for( hi=base_uppmost-slot->bitmap_top; hi < (base_uppmost-slot->bitmap_top) + slot->bitmap.rows; \
							 hi++ )
		{
			/* ROW: within bitmap.width, !!! ASSUME bitmap start from wi=0 !!! */
			for( wi=0; wi < slot->bitmap.width; wi++ ) {
				pos_symdata=symfont_page->symoffset[n] + hi*bbox_W +wi;
				pos_bitmap=(hi-(base_uppmost-slot->bitmap_top))*slot->bitmap.width+wi;
				symfont_page->alpha[pos_symdata]= slot->bitmap.buffer[pos_bitmap];

#if 0   /* ------ TEST ONLY, print alpha here ------ */

		  	if(n=='j')  {
				if(symfont_page->alpha[pos_symdata]>0)
					printf("*");
				else
					printf(" ");
				if( wi==slot->bitmap.width-1 )
					printf("\n");
	   		}

#endif /*  -------------   END TEST   -------------- */

			}
			/* ROW: blank area in BBOX */
			for( wi=slot->bitmap.width; wi<bbox_W; wi++ ) {
				pos_symdata=symfont_page->symoffset[n] + hi*bbox_W +wi;
				symfont_page->alpha[pos_symdata]=0;
			}
		}

		/* 3. From bitmap bottom to symheight( bottom),
		 *    All blank lines, and alpha values are 0 as default.
		 *
		 *   <------ So This Part Can be Ignored too! ------>
		 */

#if 0 /*   <------  This Part Can be Ignored ------> */
		for( hi=(base_uppmost-slot->bitmap_top) + slot->bitmap.rows; hi < symheight; hi++ )
		{
			for( wi=0; wi < bbox_W; wi++ ) {
				pos_symdata=symfont_page->symoffset[n] + hi*bbox_W + wi;
				/* set only alpha value */
				symfont_page->alpha[pos_symdata] = 0;
			}
		}
#endif

		/* calculate symoffset for next one */
		if(n<127)
			symfont_page->symoffset[n+1] = symfont_page->symoffset[n] + bbox_W * symheight;

	} /* end transversing all ASCII chars */


FT_FAILS:
  	FT_Done_Face    ( face );
  	FT_Done_FreeType( library );

  	return ret;
}


/*--------------------------------------------------------------------------
To get actual Max. symHeight/bitmapHeight of a string, with dedicated FT_Face
and font size.

1. Try NOT to use this function, it's slow. and use CAPITAL LETTERs instead
   of small letters.
2. The purpose of this function is to use the result symHeight to align string
   in middle of some space.

Note: It's usually for ASCII chars. For Chinese char, they are usually the same
      height.

Return:
	>0	OK
	<0	Fails
---------------------------------------------------------------------------*/
int FTsymbol_get_symheight(FT_Face face, const unsigned char *pstr, int fw, int fh )
{
	int symheight=0;
	int size;
  	FT_Error      error;
  	FT_GlyphSlot slot = face->glyph;
	wchar_t 	wcode;

   	/* Set character size in pixels */
   	error = FT_Set_Pixel_Sizes(face, fw, fh); /* width,height */
	if(error) {
		printf("%s: Fail to set pixel size!\n",__func__);
		return -1;
	}

   	/* Default transformation matrix */

	while( *pstr != L'\0' ) {  /* wchar_t string end token */

                /* convert one character to unicode, return size of utf-8 code */
                size=char_uft8_to_unicode((unsigned char *)pstr, &wcode);
		if(size<1) 	/* Maybe nonprintable/control char, Supposed they are  at very end of the string! Break then. */
			break;
		else
			pstr +=size;

	    	error = FT_Load_Char( face, wcode, FT_LOAD_RENDER );
	    	if ( error ) {
			printf("%s: FT_Load_Char() error!\n",__func__);
			return -1;
		}

		if(symheight < slot->bitmap.rows) {
			symheight=slot->bitmap.rows;
			// printf("%s: symheight=%d, deltY=%d \n", (char *)&wcode, symheight, -slot->bitmap_top + fh);
		}
	}

	return symheight;
}


/*--------------------------------------------------------------------------
Return FT_Face->size->metrics.height, which is the Max. possible face height
for given font size.
Usually it's much bigger than symheight and bitmap height. and may be used
for line distance.

@fh,fw:		Expected font size.

Return:
	>0	OK
	<0	Fails
---------------------------------------------------------------------------*/
inline int FTsymbol_get_FTface_Height(FT_Face face, int fw, int fh)
{
  	FT_Error      error;

	/* set character size in pixels */
	error = FT_Set_Pixel_Sizes(face, fw, fh);
   	/* OR set character size in 26.6 fractional points, and resolution in dpi
   	   error = FT_Set_Char_Size( face, 32*32, 0, 100,0 ); */
	if(error) {
		printf("%s: FT_Set_Pixel_Sizes() fails!\n",__func__);
		return -1;
	}

	return face->size->metrics.height>>6;
}


/*----------------------------------------------------------------------------------------------------
Get total advance value of the input string.  the FT_Face shall be preseted with expected parameters,
transforms etc. The purpose is to get the space needed for writFBing the string in a fast way.

@face: 		FT_Face
@load_flags:	A flag indicating what to load for this glyph
		!!! WARNING !!! Must be the same as in FT_Load_Char( face, wcode, flags ) as for writeFB
		the same string.
@fw,fh:		Expected font width and height, Must be the same as in FT_Set_Pixel_Sizes() as for writeFB
		the same string.
@pstr:          pointer to a string with UTF-8 encoding.


NOTE:
  1. For ASCII symbols, results are NOT same as FTsymbol_uft8strings_pixlen()??! Example: abc , . : ...
     So the function will call FTsymbol_uft8strings_pixlen() in that case.

TODO:
  1. Cross check with FTsymbol_unicode_writeFB(), for EMOJIs etc.

Return:
	>0	Sum of andvances of the chars that successfully parsed. (in pixels)
	<0	Fails,
------------------------------------------------------------------------------------------------------*/
inline int FTsymbol_uft8string_getAdvances(FT_Face face, int fw, int fh, const unsigned char *ptr)
{
	int size;
  	FT_Error   error;
	FT_Fixed   advance; /* typedef signed long  FT_Fixed;   to store 16.16 fixed-point values */
	int 	   sum=0;   /* sumup of all advances, in pixels */
	const unsigned char *p=ptr;
 	wchar_t    wcstr[1];
	int sdw;        /* Self defined width for the wcode */
	char pch[2]={0};

	if(ptr==NULL)
		return -1;

#if 0	/* 1. Mutex lock FT_library for SYSFONTs */
        if(pthread_mutex_lock(&egi_sysfonts.lib_mutex) !=0 ){
                EGI_PLOG(LOGLV_ERROR,"%s: Fail to lock lib_mutex! Err'%s'.", __func__, strerror(errno));
                return -1;
	}
/* ------ >>>  Critical Zone  */
#endif

	/* 2. Set character size in pixels */
	error = FT_Set_Pixel_Sizes(face, fw, fh);
   	/* OR set character size in 26.6 fractional points, and resolution in dpi
   	   error = FT_Set_Char_Size( face, 32*32, 0, 100,0 ); */
	if(error) {
		printf("%s: FT_Set_Pixel_Sizes() fails!\n",__func__);

/* ------ <<<  Critical Zone  */
	        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

		return -2;
	}

	/* 3. Get advance of each unicode and sum up. */
	while(*p) {
		/* convert one character to unicode, return size of utf-8 code */
		size=char_uft8_to_unicode(p, wcstr);
		if(size > 0) {
			/* If it has cooked width. */
			sdw=FTsymbol_cooked_charWidth(*wcstr, fw);
			if( sdw >= 0 ) {
				sum += sdw;
			}
			/* ASCII */
			else if( size==1 ) { //&& isascii(*p) ) {  /* If ASCII, NOT apply FT_Get_Advance() */
				pch[0]=*p;
			 	sdw=FTsymbol_uft8strings_pixlen(face, fw, fh, (unsigned char *)pch);
				if(sdw>0) sum += sdw;
			}
			/* Other unicodes, call FT_Get_Advances */
			else {
				/* FT_Get_Advances(FT_Face face, FT_UInt start, FT_UInt count, FT_Int32 load_flags, FT_Fixed *padvances)
				 * !!! WARNING !!! load_flags must be the same as in FT_Load_Char( face, wcode, flags ) when writeFB
        	        	 * the same ptr string.
				 * Strange!!! a little faster than FT_Get_Advance()
				 */
				//error= FT_Get_Advance( face, *wcstr, FT_LOAD_RENDER , &advance );
				error= FT_Get_Advances( face, *wcstr, 1, FT_LOAD_RENDER , &advance );
				if(error)
					printf("%s: Fail to call FT_Get_Advances().\n",__func__);
				else
					sum += advance>>16;
			}
			p += size;
		}
		else {  /* If it's a wrong UFT-8 encoding, try to move on... */
			p++;
			continue;
		}
	}

/* ------ <<<  Critical Zone  */
//        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

	return sum;
}


/*---------------------------------------------------------------------------------------------
TODO: self_cooked width, test
Put all advance values of given chars to buffer. and return
the pointer.

@face: 		FT_Face
//@load_flags:	A flag indicating what to load for this glyph
		!!! WARNING !!! Must be the same as in FT_Load_Char( face, wcode, flags ) as for writeFB
		the same string.
@fw,fh:		Expected font width and height, Must be the same as in FT_Set_Pixel_Sizes() as for writeFB
		the same string.
@start:         the first glyph index.
@count:		The number of advance values you want to retrieve.

Return:
	Pointer to long		OK
	NULL			Fails
---------------------------------------------------------------------------------------------*/
long * FTsymbol_buffer_fontAdvances(FT_Face face, int fw, int fh, FT_UInt start, FT_UInt count)
{
  	FT_Error   error;
	long 	   *advances=NULL;  /* typedef signed long  FT_Fixed;   to store 16.16 fixed-point values */

	/* Calloc advances */
	advances=calloc(count, sizeof(long));
	if(advances==NULL) {
		printf("%s: Fail to calloc advances.\n",__func__);
		return NULL;
	}

	/* set character size in pixels */
	error = FT_Set_Pixel_Sizes(face, fw, fh);
   	/* OR set character size in 26.6 fractional points, and resolution in dpi
   	   error = FT_Set_Char_Size( face, 32*32, 0, 100,0 ); */
	if(error) {
		printf("%s: FT_Set_Pixel_Sizes() fails!\n",__func__);
		free(advances);
		return NULL;
	}

	error= FT_Get_Advances( face, start, count, FT_LOAD_RENDER , advances );
	if(error) {
		printf("%s: Fail to call FT_Get_Advances().\n",__func__);
		free(advances);
		return NULL;
	}

	return advances;
}


/*----------------------------------------------------------
Self_define/cook width for some special unicodes.

NOTE:
1. Those wcodes are assumed to have no bitmap (in freetype),
   even it has it will be ingored!
2. However in ctypes: '\f','\v','\n','\r' all take spaces. Use
   isspace() to confirm. --- Ignored here.

@wcode:		UNICODE value
@fw:		Expected/norminal width

Return:
	>=0:	Ok, re_defined width, in pixels.
	<0:	No re_defined width value for the wocde.
-----------------------------------------------------------*/
inline int FTsymbol_cooked_charWidth(wchar_t wcode, int fw)
{
	switch(wcode)
	{
		case 32:	/* ASCII SPACE */
			return fw*factor_SpaceWidth/2;
		case 9:		/* TAB */
			return fw*factor_TabWidth;
		case 13:	/* NEW LINE */
			return 0;
		case 0x3000:	/* 0x3000(12288) SPACE defined in CJK Unicode group 'Spacing Modifier' */
			return fw*factor_SpaceWidth;
		case 0xFEFF:	/* 0xFEFF(65279) ZERO_WIDTH_NO_BREAK SPACE, Normally for BOM(Byte-order-mark), at very beginning of a file. */
			return 0;
		case 0x2060:	/* 0x2060 Word Joiner */
			return 0;
		default:
			return -1;
	}

}

/*-------------------------------------------
Convert wcode to uft8 encoding and print it.
If wcode is illegal,  then print a '#'.
-------------------------------------------*/
void FTsymbol_unicode_print(wchar_t wcode)
{
	char pch[8];
        bzero(pch,sizeof(pch));

        printf("%s",char_unicode_to_uft8(&wcode, pch)>0 ? pch : "#");
}


/*-----------------------------------------------------------------------------------------------
1. Render a CJK character presented in UNICODE with the specified face type, then write the
   bitmap to FB.
2. The caller shall check and skip nonprintable symbols, and parse ASCII control codes.
   This function only deal with symbol bitmap if it exists.
   FTsymbol_cooked_charWidth() is called for self_defined charWidth for some nonprintable symbols.
3. Xleft will be subtrated by slot->advance.x first, then check, and write data to FB only if
   Xleft >=0. However, if bitmap data is NULL for the input unicode, xleft will NOT be changed.
4. Boundary box is defined as bbox_W=MAX(advanceX,bitmap.width) bbox_H=fh.
5. CJK wchars may extrude the BBOX a little, and ASCII alphabets will extrude more, especially for
   'j','g',..etc, so keep enough gap between lines.
6. Or to get symheight just as in symbol_load_asciis_from_fontfile() as BBOX_H. However it's maybe
   a good idea to display CJK and wester charatcters separately by calling differenct functions.
   i.e. symbol_writeFB() for alphabets and FTsymbol_unicode_writeFB() for CJKs.
7. Note: FT_Face->size->metrics.height will cover all font height and with certain gaps.
8. If wcode NOT available in face, then resort to egi_sysfont.emojis.
9. The lib_mutex applys ONLY for EGI_SYSFONT at present.
10.  Combining Characters/Emojis NOT supported yet!
     Example: Keycap 3 -- 'U+0033 U+FE0F U+2043'

TODO: buffer all font bitmaps for further use.

Journal:
2022-05-26:
	1. If wcode NOT available in face, then resort to egi_sysfont.emojis.


@fbdev:         FB device
		or Virt FB!!
		if NULL, just ignore to write to FB.
@face:          A face object in FreeType2 library.
@fh,fw:		Expected font size.
@wcode:		UNICODE value
@xleft:		Pixel space left in FB X direction (horizontal writing)
		Xleft will be subtrated by slot->advance.x first anyway, If xleft<0 then, it aborts.
		if NULL, Do NOT check.
@x0,y0:		Left top coordinate of the character bitmap,relative to FB coord system.
@transpcolor:   >=0 transparent pixel will not be written to FB, so backcolor is shown there.
                <0       no transparent pixel

@fontcolor:     font color(symbol color for a symbol)
                >= 0,  use given font color.
                <0  ,  use default color in img data.

use following COLOR:
#define SYM_NOSUB_COLOR -1  --- no substitute color defined for a symbol or font
#define SYM_NOTRANSP_COLOR -1  --- no transparent color defined for a symbol or font

@opaque:        >=0     set aplha value (0-255) for all pixels, and alpha data in sympage
			will be ignored.
                <0      Use symbol alpha data, or none.
                0       100% back ground color/transparent
                255     100% front color
--------------------------------------------------------------------------------------------------*/
inline void FTsymbol_unicode_writeFB(FBDEV *fb_dev, FT_Face face, int fw, int fh, wchar_t wcode, int *xleft,
				int x0, int y0, int fontcolor, int transpcolor,int opaque )
{
  	FT_Error      error;
  	FT_GlyphSlot slot = face->glyph;
	EGI_SYMPAGE ftsympg={0};	/* a symbol page to hold the character bitmap */
	ftsympg.symtype=symtype_FT2;
	int gindex;	/* char glyph index */
	int advanceX;   /* X advance in pixels */
	int bbox_W;	/* boundary box width, taken bbox_H=fh */
	int delX;	/* adjust bitmap position in boundary box, according to bitmap_top */
	int delY;
	int sdw;	/* Self defined width for the wcode */

	/* 1. Check input data */
	if(face==NULL) {
		printf("%s: input FT_Face is NULL!\n",__func__);
		return;
	}

#if 1 /* ----- 1A. TEST: EGI_FONT_BUFFER, NO mutexlock applied, to spped up!(HK2022-06-16)  ----- */
	EGI_WCHAR_GLYPH *wchar_glyph;
	if( FTsymbol_glyph_buffered(face, fw, wcode) ) {
		wchar_glyph=egi_fontbuffer->fontdata +(wcode-egi_fontbuffer->unistart);

	        /* Check only when xleft is NOT NULL */
       		if( xleft != NULL ) {
		        /* Check if the wcode has re_defined width, those wcodes are assumed to have no bitmap.*/
       			sdw=FTsymbol_cooked_charWidth(wcode, fw);
		        if( sdw >=0 )  {
       			        *xleft -= sdw;
               			return;         /* Ignore bitmap */
			}
			/* Use face width */
	        	else  {
        	        	/* Reduce xleft */
	        	        *xleft -= wchar_glyph->advanceX; //  advanceX;
				if(fb_dev==NULL)
        	        		return;
        		}

	        	/* If NO space left, do NOT writeFB then. */
        		if( *xleft < 0 )
                		return;
		}

		/* Get glyph data from font buffer */
        	ftsympg.alpha     =  wchar_glyph->alpha; 	/* Ref. only */
	        ftsympg.symheight =  wchar_glyph->symheight;	/* MAYBE 0!? */
        	ftsympg.ftwidth   =  wchar_glyph->ftwidth;
		delX		  =  wchar_glyph->delX;
		delY		  =  wchar_glyph->delY;
		advanceX	  =  wchar_glyph->advanceX;

        	/* write to FB,  symcode =0, whatever  */
	        if(fb_dev != NULL) {
        	        symbol_writeFB(fb_dev, &ftsympg, fontcolor, transpcolor, x0+delX, y0+delY, 0, opaque);
	        }

		return;
	}
#endif /* ----- END USE EGI_FONT_BUFFER ----- */


	/* 2. Mutex lock FT_library */
	// FT_Library   FTlib=face->glyph->library;
	// EGI_FONTS *fonts=container_of(&FTlib, EGI_FONTS, library); /* XXX NOPE! FT_Library is a pointer! */
        // if(pthread_mutex_lock(&fonts->lib_mutex) !=0 ){
        if(pthread_mutex_lock(&egi_sysfonts.lib_mutex) !=0 ){
                EGI_PLOG(LOGLV_ERROR,"%s: Fail to lock lib_mutex! Err'%s'.", __func__, strerror(errno));
                return ;
	}
/* ------ >>>  Critical Zone  */

/////////////////// With lib_mutex ---- NOT NECESSAR ---- /////////////////////////
#if 0 /* ----------------    3. TEST: EGI_FONT_BUFFER  -------------------- */

EGI_WCHAR_GLYPH *wchar_glyph;
if( FTsymbol_glyph_buffered(face, fw, wcode) ) {
	wchar_glyph=egi_fontbuffer->fontdata +(wcode-egi_fontbuffer->unistart);

//	printf("Wcharuffered!\n");

 #if 0 // NOT here, no need if fb_dev==NULL */
	/* Get glyph data from font buffer */
        ftsympg.alpha     =  wchar_glyph->alpha; 	/* Ref. only */
        ftsympg.symheight =  wchar_glyph->symheight;
        ftsympg.ftwidth   =  wchar_glyph->ftwidth;
	delX		  =  wchar_glyph->delX;
	delY		  =  wchar_glyph->delY;
	advanceX	  =  wchar_glyph->advanceX;
 #endif

        /* Check only when xleft is NOT NULL */
        if( xleft != NULL )
        {
	        /* Check if the wcode has re_defined width, those wcodes are assumed to have no bitmap.*/
        	sdw=FTsymbol_cooked_charWidth(wcode, fw);
	        if( sdw >=0 )  {
        	        *xleft -= sdw;

/* ------ <<<  Critical Zone  */
		        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

                	return;         /* Ignore bitmap */
	        }
        	else  {
                	/* reduce xleft */
	                *xleft -= wchar_glyph->advanceX; //  advanceX;

			if(fb_dev==NULL) {   /* HK2022-06-14 */
/* ------ <<<  Critical Zone  */
			        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

        	        	return;
			}
        	}

	        /* If NO space left, do NOT writeFB then. */
        	if( *xleft < 0 ) {
/* ------ <<<  Critical Zone  */
		        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

                	return;
		}
	}

	/* Get glyph data from font buffer */
        ftsympg.alpha     =  wchar_glyph->alpha; 	/* Ref. only */
        ftsympg.symheight =  wchar_glyph->symheight;
        ftsympg.ftwidth   =  wchar_glyph->ftwidth;
	delX		  =  wchar_glyph->delX;
	delY		  =  wchar_glyph->delY;
	advanceX	  =  wchar_glyph->advanceX;

        /* write to FB,  symcode =0, whatever  */
        if(fb_dev != NULL) {
                symbol_writeFB(fb_dev, &ftsympg, fontcolor, transpcolor, x0+delX, y0+delY, 0, opaque);
        }

/* ------ <<<  Critical Zone  */
        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

	return;
}
#endif /* ----- END USE EGI_FONT_BUFFER ----- */

TRY_LOAD_CHAR:

	/* 4. Check if wcode is included in the face. */
	if( FT_Get_Char_Index(face, wcode)==0 ) {
//		printf("%s: FT_Get_Char_Index fails!\n", __func__);
		#if 0  /* Check if it's Emojis in egi_sysemojis */
		if( FT_Get_Char_Index(egi_sysemojis.regular, wcode) ) {
//			printf("%s: wcode U+%X is an Emoji!\n",__func__, wcode);
			face=egi_sysemojis.regular;  /* <-------- Shift face and slot */
			slot = face->glyph;
		}
		#else /* Check if its's Emojis in egi_sysfonts.emojis */
		if( FT_Get_Char_Index(egi_sysfonts.emojis, wcode) ) {
//			printf("%s: U+%X is an Emoji!\n",__func__, wcode);
			face=egi_sysfonts.emojis;  /* <-------- Shift face and slot */
			slot = face->glyph;
		}
		#endif
		else {
/* ------ <<<  Critical Zone  */
		        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);
			return;
		}
	}

    /* 5. Load char and render glyph */
    /* 5.1 FT HAS COLOR */
    if(FT_HAS_COLOR(face)) {   /* FT_HAS_COLOR(face) == face->face_flags & FT_FACE_FLAG_COLOR */
	/* Note:
	*      1. Four major color font formats: (Ref. https://www.colorfonts.co/)
	*	   .Google CBDT/CBLC tables		 Vector+Bitmap
	*	   .Apple sbix table			 Bitmap ( +libpng,  --with-png=yes )
	*	   .Microsoft COLR/CPAL tables		 Bitmap
	*	   .Joint Adobe and Mozilla SVG tables   Vector
	*/

	/* 5.1.1. FT has fixed sizes ---> Cross check 5.1.1.1  */
	if ( FT_HAS_FIXED_SIZES(face) ){
//		egi_dpstd("FT_HAS_FIXED_SIZES!\n");
		//if(FT_Select_Size(face, 0)) egi_dpstd("FT_Select_Size fails!\n");

		/* 5.1.1.1. Pick the nearest size  ---- Compare height ---- */
                int i, snum; /* snum: size index */
                for(i=0; i< face-> num_fixed_sizes; i++) {
			if(face->available_sizes[i].height > fh ) //face->available_sizes[i].width
				break;
		}
		if(i==0)  snum=0;
		else if(i==face->num_fixed_sizes)  snum=face->num_fixed_sizes-1;
		else {
		   if( fh - face->available_sizes[i-1].height > face->available_sizes[i].height -fh )
			snum=i;
		    else
			snum=i-1;
		}
//		egi_dpstd("Pick size num=%d\n", snum);
		FT_Select_Size(face, snum);
	}
	/* 5.1.2. No fixed sizes */
	else {
		FT_Set_Pixel_Sizes(face, fw, fh);
	}

	/* 5.1.3 Get char and render */
	gindex = FT_Get_Char_Index(face, wcode); /* Regain as face changed see 4.*/

//	error = FT_Load_Glyph(face, gindex,  FT_LOAD_NO_SCALE|FT_LOAD_COLOR|FT_LOAD_FORCE_AUTOHINT|FT_LOAD_TARGET_NORMAL);
	error = FT_Load_Glyph(face, gindex, FT_LOAD_COLOR); //XXX |FT_LOAD_NO_SCALE); //FT_LOAD_COLOR|FT_LOAD_DEFAULT);
						//FT_LOAD_DEFAULT==0 |FT_LOAD_RENDER ; FT_LOAD_SBITS_ONLY
	//error = FT_Render_Glyph(face->glyph,  FT_RENDER_MODE_NORMAL); //FT_RENDER_MODE_LCD, FT_RENDER_MODE_NORMAL
	if(error) {
		egi_dpstd("FT_Load_Glyph fails for U+%X, gindex=%d, error=%d, \n", wcode, gindex, error);

		/* Note: Combining Emojis NOT supported! Example: Keycap 3 -- 'U+0033 U+FE0F U+2043'.
                 * So U+20E3(Combning enclosing keycap) will fail here!
		 * HK2022-06-05
		 */

/* ------ <<<  Critical Zone  */
	        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

		return;
	}

#if 0	/* TEST: ----- Pixel mode : ftimage.h ----- */
		switch(slot->bitmap.pixel_mode) {
		case FT_PIXEL_MODE_NONE: printf("FT_PIXEL_MODE_NONE ");break;
		case FT_PIXEL_MODE_MONO: printf("FT_PIXEL_MODE_MONO ");break;
		case FT_PIXEL_MODE_GRAY: printf("FT_PIXEL_MODE_GRAY ");break;
		case FT_PIXEL_MODE_GRAY2: printf("FT_PIXEL_MODE_GRAY2 ");break;
		case FT_PIXEL_MODE_GRAY4: printf("FT_PIXEL_MODE_GRAY4 ");break;
		case FT_PIXEL_MODE_LCD: printf("FT_PIXEL_MODE_LCD ");break;
		case FT_PIXEL_MODE_LCD_V: printf("FT_PIXEL_MODE_LCD_V ");break;
		case FT_PIXEL_MODE_BGRA:printf("FT_PIXEL_MODE_BGRA ");break;
		default: printf("FT_PIXEL_MODE_UNKNOWN ");break;
	}
#endif

	/* 5.1.4 Assign symheight/ftwidth */
	ftsympg.symheight = slot->bitmap.rows; //fh; /* font height in pixels is bigger than bitmap.rows! */
	ftsympg.ftwidth   = slot->bitmap.width; /* ftwidth <= (slot->advance.x>>6) */

	/* 5.1.5 Extract color/alpha for ftsympg */
	if( slot->bitmap.buffer && slot->bitmap.pixel_mode==FT_PIXEL_MODE_BGRA) {
		/* Allocate alpha and color */
		ftsympg.alpha = malloc(ftsympg.symheight*ftsympg.ftwidth*sizeof(EGI_8BIT_ALPHA));
		ftsympg.color = malloc(ftsympg.symheight*ftsympg.ftwidth*sizeof(EGI_16BIT_COLOR));
		if( ftsympg.alpha==NULL || ftsympg.color==NULL) {
			printf("%s: Fail to malloc alpha/color for ftsympg!\n",__func__);

/* ------ <<<  Critical Zone  */
		        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);
			return;
		}

		/* Get alpha and color */
		int i,j;
		uint8_t *bgra=slot->bitmap.buffer;
		for(i=0; i<ftsympg.symheight; i++) {
		   for(j=0; j<ftsympg.ftwidth; j++) {
			/* Raw data format: BGRA */
			ftsympg.color[i*ftsympg.ftwidth+j]=COLOR_RGB_TO16BITS(bgra[2], bgra[1], bgra[0]);
			ftsympg.alpha[i*ftsympg.ftwidth+j]=bgra[3];
			bgra +=4;
	   	   }
		}
	}
	//else { ftsympg.color=NULL; ftsympg.alpha=NULL; }

    } /* END 5.1 */

    /* 5.2 FT HAS NO COLOR */
    else {

#if 0	/* C2.1   TEST: ---  Check if Emoji/Others 2022-05-26 */
	if( FT_Get_Char_Index(face, wcode)==0 ) {
//		printf("%s: FT_Get_Char_Index fails!\n", __func__);
		 // IF(IS_EMOJI()) {   https://www.unicode.org/emoji/charts/emoji-list.html
		#if 0  /* Emojis in egi_sysemojis */
		if( FT_Get_Char_Index(egi_sysemojis.regular, wcode) ) {
//			printf("%s: wcode U+%X is an Emoji!\n",__func__, wcode);
			face=egi_sysemojis.regular;  /* <-------- Shift face and slot */
			slot = face->glyph;
			goto TRY_LOAD_CHAR;
		}
		#else /* Emojis in egi_sysfonts.emojis */
		if( FT_Get_Char_Index(egi_sysfonts.emojis, wcode) ) {
//			printf("%s: wcode U+%X is an Emoji!\n",__func__, wcode);
			face=egi_sysfonts.emojis;  /* <-------- Shift face and slot */
			slot = face->glyph;
			goto TRY_LOAD_CHAR;
		}
		#endif
		else {
/* ------ <<<  Critical Zone  */
		        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);
			return;
		}
	}
#endif /////////////////////////////////////

	/* C2.2 FT has fixed size, pick one.  */
	if ( FT_HAS_FIXED_SIZES(face) ) {
//		egi_dpstd("FT_HAS_FIXED_SIZES!\n");

		/* C2.2.1. Pick the nearest size ---> Cross check C1.1.1 */
                int i, snum; /* snum: size index */
                for(i=0; i< face-> num_fixed_sizes; i++) {
			if(face->available_sizes[i].height > fh ) //face->available_sizes[i].width
				break;
		}
		if(i==0)  snum=0;
		else if(i==face->num_fixed_sizes)  snum=face->num_fixed_sizes-1;
		else {
		   if( fh - face->available_sizes[i-1].height > face->available_sizes[i].height -fh )
			snum=i;
		    else
			snum=i-1;
		}
//		egi_dpstd("Pick size num=%d\n", snum);
		FT_Select_Size(face, snum);
	}
	/* C2.3 FT has no fixed size, set character size  */
	else {
		error = FT_Set_Pixel_Sizes(face, fw, fh);
	   	/* OR set character size in 26.6 fractional points, and resolution in dpi
   		   error = FT_Set_Char_Size( face, 32*32, 0, 100,0 ); */
		if(error) {
			printf("%s: FT_Set_Pixel_Sizes() fails, only fixed sizes?\n",__func__);

/* ------ <<<  Critical Zone  */
	        	pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

			return;
		}
	}

	/* This is the MAX. possible height of glyph, as for line distance. */
	//printf("Current FT_Face height=%d pixels\n", face->size->metrics.height>>6 );

	/* Do not set transform, keep up_right and pen position(0,0)
    		FT_Set_Transform( face, &matrix, &pen ); */

	/* Load char and render, old data in face->glyph will be cleared */
    	error = FT_Load_Char( face, wcode, FT_LOAD_RENDER );
    	if (error) {
		egi_dperr("error=%d, FT_Load_Char() fails! wcode=0x%04X\n", error,wcode);

/* ------ <<<  Critical Zone  */
	        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);
		return;
	}

	/* Assign alpha to ftsympg, Ownership IS NOT transfered! */
	ftsympg.alpha 	  = slot->bitmap.buffer;
	ftsympg.symheight = slot->bitmap.rows; //fh; /* font height in pixels is bigger than bitmap.rows! */
	ftsympg.ftwidth   = slot->bitmap.width; /* ftwidth <= (slot->advance.x>>6) */

    }  /* END: face has NO color */


#if 0 /* TEST: bitmap data ------------ */
	int k,j;
	printf("bitmap: rows=%d, width=%d\n", ftsympg.symheight, ftsympg.ftwidth);
	for(k=0; k<ftsympg.symheight; k++) {
		for(j=0; j<ftsympg.ftwidth; j++)
			printf("%d ", ftsympg.alpha[ftsympg.ftwidth*k+j]);
		printf("\n");
	}
#endif

	/* Check whether xleft is used up first. */
	advanceX = slot->advance.x>>6;

#if 0	/* TEST: ----------- Check if advanceX is reasonale! OR the font lib data is corrupted!??? */
	if(advanceX>2*fw || ftsympg.symheight>2*fh || ftsympg.ftwidth>2*fw ) {
		egi_dpstd("!!! WARNING !!! Font lib data seems corrupted! advanceX=%d, bitmap.rows=%d,.width=%d\n",
						advanceX, slot->bitmap.rows,slot->bitmap.width);
	}
#endif

	//bbox_W = (advanceX > slot->bitmap.width ? advanceX : slot->bitmap.width);
	bbox_W = advanceX;  /* To be consistent with FTcharmap_uft8strings_writeFB() when calling FT_Get_Advances() to update xleft. */

#if 0	/* TEST: --- special wocdes --- */
	if( ftsympg.alpha==NULL || advanceX==0 || wcode==9 || wcode==13 || wcode==65279 ) {
		/* 1. With bitmap */
		if(ftsympg.alpha && advanceX==0)
			printf("wcode U+%X has bitmap, and zero width.\n", wcode);
		else if(ftsympg.alpha)
			printf("wcode U+%X has bitmap, and advanceX=%d.\n", wcode, advanceX);

		/* 2. Without bitmap */
		if(ftsympg.alpha==NULL && advanceX==0 )
			printf("wcode U+%X has no bitmap, and zero width.\n", wcode);
		else if(ftsympg.alpha==NULL)
			printf("wcode U+%X has no bitmap, and advanceX=%d.\n", wcode,advanceX);

	}
#endif


#if 0	/* TEST:  ------ Bitmap and advance --------- */
        if(ftsympg.alpha==NULL && advanceX==0 )
                printf("wcode U+%X has no bitmap, and zero width.\n", wcode);
        else if(ftsympg.alpha==NULL)
                printf("wcode U+%X has no bitmap, and advanceX=%d.\n", wcode,advanceX);
#endif


     /* Check only when xleft is NOT NULL */
     if( xleft != NULL )
     {

#if 0	/* Chars without bitmap, or any chars you don't want to draw!
	 * You may modify width for special chars here
	 * Check bitmap data, we need bbox_W here. alpha NULL chars:  SPACEs
	 */
	if( ftsympg.alpha==NULL || wcode==9 || wcode==13 || wcode==65279 ) { /* TAB/CR/ZERO_WIDTH_NO_BREAK_SPACE */
//		printf("%s: Alpha data is NULL for unicode=0x%x\n", __func__, wcode);
//		draw_rect(fb_dev, x0, y0, x0+bbox_W, y0+fh );

		/* ------ SPACE CONTROL ------
		 * some font file may have NO bitmap data for them!
	 	 * but it has bitmap.width and advanced defined.
	 	 */
		/* If a HALF/FULL Width SPACE */
		if( wcode == 12288 ) {  	/* wcode==12288(0x3000) LOCALE SPACE (SDB case/Full_width) */
			*xleft -= fw*factor_SpaceWidth;
		}
		else if ( wcode == 0xFEFF ) 	/* 0xFEFF(65279) ZERO_WIDTH_NO_BREAK SPACE */
			*xleft -= 0;
		else if ( wcode == 32 ) 	/* ASCII SPACE (wchar_t)(L" ") */
			*xleft -= fw*factor_SpaceWidth/2;
		else if ( wcode == 9 ) 		/* TAB */
			*xleft -= fw*factor_TabWidth;
		else				/* CR   ...Maybe other unicode, it is supposed to have defined bitmap.width and advanceX */
			*xleft -= 0;

/* ------ <<<  Critical Zone  */
	        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

		/* Free color/alpha */
		if(FT_HAS_COLOR(face)) { free(ftsympg.alpha); free(ftsympg.color); }

		return;  /* DO NOT draw image */
	}
	else
#endif

	/* Note: Even the wcode has no bitmap (ftsympg.alpha==NULL), its advance value may NOT be zero!
	 *	Example: wcode==32(SPACE), and wcode==160.
	 */

	/* Check if the wcode has re_defined width, those wcodes are assumed to have no bitmap.*/
	sdw=FTsymbol_cooked_charWidth(wcode, fw);
	if( sdw >=0 )  {
		*xleft -= sdw;

/* ------ <<<  Critical Zone  */
	        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

		/* Free color/alpha */
		if(FT_HAS_COLOR(face)) { free(ftsympg.alpha); free(ftsympg.color); }

		return;		/* Ignore bitmap */
	}
	else  {
		/* reduce xleft */
		*xleft -= bbox_W;
	}

	/* If NO space left, do NOT writeFB then. */
	if( *xleft < 0 ) {

/* ------ <<<  Critical Zone  */
	        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

		/* Free color/alpha */
		if(FT_HAS_COLOR(face)) { free(ftsympg.alpha); free(ftsympg.color); }

		return;
	}

	/* taken bbox_H as fh */

     }  /* END of if( xleft != NULL ) */


	/* Adjust bitmap position relative to boundary box */
	if(FT_HAS_COLOR(face)) { /* TODO: Only for some kind of Emojis? */
		delX=0; //delX=(fw-ftsympg.ftwidth)/2;
		delY=0; //delY=(fh-ftsympg.symheight)/2;
	} else {
		delX= slot->bitmap_left;
		delY= -slot->bitmap_top + fh;
	}
	//printf("symH=%d,  dely=%d\n", ftsympg.symheight,delY);
	//printf("Origin: (%d,%d)\n",x0+delX, y0+delY);


#if 0 /* ----TEST: Display Boundary BOX------- */
	/* Note: Assume boundary box start from x0,y0(same as bitmap)
	 */
	//draw_rect(fb_dev, x0, y0, x0+bbox_W, y0+fh);  /* Only space size,  not right position .  */
	draw_rect(fb_dev, x0, y0+delY, x0+bbox_W, y0+ftsympg.symheight+delY);  /* bitmap size */

#endif

	/* write to FB,  symcode =0, whatever  */
	if(fb_dev != NULL) {
		symbol_writeFB(fb_dev, &ftsympg, fontcolor, transpcolor, x0+delX, y0+delY, 0, opaque);
	}

	/* Free color/alpha */
	if(FT_HAS_COLOR(face)) { free(ftsympg.alpha); free(ftsympg.color); }

/* ------ <<<  Critical Zone:  NOTE: ftsympg has ref. to slot->bitmap.buffer!  */
        pthread_mutex_unlock(&egi_sysfonts.lib_mutex);

}


/*-----------------------------------------------------------------------------------------
Write a string of wchar_t(unicodes) to FB.

TODO: 1. Alphabetic words are treated letter by letter, and they may be separated at the end of
         a line, so it looks not good.



@fbdev:         FB device,
		or Vrit FB!!!
@face:          A face object in FreeType2 library.
@fh,fw:		Height and width of the wchar.
@pwchar:        pointer to a string of wchar_t.
@xleft:		Pixel space left in FB X direction (horizontal writing)
		Xleft will be subtrated by slot->advance.x first anyway, If xleft<0 then, it aborts.
@pixpl:         pixels per line.
@lines:         number of lines available.
@gap:           space between two lines, in pixel. may be minus.
@x0,y0:		Left top coordinate of the character bitmap,relative to FB coord system.

@transpcolor:   >=0 transparent pixel will not be written to FB, so backcolor is shown there.
                <0       no transparent pixel

@fontcolor:     font color(symbol color for a symbol)
                >= 0,  use given font color.
                <0  ,  use default color in img data.

use following COLOR:
#define SYM_NOSUB_COLOR -1  --- no substitute color defined for a symbol or font
#define SYM_NOTRANSP_COLOR -1  --- no transparent color defined for a symbol or font

@opaque:        >=0     set aplha value (0-255) for all pixels, and alpha data in sympage
			will be ignored.
                <0      Use symbol alpha data, or none.
                0       100% back ground color/transparent
                255     100% front color


return:
                >=0     number of wchat_t write to FB
                <0      fails
------------------------------------------------------------------------------------------------*/
int  FTsymbol_unicstrings_writeFB( FBDEV *fb_dev, FT_Face face, int fw, int fh, const wchar_t *pwchar,
			       unsigned int pixpl,  unsigned int lines,  unsigned int gap,
                               int x0, int y0,
			       int fontcolor, int transpcolor, int opaque )
{
	int px,py;		/* bitmap insertion origin(BBOX left top), relative to FB */
	wchar_t *p=(wchar_t *)pwchar;
        int xleft; 		/* available pixels remainded in current dline */
        unsigned int ln; 	/* dlines used */

	/* check input data */
	if(face==NULL) {
		printf("%s: input FT_Face is NULL!\n",__func__);
		return -1;
	}
	if( pixpl==0 || lines==0 || pwchar==NULL )
		return -1;

	px=x0;
	py=y0;
	xleft=pixpl;
	ln=0;

	//while( *p ) {
	while( *p != L'\0' ) {  /* wchar t end token */
		/* --- check whether lines are used up --- */
		if( ln > lines-1) {
			return p-pwchar;
		}

		/* CONTROL ASCII CODE:
		 * If return to next line
		 */
		if(*p=='\n') {
//			printf("%s: ASCII code: Next Line\n", __func__);
			/* change to next line, +gap */
			ln++;
			xleft=pixpl;
			py+= fh+gap;
			p++;
			continue;
		}
		/* If other control codes or DEL, skip it */
		else if( *p < 32 || *p==127  ) {
//			printf("%s: ASCII control code: %d\n", *p,__func__);
			p++;
			continue;
		}

		/* write unicode bitmap to FB, and get xleft renewed. */
		px=x0+pixpl-xleft;
		FTsymbol_unicode_writeFB(fb_dev, face, fw, fh, *p, &xleft,
							 px, py, fontcolor, transpcolor, opaque );

		/* --- check line space --- */
		if(xleft<=0) {
			if(xleft<0) { /* NOT writeFB, reel back pointer p */
				p--;
			}
			/* change to next line, +gap */
			ln++;
			xleft=pixpl;
			py+= fh+gap;
		}

		p++;

	} /* end while() */

	return p-pwchar;
}


/*-----------------------------------------------------------------------------------------
Write a string of charaters with UFT-8 encoding to FB.

XXXTODO: 1. Alphabetic words are treated letter by letter, and they may be separated at the end of
         a line, so it looks not good.
	 --- OK. SplitWord Control is done.
          2. Apply character functions in <ctype.h> to rule out chars, with consideration of locale setting?

@fbdev:         FB device
@face:          A face object in FreeType2 library.
@fh,fw:		Height and width of the wchar.
@pstr:          pointer to a string with UTF-8 encoding.
@xleft:		Pixel space left in FB X direction (horizontal writing)
		Xleft will be subtrated by slot->advance.x first anyway, If xleft<0 then, it aborts.
@pixpl:         pixels per line.
@lines:         number of lines available.
@gap:           space between two lines, in pixel. may be minus.
@x0,y0:		Left top coordinate of the character bitmap,relative to FB coord system.

@transpcolor:   >=0 transparent pixel will not be written to FB, so backcolor is shown there.
                <0       no transparent pixel

@fontcolor:     font color(symbol color for a symbol)
                >= 0,  use given font color.
                <0  ,  use default color in img data.

use following COLOR:
#define SYM_NOSUB_COLOR -1  --- no substitute color defined for a symbol or font
#define SYM_NOTRANSP_COLOR -1  --- no transparent color defined for a symbol or font

@opaque:        >=0     set aplha value (0-255) for all pixels, and alpha data in sympage
			will be ignored.
                <0      Use symbol alpha data, or none.
                0       100% back ground color/transparent
                255     100% front color

@cnt:		Total printable characters written.
@lnleft:	Lines left unwritten.

@penx:		The last pen position X
@peny:		The last pen position Y.
		Note: Whatever pstr is finished or not
		      penx,peny will be reset to starting position of next line!!!
return:
                >=0     bytes write to FB
                <0      fails
------------------------------------------------------------------------------------------------*/
int  FTsymbol_uft8strings_writeFB( FBDEV *fb_dev, FT_Face face, int fw, int fh, const unsigned char *pstr,
			       unsigned int pixpl,  unsigned int lines,  unsigned int gap,
                               int x0, int y0,
			       int fontcolor, int transpcolor, int opaque,
			       int *cnt, int *lnleft, int* penx, int* peny )
{
	int size;
	int count;		/* number of character written to FB*/
	int px,py;		/* bitmap insertion origin(BBOX left top), relative to FB */
	const unsigned char *p=pstr;
        int xleft; 		/* available pixels remainded in current line */
        unsigned int ln; 	/* lines used */
 	wchar_t wcstr[1];
	const unsigned char *pword;	/* Pointer to an English word */
	int  wordlen;
	int  wordpixlen;

	/* check input data */
	if(face==NULL) {
		printf("%s: input FT_Face is NULL!\n",__func__);
		return -1;
	}

	if( pixpl==0 || lines==0 || pstr==NULL )
		return -1;

#if 0	/* Check knerning */
	if( FT_HAS_KERNING(face) )
		printf("OK, face has kerning!\n");
	else
		printf("Face has NO knering!\n");
#endif

	/* Init vars */
	px=x0;
	py=y0;
	xleft=pixpl;
	count=0;
	ln=0;		/* Line index from 0 */

	/* SplitWord control:  Get point to the first Englisth word */
	if( !enable_SplitWord ) {
		pword=(UFT8_PCHAR)cstr_get_peword(p);
		wordlen=cstr_strlen_eword(pword);
	}

	while( *p ) {

#if 0 /* TEST: ----------- */
		printf("\r count=%d, ln=%d", count, ln);  fflush(stdout);
#endif

		/* --- check whether lines are used up --- */
		if( ln >= lines) {  /* ln index from 0 */
//			printf("%s: Lines not enough! finish only %d chars.\n", __func__, count);
			//return p-pstr;
			/* here ln is the written line number,not index number */
			goto FUNC_END;
		}

		/*  SplitWord Control */
	   	if( !enable_SplitWord && pword!=NULL ) {
		    /* If start of an English word */
		    if( p==pword ) {
			/* Check if xleft is enough for the whole word. */
			wordpixlen=FTsymbol_eword_pixlen(face, fw, fh, pword);
			/* TODO if the word's pixlen is > pixpl!!! */
			if(wordpixlen>pixpl)
				egi_dpstd("!!!WARNING!!! wordpixlen >pixpl! The word is too long!\n");
			if( xleft < wordpixlen ) {
	                        /* change to next line, +gap */
        	                ln++;
                	        xleft=pixpl;
                        	py+= fh+gap;

				continue;  /* Loop back to while() */
			}

		    }
		    /* If end of an English word */
		    else if ( p==pword+wordlen ) {  /* here pword!=NULL */
			/* Get pointer to NEXT word and and its len. */
			pword=(UFT8_PCHAR)cstr_get_peword(p);
			wordlen=cstr_strlen_eword(pword);

			continue;  /* Loop back to while() */
		    }
	   	}

		/* Convert one character to unicode, return size of utf-8 code */
		size=char_uft8_to_unicode(p, wcstr);

#if 0 /* ----TEST: print ASCII code */
		if(size==1) {
			printf("%s: ASCII code: %d \n",__func__,*wcstr);
		}
		//if( !iswprint(*wcstr) )  ---Need to setlocale(), not supported yet!
		//	printf("Not printalbe wchar code: 0x%x \n",*wcstr);

#endif /*-----TEST END----*/

		/* Shift offset to next wchar */
		if(size>0) {
			p+=size;
			count++;
		}
		else {	/* If fails, try to step 1 byte forward to locate next recognizable unicode wchar */
			p++;
			continue;
		}

		/* CONTROL ASCII CODE:
		 * If return to next line
		 */
		if(*wcstr=='\n') {
//			printf(" ... ASCII code: Next Line ...\n ");
			/* change to next line, +gap */
			ln++;
			xleft=pixpl;
			py+= fh+gap;
			continue;
		}
		/* If other control codes or DEL, skip it */
		else if( (*wcstr < 32 || *wcstr==127) && *wcstr!=9 ) {	/* Exclude TAB(9) */
//			printf(" ASCII control code: %d\n", *wcstr);
			continue;
		}

		/* write unicode bitmap to FB, and get xleft renewed. */
		px=x0+pixpl-xleft;
		FTsymbol_unicode_writeFB(fb_dev, face, fw, fh, wcstr[0], &xleft,
							 px, py, fontcolor, transpcolor, opaque);


#if 0  //////* Check if ERROR in Freetype fonts! *//////////
		//printf("xleft=%d\n", xleft);
		if(xleft < (int)-pixpl) { //TODO:  not so good!
			printf("!!!WARNING: unicode: 0x%04X, xleft=%d\n", wcstr[0], xleft);
			xleft= -1;
		}
#endif //////////////////////////////////////////////////////

		/* --- Check line space --- */
		if(xleft<0) { /* NOT writeFB, reel back pointer p */
			p-=size;
			count--;
			/* change to next line, +gap */
			ln++; xleft=pixpl; py+= fh+gap;
		}
		else if( xleft==0 && (*p)!=0 ) {  /* If JUST gets to end of the string, then NO need for increment! */
			/* change to next line, +gap */
			ln++; xleft=pixpl; py+= fh+gap;
		}

	} /* end while() */

	/* If finish writing whole strings, ln++ to get written lines, as ln index from 0 */
	if(*pstr)   /* To rule out NULL input */
		ln++;

	/* ??? TODO: if finishing writing whole strings, reset px,py to next line. */
	#if 0
	if( *wcstr != '\n' ) { /* To avoid 2 line returns */
		//px=x0;
		xleft=pixpl;
		py+=fh+gap;
	}
	#endif

FUNC_END:
	//printf("%s: %d characters written to FB.\n", __func__, count);
	if(cnt!=NULL)
		*cnt=count;
	if(lnleft != NULL)
		*lnleft=lines-ln; /* here ln is written lines, not index number */
	if(penx != NULL)
		*penx=x0+pixpl-xleft;
	if(peny != NULL)
		*peny=py;

	return p-pstr;
}


/*-----------------------------------------------------------------------------------------
Write a string of charaters with UFT-8 encoding to an IMGBUF.

Params: See FTsymbol_uft8strings_writeFB()
x0,y0:	Left top coordinate of the character bitmap,relative to IMGBUF coord system.


return:
                >=0     bytes write to FB
                <0      fails
-----------------------------------------------------------------------------------------*/
int     FTsymbol_uft8strings_writeIMG( EGI_IMGBUF *imgbuf, FT_Face face, int fw, int fh, const unsigned char *pstr,
                               unsigned int pixpl,  unsigned int lines,  unsigned int gap,
                               int x0, int y0,
                               int fontcolor, int transpcolor, int opaque,
                               int *cnt, int *lnleft, int* penx, int* peny )
{
	int ret;
	FBDEV vfb={0};
	vfb.vimg_owner=false;

	if(imgbuf==NULL)
		return -1;


	if( init_virt_fbdev(&vfb, imgbuf, NULL) !=0 ) {
		egi_dpstd("Fail to init_virt_fbdev!\n");
		return -2;
	}

	ret=FTsymbol_uft8strings_writeFB( &vfb, face, fw, fh, pstr, pixpl, lines, gap, x0,y0,
						fontcolor, transpcolor, opaque, cnt, lnleft, penx, peny );

        release_virt_fbdev(&vfb);

	return ret;
}


/*--------------------------------------------------------------------------------------
Write a string of charaters with UFT-8 encoding to an IMGBUF.
Start penxy is at the left bottom point of the first glyph.

@Params:   See FTsymbol_uft8strings_writeFB()
@x0,y0:	   Start point (Left BOTTOM) coordinate of the character bitmap,
	   relative to IMGBUF coord system.
@angle:    Line orientation.
	   Screen Coord: anti_clockwise as + direction.

TODO:
1. Mutex lock FT_library.
2. Auto. return for a NEW line if exceeds current pixpl.
3. lnleft.

return:
                >=0     bytes write to FB
                <0      fails
-----------------------------------------------------------------------------------------*/
int  FTsymbol_uft8strings_writeIMG2( EGI_IMGBUF *imgbuf, FT_Face face, int fw, int fh, const unsigned char *pstr,
                               	     unsigned int pixpl,  unsigned int lines,  unsigned int gap,
                             	     int x0, int y0,  int angle,  int fontcolor,
                                     int *cnt, int *lnleft, int* penx, int* peny )
{
	const unsigned char *p=pstr;
	int	      	size;
 	wchar_t       	wcstr[1];
        int 		xleft; 		/* available pixels remainded in current line */
        unsigned int 	ln; 		/* lines used */
	int		count;		/* Number of wcodes */
//	int		px,py;
	int		sdw;

  	FT_Error      error;
  	FT_GlyphSlot  slot;
  	FT_Matrix     matrix;	/* transformation matrix */
  	FT_Vector     pen;      /* untransformed origin  */
// 	int	      starty;   /* starting pen Y postion for rotated lines */
//  	FT_BBox	      bbox;	/* Boundary box */
	float 	      fang;
	int	      dx=0, dy=0; /* Start point dx/dy for each line, as relative to glyph local origin(0,0) */


	/* Check input */
	if( pstr== NULL )
		return -1;
	if( face==NULL ) {
		egi_dpstd("Input face is NULL!\n");
		return -1;
	}
	if( imgbuf==NULL || imgbuf->imgbuf==NULL) {
		egi_dpstd("Imgbuf is invalid!\n");
		return -1;
	}

  	/* Get pointer to the glyph slot */
  	slot = face->glyph;

	/* Init vars */
	xleft=pixpl;
	count=0;
	ln=0;

	/* Set up matrix for bitmap transformation, ROTATION only. */
  	fang= 3.14159*angle/180.0;
// 	egi_dpstd(" ------------- angle: %d, %f ----------\n", angle, fang);
	matrix.xx = (FT_Fixed)( cosf( fang ) * 0x10000L );
	matrix.xy = (FT_Fixed)(-sinf( fang ) * 0x10000L );
	matrix.yx = (FT_Fixed)( sinf( fang ) * 0x10000L );
	matrix.yy = (FT_Fixed)( cosf( fang ) * 0x10000L );

  	/* Set pen position.
   	 * 1. the pen position in 26.6 cartesian space coordinates
     	 *    64 units per pixel for pen.x and pen.y
	 * 2. Set glyph local origin at (0,0), add x0/y0 at egi_imgbuf_blend_FTbitmap().
	 */
	pen.x = 0; //x0<<6;  //  * 64;
	pen.y = 0; //y0<<6;  //  * 64;
//	startx=pen.x;
//	starty=pen.y;
	dx=0;

	/* Set character size in pixels */
	error = FT_Set_Pixel_Sizes(face, fw, fh);
   	/* OR set character size in 26.6 fractional points, and resolution in dpi
   	 *  error = FT_Set_Char_Size( face, 32*32, 0, 100,0 );
	 */
	if(error) {
		egi_dpstd("FT_Set_Pixel_Sizes() fails!\n");
		return -1;
	}

	while(*p) {
		/* --- check whether lines are used up --- */
		if( ln >= lines) {  /* ln index from 0 */
			/* here ln is the written line number,not index number */
			goto FUNC_END;
		}

		/* Convert one character to unicode, return size of utf-8 code */
		size=char_uft8_to_unicode(p, wcstr);

		/* shift offset to next wchar */
		if(size>0) {
			p+=size;
			count++;
		}
		else {	/* If fails, try to step 1 byte forward to locate next recognizable unicode wchar */
			p++;
			continue;
		}

		/* CONTROL ASCII CODE:
		 * If return to next line
		 */
		if(*wcstr=='\n') {
//			printf(" ... ASCII code: Next Line ...\n ");

			/* change to next line, +gap */
			ln++;
			xleft=pixpl;
			/* Update/Reset penxy */
			pen.x = 0;
			pen.y = 0;
			/* dx/dy as relative to x0/y0 */
			dx=roundf(sinf(3.14159*angle/180.0)*(ln*(fh+gap)));
			dy=roundf(cosf(3.14159*angle/180.0)*(ln*(fh+gap)));

			continue;
		}
		/* If other control codes or DEL, skip it */
		else if( (*wcstr < 32 || *wcstr==127) && *wcstr!=9 ) {	/* Exclude TAB(9) */
//			printf(" ASCII control code: %d\n", *wcstr);
			continue;
		}

#if 0 /* TEST:------ Draw circle at pen.x,pen.y ------ */
		fbset_color2(&gv_fb_dev, WEGI_COLOR_GREEN);
		draw_circle(&gv_fb_dev, x0+(pen.x>>6), y0-(pen.y>>6), 3);
#endif

		/* Set transform:
   		 * Re_set transformation before loading each wchar,
		 * as pen postion and transfer_matrix may changes.
        	 */
	    	FT_Set_Transform( face, &matrix, &pen );

		/* Load char and render, old data in face->glyph will be cleared */
	    	error = FT_Load_Char( face, wcstr[0], FT_LOAD_RENDER );
    		if (error) {
			egi_dperr("FT_Load_Char() fails! wcode=0x%04X\n", wcstr[0]);
			return -2;
		}
		//egi_dpstd("FT_Load_Char() wcode=0x%04X\n", wcstr[0]);

		/* Check if the wcode has re_defined width, those wcodes are assumed to have no bitmap.*/
        	sdw=FTsymbol_cooked_charWidth(wcstr[0], fw);
	        if( sdw >=0 )  {
//			printf("sdw=%d \n", sdw);
			slot->advance.x += roundf(sdw*cos(3.14159*angle/180.0))*64 ;
			slot->advance.y += roundf(sdw*sin(3.14159*angle/180.0))*64;
		}

		/* Get boundary box in grid-fitted pixel coordinates */
		//FT_Glyph_Get_CBox( face->glyph, FT_GLYPH_BBOX_PIXELS, &bbox );
		//printf("face boundary box: width=%ld, height=%ld.\n",
		//	(face->bbox.xMax - face->bbox.xMin)>>6, (face->bbox.yMax-face->bbox.yMin)>>6 );

		/* Note that even when the glyph image is transformed, the metrics are NOT ! */
		//printf("glyph metrics[in 26.6 format]: width=%ld, height=%ld.\n",
		//			slot->metrics.width>>6, slot->metrics.height>>6);

		/* adjust bitmap position relative to boundary box */
		//delX= slot->bitmap_left;
		//delY= -slot->bitmap_top + fh;

		//printf("bitmap_left=%d, bitmap_top=%d \n", slot->bitmap_left, slot->bitmap_top);

#if 1 /* TEST:------ Draw bbox ------ */
		fbset_color2(&gv_fb_dev, WEGI_COLOR_LTGREEN);
		draw_rect(&gv_fb_dev, x0+dx+slot->bitmap_left, y0+dy-slot->bitmap_top,
		 		x0+dx+slot->bitmap_left+slot->bitmap.width-1,
				y0+dy-slot->bitmap_top+slot->bitmap.rows-1);
#endif

		error=egi_imgbuf_blend_FTbitmap(imgbuf, x0+dx+slot->bitmap_left, y0+dy-slot->bitmap_top, &slot->bitmap, fontcolor);
		if(error) {
//			egi_dpstd(" Fail to blend FTbitmap wcode=0x%04X\n", wcstr[0]);
		}

    		/* Increment pen position, already including sdw. see above. */
	    	pen.x += slot->advance.x;
    		pen.y += slot->advance.y;

	} /* End while() */

	/* If finish writing whole strings, ln++ to get written lines, as ln index from 0 */
	if(*pstr)   /* To rule out NULL input */
		ln++;

FUNC_END:
	/* Pass out results */
	if(cnt)  *cnt = count;
	if(penx) *penx = pen.x >>6;
	if(peny) *peny = pen.y >>6;

	/* Reset as identity */
    	FT_Set_Transform( face, NULL, NULL);

	/* Return bytes */
	return p-pstr;
}


#if  0 ///////////////////////////////////////////////////////////////////////////////////////
/*-----------------------------------------------------------------------------------------------
Write uft-8 string to a TEXT BOX, which holds a (FB) back buffer.

return:
                >=0     bytes write to FB
                <0      fails
----------------------------------------------------------------------------------------------------*/
int  FTsymbol_uft8strings_writeFB_PAD(FBDEV *fb_dev, EGI_PAD pad,  FT_Face face,
			       int fw, int fh, const unsigned char *pstr,
			       unsigned int pixpl,  unsigned int lines,  unsigned int gap,
                               int x0, int y0,
			       int fontcolor, int transpcolor, int opaque )
{


}
#endif /////////////////////////////////////////////////////////////////////////////////


/*-------------------------------------------------------------------------------------
( In most case, try to call FTsymbol_uft8string_getAdvances() instead, it's faster! )

Get total length of characters in pixels.

Note:
1. Count all chars and '\n' will be ingnored!
2. Limit: Max. counter = 1<<30;
3. For ASCII symbols, results are NOT same as FTsymbol_uft8string_getAdvances()??!
   Example: abc , . : ..
   So It will call FTsymbol_uft8strings_pixlen() in that case.

Return:
	>=0  	length in pixels
	<0	Fails
----------------------------------------------------------------------------------------*/
int  FTsymbol_uft8strings_pixlen( FT_Face face, int fw, int fh, const unsigned char *pstr)
{
	int size;
	int count;		/* number of character written to FB*/
	//int px,py;		/* bitmap insertion origin(BBOX left top), relative to FB */
	const unsigned char *p=pstr;
        int xleft=(1<<30); 	/* available pixels remainded in current line */
 	wchar_t wcstr[1];

	/* check input data */
	if(face==NULL) {
		printf("%s: input FT_Face is NULL!\n",__func__);
		return -1;
	}

	if(pstr==NULL )
		return -1;

	count=0;
	while( *p ) {

		/* convert one character to unicode, return size of utf-8 code */
		size=char_uft8_to_unicode(p, wcstr);

		/* shift offset to next wchar */
		if(size>0) {
			p+=size;
			count++;
		}
		else {	/* if fail, step 1 byte forward to locate next recognizable unicode wchar */
			p++;
			continue;
		}

		/* CONTROL ASCII CODE:
		 * If return to next line
		 */
		if(*wcstr=='\n') {
//			printf("ASCII code: Next Line\n");
			continue;
		}
		/* If other control codes or DEL, skip it */
		else if( (*wcstr < 32 || *wcstr==127) && *wcstr!=9 ) {	/* Exclude TAB(9) */
//			printf(" ASCII control code: %d\n", *wcstr);
			continue;
		}

		/* write unicode bitmap to FB, and get xleft renewed. */
		FTsymbol_unicode_writeFB(NULL, face, fw, fh, wcstr[0], &xleft,
							 0, 0, WEGI_COLOR_BLACK, -1, -1 );

		/* --- check line space --- */
		if(xleft<=0) {
			if(xleft<0) { /* if <0: writeFB is not done, reel back pointer p */
				p-=size;
				count--;
			}
			/* if =0: writeFB is done, break */
			break;
		}

	} /* end while() */

	return (1<<30)-xleft;
}


/*-------------------------------------------------------------------------------------
( In most case, try to call FTsymbol_uft8string_getAdvances() instead, it's faster! )

Get total pixel length of an English word.

Note:
1. Limit: Max. counter = 1<<30;
2. For ASCII symbols, results are NOT same as FTsymbol_uft8string_getAdvances()??!
   Example: abc , . : ..
   So It will call FTsymbol_uft8strings_pixlen() in that case.

Return:
	>=0  	length in pixels
	<0	Fails
----------------------------------------------------------------------------------------*/
int  FTsymbol_eword_pixlen( FT_Face face, int fw, int fh, const unsigned char *pword)
{
	const unsigned char *p;
        int xleft=(1<<30); 	/* available pixels remainded in current line */
 	wchar_t wcstr[1];

	/* check input data */
	if(face==NULL) {
		printf("%s: input FT_Face is NULL!\n",__func__);
		return -1;
	}

	if(pword==NULL)
		return -1;

	/* Addup all pixlen for each char in the eword. */
	p=pword;
	while(*p && isalnum(*p)) {

		wcstr[0] = *p;

		/* write unicode bitmap to FB, and get xleft renewed. */
		FTsymbol_unicode_writeFB(NULL, face, fw, fh, wcstr[0], &xleft,
							 0, 0, WEGI_COLOR_BLACK, -1, -1 );

		/* --- check line space --- */
		if(xleft<=0) {
			if(xleft<0) { /* if <0: writeFB is not done, reel back pointer p */
				p-=1;
			}
			/* if =0: writeFB is done, break */
			break;
		}

		/* An English alphanumeric character is ALWAYS 1 byte! */
		p+=1;

	} /* end while() */

        /* Include any trailing SPACEs/TABs */
        if(*p && p!=pword) {
                while( *p && isblank(*p) ) {

			wcstr[0] = *p;

	                /* write unicode bitmap to FB, and get xleft renewed. */
	                FTsymbol_unicode_writeFB(NULL, face, fw, fh, wcstr[0], &xleft,
                                                         0, 0, WEGI_COLOR_BLACK, -1, -1 );

        	        /* --- check line space --- */
                	if(xleft<=0) {
                        	if(xleft<0) { /* if <0: writeFB is not done, reel back pointer p */
                                	p-=1;
                        	}
                        	/* if =0: writeFB is done, break */
                        	break;
                	}

                	/* An English alphanumeric character is ALWAYS 1 byte! */
                	p+=1;
		}
        }

	/* Return occupied pixels. */
	return (1<<30)-xleft;
}


/*---------------------------------------------------------------------------------
Create a EGI_FONT_BUFFER holding glyph data of a group of characters represented by
a series of UNICODES.

@FT_Face	Typeface.
		!!! It MUST be a ref. to a facetype of already loaded sys EGI_FONTS
@fw,fh		Expected font width/height, in pixels.
@unistart	A unicode, as start of the character group.
		Basic Han Unicode range:      U+4E00 - U+9FA5
		Basic CJK Unified Ideographs: 0x4E00 - U+9FFC

@size:		Size of the character group, total number of characters buffered.

Note:
1. A wchar may have no image data! however may still have advance value.
2. FTsymbol_cooked_charWidth() is NOT applied here!

Return:
	A pointer to an EGI_FONT_BUFFER		OK
	NULL					Fails
----------------------------------------------------------------------------------*/
EGI_FONT_BUFFER* FTsymbol_create_fontBuffer(FT_Face face, int fw, int fh, wchar_t unistart, size_t size)
{
	EGI_FONT_BUFFER *fontbuff;
  	FT_Error      error;
	size_t i;
	int	alphasize;
	int	memsize=0; /* Space for EGI_FONT_BUFFER */

	/* Check input data */
	if(face==NULL) {
		printf("%s: input FT_Face is NULL!\n",__func__);
		return NULL;
	}
	if( fw <=0 || fh <=0 ) {
		printf("%s: input font size invalid!\n",__func__);
		return NULL;
	}

        /* Face info. */
        printf("%s: Face family_name:%s, style_name: %s\n", __func__, face->family_name, face->style_name);
        printf("%s: It contains %ld faces and %ld glyphs.\n", __func__, face->num_faces, face->num_glyphs);
	printf("%s: It contains %d num_charmaps.\n", __func__, face->num_charmaps);

	/* Allocate EGI_FONT_BUFFER */
	fontbuff=calloc(1, sizeof(EGI_FONT_BUFFER));
	if(fontbuff==NULL) {
		printf("%s: Fail to calloc fontbuff!\n",__func__);
		return NULL;
	}
	memsize +=sizeof(EGI_FONT_BUFFER);

	/* Family_name and Style_name  */
	if(face->family_name)
		strncpy(fontbuff->family_name, face->family_name, EGI_FONTBUFF_NAME_MAX-1);
	if(face->style_name)
		strncpy(fontbuff->style_name, face->style_name, EGI_FONTBUFF_NAME_MAX-1);

	/* Allocate fontbuff->fontdata */
	fontbuff->fontdata=calloc(size, sizeof(typeof(*fontbuff->fontdata)));
	if(fontbuff->fontdata==NULL) {
		printf("%s: Fail to calloc fontbuFF->fontdata!\n",__func__);
		free(fontbuff);
		return NULL;
	}
	memsize +=size*sizeof(typeof(*fontbuff->fontdata));

	/* set character size in pixels */
	error = FT_Set_Pixel_Sizes(face, fw, fh);
	if(error) {
		printf("%s: FT_Set_Pixel_Sizes() fails!\n",__func__);
		free(fontbuff->fontdata);
		free(fontbuff);
		return NULL;
	}

	/* Load character and render glyph one by one, buffer to fontbuff. */
	for(i=0; i<size; i++) {

		/* Load char and render, old data in face->glyph will be cleared */
    		error = FT_Load_Char( face, unistart+i, FT_LOAD_RENDER );
	    	if (error) {
			printf("%s: FT_Load_Char() fails! with i=%zd\n",__func__, size);
			FTsymbol_free_fontBuffer(&fontbuff);
			return NULL;
		}

		/* Buffer data */
		fontbuff->fontdata[i].symheight	=face->glyph->bitmap.rows;    /* fh >= bitmap.rows! */
		fontbuff->fontdata[i].ftwidth	=face->glyph->bitmap.width;    /* ftwidth <= (slot->advance.x>>6) */
		fontbuff->fontdata[i].advanceX	=(face->glyph->advance.x)>>6;
		fontbuff->fontdata[i].delX 	=face->glyph->bitmap_left;
		fontbuff->fontdata[i].delY 	=fh-face->glyph->bitmap_top;

		/* Allocat alpha and copy bitmap data */
		if( face->glyph->bitmap.buffer != NULL ) {
			alphasize=(fontbuff->fontdata[i].symheight) * (fontbuff->fontdata[i].ftwidth) *sizeof(EGI_8BIT_ALPHA);
			fontbuff->fontdata[i].alpha=calloc(1, alphasize);
			if(fontbuff->fontdata[i].alpha==NULL) {
				printf("%s: Fail to calloc fontdata[%zd].alpha!\n", __func__, i);
				FTsymbol_free_fontBuffer(&fontbuff);
				return NULL;
			}
			memcpy(fontbuff->fontdata[i].alpha, face->glyph->bitmap.buffer, alphasize);

			memsize +=alphasize;
		}
		/* ELSE: The wchar has NO bitmap! */
#if 1 /* TEST: ------- */
		else if(face->glyph->bitmap.width) {
			printf("%s: !!! CAUTION !!!\n  Unicode U+%X has NO bitmap while its bitmap.width=%d!\n",
								__func__, unistart+i, face->glyph->bitmap.width);
		}
#endif

	}

	/* Assign other parameters */
	fontbuff->face=face;
	fontbuff->fw=fw;
	fontbuff->fh=fh;
	fontbuff->unistart=unistart;
	fontbuff->size=size;

	printf("%s: memsize=%.2fKBs\n", __func__, memsize/1024.0);
	return fontbuff;
}


/*--------------------------------
Free an EGI_FONT_BUFFER.
--------------------------------*/
void FTsymbol_free_fontBuffer(EGI_FONT_BUFFER **fontbuff)
{
	int i;

	if(fontbuff==NULL || *fontbuff==NULL)
		return;

	/* Free all alpha data */
	for(i=0; i < (*fontbuff)->size; i++)
		free((*fontbuff)->fontdata[i].alpha);

	/* Free all fontdata */
	free((*fontbuff)->fontdata);

	free(*fontbuff);
	*fontbuff=NULL;
}

/*-----------------------------------------------------------------------
Save fontbuffer to a file.
If the file already exists, then it truncates the file first.

--- Cross check with struct EGI_FONT_BUFFER and EGI_WCHAR_GLYPH ---

@fontbuffer: Pointer to an EGI_FONT_BUFFER
@fpath:	     File path

Return:
	0 	OK
	<0	Fails
-----------------------------------------------------------------------*/
int FTsymbol_save_fontBuffer(const EGI_FONT_BUFFER *fontbuff, const char *fpath)
{
	int i,j;
	int nwrite;
	int nmemb;
	FILE *fil;
	char name[EGI_FONTBUFF_NAME_MAX]={0};
	int ret=0;

	if( fontbuff==NULL || fpath==NULL)
		return -1;

	/* Open/Create file */
	fil=fopen(fpath, "wb");
	if(fil==NULL) {
		egi_dperr("Fail to open file '%s'.", fpath);
		return -2;
	}

	/* Write magic words, Totally 16bytes. */
	nwrite=fwrite(FONTBUFFER_MAGIC_WORDS, 16, 1, fil);
	if(nwrite<1) {
		egi_dperr("Fail to write magic words!");
		ret=-3;
		goto END_FUNC;
	}

	/* Write Byte_Order_Mark. "FFFE"--Little_Endian, "FEFF"--Big Endian  0xFEFF(65279) ZERO_WIDTH_NO_BREAK SPACE  */
	char bomark[2];
	if(egi_host_littleEndian()) {
		bomark[0]=0xFF; bomark[1]=0xFE;
	} else {
		bomark[0]=0xFE; bomark[1]=0xFF;
	}
	nmemb=2;
	nwrite=fwrite(bomark, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write byte order mark!");
		ret=-4;
		goto END_FUNC;
	}

	/* Write family_name */
	if(fontbuff->family_name) {
		strncpy(name, fontbuff->family_name, EGI_FONTBUFF_NAME_MAX-1);
		nmemb=strlen(fontbuff->family_name)+1; /* +1 EOF */
	}
	else { /* At least write a EOF */
		name[0]=0;
		nmemb=1;
	}
	nwrite=fwrite(fontbuff->family_name, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write fontbuff->family_name!");
		ret=-5;
		goto END_FUNC;
	}

	/* Write style_name */
	if(fontbuff->style_name) {
		strncpy(name, fontbuff->style_name, EGI_FONTBUFF_NAME_MAX-1);
		nmemb=strlen(fontbuff->style_name)+1; /* +1 EOF */
	}
	else { /* At least write a EOF */
		name[0]=0;
		nmemb=1;
	}
	nwrite=fwrite(fontbuff->style_name, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write fontbuff->style_name!");
		ret=-5;
		goto END_FUNC;
	}


	/* Write fw, fh */
	nmemb=sizeof(typeof(fontbuff->fw));
	nwrite=fwrite(&fontbuff->fw, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write fontbuff->fw!");
		ret=-6;
		goto END_FUNC;
	}
	nmemb=sizeof(typeof(fontbuff->fh));
	nwrite=fwrite(&fontbuff->fh, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write fontbuff->fh!");
		ret=-7;
		goto END_FUNC;
	}

	/* Write unistart */
	nmemb=sizeof(typeof(fontbuff->unistart));
	nwrite=fwrite(&fontbuff->unistart, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write fontbuff->unistart!");
		ret=-8;
		goto END_FUNC;
	}

	/* Write size */
	nmemb=sizeof(typeof(fontbuff->size));
	nwrite=fwrite(&fontbuff->size, 1, nmemb, fil);
	if(nwrite<nmemb) {
		egi_dperr("Fail to write fontbuff->size!");
		ret=-9;
		goto END_FUNC;
	}

	/* Check fontdata */
	if(fontbuff->fontdata==NULL) {
		egi_dperr("fontbuff->fontdata is NULL!");
		ret=-9;
		goto END_FUNC;
	}

	/* Write fontbuff->fontdata;  index 0 --> unistart, 1 --> unistart+1, ... size-1 --> unistart+size-1 */
	for(i=0; i< fontbuff->size; i++) {
		/* Write symheight */
		nmemb=sizeof(typeof(fontbuff->fontdata[0].symheight));
		nwrite=fwrite(&fontbuff->fontdata[i].symheight, 1, nmemb, fil);
		if(nwrite<nmemb) {
			egi_dperr("Fail to write fontbuff->fontdata[%d].symheight!", i);
			ret=-10;
			goto END_FUNC;
		}

		/* Write ftwidth */
		nmemb=sizeof(typeof(fontbuff->fontdata[0].ftwidth));
		nwrite=fwrite(&fontbuff->fontdata[i].ftwidth, 1, nmemb, fil);
		if(nwrite<nmemb) {
			egi_dperr("Fail to write fontbuff->fontdata[%d].ftwidth!", i);
			ret=-10;
			goto END_FUNC;
		}

		/* Write advanceX */
		nmemb=sizeof(typeof(fontbuff->fontdata[0].advanceX));
		nwrite=fwrite(&fontbuff->fontdata[i].advanceX, 1, nmemb, fil);
		if(nwrite<nmemb) {
			egi_dperr("Fail to write fontbuff->fontdata[%d].advanceX!", i);
			ret=-10;
			goto END_FUNC;
		}

		/* Write delX/Y */
		nmemb=sizeof(typeof(fontbuff->fontdata[0].delX));
		nwrite=fwrite(&fontbuff->fontdata[i].delX, 1, nmemb, fil);
		if(nwrite<nmemb) {
			egi_dperr("Fail to write fontbuff->fontdata[%d].delX!", i);
			ret=-10;
			goto END_FUNC;
		}
		//nmemb=sizeof(typeof(fontbuff->fontdata[0].delY));
		nwrite=fwrite(&fontbuff->fontdata[i].delY, 1, nmemb, fil);
		if(nwrite<nmemb) {
			egi_dperr("Fail to write fontbuff->fontdata[%d].delY!", i);
			ret=-10;
			goto END_FUNC;
		}

		/* Write alpha data */
		int datasize=fontbuff->fontdata[i].symheight * fontbuff->fontdata[i].ftwidth;
		nwrite=fwrite(fontbuff->fontdata[i].alpha, datasize, 1, fil); /* write at once */
		if(nwrite<1) {
			egi_dperr("Fail to write fontbuff->fontdata[%d].alpha!", j);
			ret=-10;
			goto END_FUNC;
		}
	}


END_FUNC:
	/* Close fil */
	if( fclose(fil) !=0 ) {
		egi_dperr("Fail to close file!\n");
		ret=-10;
	}

	return ret;
}

/*----------------------------------------------------------------
Load an EGI_FONT_BUFFER from a data file.

--- Cross check with struct EGI_FONT_BUFFER and EGI_WCHAR_GLYPH ---

TODO:
1. ByteOrder auto conversion.

@fpath:      File path

Return:
        !NULL       OK
        NULL        Fails
----------------------------------------------------------------*/
EGI_FONT_BUFFER* FTsymbol_load_fontBuffer(const char *fpath)
{
        int i,j;
        int nread;
        int nmemb;
        FILE *fil;
	char magic_words[16+1]={0};
	char name[EGI_FONTBUFF_NAME_MAX]={0};
	int pos;
	bool IsHostByteOrder; /* If ByteOrder is the same as host's */
	EGI_FONT_BUFFER *fontbuff=NULL;
	bool ret=0;

	if(fpath==NULL)
		return NULL;

	fontbuff=(EGI_FONT_BUFFER *)calloc(1, sizeof(EGI_FONT_BUFFER));
	if(fontbuff==NULL) {
		egi_dperr("Fail to calloc fontbuff!");
		return NULL;
	}

	/* Open file */
	fil=fopen(fpath, "rb");
	if(fil==NULL) {
		egi_dperr("Fail to open file '%s'.", fpath);
		free(fontbuff); // FTsymbol_free_fontBuffer(&fontbuff));
		return NULL;
	}

	/* Read magic words, Totally 16bytes */
	nmemb=16;
	nread=fread(magic_words, 1, nmemb, fil);
	if(nread<nmemb) {
		egi_dperr("Fail to read magic_words!");
		ret=-1;
		goto END_FUNC;
	}
	egi_dpstd("Magic words: %s\n", magic_words);
	/* Check magic words */
	if( strncmp(magic_words, FONTBUFFER_MAGIC_WORDS, 12) !=0 ) {
		egi_dpstd("magic words error! NOT a fontbuffer data file?\n");
		ret=-2;
		goto END_FUNC;
	}

	/* Read Byte_Order_Mark. "FFFE"--Little_Endian, "FEFF"--Big Endian  0xFEFF(65279) ZERO_WIDTH_NO_BREAK SPACE  */
	unsigned char bomark[2];
	nmemb=2;
	nread=fread(bomark, 1, nmemb, fil);
	if(nread<nmemb) {
		egi_dperr("Fail to read byte order mark!");
		ret=-3;
		goto END_FUNC;
	}
	if( bomark[0]==(unsigned char)0xFF && bomark[1]==(unsigned char)0xFE ) {
		egi_dpstd("Byte Order: Little_Endian\n");
		if(egi_host_littleEndian())
			IsHostByteOrder=true;
		else
			IsHostByteOrder=false;
	}
	else if( bomark[0]==(unsigned char)0xFE && bomark[1]==(unsigned char)0xFF ) {
		egi_dpstd("Byte Order: Big_Endian\n");
		if(!egi_host_littleEndian())
			IsHostByteOrder=true;
		else
			IsHostByteOrder=false;
	}
	else {
		egi_dpstd("Byte Order Mark error! bomark: %X%X\n", (unsigned char)bomark[0], (unsigned char)bomark[1]);
		ret=-4;
		goto END_FUNC;
	}

	/* Read family_name */
	pos=0;
	while( fread(name+pos,1,1,fil)>0 ) {
		if(name[pos]=='\0') break;
		pos++;
	}
	egi_dpstd("Fontbuffer family_name: '%s'\n", name);
	/* set name */
	strncpy(fontbuff->family_name, name, EGI_FONTBUFF_NAME_MAX-1);

	/* Read style_name */
	pos=0;
	while( fread(name+pos,1,1,fil)>0 ) {
		if(name[pos]=='\0') break;
		pos++;
	}
	egi_dpstd("Fontbuffer styple_name: '%s'\n", name);
	/* set name */
	strncpy(fontbuff->style_name, name, EGI_FONTBUFF_NAME_MAX-1);


	/* TODO: ByteOrder conversion */
	if(!IsHostByteOrder) {
		egi_dpstd("Byte Order Mark NOT same as the host!\n");
		ret=-5;
		goto END_FUNC;
	}

	/* Read fw, fh */
	nmemb=sizeof(typeof(fontbuff->fw));
	nread=fread(&fontbuff->fw, 1, nmemb, fil);
	if(nread<nmemb) {
		egi_dperr("Fail to read fontbuff->fw!");
		ret=-6;
		goto END_FUNC;
	}
	nmemb=sizeof(typeof(fontbuff->fh));
	nread=fread(&fontbuff->fh, 1, nmemb, fil);
	if(nread<nmemb) {
		egi_dperr("Fail to read fontbuff->fh!");
		ret=-7;
		goto END_FUNC;
	}
	egi_dpstd("Fontsize fw=%d, fh=%d\n", fontbuff->fw,fontbuff->fh);

	/* Read unistart */
	nmemb=sizeof(typeof(fontbuff->unistart));
	nread=fread(&fontbuff->unistart, 1, nmemb, fil);
	if(nread<nmemb) {
		egi_dperr("Fail to read fontbuff->unistart!");
		ret=-8;
		goto END_FUNC;
	}
	egi_dpstd("unistart: U+%X\n", fontbuff->unistart);

	/* Read size */
	nmemb=sizeof(typeof(fontbuff->size));
	nread=fread(&fontbuff->size, 1, nmemb, fil);
	if(nread<nmemb) {
		egi_dperr("Fail to read fontbuff->size!");
		ret=-9;
		goto END_FUNC;
	}
	egi_dpstd("%d unicodes in the fontbuffer.\n", fontbuff->size);

	/* Allocate fontbuff->fontdata */
	fontbuff->fontdata=calloc(fontbuff->size, sizeof(EGI_WCHAR_GLYPH));
	if(fontbuff==NULL) {
		egi_dperr("Fail to calloc fontbuff->fontdata!");
                ret=-10;
                goto END_FUNC;
	}


	/* Read fontbuff->fontdata;  index 0 --> unistart, 1 --> unistart+1, ... size-1 --> unistart+size-1 */
	EGI_WCHAR_GLYPH  tmpGlyph;
	for(i=0; i< fontbuff->size; i++) {
		/* Read symheight */
		nmemb=sizeof(typeof(tmpGlyph.symheight));
		nread=fread(&fontbuff->fontdata[i].symheight, 1, nmemb, fil);
		if(nread<nmemb) {
			egi_dperr("Fail to read fontbuff->fontdata[%d].symheight!", i);
			ret=-11;
			goto END_FUNC;
		}

		/* Read ftwidth */
		nmemb=sizeof(typeof(tmpGlyph.ftwidth));
		nread=fread(&fontbuff->fontdata[i].ftwidth, 1, nmemb, fil);
		if(nread<nmemb) {
			egi_dperr("Fail to read fontbuff->fontdata[%d].ftwidth!", i);
			ret=-11;
			goto END_FUNC;
		}

		/* Read advanceX */
		nmemb=sizeof(typeof(tmpGlyph.advanceX));
		nread=fread(&fontbuff->fontdata[i].advanceX, 1, nmemb, fil);
		if(nread<nmemb) {
			egi_dperr("Fail to read fontbuff->fontdata[%d].advanceX!", i);
			ret=-11;
			goto END_FUNC;
		}

		/* Write delX/Y */
		nmemb=sizeof(typeof(tmpGlyph.delX));
		nread=fread(&fontbuff->fontdata[i].delX, 1, nmemb, fil);
		if(nread<nmemb) {
			egi_dperr("Fail to read fontbuff->fontdata[%d].delX!", i);
			ret=-11;
			goto END_FUNC;
		}
		//nmemb=sizeof(typeof(tmpGlyph.delY));
		nread=fread(&fontbuff->fontdata[i].delY, 1, nmemb, fil);
		if(nread<nmemb) {
			egi_dperr("Fail to read fontbuff->fontdata[%d].delY!", i);
			ret=-11;
			goto END_FUNC;
		}

		/* Callocate  alpha */
		int datasize=fontbuff->fontdata[i].symheight * fontbuff->fontdata[i].ftwidth;
		fontbuff->fontdata[i].alpha=calloc(1,datasize);
		if(fontbuff->fontdata[i].alpha==NULL) {
			egi_dperr("Fail to calloc fontbuff->fontdata[%d].alpha!", i);
			ret=-11;
			goto END_FUNC;
		}

		/* Read alpha data */
		nread=fread(fontbuff->fontdata[i].alpha, datasize, 1, fil); /* write at once */
		if(nread<1) {
			egi_dperr("Fail to read fontbuff->fontdata[%d].alpha!", j);
			ret=-10;
			goto END_FUNC;
		}
	}

END_FUNC:
	/* If fails, free fontbuff */
	if(ret<0) {
 		FTsymbol_free_fontBuffer(&fontbuff);
	}

	/* Close fil */
	if( fclose(fil) !=0 ) {
		egi_dperr("Fail to close file!\n");
	}

	return fontbuff;
}

/*----------------------------------------------------
To check whether the font is buffered!

@face:   Typeface
@fsize:  Font size, here as font width!
@wcode:  As unicode of the wchar.
-----------------------------------------------------*/
inline bool FTsymbol_glyph_buffered(FT_Face face, int fsize, wchar_t wcode)
{

	if( !( egi_fontbuffer && egi_fontbuffer->face == face && egi_fontbuffer->fw == fsize ) )
		return false;

	if( !( wcode >= egi_fontbuffer->unistart && wcode < egi_fontbuffer->unistart+egi_fontbuffer->size ) )
		return false;

	return true;
}



