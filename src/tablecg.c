/*
 * TABLECG.C
 *
 *	This is used to synthesize the table structure surrounding the site
 *	pages, to highlight Horizontal or Vertical selection features, and
 *	to track selections by modifying embedded LOCALLINK() directives.
 *
 *
 * $DragonFly: site/src/tablecg.c,v 1.28 2004/07/14 23:59:10 hmp Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

static int VerboseOpt;
static const char *FilePath;
static const char *FileName;
static const char *DirPath;
static const char *DirName;
static const char *ParentDir;
static char *Av0;
static char *OBuf;
static int  OSize;
static int  OMax;
static char *Title;

static void generate_side_headers(char *, char *, char * []);
static void process_vars(const char *ptr, int len);
static void process_command(const char *cmd, char *args);
static void *safe_malloc(int bytes);
static char *safe_strdup(const char *str);
static void read_all(int fd, void *buf, int n);
static void buildout(const char *ptr, int len);
static void buildflush(void);
static const char *choppath(const char *path);
static const char *filecomp(const char *path);
static time_t parse_http_date(const char *header);

#define	SITE_ROOT	"http://www.dragonflybsd.org"

char *Main[] = {
    "bugs.cgi",
    "download.cgi",
    "forums.cgi",
    "mascot.cgi",
    "team.cgi",
    "FAQ.cgi",
    NULL
};

char *Goals[] = {
    "caching.cgi",
    "iomodel.cgi",
    "messaging.cgi",
    "packages.cgi",
    "threads.cgi",
    "userapi.cgi",
    "vfsmodel.cgi",
    NULL
};

char *Status[] = {
    "diary.cgi",
    "report-2003.cgi",
    NULL
};

char *Docs[] = {
    NULL
};

/*
 * Phrases work is currently commented out.  Remove comments here 
 * and in header setup to enable.
 *
 * char *Phrases[] = {
 *     "Tired of Penguins?  Switch totheDragon!",
 *     "Dragging BSD, kicking and screaming, into the 21st century.",
 *     "Best thing since sliced bread.",
 *     "A new day and a new way.",
 *     "The way BSD should be!",
 *     "Here be Dragons.",
 *     "DragonFly BSD, the logical successor to FreeBSD 4.x.",
 *     "Use the Force: DragonFly release1 in June!"
 *     "Catch the buzz!",
 *     "When one wing isn't enough!",
 *     "The best breed",
 *     "A Dragonfly a day will keep Microsoft away."
 * };
 */
 
/*
 * suggested but not yet added: "BSD with style(9)"
 */


int
main(int ac, char **av)
{
    FILE *fi;
    char *base;
    int i;

    /*
     * Process options
     */
    Av0 = av[0];
    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];
	if (*ptr != '-') {
	    FilePath = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'v':
	    VerboseOpt = 1;
	    break;
	}
    }

    /*
     * Output headers and HTML start.
     */

    if (FilePath == NULL) {
	fprintf(stderr, "%s: no file specified\n", av[0]);
	exit(1);
    }
    FileName = filecomp(FilePath);
    DirPath = choppath(FilePath);
    DirName = filecomp(DirPath);
    ParentDir = choppath(DirPath);

    /*
     * Process arguments
     */
    if ((base = getenv("CONTENT_LENGTH")) != NULL) {
	int l = strtol(base, NULL, 10);
	if (l < 0 || l > 1000000) {
	    fprintf(stderr, "%s: bad length %d processing %s\n", 
		av[0], l, FilePath);
	    exit(1);
	}
	base = safe_malloc(l + 1);
	base[l] = 0;
	read_all(0, base, l) ;
	process_vars(base, l);
	free(base);
    }
    if ((base = getenv("QUERY_STRING")) != NULL)
	process_vars(base, strlen(base));

    /*
     * Process body
     */
    if (FilePath[0] && (fi = fopen(FilePath, "r")) != NULL) {
	char buf[256];
	char *las;
	char *ptr;
	struct stat sb;
	time_t t;

	fstat(fileno(fi), &sb);
	t = sb.st_mtime;
	if (stat(av[0], &sb) < 0 || (t >= sb.st_mtime))
		sb.st_mtime = t;
		
	if ((t = parse_http_date("HTTP_IF_MODIFIED_SINCE")) != 0) {
	    if (t >= sb.st_mtime) {
		printf("Status: 304 Not Modified");
		return(0);	
	    }
	}
	if ((t = parse_http_date("HTTP_IF_UNMODIFIED_SINCE")) != 0) {
	    if (t < sb.st_mtime) {
		printf("Status: 304 Not Modified");
		return(0);	
	    }
	}
	strftime(buf,sizeof buf, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&sb.st_mtime));
	printf("Last-Modified: %s\n", buf);	

	while (fgets(buf, sizeof(buf), fi) != NULL) {
	    if (buf[0] == '#')
		continue;
	    las = ptr = buf;
	    while ((ptr = strchr(ptr, '$')) != NULL) {
		int i;
		int j;

		++ptr;

		for (i = 0; isalpha(ptr[i]); ++i)
		    ;
		if (i > 0 && ptr[i] == '(') {
		    for (j = i + 1; ptr[j] && ptr[j] != ')'; ++j)
			;
		    if (ptr[j] == ')') {
			buildout(las, ptr - las - 1);
			ptr[i] = 0;
			ptr[j] = 0;
			process_command(ptr, ptr + i + 1);
			las = ptr + j + 1;
			ptr = las;
		    }
		}
	    }
	    buildout(las, strlen(las));
	}
	fclose(fi);
    }
    /*
     * End request header first.
     */
    printf("Content-Type: text/html\n");
    printf("\n");
    /*
     * Generate the table structure after processing the web page so
     * we can populate the tags properly.
     */
    printf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    printf("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" ");
    printf("\n \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n");
    printf("<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
    printf("<head>\n");

    if (Title)
	printf("<title>%s</title>\n", Title);
    else
	printf("<title>DragonFly</title>\n");

    printf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>\n");
    printf("<link href=\"/favicon.ico\" rel=\"shortcut icon\"/>\n");
    printf("<link rel=\"stylesheet\" href=\"%s/stylesheet.css\" "
		"type=\"text/css\"/>", SITE_ROOT);
    printf("</head>\n");
    printf("<body>\n");

    printf("<table border=\"0\" width=\"760\" bgcolor=\"#FFFFFF\">\n");
    printf("<tr><td width=\"134\">"
		"<img src=\"%s/smalldf.jpg\" alt=\"\"/></td>\n", SITE_ROOT);
    printf("<td valign=\"bottom\">");

    if (Title)
	printf("<span class=\"pagetitle\">%s</span>", Title);
    else
        printf("<span class=\"pagetitle\">The DragonFly BSD Project</span>");

/*
 *  Random phrase printer - commented out until more phrases notes, 
 *  or just one picked.
 *
 *  printf("<br/><i><small>\n");
 *  srandom(time(NULL));
 *  printf("%s", Phrases[random()%(sizeof(Phrases)/sizeof(Phrases[0]))]);
 *  printf("</small></i>
 */
 
    printf("</td></tr>");
     printf("<tr><td colspan=\"2\"><hr size=\"1\" noshade=\"noshade\" /></td></tr>");

    printf("<tr><td valign=\"top\">");

    generate_side_headers("main", "Main", Main);
    generate_side_headers("goals", "Goals", Goals);
    generate_side_headers("status", "Status", Status);
    generate_side_headers("docs", "Docs", Docs);

    printf("</td><td valign=\"top\" bgcolor=\"#ffffff\">");
    fflush(stdout);
    buildflush();
    printf("<pre>\n");
    fflush(stdout);
    printf("</pre>\n");

    /*
     * Finish table structure and add terminators.
     */
    printf("</td></tr></table>\n");
    printf("</body>\n");
    printf("</html>\n");
    return(0);
}

/*
 * The menu is synthesized from arrays above listing the files
 * we want to have available in the side menu.  A peek at the 
 * filesystem is needed so we know where we are, and how to 
 * format accordingly.
 *
 * files[] never includes index.cgi.  We highlight the section name
 * instead.
 */
static void
generate_side_headers(char *section1, char *section2, char *files[])
{
    int len;
    const char *ptr;
    int i;
    const char *fileclass = "";

    printf("\n<table border=\"0\" cellpadding=\"4\" width=\"100%%\">\n");
    printf("\t<tr>");

    if (strcmp(FileName, "index.cgi") == 0 &&
	strcmp(section1, DirName) == 0
    ) {
	fileclass = " class=\"selected\"";
    } else {
	fileclass = " class=\"unselected\"";
    }

    printf("<td%s><a href=\"%s/%s\">%s</a></td></tr>\n",
	fileclass, SITE_ROOT, section1, section2);

	if (files[0] != NULL) {
        printf("\t<tr><td>\n");
	printf("<table border=\"0\" width=\"100%%\">\n");

    	for (i = 0; files[i] != NULL; i++) {
        	if ((strcmp(files[i], FileName) == 0) &&
	    	    (strcmp(section1, DirName) == 0) 
		) {
        	    fileclass = " class=\"subselected\"";
		} else {
        	    fileclass = " class=\"subunselected\"";
		}
      
        	if ((ptr = strchr(files[i], '.')) != NULL &&
            	(strcmp(ptr + 1, "cgi") == 0 ||
            	strcmp(ptr + 1, "html") == 0)
        	) {
            	len = ptr - files[i];
            	printf("\t<tr><td%s>", fileclass);
            	printf("&nbsp;&nbsp;&nbsp;&nbsp;");
            	printf("<a class=\"nounderline\" ");
            	printf("href=\"%s/%s/%s\">%*.*s</a></td></tr>\n", SITE_ROOT,
			section1, 
			files[i], len, len, files[i]);
        	}
    	}
    	printf("</table>\n</td></tr>\n");
	}

    printf("\t<tr><td width=\"100%%\">&nbsp;</td></tr>\n");
    printf("</table>\n");
}

static void
process_vars(const char *ptr, int len)
{
}

static void
process_command(const char *cmd, char *args)
{
    if (strcmp(cmd, "TITLE") == 0) {
	Title = safe_strdup(args);
    }
}

static void
read_all(int fd, void *buf, int n)
{
    int r;
    int t = 0;

    while (n > 0) {
	r = read(fd, buf, n);
	if (r < 0) {
	    fprintf(stderr, "%s: post read failed %d/%d: %s page %s\n",
		Av0, t, n, strerror(errno), FilePath);
	    exit(1);
	}
	n -= r;
	buf = (char *)buf + r;
    }
}

static void
buildout(const char *ptr, int len)
{
    if (OBuf == NULL) {
	OMax = 1024*1024;
	OBuf = safe_malloc(OMax);
    }
    if (OSize + len > OMax)
	len = OMax - OSize;
    bcopy(ptr, OBuf + OSize, len);
    OSize += len;
}

static void
buildflush(void)
{
    if (OSize)
	fwrite(OBuf, OSize, 1, stdout);
}

static
const char *
choppath(const char *path)
{
    const char *ptr;
    char *nptr;

    if ((ptr = strrchr(path, '/')) != NULL) {
	nptr = safe_malloc(ptr - path + 1);
	bcopy(path, nptr, ptr - path);
	nptr[ptr - path] = 0;
    } else {
	nptr = ".";
    }
    return(nptr);
}

static
const char *
filecomp(const char *path)
{
    const char *ptr;

    if ((ptr = strrchr(path, '/')) != NULL)
	return(ptr + 1);
    else
	return(path);
}

static void *
safe_malloc(int bytes)
{
    void *ptr = malloc(bytes);
    if (ptr == NULL) {
	fprintf(stderr, "%s: malloc() failed page %s\n", Av0, FilePath);
	exit(1);
    }
    return(ptr);
}

static char *
safe_strdup(const char *ptr)
{
    char *str;

    if ((str = strdup(ptr)) == NULL) {
	fprintf(stderr, "%s: malloc() failed page %s\n", Av0, FilePath);
	exit(1);
    }
    return (str);
}

static time_t
parse_http_date(const char *header)
{
    char *val;
    size_t len;
    struct tm t;
    if ((val = getenv(header)) == NULL)
    	return 0;
    len = strlen(val);
    if (len == strptime(val, "%a, %d %b %Y %H:%M:%S GMT", &t) - val)
    	return timegm(&t);
    if (len == strptime(val, "%a %b %d %H:%M:%S %Y", &t) - val)
    	return timegm(&t);
    if (len == strptime(val, "%A %d-%b-%C %H:%M:%S GMT", &t) - val)
    	return timegm(&t);
    return 0;
}
