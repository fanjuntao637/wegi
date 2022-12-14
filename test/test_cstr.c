/*---------------------------------------------------------------------------------
This program is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License version 2 as published by the Free Software
Foundation.

Journal:
2021-12-17:
	1. test cstr_parse_simple_html().
2022-06-06:
	1. test cstr_check_uft8encoding().
2022-06-14:
	1. cstr_count_lines(), egi_check_uft8File()
2022-06-15:
	1. str_nextchar_uft8()  cstr_prevchar_uft8()

Midas Zhou
midaszhou@yahoo.com(Not in use since 2022_03_01)
----------------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <freetype2/ft2build.h>
#include <freetype2/ftglyph.h>
#include <ctype.h>
#include "egi_common.h"
#include "egi_utils.h"
#include "egi_FTsymbol.h"
#include "egi_cstring.h"

int main( int  argc,   char**  argv )
{
  int 		i,j,k;

  wchar_t *wbook=L"assd人心生一念，天地尽皆知。善恶若无报，乾坤必有私。\
　　那菩萨闻得此言，满心欢喜，对大圣道：“圣经云：‘出其言善。\
　　则千里之外应之；出其言不善，则千里之外适之。’你既有此心，待我到了东土大唐国寻一个取经的人来，教他救你。你可跟他做个徒弟，秉教伽持，入我佛门。再修正果，如何？”大圣声声道：“愿去！愿去！”菩萨道：“既有善果，我与你起个法名。”大圣道：“我已有名了，叫做孙悟空。”菩萨又喜道：“我前面也有二人归降，正是‘悟’字排行。你今也是‘悟’字，却与他相合，甚好，甚好。这等也不消叮嘱，我去也。”那大圣见性明心归佛教，这菩萨留情在意访神谱。\
　　他与木吒离了此处，一直东来，不一日就到了长安大唐国。敛雾收云，师徒们变作两个疥癫游憎，入长安城里，竟不觉天晚。行至大市街旁，见一座土地庙祠，二人径进，唬得那土地心慌，鬼兵胆战。知是菩萨，叩头接入。那土地又急跑报与城隍社令及满长安城各庙神抵，都来参见，告道：“菩萨，恕众神接迟之罪。”菩萨道：“汝等不可走漏消息。我奉佛旨，特来此处寻访取经人。借你庙宇，权住几日，待访着真僧即回。”众神各归本处，把个土地赶到城隍庙里暂住，他师徒们隐遁真形。\
　　毕竟不知寻出那个取经来，且听下回分解。";


#if 1 /////////// strtok_r ///////////////////
   //char *strp="111,222,333,444,555";  const char string, strtok_r() segmentaton fault!
   char strp[256];
   strcpy(strp, "111,222,333,444,555");

   char *savept;
   char *pt=NULL;
   const char *delim=", ";

   printf("strp before strtok_r(): %s\n", strp);

   pt=strtok_r(strp, delim, &savept);
   if(pt==NULL)
	printf("pt is NULL\n");
   else
   	printf("%s\n", pt);

   while(pt!=NULL) {
	printf("%s\n", pt);
	pt=strtok_r(NULL, delim, &savept);
   }

   printf("strp after strtok_r(): %s\n", strp);
   exit(0);
#endif


#if 0 /////////////////// cstr_nextchar_uft8()  cstr_prevchar_uft8()  //////////////////
   struct timeval tms,tme;
   UFT8_PCHAR pch;
   unsigned int ucnt;  /* uchar counter */

   EGI_FILEMMAP *fmap=egi_fmap_create(argv[1], 0, PROT_READ, MAP_PRIVATE);
   if(fmap==NULL) {
	printf("Please input nonzero file path!\n");
	exit(0);
   }

   /* TEST cstr_nextchar_uft8() */
   gettimeofday(&tms,NULL);
   pch=(UFT8_PCHAR)fmap->fp;
   ucnt=0;
   while(pch && *pch) {  /* Kick out '\0' */
	ucnt++;
	pch=cstr_nextchar_uft8(pch); /* '\0' count in <-------- */
   }
   gettimeofday(&tme,NULL);
   printf("cstr_nextchar_uft8(): Total %d uchars, cost %luus\n", ucnt, tm_diffus(tms,tme));
//cstr_nextchar_uft8(): Total 881378 uchars, cost 216753us

   /* TEST cstr_prevchar_uft8() */
   gettimeofday(&tms,NULL);
   pch=(UFT8_PCHAR)fmap->fp+fmap->fsize;
   ucnt=0;
   pch=cstr_prevchar_uft8(pch, (UFT8_PCHAR)fmap->fp); /* '\0' NOT count in <----- */
   while(pch) {
	ucnt++;
	pch=cstr_prevchar_uft8(pch, (UFT8_PCHAR)fmap->fp);
   }
   gettimeofday(&tme,NULL);
   printf("cstr_prevchar_uft8(): Total %d uchars, cost %luus\n", ucnt, tm_diffus(tms,tme));
//cstr_prevchar_uft8(): Total 881378 uchars, cost 596791us

   egi_fmap_free(&fmap);
   exit(0);
#endif

#if 0 //////////////////  cstr_count_lines()  ///////////////////////
   struct timeval tms,tme;
   printf("egi_count_file_lines: %d\n", egi_count_file_lines(argv[1]));

   EGI_FILEMMAP *fmap=egi_fmap_create(argv[1], 0, PROT_READ, MAP_PRIVATE);
   if(fmap==NULL) {
	printf("Please input nonzero file path!\n");
	exit(0);
   }
   while(1) {
	   gettimeofday(&tms,NULL);
	   printf("cstr_count_lines: %d\n", cstr_count_lines(fmap->fp));
	   gettimeofday(&tme,NULL);

	   printf("----- cost time in us: %lu ----\n",tm_diffus(tms,tme));
   }

   egi_fmap_free(&fmap);
   exit(0);
#endif

#if 0  /////////////////  off_t   egi_check_uft8File(const char *fpath)  ///////////////////////
	off_t offset;
	offset=egi_check_uft8File(argv[1]);

	if(offset==0) printf("Check UTF-8 encoding OK!\n");
	else if(offset>0) printf("Error uchar found at offset position %jd.\n", offset);
#endif

#if 0  /////////////////  cstr_check_uft8encoding(const unsigned char *ustr, off_t strlen)  ///////////////////////
   off_t off;

//   EGI_FILEMMAP *fmap=egi_fmap_create("utf8error.txt", 0, PROT_READ|PROT_WRITE, MAP_SHARED);
   EGI_FILEMMAP *fmap=egi_fmap_create(argv[1], 0, PROT_READ, MAP_PRIVATE);
   if(fmap==NULL) {
	printf("Please input nonzero file path!\n");
	exit(0);
   }

   /* TEST: Byte_Order_Mark
	UTF-8 Without BOM   ...
	UTF-8 with BOM      EF BB BF
        UTF-16LE            FF FE  //0xFEFF(65279) ZERO_WIDTH_NO_BREAK SPACE
	UTF-16BE	    FE FF
	UTF-32LE	    FF FE 00 00
	UTF-32BE	    00 00 FE FF
    */
   int skip=0;  /* Skip UTF8 BOM if any. */
   if( fmap->fsize >=4 ) {
   	if(fmap->fp[0]==0xEF && fmap->fp[1]==0xBB && fmap->fp[0]==0xBF ) {
		egi_dpstd("UTF-8 with BOM EF_BB_BF\n");
		skip=3;  /* <---- skip */
   	}
   	else if((unsigned char)(fmap->fp[0]) == (unsigned char)(0xFF) ) {
		if((unsigned char)(fmap->fp[1])==(unsigned char)0xFE) {
			if(fmap->fp[2]==0 && fmap->fp[3]==0)
				egi_dpstd("UTF-32LE with BOM FF_FE_00_00\n");
			else
				egi_dpstd("UTF-16LE with BOM FF_FE\n");
		}
   	}
	else if((unsigned char)(fmap->fp[0])==(unsigned char)0xFE && (unsigned char)(fmap->fp[1])==(unsigned char)0xFF )
		egi_dpstd("UTF-16BE with BOM FE_FF\n");
	else if( fmap->fp[0]==0x00 && fmap->fp[1]==0x00 && (unsigned char)(fmap->fp[2])==(unsigned char)0xFE
		 && (unsigned char)(fmap->fp[2])==(unsigned char)0xFF )
		egi_dpstd("UTF-32LE with BOM 00_00_FE_FF\n");
	else
		egi_dpstd("fsize>4, UTF-8 without BOM\n");
   }
   else { /* fsize<4 */
	egi_dpstd("fsize<4, UTF-8 without BOM\n");
   }

   #if 0 /* TEST: ---------- */
   printf("fmap->fp[10000]=0x%02X\n", ((unsigned char *)(fmap->fp))[10000] );
   fmap->fp[10000]=(char)0x60; //0b01100000;  /* 0x60 */
   printf("fmap->fp[10000]=0x%02X\n", ((unsigned char *)(fmap->fp))[10000] );
//   fmap->fp[2000]=0b01100000;
//   fmap->fp[3000]=0b01100000;
   egi_fmap_msync(fmap);
   #endif

   printf("Check...\n");
   off=cstr_check_uft8encoding((UFT8_PCHAR)fmap->fp+skip, fmap->fsize-skip);
   printf("Finish checking, off=%jd, fsize=%jd\n", off, fmap->fsize-skip);


   egi_fmap_free(&fmap);
   exit(0);
#endif


#if 0 /////////////////  egi_free_charList()  ///////////////////////
	int n=128;
   	char **ps=NULL;

while(1) {
   	ps=calloc(n, sizeof(char *));
   	for(i=0; i<n; i++) {
		ps[i]=calloc((i+1)*10, sizeof(char));
		sprintf(ps[i],"Hello_%d!", i);
   	}
	for(i=0; i<n; i++)
		printf("ps[%d]:%s\n", i, ps[i]);

	egi_free_charList(&ps, n);
	usleep(1000);
}
#endif

#if 0 /////////////////  cstr_parse_URL()  ///////////////////////
	const char * myURL="https://www.abcde.com:1234/doc/test?id=3&lan=en#p1";

	char *protocol, *hostname, *path, *dir, *filename, *dirURL;
	unsigned int port;

	if(argc>1)
	   myURL=argv[1];

	if( cstr_parse_URL(myURL, &protocol, &hostname, &port, &path, &filename, &dir, &dirURL)==0 ) {
		printf(" Protocol: %s\n Hostname: %s\n Port: %d\n Path: %s\n Filename: %s\n Dir: %s\n dirURL: %s\n",
			protocol, hostname, port, path, filename, dir, dirURL);
	}
	else
		printf("Fail to parse URL!\n");

	exit(0);
#endif

#if 0 /////////////////  cstr_parse_simple_html()  ///////////////////////
   char text[1024*5];
   int len;

   EGI_FILEMMAP *fmap=egi_fmap_create(argv[1], 0, PROT_READ, MAP_SHARED);
   if(fmap==NULL) {
	printf("Please input test HTML file path!\n");
	exit(0);
   }

   len=cstr_parse_simple_html(fmap->fp, text, sizeof(text));/* str_html, text */
   printf("len=%d, text: %s\n", len, text);

   exit(0);
#endif

#if 0 ////////////    cstr_replace_string() ## cstr_decode_htmlSpecailChars()  ////////////////////

	//char *cstr_replace_string(char **src, const char *obj, const char *sub);
	//char* cstr_decode_htmlSpecailChars(char *strHtml)

	char str1[]="Illustrate the &apos;insidious relationship&apos; between prices and wages.";
	char str2[]="Hello xx&nbsp;xx&nbspxx; Have a &quot;GOOD DAY&quot;! ---&apos; 1&lt;2 &apos;---, &amp;&amp;";
	char *pt;

	pt=str1;
//	printf("SRC: %s.\n SUB: %s\n", str1, cstr_replace_string(&pt, "&apos;", "'"));
	printf("SRC: %s.\n", str1);
	printf("HTML: %s\n", cstr_decode_htmlSpecailChars(pt));

	pt=str2;
	printf("SRC: %s.\n", str2);
	printf("HTML: %s\n", cstr_decode_htmlSpecailChars(pt));
#endif

#if 0 ////////////    cstr_parse_html_tag()  ////////////////////
	EGI_FILEMMAP *fmap=egi_fmap_create(argv[1], 0, PROT_READ, MAP_PRIVATE);
	if(fmap==NULL) {
		printf("Fail to mmap input file '%s'!\n", argv[1]);
		exit(0);
	}

	char *pt=fmap->fp;
	char attrString[1024];	/* To be big enough! */
	char value[512];		/* To be big enough! */
	char *content=NULL;
	int len;

while( (pt=cstr_parse_html_tag(pt, "div", attrString, &content, &len))!=NULL ) {
	printf("attribues: %s\n", attrString);
	//printf("content: '%s'\n", content);
	//printf("content length: %d bytes\n", len);

	/* Get tag attriS value and check */
	cstr_get_html_attribute(attrString, "class", value);
	if(strcmp(value, "top-story")==0) {
		printf("        [[[ top-story  ]]]\n %s\n", content);

		/* To extract date */
		cstr_get_html_attribute(content, "data-date", value);
		printf("Date: %s\n", value);
	}
	else {
		//printf("Top story NOT found!\n");
	}

	/* Free content */
	if(content!=NULL) {
		free(content);
		content=NULL;
	}

	/* Search on .. */
	//printf("search on ....\n");
	pt+=strlen("div");
	if( pt-fmap->fp >= fmap->fsize ) {
		printf("Get end of html, break now...\n");
		break;
	}

	//printf("Parse tag again...\n");
}

	if(content)
		free(content);

	printf(" ----- END -----\n");
	exit(0);
#endif


#if 0 /////////  cstr_get_peword(), cstr_strlen_eword()  //////////
    const char *pstr="人心生一念，天地尽皆知HELLOOO WORLD! this is really very,,,, god 徒们变nice ssooooo yes! --++12283NDD";

/* --- Result -----
len=7 Word: HELLOOO
len=5 Word: WORLD
len=4 Word: this
len=2 Word: is
len=6 Word: really
len=4 Word: very
len=3 Word: god
len=4 Word: nice
len=7 Word: ssooooo
len=3 Word: yes
len=8 Word: 12283NDD

*/
    char *pt;
    int len;

    /* Print out all alphanumeric characters */
    for(i=0; i<128; i++) {
	    if( isalnum(i) )
		printf("%c is an alphanumeric character!\n", i);
    }
    exit(0);


    pt =(char *)pstr;
    while( (pt=cstr_get_peword((UFT8_PCHAR)pt)) ) {
	  len=cstr_strlen_eword((UFT8_PCHAR)pt);
	  printf("Word: ");
	  for(i=0; i<len; i++)
		printf("%c", pt[i]);
	  printf("   (len=%d) \n", len);
	  pt +=len;
    }

    exit(0);
#endif

#if 0 /////////  cstr_hash_string() //////////
char buff[1024];

while( fgets(buff, sizeof(buff)-1, stdin)!=NULL ) {
	printf("String HASH: %lld\n", cstr_hash_string((UFT8_PCHAR)buff,0));
}

exit(0);
#endif


#if 0  //////////  egi_count_file_lines(const char *fpath) ///////////

if(argc<2)
  exit(-1);

printf("Lines of egi.conf: %d\n", egi_count_file_lines(argv[1]) );

exit(0);
#endif


#if 0  //////////  int cstr_copy_line(const char *src, char *dest, size_t size)  ///////////

char src[]="0123456";
src[0]='\0';
char dest[16];
int ret;


 ret=cstr_copy_line( src, dest, 16 );

 printf("ret=%d,  dest[max 16]='%s'  \n",ret, dest);

exit(0);
#endif


#if 0 	//////////    int egi_update_config_value(char *sect, char *key, char* pvalue)   //////////

	egi_update_config_value("TOUCH_PAD", "xpt_facts", "HELLO");
	egi_update_config_value("TOUCH_PAD", "xpt_factX", "12.1");
	egi_update_config_value("TOUCH_PAD", "xpt_factY", "20.1");
	egi_update_config_value("TOUCH_PAD_", "xpt_facts", "HELLO");

exit(0);
#endif


#if 0  ////////// cstr_extract_ChnUft8TimeStr()    cstr_getSecFrom_ChnUft8TimeStr()   ////////////

/*
十五小时一百零七分钟七百零零九秒以后提醒
五分钟后提醒
35分钟后提醒
一小时48分钟后提醒。
半个小时后提醒。
12点58分提醒。
95分钟以后提醒。
12小时107分钟以后提醒。
108小时78分钟以后提醒。
三刻钟以后提醒
11点零六十二秒
862小时58分钟，72秒
862小时58分钟72秒
*/

   char strwords[]="请在上午一千六百十七时六分一千六百十七秒提醒我";
   char strtm[128]={0};
   int	secs;

   if(argc<2)exit(1);

   if( cstr_extract_ChnUft8TimeStr( (const char *)argv[1], strtm, sizeof(strtm))==0 )
	printf("Time str: %s\n",strtm);

   secs=cstr_getSecFrom_ChnUft8TimeStr(strtm,NULL);
   printf("Total %d seconds.\n",secs);

   exit(0);
#endif

  uint32_t a=0x12345678;
  char *pa=(char *)&a;
  printf(" *pa=0x%x \n", *(pa+3));


#if 0	///////////////  cstr/char_unicode_to_uft8( )  /////////////

	char char_uft8[4+1];
	char buff[2048];
	wchar_t *wp=wbook;

	char *pbuf=buff;

	#if  1 /* ------- char_unicode_to_uft8() ------- */
	memset(buff,0,sizeof(buff));
	while(*wp) {
		//memset(char_uft8,0,sizeof(char_uft8));
		k=char_unicode_to_uft8(wp, pbuf);
		if(k<=0)
			break;

		pbuf+=k;
		wp++;
	}
	printf("%s\n",buff);
	#endif

	#if  1 /* ------- cstr_unicode_to_uft8() ------- */
	memset(buff,0,sizeof(buff));
	cstr_unicode_to_uft8(wbook,buff);
	printf("%s\n",buff);
	#endif


	exit(0);

#endif

  return 0;
}

