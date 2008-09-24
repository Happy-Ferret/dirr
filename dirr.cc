/*

	Unix directorylister (replacement for ls command)
	Copyright (C) 1992,2000 Bisqwit (http://iki.fi/bisqwit/)
	
*/

#define _BSD_SOURCE 1

#define VERSIONSTR \
    "DIRR "VERSION" copyright (C) 1992,2000 Bisqwit (http://iki.fi/bisqwit/)\n" \
    "This program is under GPL. dirr-"VERSION".tar.gz\n" \
    "is available at the homepage of the author.\n" \
    "About some ideas about this program, thanks to Warp.\n"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <cerrno>
#include <csignal>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <map>
#include <algorithm>
#include <vector>
#include <string>

#ifndef major
 #define major(dev) ((dev) >> 8)
 #define minor(dev) ((dev) & 255)
#endif

#ifdef HAVE_DIR_H
#include <dir.h>
#endif

#ifndef NAME_MAX
 #define NAME_MAX 255  /* Chars in a file name */
#endif
#ifndef PATH_MAX
 #define PATH_MAX 1023 /* Chars in a path name */
#endif

#define STRANGE 077642 /* May not have & 0111 */

#define DEFAULTATTR 7

static int LongestName;
static int LongestSize;
static int LongestUID;
static int LongestGID;

enum {SumDir=1,SumFifo,SumSock,SumFile,SumLink,SumChrDev,SumBlkDev};

static unsigned long SumCnt[10] = {0};
static unsigned long Summa[10]  = {0};

static bool Contents, Colors, PreScan, Sara, Totals, AnsiOpt;
static bool Pagebreaks;
static int DateTime, Compact;
static int TotalSep;
#ifdef S_ISLNK
static int Links;
#endif
static string BlkStr, ChrStr;

static string Sorting; /* n,d,s,u,g */
static string DateForm;
static string FieldOrder;

static int TextAttr = DEFAULTATTR;
static bool Dumping = false; // If 0, save some time
static int LINES=25, COLS=80;
#define MAX_COLS 512 // Expect no bigger COLS-values

static void SetDefaultOptions()
{
	PreScan = true; // Clear with -e
	Sara    = false;// Set with -C
	Compact = 0;    // Set with -m
	#ifdef S_ISLNK
	Links   = 3;    // Modify with -l#
	#endif
	Colors  = true; // Clear with -c
	Contents= true; // Clear with -D
	DateTime= 2;    // Modify with -d#
	Totals  = true; // Modify with -m
	Pagebreaks = false; // Set with -p
	AnsiOpt = true; // Clear with -P
	
	TotalSep= 0;    // Modify with -Mx
	
	Sorting = "pmgU";
	DateForm = "%d.%m.%y %H:%M";
				    // Modify with -F
				 
				    // Modify with -f
	#ifdef DJGPP
	FieldOrder = ".f.s_.d";
	#else
	FieldOrder = ".f.s_.a4_.d_.o_.g";
	#endif
	
	BlkStr = "<B%u,%u>";	// Modify with -db
	ChrStr = "<C%u,%u>";	// Modify with -dc
}

/***
 *** Settings - This is the default DIRR_COLORS
 ***
 *** The colour codes are one- or two-digit hexadecimal
 *** numbers, which consist of the following values:
 ***
 ***   80 = blink
 ***   40 = background color red
 ***   20 = background color green
 ***   10 = background color blue
 ***   08 = foreground high intensity
 ***   04 = foreground color red
 ***   02 = foreground color green
 ***   01 = foreground color blue
 ***
 *** Add those to make combination colors:
 ***
 ***   09 = 9 = 8+1         = bright blue
 ***   4E = 40+E = 40+8+4+2 = bright yellow (red + green) on red background
 ***    2 = 02              = green.
 ***
 *** I hope you understand the basics of hexadecimal arithmetic.
 *** If you have ever played with textattr() function in
 *** Borland/Turbo C++ or with TextAttr variable in
 *** Borland/Turbo Pascal or assigned colour values directly
 *** into the PC textmode video memory, you should understand
 *** how the color codes work.
 ***/
 
static string Settings =
#include SETTINGSFILE
;

/* Setting separator marks */
#define SetSep(c) ((c)== ')'|| \
                   (c)=='\t'|| \
                   (c)=='\n'|| \
                   (c)=='\r')

#define ALLOCATE(p, type, len) \
	if((p = (type)malloc((size_t)(len))) == NULL) \
    { \
    	SetAttr(7); \
		fprintf(stderr, \
			"\nOut of memory at line %d\n" \
			"\tALLOCATE(%s, %s, %s);\n", __LINE__, #p, #type, #len); \
        exit(EXIT_FAILURE); \
    }

static int RowLen;

#ifndef __GNUC__
 #define __attribute__
#endif

#ifdef DJGPP
#include <crt0.h>
#include <conio.h>
int __opendir_flags =
	__OPENDIR_PRESERVE_CASE
  | __OPENDIR_FIND_HIDDEN;
int _crt0_startup_flags =
	_CRT0_FLAG_PRESERVE_UPPER_CASE
  | _CRT0_FLAG_PRESERVE_FILENAME_CASE;
#define Ggetch getch
#endif

static int WhereX=0;

static int OldAttr=DEFAULTATTR;
static void FlushSetAttr();
static void EstimateFields();
static void SetAttr(int newattr);
static void Summat();
static int GetDescrColor(const string &descr, int index);

static int Gprintf(const char *fmt, ...) __attribute__((format(printf,1,2)));

static int Gputch(int x)
{
	static int Mask[MAX_COLS]={0};
	int TmpAttr = TextAttr;
    if(x=='\n' && (TextAttr&0xF0))GetDescrColor("txt", 1);
    
    if(x!=' ' || ((TextAttr&0xF0) != (OldAttr&0xF0)))
		FlushSetAttr();
		
	#ifdef DJGPP
	(Colors?putch:putchar)(x);
	#else
	if(x=='\a')
	{
		putchar(x);
		return x;
	}
	if(AnsiOpt && Colors)
	{
		static int Spaces=0;
		if(x==' ')
		{
			Spaces++;
			return x;
		}
		while(Spaces)
		{
			int a=WhereX, Now, mask=Mask[a];
			
			for(Now=0; Now<Spaces && Mask[a]==mask; Now++, a++);
			
			Spaces -= Now;
			if(mask)
			{
			Fill:
				while(Now>0){Now--;putchar(' ');}
			}
			else if(Spaces || !(x=='\r' || x=='\n'))
			{
				if(Now<=4)goto Fill;
				printf("\33[%dC", Now);
			}
			WhereX += Now;
		}
	}
	putchar(x);	
	#endif	
	if(x=='\n')SetAttr(TmpAttr);
	if(x=='\r')
		WhereX=0;
	else if(x!='\b')
    {
		if(x >= ' ')Mask[WhereX++] = 1;
		else		memset(Mask, WhereX=0, sizeof Mask);
    }
	else if(WhereX)
		WhereX--;	
		
	return x;
}

#ifndef DJGPP
#ifdef HAVE_TERMIO_H
#include <termio.h>
#endif
static int Ggetch()
{
	struct termio term, back;
	int c;
	ioctl(STDIN_FILENO, TCGETA, &term);
	ioctl(STDIN_FILENO, TCGETA, &back);
	term.c_lflag &= ~(ECHO | ICANON);
	term.c_cc[VMIN] = 1;
	ioctl(STDIN_FILENO, TCSETA, &term);
	c = getchar();
	ioctl(STDIN_FILENO, TCSETA, &back);
	return c;
}
#endif

static string GetError(int e)
{
    return strerror(e); /*
	int a;
	static char Buffer[64];
	strncpy(Buffer, strerror(e), 63);
	Buffer[63] = 0;
	a=strlen(Buffer);
	while(a && Buffer[a-1]=='\n')Buffer[--a]=0;
	return Buffer; */
}

static void SetAttr(int newattr)
{
	TextAttr = newattr;
}
static void FlushSetAttr()
{
    if(TextAttr == OldAttr)return;
    if(!Colors)goto Ret;

    #ifdef DJGPP

    textattr(TextAttr);

    #else

    printf("\33[");

    if(TextAttr != 7)
    {
    	static const char Swap[] = "04261537";
    	
    	if(AnsiOpt)
    	{
	    	int pp=0;
	    	
	    	if((OldAttr&0x80) > (TextAttr&0x80)
	    	|| (OldAttr&0x08) > (TextAttr&0x08))
	    	{
	    		putchar('0'); pp=1;
	    		OldAttr = 7;
	    	}    	
	    	
	    	if((TextAttr&0x08) && !(OldAttr&0x08)){if(pp)putchar(';');putchar('1');pp=1;}
	    	if((TextAttr&0x80) && !(OldAttr&0x80)){if(pp)putchar(';');putchar('5');pp=1;}
	       	
	       	if((TextAttr&0x70) != (OldAttr&0x70))
	       	{
	   	   		if(pp)putchar(';');
	       		putchar('4');
	       		putchar(Swap[(TextAttr>>4)&7]);
	       		pp=1;
	       	}
	       	
	      	if((TextAttr&7) != (OldAttr&7))
	      	{
	       		if(pp)putchar(';');
	       		putchar('3');
	       		putchar(Swap[TextAttr&7]);
	       	}
       	}
       	else
	    	printf("0%s%s;4%c;3%c",
    			(TextAttr&8)?";1":"",
    			(TextAttr&0x80)?";5":"",
    			Swap[(TextAttr>>4)&7],
	    		Swap[TextAttr&7]);
    }

    putchar('m');
    #endif
Ret:OldAttr = TextAttr;
}

static int ColorNums = -1;
static int ColorNumToggle = 0;
static int Gprintf(const char *fmt, ...)
{
    static int Line=2;
    char Buf[2048];

    va_list ap;
    va_start(ap, fmt);
    int a = vsprintf(Buf, fmt, ap);
    va_end(ap);
    
    for(char *s=Buf; *s; s++)
    {
    	if(*s=='\1')ColorNumToggle ^= 1;
    	else if(*s=='\t')Gprintf("   ");
        else
        {
#ifdef DJGPP
            if(*s=='\n')Gputch('\r');
#endif
            if(ColorNumToggle)
            {
            	int ta = TextAttr;
            	SetAttr(ColorNums);
            	Gputch(*s);
            	SetAttr(ta);
            }
            else
            	Gputch(*s);
            if(*s=='\n')
            {
                if(++Line >= LINES)
                {
                    if(Pagebreaks)
                    {
                    	int More=LINES-2;
                        int ta = TextAttr;
                        SetAttr(0x70);
                        Gprintf("\r--More--");
					    GetDescrColor("txt", 1);
                        Gprintf(" \b");
                        fflush(stdout);
                        for(;;)
                        {
                        	int Key = Ggetch();
                        	if(Key=='q' 
                        	|| Key=='Q'
                        	|| Key==3){More=-1;break;}
                        	if(Key=='\r'|| Key=='\n'){More=1;break;}
                        	if(Key==' ')break;
                        	Gputch('\a');
                       	}
                        Gprintf("\r        \r");
                        if(More<0)exit(0);
                        SetAttr(ta);
                        Line -= More;
    }	}	}	}	}
    return a;
}

#if CACHE_GETSET
static map <int, map<string, string> > gscache;
static const string sNULL = "\1";
#endif

static string GetSet(const char **S, const string &name, int index)
{
#if CACHE_GETSET
    map<int, map<string, string> >::const_iterator i = gscache.find(index);
    if(i != gscache.end())
    {
    	map<string, string>::const_iterator j = i->second.find(name);
    	if(j != i->second.end())return j->second;
    }
#else
	index=index;
#endif
    const char *s = *S;
    string t;
    
    if(s)
    {
        /* Setting string separators */
        unsigned namelen = name.size();
        
        for(;;)
        {
        	while(SetSep(*s))s++;

            if(!*s)break;

            if(!memcmp(s, name.c_str(), namelen) && s[namelen] == '(')
            {
            	const char *p;
                int Len;
                for(Len=0, p=s; *p && !SetSep(*p); ++Len)++p;
        
                *S = *p?p+1:NULL;
                if(!**S)*S=NULL;
                
                t.assign(s, 0, Len);
                goto Retu;
            }
        
            while(*s && !SetSep(*s))s++;
        }
    }
    t = sNULL;
    *S = NULL;
Retu:
#if CACHE_GETSET
	gscache[index][name] = t;
#endif
    return t;
}

static int GetHex(int Default, const char **S)
{
    int eka, Color = Default;
    const char *s = *S;

    for(eka=1; *s && isxdigit((int)*s); eka=0, s++)
    {
        if(eka)Color=0;else Color<<=4;

        if(*s > '9')
            Color += 10 + toupper(*s) - 'A';
        else
            Color += *s - '0';
    }

    *S = s;
    return Color;
}

static int GetModeColor(const string &text, int Chr)
{
    int Dfl = 7;

    const char *s = Settings.c_str();

    int Char = Chr<0?-Chr:Chr;

    for(;;)
    {
    	string T = GetSet(&s, text, 0);
        if(T == sNULL)break;

        const char *t = T.c_str() + text.size() + 1; /* skip 'text=' */

        while(*t)
        {
            int C, c=*t++;

            C = GetHex(Dfl, &t);

            if(c == Char)
            {
                if(Chr > 0)SetAttr(C);
                return C;
            }
            if(*t == ',')t++;
        }
    }

    Gprintf("DIRR_COLORS error: No color for '%c' found in '%s'\n", Char, text.c_str());

    return Dfl;
}

static int GetDescrColor(const string &descr, int index)
{
    int Dfl = 7;
    if(!Colors || !Dumping)return Dfl;
    
    int ind = index<0 ? -index : index;
    
    const char *s = Settings.c_str();    
    string t = GetSet(&s, descr, 0);
    
    if(t == sNULL)
        Gprintf("DIRR_COLORS error: No '%s'\n", descr.c_str());
    else
    {
        const char *S = t.c_str() + descr.size();
        for(; ind; ind--)
        {	
        	++S;
        	Dfl = GetHex(Dfl, &S);
        }
        if(index>0)SetAttr(Dfl);
    }
    return Dfl;
}

static void PrintSettings()
{
    const char *s = Settings.c_str();
    int LineLen, Dfl;
    
    Dfl = GetDescrColor("txt", -1);
    
    for(LineLen=0;;)
    {
        while(SetSep(*s))s++;
        if(!*s)break;
        
        int Len;
        const char *t = s;
        for(Len=0; *t && !SetSep(*t); Len++)t++;

        string T(s, 0, Len);
        T += ')';

        if(LineLen && LineLen+strlen(t) > 75)
        {
            Gprintf("\n");
            LineLen=0;
        }
        if(!LineLen)Gprintf("\t");

        LineLen += T.size();
        const char *n = t = T.c_str();

        while(*t!='(')Gputch(*t++);
        Gputch(*t++);

        if(n[4]=='('
        && (!strncmp(n, "mode", 4)
         || !strncmp(n, "type", 4)
         || !strncmp(n, "info", 4)))
        {
            while(*t)
            {
                int c;
                const char *k;
                int len;

                c = *t++;

                k=t;    
                SetAttr(GetHex(Dfl, &k));

                Gputch(c);

                SetAttr(Dfl);

                for(len=k-t; len; len--)Gputch(*t++);

                if(*t != ',')break;
                Gputch(*t++);
            }
            Gprintf("%s", t);
        }
        else
        {
            int C=Dfl, len;
            const char *k;
            for(;;)
            {
                k = t;
                SetAttr(C=GetHex(C, &k));
                for(len=k-t; len; len--)Gputch(*t++);
                SetAttr(Dfl);
                if(*t != ',')break;
                Gputch(*t++);
            }
            if(*t!=')')Gputch(*t++);
            SetAttr(C);
            while(*t!=')')
            {
            	if(!*t)
            	{
            		SetAttr(0x0C);
            		Gprintf("(Error:unterminated)");
            		SetAttr(Dfl);
            		break;
            	}
            	Gputch(*t++);
          	}
            SetAttr(Dfl);
            Gputch(')');
        }
        
        while(*s && !SetSep(*s))s++;
    }
    if(LineLen)
        Gprintf("\n");
}

static string NameOnly(const string &Name)
{
    const char *q, *s = Name.c_str();
    
    while((q = strchr(s, '/')) && q[1])s = q+1;
    
    q = strchr(s, '/');
    if(!q)q = strchr(s, 0);
    
    return string(s, 0, q-s);
}

// Ends with '/'.
// If no directory, returns empty string.
static string DirOnly(const string &Name)
{
	const char *q, *s = Name.c_str();
	q = strchr(s, '/');
	if(!q)return "";
	
	return string(s, 0, q-s+1);
}

/***********************************************
 *
 * Getpwuid(uid)
 * Getgrgid(gid)
 *
 *   Get user and group name, quickly using binary search
 *
 * ReadGidUid()
 *
 *   Builds the data structures for Getpwuid() and Getgrgid()
 *
 *************************************************************/

#if PRELOAD_UIDGID||CACHE_UIDGID

typedef map<int, string> idCache;
static idCache GidItems, UidItems;

#endif

#if PRELOAD_UIDGID

static char *Getpwuid(int uid)
{
	return UidItems[uid].c_str();
}
static char *Getgrgid(int gid)
{
	return GidItems[gid].c_str();
}
static void ReadGidUid()
{
	int a;
	
	for(setpwent(); ;)
	{
		struct passwd *p = getpwent();
		UidItems[p->pw_uid] = p->pw_name;
	}
	endpwent();
	
	for(setgrent(); ;)
	{
		struct group *p = getgrent();
		UidItems[p->gr_gid] = p->gr_name;
	}
	endgrent();
}
#else /* no PRELOAD_UIDGID */
#define ReadGidUid()
#if CACHE_UIDGID
static const char *Getpwuid(int uid)
{
	idCache::iterator i;
	i = UidItems.find(uid);
	if(i==UidItems.end())
	{
		const char *s;
		struct passwd *tmp = getpwuid(uid);
		s = tmp ? tmp->pw_name : "";
		UidItems[uid] = s;
		return s;
	}
	return i->second.c_str();
}
static const char *Getgrgid(int gid)
{
	idCache::iterator i;
	i = GidItems.find(gid);
	if(i==GidItems.end())
	{
		const char *s;
		struct group *tmp = getgrgid(gid);
		s = tmp ? tmp->gr_name : "";
		GidItems[gid] = s;
		return s;
	}
	return i->second.c_str();
}
#else
static char *Getpwuid(int uid)
{
	struct passwd *tmp = getpwuid(uid);
	if(!tmp)return NULL;
	return tmp->pw_name;
}
static char *Getgrgid(int gid)
{
	struct group *tmp = getgrgid(gid);
	if(!tmp)return NULL;
	return tmp->gr_name;
}
#endif
#endif

/***********************************************
 *
 * WildMatch(Pattern, What)
 *
 *   Compares with wildcards.
 *
 *   This routine is a descendant of the fnmatch()
 *   function in the GNU fileutils-3.12 package.
 *
 *   Supported wildcards:
 *       *
 *           matches multiple characters
 *       ?
 *           matches one character
 *       [much]
 *           "much" may have - (range) and ^ or ! (negate)
 *       \d
 *           matches a digit (same as [0-9])
 *       \w
 *           matches alpha (same as [a-zA-Z])
 *       \
 *           quote next wildcard
 *
 *   Global variable IgnoreCase controls the case
 *   sensitivity of the operation as 0=case sensitive, 1=not.
 *
 *   Return value: 0=no match, 1=match
 *
 **********************************************************/
 
static int IgnoreCase;

#if NEW_WILDMATCH
static int WildMatch(const char *pattern, const char *what)
{
	for(;;)
	{
		#define cc(x) (IgnoreCase ? tolower((int)(x)) : (x))
		if(*pattern == '*')
		{
			while(*++pattern == '*');
			for(;;)
			{
				register int a = WildMatch(pattern, what);
				if(a != 0)return a; /* T�yst�sm�(1) tai meni ohi(-1) */
				what++;
			}
			return 0;
		}
		if(*pattern == '?' && *what)goto AnyMerk;
		#if SUPPORT_BRACKETS
		if(*pattern == '[')
		{
			int Not=0;
			register int mrk=*what;
			if(*++pattern=='^')++pattern, Not=1;
			while(*pattern != ']')
			{
				register int a, left = cc(*pattern++), m = cc(mrk);
				if(*pattern=='-')
					a = m >= left && m <= cc(pattern[1]), pattern += 2;
				else
					a = m == left;
				if(Not ^ !a)return 0;
			}
		}
		else
		#endif
		{
			if(*pattern == '\\')
			{
				++pattern;
				if(*pattern=='d') { if(!isdigit((int)*what))return 0; goto AnyMerk; }
				if(*pattern=='w') { if(!isalpha((int)*what))return 0; goto AnyMerk; }
			}
			if(!*what)return *pattern
				? -1		/* nimi loppui, patterni ei (meni ohi) */
				: 1;		/* molemmat loppui, hieno juttu        */
			if(cc(*pattern) != cc(*what))return 0; /* Ep�t�sm�.    */
		}
	AnyMerk:
		what++;
		pattern++;
		#undef cc
	}
}
#else
static int WildMatch(const char *Pattern, const char *What)
{
	register const char *p=Pattern, *n = What;
	register char c;
	
	while((c = *p++) != 0)
    {
    	#define FOLD(c) (IgnoreCase ? toupper(c) : (c))
    	c = FOLD(c);
		switch(c)
		{
			case '?':
				if(!*n || *n=='/')return 0;
				break;
			case '\\':
				if(FOLD(*n) != c)return 0;
				break;
			case '*':
				for(c=*p++; c=='?' || c=='*'; c=*p++, ++n)
					if(*n == '/' || (c == '?' && *n == 0))return 0;
				if(!c)return 1;
				{
					char c1 = FOLD(c);
					for(--p; *n; n++)
						if((c == '[' || FOLD(*n) == c1) && WildMatch(p, n))
							return 1;
					return 0;
				}
			case '[':
			{
				/* Nonzero if the sense of the character class is inverted.  */
				register int Not;
				if(!*n)return 0;
				Not = (*p == '!' || *p == '^');
				if(Not)p++;
				c = *p++;
				for(;;)
				{
					register char cstart, cend;
					cstart = cend = FOLD(c);
					if(!c)return 0;
					c = FOLD(*p);
					p++;
					if(c=='/')return 0;
					if(c=='-' && *p!=']')
					{
						if(!(cend = *p++))return 0;
						cend = FOLD(cend);
						c = *p++;
					}
					if(FOLD(*n) >= cstart && FOLD(*n) <= cend)
					{
						while(c != ']')
						{
							if(!c)return 0;
							c = *p++;
						}
						if(Not)return 0;
						break;
					}
					if(c == ']')break;
				}
				if(!Not)return 0;
				break;
			}
			default:
				if(c != FOLD(*n))return 0;
		}
		n++;
	}
	return !*n;
}
#endif

#if !CACHE_NAMECOLOR
/*
 * rmatch(Name, Items)
 *
 *   Finds a name from the space-separated wildcard list
 *   Uses WildMatch() to compare.
 *
 *   Return value: 0=no match, >0=index of matched string
 */
static int rmatch(const char *Name, const char *Items)
{
    char Buffer[PATH_MAX+1];

	int Index;
    char *s, *n;
    if(!Name || !Items)return 0;

    strcpy(Buffer, Items);

    for(n=Buffer, Index=1; (s=strtok(n, " ")) != NULL; Index++)
    {
        if(WildMatch(s, Name) > 0)goto Ret;
        n=NULL;
    }

    Index=0;

Ret:return Index;
}
#endif

/*
18:35|>Warp= 1) int NameColor(const char *s) - palauttaa
                nime� vastaavan v�rin.
18:36|>Warp= 2) NameColor() k�y kaikki byext():t l�pi, tai siihen asti
                kunnes sielt� l�ytyy joku patterni johon annettu nimi menee.
18:36|>Warp= 3) Jokaisen byext():n sis�ll� on l�j� patterneja.
                Stringi annetaan rmatch() -funktiolle.
18:36|>Warp= 4) rmatch() pilkkoo stringin ja testaa jokaista patternia
                erikseen wildmatch:lla.
18:37|>Warp= 5) wildmatch() palauttaa >0 jos t�sm�si.
18:37|>Warp= 6) rmatch() lopettaa testauksen jos wildmatch t�sm�si.
18:37|>Warp= 7) Jos rmatch() palautti 0 (ei t�sm�nnyt) ja viimeinen
                byext() on k�yty l�pi, palautetaan default-v�ri.
18:38|>Warp= Ja byext():t t�ss� ovat niit� asetusstringej�, ei funktioita

*/

static int WasNormalColor;

#if CACHE_NAMECOLOR
class NameColorItem
{
public:
	string pat;
	int ignorecase;
	int colour;
	NameColorItem(const string &p, int i, int c) : pat(p), ignorecase(i), colour(c) { }
};
static vector<NameColorItem> nccache;
static map<string, int> NameColorCache2;

static void BuildNameColorCache()
{
    const char *DirrVar = getenv("DIRR_COLORS");
    if(DirrVar)Settings = DirrVar;

	const char *s = Settings.c_str();
	int Normal = GetDescrColor("txt", -1);
	int index=0;
	
	for(;;)
	{
		int c;
		string T = GetSet(&s, "byext", index++);
		if(T == sNULL)break;
		const char *t = T.c_str() + 6;
		c = GetHex(Normal, &t);
		IgnoreCase = *t++ == 'i';
		string Buffer = t;
		char *Q, *q = (char *)Buffer.c_str();
		for(; (Q=strtok(q, " ")) != NULL; q=NULL)
		{
			NameColorItem tmp(Q, IgnoreCase, c);
			nccache.push_back(tmp);
		}
	}
}
static int NameColor(const string &Name)
{
	map<string, int>::const_iterator i = NameColorCache2.find(Name);
	if(i != NameColorCache2.end())return i->second;
	int colo;
	WasNormalColor=0;
	unsigned a, b=nccache.size();
	for(a=0; a<b; ++a)
	{
		IgnoreCase = nccache[a].ignorecase;
		if(WildMatch(nccache[a].pat.c_str(), Name.c_str()) > 0)
		{
			colo = nccache[a].colour;
			goto Done;
		}
	}
	WasNormalColor=1;
	colo = GetDescrColor("txt", -1);
Done:
	return NameColorCache2[Name] = colo;
}
#else
static int NameColor(const string &Name)
{
    const char *s = Settings.c_str();
    int Normal = GetDescrColor("txt", -1);
    int index = 0;

    for(WasNormalColor=0;;)
    {
        int c, result;
        const char *t, *T = GetSet(&s, "byext", index++);

        if(!T)break;

        t = T+6; /* skip "byext" */

        c = GetHex(Normal, &t);

        IgnoreCase = *t++ == 'i';

        result = rmatch(Name, t);

        /* free(T); */

        if(result)return c;
    }

    WasNormalColor=1;

    return Normal;
}
#endif

static int GetNameAttr(const struct stat &Stat, const string &fn)
{
    int NameAttr = NameColor(fn);

    if(!WasNormalColor)return NameAttr;

    #ifdef S_ISLNK
    if(S_ISLNK(Stat.st_mode))       NameAttr=GetModeColor("type", -'l');
    else
    #endif
         if(S_ISDIR(Stat.st_mode))  NameAttr=GetModeColor("type", -'d');
    else if(S_ISCHR(Stat.st_mode))  NameAttr=GetModeColor("type", -'c');
    else if(S_ISBLK(Stat.st_mode))  NameAttr=GetModeColor("type", -'b');
    #ifdef S_ISFIFO
    else if(S_ISFIFO(Stat.st_mode)) NameAttr=GetModeColor("type", -'p');
    #endif
    #ifdef S_ISSOCK
    else if(S_ISSOCK(Stat.st_mode)) NameAttr=GetModeColor("type", -'s');
    #endif
    else if(Stat.st_mode&00111)     NameAttr=GetModeColor("type", -'x');

    return NameAttr;
}

static void PrintAttr(const struct stat &Stat, char Attrs, int *Len, unsigned int dosattr)
{
    switch(Attrs)
    {
        case '0':
            #define PutSet(c) {GetModeColor("mode", c);Gputch(c);(*Len)++;}
        	if(dosattr&0x20)PutSet('A')else PutSet('-')
        	if(dosattr&0x02)PutSet('H')else PutSet('-')
        	if(dosattr&0x04)PutSet('S')else PutSet('-')
        	if(dosattr&0x01)PutSet('R')else PutSet('-')
            break;
        case '1':
        {
        #ifndef DJGPP
        	int Viva = GetModeColor("mode", '-');
            #ifdef S_ISLNK
                 if(S_ISLNK(Stat.st_mode))  PutSet('l')
            else
            #endif
                 if(S_ISDIR(Stat.st_mode))  PutSet('d')
            else if(S_ISCHR(Stat.st_mode))  PutSet('c')
            else if(S_ISBLK(Stat.st_mode))  PutSet('b')
            #ifdef S_ISFIFO
            else if(S_ISFIFO(Stat.st_mode)) PutSet('p')
            #endif
            #ifdef S_ISSOCK
            else if(S_ISSOCK(Stat.st_mode)) PutSet('s')
            #endif
            else if(S_ISREG(Stat.st_mode))  PutSet('-')
            else                             PutSet('?')

            SetAttr(Viva);
            Gputch((Stat.st_mode&00400)?'r':'-');
            Gputch((Stat.st_mode&00200)?'w':'-');

            GetModeColor("mode", (Stat.st_mode&00100)?'x':'-');
            
            Gputch("-xSs"[((Stat.st_mode&04000)>>10)|((Stat.st_mode&00100)>>6)]);

            SetAttr(Viva);
            Gputch((Stat.st_mode&00040)?'r':'-');
            Gputch((Stat.st_mode&00020)?'w':'-');

            GetModeColor("mode", (Stat.st_mode&00010)?'x':'-');
        #if defined(SUNOS)||defined(__sun)||defined(SOLARIS)
            Gputch("-xls"[((Stat.st_mode&02000)>>9)|((Stat.st_mode&00010)>>3)]);
        #else
            Gputch("-xSs"[((Stat.st_mode&02000)>>9)|((Stat.st_mode&00010)>>3)]);
        #endif

            SetAttr(Viva);
            Gputch((Stat.st_mode&00004)?'r':'-');
            Gputch((Stat.st_mode&00002)?'w':'-');

            GetModeColor("mode", (Stat.st_mode&00001)?'x':'-');
            Gputch("-xTt"[((Stat.st_mode&01000)>>8)|((Stat.st_mode&00001))]);

            (*Len) += 9;

        #endif /* not djgpp */
            break;

           	#undef PutSet
        }
        case '2':
            Attrs = '3';
        default:
        {
    		char Buf[104]; /* 4 is max extra */
            Attrs -= '0';
            sprintf(Buf, "%0100o", (int)Stat.st_mode);
            GetModeColor("mode", '#');
            (*Len) += Gprintf("%s", Buf+100-Attrs);
        }
}   }

static string LinkTarget(const string &link, bool fixit=false)
{
	char Target[PATH_MAX+1];

    int a = readlink(link.c_str(), Target, sizeof Target);
	if(a < 0)a = 0;
	Target[a] = 0;
	
	if(!fixit)return Target;
 
	if(Target[0] == '/')
	{
		// Absolute link
		return Target;
	}

	// Relative link
	return DirOnly(link) + Target;
}

/***********************************************
 *
 * GetName(fn, Stat, Space, Fill)
 *
 *   Prints the filename
 *
 *     fn:     Filename
 *     Stat:   stat-bufferi, jonka modesta riippuen
 *             saatetaan tulostaa per��n merkkej�
 *             =,|,?,/,*,@. My�s ->:t katsotaan.
 *     Space:  Maximum usable printing space
 *     Fill:   Jos muu kuin nolla, ylim��r�inen tila
 *             t�ytet��n spaceilla oikealta puolelta
 * 
 *   Return value: True (not necessarily printed) length
 *
 **********************************************************/
 
static int GetName(const string &fn, const struct stat &sta, int Space, int Fill, bool nameonly)
{
    string Puuh, Buf, s=fn;
    const struct stat *Stat = &sta;

    unsigned Len = 0;
    int i;

    Puuh = nameonly ? NameOnly(s) : s;
#ifdef S_ISLNK
Redo:
#endif
    Buf = Puuh;
    Len += (i = Buf.size());
    
    if(i > Space && nameonly)i = Space;
    Buf.erase(i);
    Gprintf("%*s", -i, Buf.c_str());
    Space -= i;

    #define PutSet(c) do if((++Len,Space)&&GetModeColor("info", c))Gputch(c),--Space;while(0)

    #ifdef S_ISSOCK
    if(S_ISSOCK(Stat->st_mode)) PutSet('=');
    #endif
    #ifdef S_ISFIFO
    if(S_ISFIFO(Stat->st_mode)) PutSet('|');
    #endif
    if(Stat->st_mode==STRANGE)  PutSet('?');

    #ifdef S_ISLNK
    if(S_ISLNK(Stat->st_mode))
    {
        if(Links >= 2)
        {
        	int a;
            
            Buf = " -> ";

            Len += (a = Buf.size());
            
            if(a > Space)a = Space;
            if(a)
            {
            	Buf.erase(a);
                GetModeColor("info", '@');
                Gprintf("%*s", -a, Buf.c_str());
            }
            Space -= a;
            
            struct stat Stat1;
            Buf = LinkTarget(s, true);

            /* Target status */
	        if(stat(Buf.c_str(), &Stat1) < 0)
    	    {
        	    Stat1.st_mode = STRANGE;
            	if(Space)GetModeColor("type", '?');
			}
	        else
    	        if(Space)SetAttr(GetNameAttr(Stat1, Buf));
            
            Puuh = s = LinkTarget(s, false); // Unfixed link.
            Stat = &Stat1;
            goto Redo;
        }
        PutSet('@');
    }
    else
    #endif
    
    if(S_ISDIR(Stat->st_mode))  PutSet('/');
    else if(Stat->st_mode & 00111)PutSet('*');

    #undef PutSet

    if(Fill)
        while(Space)
        {
            Gputch(' ');
            Space--;
        }

    return Len;
}

static void PrintNum(string &Dest, int Seps, const char *fmt, ...)
	__attribute__((format(printf,3,4)));
	
static void PrintNum(string &Dest, int Seps, const char *fmt, ...)
{
	int Len;
	
	Dest.resize(2048);
	
	va_list ap;
	va_start(ap, fmt);
	Dest.erase(vsprintf((char *)Dest.c_str(), fmt, ap));
	va_end(ap);
	
	if(Seps)
    {
        int SepCount;
        /* 7:2, 6:1, 5:1, 4:1, 3:0, 2:0, 1:0, 0:0 */

		const char *End = strchr(Dest.c_str(), '.');
		if(!End)End = Dest.c_str() + Dest.size();
		
		Len = (int)(End-Dest.c_str());
		
		SepCount = (Len - 1) / 3;
	
        for(; SepCount>0; SepCount--)
        {
        	Len -= 3;
        	Dest.insert(Len, " ");
        }
    }
}
 
static char *GetSize(const string &s, const struct stat &Sta, int Space, int Seps)
{
    static char Buf[128];
    char *descr = "descr";

    if(Space >= (int)(sizeof Buf))
    {
    	fprintf(stderr, "\ndirr: FATAL internal error - line %d\n", __LINE__);
        exit(EXIT_FAILURE);
    }
    
    const struct stat *Stat = &Sta;

#ifdef S_ISLNK
GotSize:
#endif
		 if(S_ISDIR (Stat->st_mode))sprintf(Buf, "%*s", -Space, "<DIR>");
    #ifdef S_ISFIFO
    else if(S_ISFIFO(Stat->st_mode))sprintf(Buf, "%*s", -Space, "<PIPE>");
    #endif
    #ifdef S_ISSOCK
    else if(S_ISSOCK(Stat->st_mode))sprintf(Buf, "%*s", -Space, "<SOCKET>");
    #endif
    else if(S_ISCHR (Stat->st_mode))
    {
    	char *e;
    	sprintf(Buf, ChrStr.c_str(),
    		(unsigned)major(Stat->st_rdev),
    		(unsigned)minor(Stat->st_rdev));
    	if(Space)
    	{
		    e = strchr(Buf, 0);
    		memset(e, ' ', (Buf+(sizeof(Buf)))-e);
    		Buf[Space] = 0;
    	}
    }
    else if(S_ISBLK (Stat->st_mode))
    {
    	char *e;
    	sprintf(Buf, BlkStr.c_str(),
    		(unsigned)major(Stat->st_rdev),
    		(unsigned)minor(Stat->st_rdev));
    	if(Space)
    	{
	    	e = strchr(Buf, 0);
	    	memset(e, ' ', (Buf+(sizeof(Buf)))-e);
    		Buf[Space] = 0;
    	}
    }
    #ifdef S_ISLNK
    else if(S_ISLNK (Stat->st_mode))
    {
        if((Links!=1)&&(Links!=4))goto P1;
        sprintf(Buf, "%*s", -Space, "<LINK>");
    }
    #endif
    else
    {
        long l;

#ifdef S_ISLNK
P1:
		if(S_ISLNK(Stat->st_mode))
		{
			if(Links==2)descr=NULL;
			if(Links==3)
			{
				static struct stat Stat1;
				string Buf = LinkTarget(s);
       	     	
    	        // Target status
	    	    if(stat(Buf.c_str(), &Stat1) >= 0)
	    	    {
    		    	Stat = &Stat1;
    	    		goto GotSize;
		}	}	}
#endif
        l = Stat->st_size;
        
        string TmpBuf;
        PrintNum(TmpBuf, Seps, "%lu", l);
        sprintf(Buf, "%*s", Space, TmpBuf.c_str());
        
        if(descr)descr = "size";
    }
    
    if(descr)
	    GetDescrColor(descr, 1);
	else
    	GetModeColor("info", '@');

    return Buf;
}

static void TellMe(const struct stat &Stat, const string &Name
#ifdef DJGPP
	, unsigned int dosattr
#endif
	)
{
    int Len;
    char OwNam[16];
    char GrNam[16];
    const char *Passwd, *Group;
    int ItemLen, NameAttr;
    int NeedSpace=0;
    const char *s;

    if(S_ISDIR(Stat.st_mode))
    {
        SumCnt[SumDir]++;
        Summa[SumDir] += Stat.st_size;
    }
    #ifdef S_ISFIFO
    else if(S_ISFIFO(Stat.st_mode))
    {
        SumCnt[SumFifo]++;
        Summa[SumFifo] += Stat.st_size;
    }
    #endif
    #ifdef S_ISSOCK
    else if(S_ISSOCK(Stat.st_mode))
    {
        SumCnt[SumSock]++;
        Summa[SumSock] += Stat.st_size;
    }
    #endif
    else if(S_ISCHR(Stat.st_mode))
    {
        SumCnt[SumChrDev]++;
        Summa[SumChrDev] += Stat.st_size;
    }
    else if(S_ISBLK(Stat.st_mode))
    {
        SumCnt[SumBlkDev]++;
        Summa[SumBlkDev] += Stat.st_size;
    }
    #ifdef S_ISLNK
    else if(S_ISLNK(Stat.st_mode))
    {
        SumCnt[SumLink]++;
        Summa[SumLink] += Stat.st_size;
    }
    #endif
    else
    {
        SumCnt[SumFile]++;
        Summa[SumFile] += Stat.st_size;
    }

    NameAttr = GetNameAttr(Stat, NameOnly(Name));

    Passwd = Getpwuid((int)Stat.st_uid);
    Group  = Getgrgid((int)Stat.st_gid);
    if(Passwd && *Passwd)strcpy(OwNam, Passwd);else sprintf(OwNam,"%d",(int)Stat.st_uid);
    if( Group &&  *Group)strcpy(GrNam,  Group);else sprintf(GrNam,"%d",(int)Stat.st_gid);

   	Len = strlen(OwNam); if(Len > LongestUID)LongestUID = Len;
   	Len = strlen(GrNam); if(Len > LongestGID)LongestGID = Len;

    for(ItemLen=0, s=FieldOrder.c_str(); *s; )
    {
    	NeedSpace=0;
        switch(*s)
        {
            case '.':
            	NeedSpace=1;
                switch(*++s)
                {
                    case 'a':
                    {
                        int i = 0;
                        s++;
                        Len = '1';
                        if((*s>='0')&&(*s<='9'))Len = *s;
                        PrintAttr(Stat, Len, &i,
						#ifdef DJGPP
                        	dosattr
                        #else
                        	0
						#endif
                        	);
                        ItemLen += i;
                        RowLen += i;
                        break;
                    }
                    case 'x':
                        s++;
                        SetAttr(GetHex(7, (const char **)&s));
                        s--;
                        NeedSpace=0;
                        break;
                    case 'o':
                    case 'O':
                    {
                        int i;
                        GetDescrColor("owner", (Stat.st_uid==getuid())?1:2);
                        if(isupper((int)*s) || !s[1])
                            i = Gprintf("%s", OwNam);
                        else
                            i = Gprintf("%*s", -LongestUID, OwNam);
                        ItemLen += i;
                        RowLen += i;
                        break;
                    }
#ifdef DJGPP
					case 'G':
                    case 'g':
                    case 'h':
                        break;
#else
					case 'G':
                    case 'g':
                    {
                        int i;
                        GetDescrColor("group", (Stat.st_gid==getgid())?1:2);
                        if(isupper((int)*s) || !s[1])
                            i = Gprintf("%s", GrNam);
                        else
                            i = Gprintf("%*s", -LongestGID, GrNam);
                        ItemLen += i;
                        RowLen += i;
                        break;
                    }
                    case 'h':
                        GetDescrColor("nrlink", 1);
                        Gprintf("%4d", (int)Stat.st_nlink);
                        ItemLen += 4;
                        RowLen += 4;
                        break;
#endif
                    case 'F':
                    case 'f':
                        SetAttr(NameAttr);
                        GetName(Name, Stat, LongestName, (Sara||s[1]) && (*s=='f'), *s=='f');
                        ItemLen += LongestName;
                        RowLen += LongestName;
                        break;
                    case 's':
                        Gprintf("%s", GetSize(Name, Stat, LongestSize, 0));
                        ItemLen += LongestSize;
                        RowLen += LongestSize;
                        break;
                    case 'S':
                    {
                        int i;
                        s++; /* 's' on sepitt�m�lle versiolle */
                        i = Gprintf("%s", GetSize(Name, Stat, LongestSize+3, *s?*s:' '));
                        ItemLen += i;
                        RowLen += i;
                        break;
                    }
                    case 'd':
                    {
                        char Buf[64]; /* For the date */
                    	char *s;
                        int i;

                        time_t t;

                        switch(DateTime)
                        {
                            case 1: t = Stat.st_atime; break;
                            case 2: t = Stat.st_mtime; break;
                            case 3: t = Stat.st_ctime;
                        }

                        if(DateForm == "%u")
                        {
                            time_t now = time(NULL);
                            strcpy(Buf, ctime(&t));
                            if(now > t + 6L * 30L * 24L * 3600L /* Old. */
                            || now < t - 3600L)       /* In the future. */
                            {
                                /* 6 months in past, one hour in future */
                                strcpy(Buf+11, Buf+19);
                            }
                            Buf[16] = 0;
                            strcpy(Buf, Buf+4);
                        }
                        else if(DateForm == "%z")
                        {
                        	struct tm *TM = localtime(&t);
                            time_t now = time(NULL);
                            int m = TM->tm_mon, d=TM->tm_mday, y=TM->tm_year;
                            struct tm *NOW = localtime(&now);
                        	if(NOW->tm_year == y || (y==NOW->tm_year-1 && m>NOW->tm_mon))
                        	{
                        		sprintf(Buf, "%3d.%d", d,m+1);
                        		if(Buf[5])strcpy(Buf, Buf+1);
                        	}
                        	else
                        		sprintf(Buf, "%5d", y+1900);
                        }
                        else
                            strftime(Buf, sizeof Buf, DateForm.c_str(), localtime(&t));

                        while((s=strchr(Buf,'_'))!=NULL)*s=' ';
                        GetDescrColor("date", 1);
                        i = Gprintf("%s", Buf);
                        ItemLen += i;
                        RowLen += i;
                        break;
                    }
                    default:
                        Gputch(*s);
                        ItemLen++;
                        RowLen++;
                }
                break;
            case '_':
                Gputch(' ');
                ItemLen++;
                RowLen++;
                break;
            default:
                Gputch(*s);
                ItemLen++;
                RowLen++;
        }
        if(*s)s++;
    }
    if(FieldOrder.size())
    {
        if(!Sara)goto P1;
        if(RowLen + ItemLen >= COLS)
        {
P1:         Gprintf("\n");
            RowLen = 0;
        }
        else
        {
			if(NeedSpace)Gputch(' ');
            RowLen++;
}   }   }

#ifndef DJGPP
static int stricmp(const char *eka, const char *toka)
{
	while(toupper(*eka) == toupper(*toka))
	{
		if(!*eka)return 0;
		eka++;
		toka++;
	}
	return toupper((int)*eka) - toupper((int)*toka);
}
#endif

class StatItem
{
public:
	struct stat Stat;
    #ifdef DJGPP
    unsigned dosattr;
    #endif
    string Name;
public:
	StatItem(const struct stat &t,
	#ifdef DJGPP
	         unsigned da,
	#endif
	         const string &n) : Stat(t),
	#ifdef DJGPP
	                            dosattr(da),
	#endif
	                            Name(n) { }
    
    /* Returns the class code for grouping sort */
    int Class(int LinksAreFiles) const
    {
    	if(S_ISDIR(Stat.st_mode))return 0;
        #ifdef S_ISLNK
    	if(S_ISLNK(Stat.st_mode))return 2-LinksAreFiles;
        #else
        LinksAreFiles = LinksAreFiles; /* Not used */
        #endif
    	if(S_ISCHR(Stat.st_mode))return 3;
    	if(S_ISBLK(Stat.st_mode))return 4;
    	#ifdef S_ISFIFO
    	if(S_ISFIFO(Stat.st_mode))return 5;
    	#endif
        #ifdef S_ISSOCK
    	if(S_ISSOCK(Stat.st_mode))return 6;
    	#endif
    	return 1;
    }
    
	bool operator> (const StatItem &toka) const { return compare(toka, true); }
	bool operator< (const StatItem &toka) const { return compare(toka, false); }
	
	bool compare(const StatItem &toka, bool suur) const
    {
    	register const class StatItem &eka = *this;
    	
        for(const char *s=Sorting.c_str(); *s; s++)
        {
        	int Result=0;
        	
        	switch(tolower(*s))
    	    {
    	    	case 'c':
    	    		Result = GetNameAttr(eka.Stat, eka.Name.c_str()) 
    	    			   - GetNameAttr(toka.Stat, toka.Name.c_str());
    	    		break;
    	   	 	case 'n':
       		 		Result = strcmp(eka.Name.c_str(), toka.Name.c_str());
       				break;
    	   	 	case 'm':
       		 		Result = stricmp(eka.Name.c_str(), toka.Name.c_str());
       				break;
    	   	 	case 's':
        			Result = eka.Stat.st_size - toka.Stat.st_size;
        			break;
    	    	case 'd':
        			switch(DateTime)
        			{
        				case 1: Result = eka.Stat.st_atime-toka.Stat.st_atime; break;
    	    			case 2: Result = eka.Stat.st_mtime-toka.Stat.st_mtime; break;
        				case 3: Result = eka.Stat.st_ctime-toka.Stat.st_ctime;
        			}
        			break;
    	    	case 'u':
        			Result = (int)eka.Stat.st_uid - (int)toka.Stat.st_uid;
        			break;
    	    	case 'g':
        			Result = (int)eka.Stat.st_gid - (int)toka.Stat.st_gid;
        			break;
    	    	case 'h':
        			Result = (int)eka.Stat.st_nlink - (int)toka.Stat.st_nlink;
        			break;
    		   	case 'r':
        			Result = eka.Class(0) - toka.Class(0);
        			break;
    		   	case 'p':
        			Result = eka.Class(1) - toka.Class(1);
        			break;
        		default:
        		{
        			const char *t = Sorting.c_str();
    		        SetAttr(GetDescrColor("txt", 1));
    			    Gprintf("\nError: `-o");
    			    while(t < s)Gputch(*t++);
    			    GetModeColor("info", '?');
    			    Gputch(*s++);
    		        SetAttr(GetDescrColor("txt", 1));
    		        Gprintf("%s'\n\n", s);
    		        return (Sorting = ""), 0;
    		    }
    		}
    		if(Result)
    			return isupper((int)*s)^suur ? Result>0 : Result<0;
    	}
        
        return false;
    }
};

static vector<StatItem> StatPuu;

static void Puuhun(const struct stat &Stat, const string &Name
#ifdef DJGPP
	, unsigned int attr
#endif
	)
{
	if(PreScan)
	{
		StatItem t(Stat,
        #ifdef DJGPP
                   attr,
        #endif
                   Name);
        StatPuu.push_back(t);
        
		if(Colors)
		{
			#if STARTUP_COUNTER
			if(AnsiOpt)
			{
				int b, c, d;
				
				for(b=d=1; (c=StatPuuKoko/b) != 0; b*=10, d++)if(c%10)break;
				
				for(c=0; c<d; c++)Gputch('\b');
				
				while(c)
				{
					Gprintf("%d", (StatPuuKoko/b)%10);
					b /= 10;
					c--;
				}
				
				fflush(stdout);
			}
			else
				Gprintf("%u\r", StatPuu.size());
			#endif
		}
	}
	else
	{
		Dumping = true;
		TellMe(Stat, Name
        #ifdef DJGPP
        , attr
        #endif
		);
		Dumping = false;
	}
}

static void SortFiles()
{
	sort(StatPuu.begin(), StatPuu.end());
}

static void DropOut()
{
    EstimateFields();
    
    if(Sorting.size())SortFiles();
    
    Dumping = true;
    
    unsigned a, b = StatPuu.size();
    
    if(Colors && b && AnsiOpt)Gprintf("\r");
    
	for(a=0; a<b; ++a)
    {
        StatItem &tmp = StatPuu[a];
        TellMe(tmp.Stat, tmp.Name.c_str()
        #ifdef DJGPP
        	, tmp.dosattr
        #endif
        	);
    }
    
    StatPuu.clear();
    
    LongestName = 0;
    LongestSize = 0;
	
	Dumping = false;
}

/* This function is supposed to stat() to Stat! */
static void SingleFile(const string &Buffer, struct stat &Stat)
{
    #ifndef S_ISLNK
    int Links=0; /* hack */
    #endif

    #ifdef DJGPP
    struct ffblk Bla;
    if(findfirst(Buffer.c_str(), &Bla, 0x37))
    {
        if(Buffer[0]=='.')return;
        Gprintf("%s - findfirst() error: %d (%s)\n", Buffer.c_str(), errno, GetError(errno).c_str());
    }
    #endif

    if((stat(Buffer.c_str(), &Stat) == -1) && !Links)
        Gprintf("%s - stat() error: %d (%s)\n", Buffer.c_str(), errno, GetError(errno).c_str());
    #ifdef S_ISLNK
    else if((lstat(Buffer.c_str(), &Stat) == -1) && Links)
        Gprintf("%s - lstat() error: %d (%s)\n", Buffer.c_str(), errno, GetError(errno).c_str());
    #endif
    else
    {
	    char OwNam[16];
    	char GrNam[16];
	    const char *Group, *Passwd;
    	
        string s = GetSize(Buffer, Stat, 0, 0);
        int i = GetName(Buffer, Stat, 0, 0, true);
        if(i > LongestName)LongestName = i;
        i = s.size();
        if(i > LongestSize)LongestSize = i;
        
        Passwd = Getpwuid((int)Stat.st_uid);
		Group  = Getgrgid((int)Stat.st_gid);
	    if(Passwd && *Passwd)strcpy(OwNam, Passwd);else sprintf(OwNam,"%d",(int)Stat.st_uid);
    	if( Group &&  *Group)strcpy(GrNam,  Group);else sprintf(GrNam,"%d",(int)Stat.st_gid);
    	
    	i = strlen(OwNam); if(i > LongestUID)LongestUID = i;
    	i = strlen(GrNam); if(i > LongestGID)LongestGID = i;
        
        Puuhun(Stat, Buffer
        #ifdef DJGPP
        , Bla.ff_attrib
        #endif
        );
    }
}

static string LastDir;
/* impossible, or at least improbable filename */

static void DirChangeCheck(string Source)
{
	register unsigned ss = Source.size();
	if(ss)
	{
		if(Source[ss-1] == '/')
			Source.erase(--ss);
		else if(ss>=2 && Source[0]=='.' && Source[1]=='/')
			Source.erase(0, 2);
	}
    if(LastDir != Source)
    {
    	DropOut();
        if(Totals)
        {
        	static int Eka=1;
	        GetDescrColor("txt", 1);
        	if(WhereX)Gprintf("\n");
        	if(!Eka)
        	{
        		Summat();
        		Gprintf("\n");
        	}
        	Eka=0;
			Gprintf(" Directory of %s\n", Source.size()?Source.c_str():".");
		}
		LastDir = Source;
    }
}

static void ScanDir(const char *Dirrikka)
{
    char Source[PATH_MAX+1];
    char DirName[PATH_MAX+1];
    int Tried = 0;

    struct stat Stat;
    struct dirent *ent;
    DIR *dir;

    strcpy(DirName, Dirrikka);

    #ifdef DJGPP
    if(DirName[strlen(DirName)-1] == ':')
        strcat(DirName, ".");
    #endif

    strcpy(Source, DirName);

R1: if((dir = opendir(Source)) == NULL)
    {
    	if(
	    #ifdef DJGPP
    		errno==EACCES ||
        #endif
        	errno==ENOENT ||	/* Piti lis�t� linkkej� varten */
        	errno==ENOTDIR)
        {
P1:			string Tmp = DirOnly(Source);
			if(!Tmp.size())Tmp = "./";
	    	DirChangeCheck(Tmp.c_str());

			SingleFile(Source, Stat);
			goto End_ScanDir;
        }
        else if(!Tried)
        {
        	strcat(Source, "/");
            Tried=1;
            goto R1;
        }
        else if(errno==EACCES)
        {
            Gprintf(" No access to %s\n", Source);
            goto End_ScanDir;
        }

        if(errno)
            Gprintf("\n%s - error: %d (%s)\n", Source, errno, GetError(errno).c_str());

        goto End_ScanDir;
    }    
    if(!Contents)
    {
        errno = 0;
        Tried = 1;
        goto P1;
    }
    
    DirChangeCheck(Source);
    
    while((ent = readdir(dir)) != NULL)
    {
    	string Buffer = Source;
        if(Buffer[Buffer.size()-1] != '/')Buffer += '/';
        Buffer += ent->d_name;

       	SingleFile(Buffer, Stat);
    }

    if(closedir(dir) != 0)
        Gprintf("Trouble #%d (%s) at closedir() call.\n",
            errno, GetError(errno).c_str());

End_ScanDir:
    return;
}

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif
#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

static void Summat()
{
#if HAVE_STATFS
#if defined(SUNOS)||defined(__sun)||defined(SOLARIS)
#define STATFS(mountpoint, structp) statvfs(mountpoint, structp)
#define STATFST statvfs
#define STATFREEKB(tmp) ((tmp).f_bavail / 1024.0 * (tmp).f_frsize)
#else
#define STATFS(mountpoint, structp) statfs(mountpoint, structp)
#define STATFST statfs
#define STATFREEKB(tmp) ((tmp).f_bavail / 1024.0 * (tmp).f_bsize)
#endif
	struct STATFST tmp;
#endif
	unsigned long Koko;
	
	string NumBuf;
    
    Dumping = true;
    GetDescrColor("txt", 1);

    if(RowLen > 0)Gprintf("\n");
    if(!Totals)
    {
        if(Colors)Gprintf("\r \r"); /* Ensure color */
        return;
    }

    Koko = /* Grand total */
        Summa[SumDir]
      + Summa[SumFifo]
      + Summa[SumFile]
      + Summa[SumLink]
      + Summa[SumChrDev]
      + Summa[SumBlkDev];

    ColorNums = GetDescrColor("num", -1);
    
#if HAVE_STATFS
    if(STATFS(LastDir.c_str(), &tmp))tmp.f_bavail = 0;
#endif
        	
    if(Compact)
    {
		unsigned long Tmp = SumCnt[SumChrDev] + SumCnt[SumBlkDev];
		unsigned long Tmp2= SumCnt[SumFifo]+SumCnt[SumSock]+SumCnt[SumLink];
		
        PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumDir]);
        Gprintf(" \1%s\1 dir%s%s", NumBuf.c_str(),
#if HAVE_STATFS
        	(tmp.f_bavail > 0 && Tmp)?"":
#endif        	
        	"ector",
        	SumCnt[SumDir]==1?"y":
#if HAVE_STATFS
        	(tmp.f_bavail > 0 && Tmp)?"s":
#endif        	
        	"ies");
        		
        if(SumCnt[SumFile])
        {
        	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumFile]);
        	Gprintf(", \1%s\1 file%s",
        		NumBuf.c_str(),
        		SumCnt[SumFile]==1?"":"s");
       	}
        		
        if(Tmp)
        {
        	PrintNum(NumBuf, TotalSep, "%lu", Tmp);
        	Gprintf(", \1%s\1 device%s", NumBuf.c_str(), Tmp==1?"":"s");
        }
        if(Tmp2)
        {
        	PrintNum(NumBuf, TotalSep, "%lu", Tmp2);
        	Gprintf(", \1%s\1 other%s", NumBuf.c_str(), Tmp2==1?"":"s");
        }
        		
        PrintNum(NumBuf, TotalSep, "%lu", Koko);
        Gprintf(", \1%s\1 bytes", NumBuf.c_str());
        
#if HAVE_STATFS
        if(tmp.f_bavail > 0)
        {
            // Size = kilobytes
        	double Size = STATFREEKB(tmp);
        	
        	if(Compact == 2)
        	{
        		PrintNum(NumBuf, TotalSep, "%.0f", Size*1024.0);
        		Gprintf(", \1%s\1 bytes", NumBuf.c_str());
        	}
        	else if(Size >= 1024)
        	{
        		PrintNum(NumBuf, TotalSep, "%.1f", Size/1024.0);
        		Gprintf(", \1%s\1 MB", NumBuf.c_str());
        	}
        	else if(Size >= 1048576*10)
        	{
        		PrintNum(NumBuf, TotalSep, "%.1f", Size/1048576.0);
        		Gprintf(", \1%s\1 GB", NumBuf.c_str());
        	}
       		else
       		{
       			PrintNum(NumBuf, TotalSep, "%.1f", Size);
        		Gprintf(", \1%s\1 kB", NumBuf.c_str());
        	}
        		
        	Gprintf(" free(\1%.1f\1%%)",
        		(double)tmp.f_bavail * 100.0 / tmp.f_blocks);
        }
#endif	    
	    Gprintf("\n");
    }
    else
    {
		unsigned long Tmp = SumCnt[SumChrDev] + SumCnt[SumBlkDev];
		
        if(Tmp)
        {
        	PrintNum(NumBuf, TotalSep, "%lu", Tmp);
            Gprintf("\1%5s\1 device%s (", NumBuf.c_str(), (Tmp==1)?"":"s");
    
            if(SumCnt[SumChrDev])
            {
            	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumChrDev]);
            	Gprintf("\1%s\1 char", NumBuf.c_str());
            }
            if(SumCnt[SumChrDev]
            && SumCnt[SumBlkDev])Gprintf(", ");    
            if(SumCnt[SumBlkDev])
            {
            	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumBlkDev]);
            	Gprintf("\1%s\1 block", NumBuf.c_str());
            }
            Gprintf(")\n");
        }

        if(SumCnt[SumDir])
        {
        	string TmpBuf;
        	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumDir]);
        	PrintNum(TmpBuf, TotalSep, "%lu", Summa[SumDir]);
            Gprintf("\1%5s\1 directories,\1%11s\1 bytes\n",
                NumBuf.c_str(), TmpBuf.c_str());
        }
    
        if(SumCnt[SumFifo])
        {
        	string TmpBuf;
        	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumFifo]);
        	PrintNum(TmpBuf, TotalSep, "%lu", Summa[SumFifo]);
            Gprintf("\1%5s\1 fifo%s\1%17s\1 bytes\n",
                NumBuf.c_str(), (SumCnt[SumFifo]==1)?", ":"s,", TmpBuf.c_str());
        }    
        if(SumCnt[SumFile])
        {
        	string TmpBuf;
        	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumFile]);
        	PrintNum(TmpBuf, TotalSep, "%lu", Summa[SumFile]);
            Gprintf("\1%5s\1 file%s\1%17s\1 bytes\n",
                NumBuf.c_str(), (SumCnt[SumFile]==1)?", ":"s,", TmpBuf.c_str());
    	}
        if(SumCnt[SumLink])
        {
        	string TmpBuf;
        	PrintNum(NumBuf, TotalSep, "%lu", SumCnt[SumLink]);
        	PrintNum(TmpBuf, TotalSep, "%lu", Summa[SumLink]);
            Gprintf("\1%5s\1 link%s\1%17s\1 bytes\n",
                NumBuf.c_str(), (SumCnt[SumLink]==1)?", ":"s,", TmpBuf.c_str());
		}
		PrintNum(NumBuf, TotalSep, "%lu", Koko);
        Gprintf("Total\1%24s\1 bytes\n", NumBuf.c_str());
#if HAVE_STATFS
        if(tmp.f_bavail > 0)
        {
        	double Size = STATFREEKB(tmp) * 1024.0;
        	
        	/* FIXME: Thousand separators for free space also      *
        	 *        Currently not implemented, because there     *
        	 *        may be more free space than 'unsigned long'  *
        	 *        is able to express.                          */
        	
        	PrintNum(NumBuf, TotalSep, "%.0f", Size);
        	
        	Gprintf("Free space\1%19s\1 bytes (\1%.1f\1%%)\n",
        		NumBuf.c_str(),
        		(double)((double)tmp.f_bavail * 100.0 / tmp.f_blocks));
        }
#endif
    }
    
    ColorNums = -1;

    memset(&SumCnt, 0, sizeof SumCnt);
    memset(&Summa, 0, sizeof Summa);
}

#if defined(SIGINT) || defined(SIGTERM)
static void Term(int dummy)
{
	Gprintf("^C\n");
    dummy=dummy;

    RowLen=1;
    Summat();
    
    exit(0);
}
#endif

void GetScreenGeometry()
{
#if defined(DJGPP) || defined(__BORLANDC__)
    struct text_info w;
    gettextinfo(&w);
    LINES=w.screenheight;
    COLS =w.screenwidth;
#else
#ifdef TIOCGWINSZ
    struct winsize w;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) >= 0)
    {
        LINES=w.ws_row;
        COLS =w.ws_col;
    }
#else
#ifdef WIOCGETD
    struct uwdata w;
    if(ioctl(STDOUT_FILENO, WIOCGETD, &w) >= 0)
    {
        LINES = w.uw_height / w.uw_vs;
        COLS  = w.uw_width / w.uw_hs;
    }
#endif
#endif
#endif
}

const char *GetNextArg(int argc, const char *const *argv)
{
    static char *DirrBuf = NULL;
    static char *DirrVar = NULL;
    static int Argc=1;
    const char *Temp = NULL;
    static int Ekastrtok;

    if(!DirrBuf && !DirrVar)
    {
        DirrVar = getenv("DIRR");
        if(DirrVar)
        {
        	ALLOCATE(DirrBuf, char *, strlen(DirrVar)+1);
            DirrVar = strcpy(DirrBuf, DirrVar);
            Ekastrtok=1;
    }   }

Retry:
    if(DirrVar)
    {
        Temp = strtok(Ekastrtok?DirrVar:NULL, " \t\r\n");
        Ekastrtok=0;
        if(Temp)goto Ok;
        free(DirrBuf);
        DirrVar = NULL;
        DirrBuf = (char *)&DirrBuf; /* Kunhan vain on muuta kuin NULL */
    }

    if(Argc >= argc)return NULL;

    Temp = argv[Argc++];
Ok: if(Temp && !*Temp)goto Retry;
    return Temp;
}

static void EstimateFields()
{
    int RowLen;
    
    if(PreScan)
    	LongestName++;
    else
    {
    	LongestSize = Sara?7:9;
    	LongestGID  = 8;
    	LongestUID  = 8;
   	}
   	const char *s;
    for(RowLen=0, s=FieldOrder.c_str(); *s; )
    {
        switch(*s)
        {
            case '.':
                s++;
                switch(*s)
                {
                    case 'x':
                        s++;
                        GetHex(7, (const char **)&s);
                        s--;
                        break;
                    case 'F':
                    case 'f':
                        break;
                    case 'S':
                        s++;
                        RowLen += 3;
                    case 's':
                        RowLen += LongestSize;
                        break;
                    case 'd':
                        if(DateForm == "%u")
                            RowLen += 12;
                        else if(DateForm == "%z")
                            RowLen += 5;
                        else
                        {
                            char Buf[64];
                            struct tm TM;
                            memset(&TM, 0, sizeof(TM));
                            strftime(Buf, sizeof Buf, DateForm.c_str(), &TM);
                            RowLen += strlen(Buf);
                        }
                        break;
#ifndef DJGPP
                    case 'h':
                        RowLen += 4;
                        break;
                    case 'g':
                    	RowLen += LongestGID;
                    	break;
#endif
                    case 'o':
                        RowLen += LongestUID;
                        break;
                    case 'a':
                    {
                        int Len = '1';
                        s++;
                        if((*s>='0')&&(*s<='9'))Len = *s;
                        RowLen += (Len < '2')?10:Len-'0';
                        break;
                    }
                    default:
                        RowLen++;
                }
                break;
            default:
                RowLen++;
        }
        if(*s)s++;
    }
    
    RowLen = (Sara?COLS/2:COLS)-1-RowLen;
    if(RowLen < 0)RowLen = 0;
    
    if(!PreScan || LongestName > RowLen)
    	LongestName = RowLen;
}

static vector<string> Dirpuu, Argpuu;
static vector<unsigned> Virhepuu;

static unsigned RememberParam(vector<string> &Dirs, const char *s)
{
	Dirs.push_back(s);
	return Dirs.size();
}

static void DumpDirs()
{
    EstimateFields();
    
    unsigned a, b=Dirpuu.size();
    
    for(a=0; a<b; ++a)
        ScanDir(Dirpuu[a].c_str());
    
    Dirpuu.clear();
    
    DropOut();
}

static void DumpVirheet()
{
    if(!Virhepuu.size())return;

    Gprintf("Invalid option%s\n", Virhepuu.size()==1?"":"s");
    
    unsigned a, b=Argpuu.size();
    
    for(a=0; a<b; ++a)
        Gprintf("%s%3u: `%s'\n",
            binary_search(Virhepuu.begin(),
                          Virhepuu.end(),
                          a+1)?"-->":"\t", a+1, Argpuu[a].c_str());
    exit(EXIT_FAILURE);
}

int main(int argc, const char *const *argv)
{
    int Args = 0;
    int Help = 0;

    const char *Arggi;

    #ifdef DJGPP
    if(!isatty(fileno(stdout)))Colors = false;
    _djstat_flags &= ~(_STAT_EXEC_EXT | _STAT_WRITEBIT);
    #endif

    GetScreenGeometry();
	SetDefaultOptions();
	
	#ifdef SIGINT
    signal(SIGINT,  Term);
    #endif
    #ifdef SIGTERM
    signal(SIGTERM, Term);
    #endif

	#if CACHE_NAMECOLOR
	BuildNameColorCache();
	#endif
	
    while((Arggi = GetNextArg(argc, argv)) != NULL)
    {
        unsigned ArgIndex = RememberParam(Argpuu, Arggi);

        if(*Arggi=='-'
        #ifdef DJGPP
        || *Arggi=='/'
        #endif
        )
        {
            const char *s = Arggi;
            while(*s)
            {
                switch(*s)
                {
                    case '/':
                    case '-':
                        break;
                    case 'r':
                    	SetDefaultOptions();
                    	break;
                    case 'd':
                    	switch(*++s)
                    	{
                    		case '1': DateTime=1; break;
                    		case '2': DateTime=2; break;
                    		case '3': DateTime=3; break;
                    		case 'b':
                    			BlkStr = s+1;
                    			goto ThisIsOk;
                    		case 'c':
                    			ChrStr = s+1;
                    			goto ThisIsOk;
                    		default:
                    			goto VirheParam;
                    	}
                        break;
                    case 'l':
                    	s++;
                    	if(*s=='a')goto VipuAL; /* Support -la and -al */
                    	#ifdef S_ISLNK
                        Links = 
                        #endif
                        strtol(s, (char **)&s, 10);
                        s--;
                        break;
                    case 'X':
                    	s++;
                    	if(*s=='0')
                    		printf("Window size = %dx%d\n", COLS, LINES);
                        COLS=strtol(s, (char **)&s, 10);
                        s--;
                        break;
                    case 'h':
                    case '?': Help = 1; break;
                    case 'C': Sara = true; break;
                    case 'D': Contents = false; break;
                    case 'c': Colors = false; break;
                    case 'p': Pagebreaks = true; break;
                    case 'P': AnsiOpt = false; break;
                    case 'e':
                    	PreScan = false;
                    	Sorting = "";
                    	break;
                   	case 'o':
                   		Sorting = s+1;
                   		goto ThisIsOk;
                    case 'F':
                    	DateForm = s+1;
                        goto ThisIsOk;
                    case 'f':
                    	FieldOrder = s+1;
                        goto ThisIsOk;
                    case 'v':
                    case 'V':
                    	printf(VERSIONSTR);
                    	return 0;
                    case 'a':
                    	switch(*++s)
                    	{
                    		case '0':
                    			FieldOrder = ".f_.s.d|";
                    			DateForm = "%z";
                            #ifdef S_ISLNK
                                Links = 1;
                            #endif
                                Sara = true;
                                Compact = 1;
                    			break;
                    		case '1':
                    			FieldOrder = ".xF|.a1.xF|.f.s.xF|.d.xF|.o_.g.xF|";
                    			break;
                    		case '2':
                            #ifdef S_ISLNK
                    			Links=0;
                            #endif
                    			Sara = true;
                    			FieldOrder = ".f_.a4_.o_.g.xF|";
                    			break;
                    		case '3':
                            #ifdef S_ISLNK
                    			Links=0;
                            #endif
                    			Sara = true;
                    			FieldOrder = ".f_.s_.o.xF|";
                    			break;
                    		case '4':
                            #ifdef S_ISLNK
                    			Links=1;
                            #endif
                    			Sara = true;
                    			FieldOrder = ".f_.s_.d_.o.xF|";
                    			DateForm = "%z";
                    			break;
                    		case 't':
                    			Sorting = "dn";
                    			PreScan = true;
                    		case 'l':
                    VipuAL:		FieldOrder = ".a1_.h__.o_.g_.s_.d_.f";
                    			DateForm = "%u";
                    			break;
                    		default:
                    			goto VirheParam;
                    	}
                    	break;
                    case 'M':
                    case 'm':
                    	TotalSep = (*s=='M') ? s[2] : 0;
                    	switch(*++s)
                    	{
                    		case '0':
                    			Compact = 0;
                    			Totals = true;
                    			break;
                    		case '1':
		                    	Compact = 1;
        		            	Totals = true;
                    			break;
                    		case '2':
                    			Totals = false;
                    			break;
                    		case '3':
		                    	Compact = 2;
        		            	Totals = true;
                    			break;
                    		default:
                    			goto VirheParam;
                    	}
                    	if(TotalSep)s++;
                    	break;
                    case 'w':
                    case 'W':
                    	Compact = 1;
                    	Totals = true;
                    	FieldOrder = ".f";
                        Sorting = (isupper((int)*s)?"PCM":"pcm");
                    #ifdef S_ISLNK
                        Links = 1;
                    #endif
                        Sara = true;
                        break;
                    default:
            VirheParam:	;
                    {
                    	Virhepuu.push_back(ArgIndex);
                        goto ThisIsOk;
                    }
                }
                s++;
            }
ThisIsOk:;
        }
        else
        {
            RememberParam(Dirpuu, Arggi);
            Args=1;
        }
    }

    DumpVirheet();

    if(Help)
    {
    	bool c=Colors, a=AnsiOpt, p=Pagebreaks;
    	SetDefaultOptions();
    	Pagebreaks = p;
    	AnsiOpt = a;
    	Dumping = true;
    	Colors = c;

        GetDescrColor("txt", 1);
        Gprintf(
            #ifndef DJGPP
            "\r\33[K\r"
            #endif
            "Usage: %s [options] { dirs | files }\n\n"
            "Options:\n"
            "\t-c  Disables colors.\n"
            "\t-w  Short for -Cm1l1f.f -opcm\n"
            "\t-W  Same as -w, but reverse sort order\n"
            "\t-a0 Short for -wf.f_.s.d| -F%%z\n"
            "\t-a1 Short for -f.xF|.a1.xF|.f.s.xF|.d.xF|.o_.g.xF|\n"
            "\t-a2 Short for -l0Cf.f_.a4_.o_.g.xF|\n"
            "\t-a3 Short for -l0Cf._s_.o.xF|\n"
            "\t-a4 Short for -l1Cf_.s_.d_.o.xF| -F%%z\n"
            "\t-al Short for -f.a1_.h__.o_.g_.s_.d_.f -F%%u\n"
            "\t-at Short for -alodn\n"
            "\t-m0 Selects verbose \"total\" list. This is the default.\n"
            "\t-m1 Compactifies the \"total\" list.\n"
            "\t-m2 Disables total sums.\n"
            "\t    Instead of -mx you can use -Mxn and specify n\n"
            "\t    as the thousand separator in total sums.\n"
            "\t-m3 Compactified \"total\" list with exact free space.\n"
            "\t-p  Enables page pauses.\n"
            "\t-P  Disables the vt100 code optimization.\n"
            "\t-D  Show directory names instead of contents.\n"
            "\t-e  Quick list (uses space estimation). Disables sorting.\n"
            "\t-r  Undoes all options, including the DIRR environment variable.\n"
            "\t-on Sort the list (disables -e), with n as combination of:\n"
            "\t    (n)ame, (s)ize, (d)ate, (u)id, (g)id, (h)linkcount\n"
            "\t    (c)olor, na(m)e case insensitively\n"
            "\t    g(r)oup dirs,files,links,chrdevs,blkdevs,fifos,socks\n"
            "\t    grou(p) dirs,links=files,chrdevs,blkdevs,fifos,socks\n"
            "\t    Use Uppercase for reverse order.\n"
            "\t    Default is `-o%s'\n"
           	"\t-X# Force screen width\n"
            "\t-C  Enables multicolumn mode.\n"
            "\t-d1 Use atime, last access datetime (disables -d2 and -d3)\n"
            "\t-d2 Use mtime, last modification datetime (default, disables -d1 and -d3)\n"
            "\t-d3 Use ctime, creation datetime (disables -d1 and -d2)\n"
            "\t-fx Output format\n"
            "\t    .s=Size,    .f=File,   .d=Datetime,     .o=Owner,   .g=Group,\n"
            "\t    .S#=size with thsep #, .x##=Color 0x##, .h=Number of hard links\n"
            #ifdef DJGPP
            "\t    .a0=SHRA style attributes      "
            #else
            "\t    .a1=drwxrwxrwx style attributes"
            #endif
                                                 "         .a#=Mode as #-decimal octal\n"
            "\t    .F and .G and .O are respectively, without space fitting\n"
            "\t    anything other=printed\n"
            "\t    Default is `-f%s'\n"
            "\t-Fx Specify new date format. Result maxlen 64.\n"
            "\t    Default is `-F%s'\n",
            argv[0],
            Sorting.c_str(),
            FieldOrder.c_str(),
            DateForm.c_str());
                    Gprintf(
            "\t-dbx Specify how the blockdevices are shown\n"
            "\t     Example: `-db<B%%u,%%u>'\n"
            "\t     Default is `-db%s'\n"
            "\t-dcx Specify how the character devices are shown\n"
            "\t     Example: `-dc<C%%u,%%u>'\n"
            "\t     Default is `-dc%s'\n", BlkStr.c_str(), ChrStr.c_str());
#ifdef S_ISLNK
                    Gprintf(
            "\t-ln Specify how the links are shown\n"
            "\t  0 Show link name and stats of target\n"
            "\t  1 Show link name and <LINK>\n"
            "\t  2 Show link name, link's target and stats of link\n"
            "\t  3 Show link name, link's target and stats of target\n"
            "\t  4 Show link name, link's target and <LINK>\n"
#else
					Gprintf(
			"\n-l# Does nothing in this environment.\n"
#endif
            "You can set environment variable 'DIRR' for the options.\n"
            "You can also use 'DIRR_COLORS' -variable for color settings.\n"
            "Current DIRR_COLORS:\n"
        );

        PrintSettings();

        return 0;
    }

    if(!Args)
        RememberParam(Dirpuu, ".");

	ReadGidUid();
	
	Dumping = true;
    DumpDirs();

    Summat();

    return 0;
}
