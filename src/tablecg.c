/*
 * TABLECG.C
 *
 *	This is used to synthesize the table structure surrounding the site
 *	pages, to highlight Horizontal or Vertical selection features, and
 *	to track selections by modifying embedded LOCALLINK() directives.
 *
 *
 * $DragonFly: site/src/tablecg.c,v 1.3 2003/07/24 02:13:42 cvstest Exp $
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

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

static void generate_top_headers(void);
static void generate_side_headers(void);
static void process_vars(const char *ptr, int len);
static void process_command(char *cmd, char *args);
static void *safe_malloc(int bytes);
static char *safe_strdup(const char *str);
static void read_all(int fd, void *buf, int n);
static void buildout(const char *ptr, int len);
static void buildflush(void);
static const char *choppath(const char *path);
static const char *filecomp(const char *path);

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
    printf("Content-Type: text/html\r\n");
    printf("\r\n");

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
    fflush(stdout);

    /*
     * Generate table structure
     */
    printf("<HTML>\n");
    printf("<HEAD>\n");
    printf("<TITLE></TITLE>\n");
    printf("</HEAD>\n");
    printf("<BODY>\n");
    /*
     * Process body
     */
    if (FilePath[0] && (fi = fopen(FilePath, "r")) != NULL) {
	char buf[256];
	char *las;
	char *ptr;

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
    printf("<TABLE BORDER=0 WIDTH=100%% BGCOLOR=\"#b0b0e0\">\n");
    printf("<TR><TD><IMG SRC=\"/smalldf.jpg\"></TD>");
    printf("<TD WIDTH=100%%>");
	printf("<TABLE BORDER=0 WIDTH=100%%>");
	printf("<TR><TD ALIGN=center WIDTH=100%%>");
	if (Title) {
	    printf("<H2>%s</H2>", Title);
	}
	printf("</TD></TR>");
	printf("<TR><TD ALIGN=left>");
	generate_top_headers();
	printf("</TD></TR>\n");
	printf("</TABLE>");
    printf("</TD></TR>");
    printf("<TR><TD VALIGN=top>");
    generate_side_headers();
    printf("</TD><TD WIDTH=100%%  BGCOLOR=\"#c0c0ff\">");
    fflush(stdout);
    buildflush();
    printf("<PRE>\n");
    fflush(stdout);
    printf("</PRE>\n");

    /*
     * Finish table structure and add terminators.
     */
    printf("</TD></TR></TABLE>\n");
    printf("</BODY>\n");
    printf("</HTML>\n");
    return(0);
}

/*
 * The headers along the top are synthesized from the directory level
 * just above the file.
 */
static void
generate_top_headers(void)
{
    DIR *dir;
    struct dirent *den;

    printf("<TABLE BORDER=0 CELLPADDING=4>");
    if ((dir = opendir(ParentDir)) != NULL) {
	printf("<TR>");
	while ((den = readdir(dir)) != NULL) {
	    const char *bgcolor = "";

	    if (den->d_name[0] == '.')
		continue;
	    if (strchr(den->d_name, '.'))
		continue;
	    if (strcmp(den->d_name, "CVS") == 0)
		continue;
	    if (den->d_type != DT_DIR)
		continue;
	    if (strcmp(den->d_name, DirName) == 0)
		bgcolor = " BGCOLOR=\"lightgreen\"";
	    printf("<TD%s><H2><A HREF=\"../%s\">%s</A></H2></TD>", 
		bgcolor, den->d_name, den->d_name);
	}
	printf("<TD WIDTH=100%%></TD>");
	printf("</TR>");
	closedir(dir);
    }
    printf("</TABLE>");
}

static void
generate_side_headers(void)
{
    DIR *dir;
    struct dirent *den;

    printf("<TABLE BORDER=\"0\">");
    if ((dir = opendir(DirPath)) != NULL) {
	while ((den = readdir(dir)) != NULL) {
 	    int len;
	    const char *ptr;
	    const char *bgcolor = "";

	    if (den->d_name[0] == '.')
		continue;
	    if (den->d_type != DT_REG)
		continue;
	    if (strcmp(den->d_name, FileName) == 0)
		bgcolor = " BGCOLOR=\"lightgreen\"";

	    if ((ptr = strchr(den->d_name, '.')) != NULL &&
		(strcmp(ptr + 1, "cgi") == 0 ||
		 strcmp(ptr + 1, "html") == 0)
	    ) {
		len = ptr - den->d_name;
		printf("<TR><TD%s><H2><A HREF=\"%s\">%*.*s</A></H2></TD></TR>\n",
		    bgcolor,
		    den->d_name,
		    len, len, den->d_name);
	    }
	}
	closedir(dir);
    }
    printf("</TABLE>\n");
}

static void
process_vars(const char *ptr, int len)
{
}

static void
process_command(char *cmd, char *args)
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
	OBuf = malloc(OMax);
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
	nptr = malloc(ptr - path + 1);
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
    const char *str;

    if ((str = strdup(ptr)) == NULL) {
	fprintf(stderr, "%s: malloc() failed page %s\n", Av0, FilePath);
	exit(1);
    }
}

