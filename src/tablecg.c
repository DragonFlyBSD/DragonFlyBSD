/*
 * TABLECG.C
 *
 *	This is used to synthesize the table structure surrounding the site
 *	pages, to highlight Horizontal or Vertical selection features, and
 *	to track selections by modifying embedded LOCALLINK() directives.
 *
 *
 * $DragonFly: site/src/tablecg.c,v 1.36 2006/05/12 02:46:23 dillon Exp $
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
static const char *QueryString;
static char *Av0;
static char *OBuf;
static int  OSize;
static int  OMax;
static char *Title;

const char *generate_query_string(void);
static void generate_menu_layer(const char *path, int baselen, int layer);
static void process_vars(const char *ptr, int len);
static void process_element(char *varName, char *varData);
static void process_command(const char *cmd, char *args);
static void *safe_malloc(int bytes);
static char *safe_strdup(const char *str);
static void read_all(int fd, void *buf, int n);
static void buildout(const char *ptr, int len);
static void buildflush(void);
static char *choppath(const char *path);
static const char *filecomp(const char *path);
static time_t parse_http_date(const char *header);

#define	SITE_ROOT	"http://www.dragonflybsd.org"
#define HTDOCS_ROOT	"/usr/local/www/site/data"

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
    if ((DirPath = choppath(FilePath)) == NULL) {
	fprintf(stderr, "%s: no directory path\n", av[0]);
	exit(1);
    }
    DirName = filecomp(DirPath);

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

    QueryString = generate_query_string();

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
		printf("Status: 304 Not Modified\r\n");
		printf("\r\n");
		return(0);	
	    }
	}
	if ((t = parse_http_date("HTTP_IF_UNMODIFIED_SINCE")) != 0) {
	    if (t < sb.st_mtime) {
		printf("Status: 304 Not Modified\r\n");
		printf("\r\n");
		return(0);	
	    }
	}
	strftime(buf,sizeof buf, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&sb.st_mtime));
	printf("Last-Modified: %s\r\n", buf);	

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
    printf("Content-Type: text/html\r\n");
    printf("\r\n");
    fflush(stdout);
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
		"<a href=\"/\"><img src=\"%s/smalldf.jpg\" border=\"0\" alt=\"\"/></a></td>\n", SITE_ROOT);
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

    generate_menu_layer(HTDOCS_ROOT, strlen(HTDOCS_ROOT), 0);

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
 * Generate a menu layer.  Each menu layer contains a menu.ctl file.  This
 * file holds a list of terminal elements (typically .cgi's), directories,
 * and full URLs.
 */
static void
generate_menu_layer(const char *dirpath, int baselen, int layer)
{
    struct stat st;
    char buf[256];
    char *path;
    const char *relpath;
    const char *selected;
    char *nameplate;
    FILE *fi;
    int len;

    /*
     * relpath is either empty or begins with a '/'
     */
    relpath = dirpath + baselen;
    asprintf(&path, "%s/menu.ctl", dirpath);
    fi = fopen(path, "r");
    free(path);

    if (fi == NULL)
	return;

    if (layer == 0)
	printf("\n<table border=\"0\" cellpadding=\"4\" width=\"100%%\">\n");
    else
	printf("\n<table border=\"0\">\n");

    while (fgets(buf, sizeof(buf), fi) != NULL) {
	if ((len = strlen(buf)) > 0 && buf[len-1] == '\n')
	    buf[--len] = 0;
	if (len == 0 || buf[0] == '#')
	    continue;
	asprintf(&path, "%s/%s", dirpath, buf);

	/*
	 * Figure out if this element is part of the path to the current
	 * page.
	 */
	if (strstr(FilePath, buf))
	    selected = " class=\"selected\"";
	else
	    selected = " class=\"unselected\"";

	/*
	 * Generate the visible label
	 */
	if (strchr(buf, ' ')) {
	    nameplate = strchr(buf, ' ');
	    while (*nameplate == ' ' || *nameplate == '\t')
		*nameplate++ = 0;
	    nameplate = strdup(nameplate);
	} else if (strchr(buf, '\t')) {
	    nameplate = strchr(buf, ' ');
	    while (*nameplate == ' ' || *nameplate == '\t')
		*nameplate++ = 0;
	    nameplate = strdup(nameplate);
	} else if (strrchr(buf, '/')) {
	    nameplate = strdup(strrchr(buf, '/') + 1);
	} else {
	    nameplate = strdup(buf);
	}
	if (isalpha(nameplate[0]))
	    nameplate[0] = toupper(nameplate[0]);
	if (strchr(nameplate, '.'))
	    *strchr(nameplate, '.') = 0;

	/*
	 * Process a URL, directory, or terminal file
	 */
	printf("<tr><td%s>", selected);
	if (layer)
	    printf("&nbsp;&nbsp;&nbsp;");

	if (stat(path, &st) < 0) {
	    /*
	     * Assume a full URL
	     */
	    printf("<a href=\"%s%s\">%s</a>",
		buf, QueryString, nameplate);
	} else if (S_ISDIR(st.st_mode)) {
	    /*
	     * Assume directory containing sub-menu
	     */
	    printf("<a href=\"%s%s/%s%s\">%s</a>",
		SITE_ROOT, relpath, buf, QueryString, nameplate);
	    printf("<tr><td>");
	    generate_menu_layer(path, baselen, layer + 1);
	    printf("</td></tr>");
	} else {
	    /*
	     * Assume terminal element
	     */
	    printf("<a href=\"%s%s/%s%s\">%s</a>",
		SITE_ROOT, relpath, buf, QueryString, nameplate);
	}
	printf("</td></tr>\n");
	free(nameplate);
    }
    printf("\t<tr><td width=\"100%%\">&nbsp;</td></tr>\n");
    printf("</table>\n");
}

static void
process_vars(const char *ptr, int len)
{
    int i;
    int j;
    char *varName;
    char *varData;

    while (len) {
	varName = NULL;
	varData = NULL;
	for (j = 0; j < len && ptr[j] != '&'; ++j)
	    ;
	for (i = 0; i < j && ptr[i] != '='; ++i)
	    ;
	asprintf(&varName, "%*.*s", i, i, ptr);
	if (i < j)
	    asprintf(&varData, "%*.*s", j - i - 1, j - i - 1, ptr + i + 1);
	else
	    asprintf(&varData, "");
	process_element(varName, varData);
	free(varName);
	free(varData);
	if (j < len)
	    ++j;
	len -= j;
	ptr += j;
    }
}

static void
process_element(char *varName, char *varData)
{
#if 0
    if (strcmp(varName, "test") == 0)
	TestMode = 1;
#endif
}

static void
process_command(const char *cmd, char *args)
{
    if (strcmp(cmd, "TITLE") == 0) {
	Title = safe_strdup(args);
    }
}

const char *
generate_query_string(void)
{
#if 0
    if (TestMode)
	return("?test=1");
    else
	return("");
#endif
    return("");
}

static void
read_all(int fd, void *buf, int n)
{
    int r;
    int t = 0;

    while (n > 0) {
	r = read(fd, buf, n);
	if (r <= 0) {
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
char *
choppath(const char *path)
{
    const char *ptr;
    char *nptr;

    if ((ptr = strrchr(path, '/')) != NULL) {
	nptr = safe_malloc(ptr - path + 1);
	bcopy(path, nptr, ptr - path);
	nptr[ptr - path] = 0;
    } else {
	nptr = NULL;
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
