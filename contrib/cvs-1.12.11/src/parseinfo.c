/*
 * Copyright (c) 1992, Brian Berliner and Jeff Polk
 * Copyright (c) 1989-1992, Brian Berliner
 * 
 * You may distribute under the terms of the GNU General Public License as
 * specified in the README file that comes with the CVS source distribution.
 */

#include "cvs.h"
#include "getline.h"
#include "history.h"



/*
 * Parse the INFOFILE file for the specified REPOSITORY.  Invoke CALLPROC for
 * the first line in the file that matches the REPOSITORY, or if ALL != 0, any
 * lines matching "ALL", or if no lines match, the last line matching
 * "DEFAULT".
 *
 * Return 0 for success, -1 if there was not an INFOFILE, and >0 for failure.
 */
int
Parse_Info (const char *infofile, const char *repository, CALLPROC callproc,
            int opt, void *closure)
{
    int err = 0;
    FILE *fp_info;
    char *infopath;
    char *line = NULL;
    size_t line_allocated = 0;
    char *default_value = NULL;
    int default_line = 0;
    char *expanded_value;
    int callback_done, line_number;
    char *cp, *exp, *value;
    const char *srepos;
    const char *regex_err;

    if (current_parsed_root == NULL)
    {
	/* XXX - should be error maybe? */
	error (0, 0, "CVSROOT variable not set");
	return 1;
    }

    /* find the info file and open it */
    infopath = xmalloc (strlen (current_parsed_root->directory)
			+ strlen (infofile)
			+ sizeof (CVSROOTADM)
			+ 3);
    (void) sprintf (infopath, "%s/%s/%s", current_parsed_root->directory,
		    CVSROOTADM, infofile);
    fp_info = CVS_FOPEN (infopath, "r");
    if (fp_info == NULL)
    {
	/* If no file, don't do anything special.  */
	if (!existence_error (errno))
	    error (0, errno, "cannot open %s", infopath);
	free (infopath);
	return 0;
    }

    /* strip off the CVSROOT if repository was absolute */
    srepos = Short_Repository (repository);

    TRACE (1, "Parse_Info (%s, %s, %s)",
	   infopath, srepos,  (opt & PIOPT_ALL) ? "ALL" : "not ALL");

    /* search the info file for lines that match */
    callback_done = line_number = 0;
    while (getline (&line, &line_allocated, fp_info) >= 0)
    {
	line_number++;

	/* skip lines starting with # */
	if (line[0] == '#')
	    continue;

	/* skip whitespace at beginning of line */
	for (cp = line; *cp && isspace ((unsigned char) *cp); cp++)
	    ;

	/* if *cp is null, the whole line was blank */
	if (*cp == '\0')
	    continue;

	/* the regular expression is everything up to the first space */
	for (exp = cp; *cp && !isspace ((unsigned char) *cp); cp++)
	    ;
	if (*cp != '\0')
	    *cp++ = '\0';

	/* skip whitespace up to the start of the matching value */
	while (*cp && isspace ((unsigned char) *cp))
	    cp++;

	/* no value to match with the regular expression is an error */
	if (*cp == '\0')
	{
	    error (0, 0, "syntax error at line %d file %s; ignored",
		   line_number, infopath);
	    continue;
	}
	value = cp;

	/* strip the newline off the end of the value */
	if ((cp = strrchr (value, '\n')) != NULL)
	    *cp = '\0';

	/*
	 * At this point, exp points to the regular expression, and value
	 * points to the value to call the callback routine with.  Evaluate
	 * the regular expression against srepos and callback with the value
	 * if it matches.
	 */

	/* save the default value so we have it later if we need it */
	if (strcmp (exp, "DEFAULT") == 0)
	{
	    if (default_value != NULL)
	    {
		error (0, 0, "Multiple `DEFAULT' lines (%d and %d) in %s file",
		       default_line, line_number, infofile);
		free (default_value);
	    }
	    default_value = xstrdup (value);
	    default_line = line_number;
	    continue;
	}

	/*
	 * For a regular expression of "ALL", do the callback always We may
	 * execute lots of ALL callbacks in addition to *one* regular matching
	 * callback or default
	 */
	if (strcmp (exp, "ALL") == 0)
	{
	    if (!(opt & PIOPT_ALL))
		error (0, 0, "Keyword `ALL' is ignored at line %d in %s file",
		       line_number, infofile);
	    else if ((expanded_value = expand_path (value, infofile,
	                                            line_number, 1))
	             != NULL )
	    {
		err += callproc (repository, expanded_value, closure);
		free (expanded_value);
	    }
	    else
		err++;
	    continue;
	}

	if (callback_done)
	    /* only first matching, plus "ALL"'s */
	    continue;

	/* see if the repository matched this regular expression */
	if ((regex_err = re_comp (exp)) != NULL)
	{
	    error (0, 0, "bad regular expression at line %d file %s: %s",
		   line_number, infofile, regex_err);
	    continue;
	}
	if (re_exec (srepos) == 0)
	    continue;				/* no match */

	/* it did, so do the callback and note that we did one */
	if ((expanded_value = expand_path( value, infofile, line_number, 1)
	    ) != NULL)
	{
	    err += callproc (repository, expanded_value, closure);
	    free (expanded_value);
	}
	else
	    err++;
	callback_done = 1;
    }
    if (ferror (fp_info))
	error (0, errno, "cannot read %s", infopath);
    if (fclose (fp_info) < 0)
	error (0, errno, "cannot close %s", infopath);

    /* if we fell through and didn't callback at all, do the default */
    if (callback_done == 0 && default_value != NULL)
    {
	if ((expanded_value = expand_path (default_value, infofile,
	                                   line_number, 1)
	    ) != NULL)
	{
	    err += callproc (repository, expanded_value, closure);
	    free (expanded_value);
	}
	else
	    err++;
    }

    /* free up space if necessary */
    if (default_value != NULL)
	free (default_value);
    free (infopath);
    if (line != NULL)
	free (line);

    return err;
}



/* Print a warning and return false if P doesn't look like a string specifying
 * something that can be converted into a size_t.
 *
 * Sets *VAL to the parsed value when it is found to be valid.  *VAL will not
 * be altered when false is returned.
 */
static bool
readSizeT (const char *infopath, const char *option, const char *p,
	   size_t *val)
{
    const char *q;
    size_t num, factor = 1;

    if (!strcasecmp ("unlimited", p))
    {
	*val = SIZE_MAX;
	return true;
    }

    /* Record the factor character (kilo, mega, giga, tera).  */
    if (!isdigit (p[strlen(p) - 1]))
    {
	switch (p[strlen(p) - 1])
	{
	    case 'T':
		factor = xtimes (factor, 1024);
	    case 'G':
		factor = xtimes (factor, 1024);
	    case 'M':
		factor = xtimes (factor, 1024);
	    case 'k':
		factor = xtimes (factor, 1024);
		break;
	    default:
		error (0, 0,
    "%s: Unknown %s factor: `%c'",
		       infopath, option, p[strlen(p)]);
		return false;
	}
	TRACE (TRACE_DATA, "readSizeT(): Found factor %u for %s",
	       factor, option);
    }

    /* Verify that *q is a number.  */
    q = p;
    while (q < p + strlen(p) - 1 /* Checked last character above.  */)
    {
	if (!isdigit(*q))
	{
	    error (0, 0,
"%s: %s must be a postitive integer, not '%s'",
		   infopath, option, p);
	    return false;
	}
	q++;
    }

    /* Compute final value.  */
    num = strtoul (p, NULL, 10);
    if (num == ULONG_MAX || num > SIZE_MAX)
	/* Don't return an error, just max out.  */
	num = SIZE_MAX;

    TRACE (TRACE_DATA, "readSizeT(): read number %u for %s", num, option);
    *val = xtimes (strtoul (p, NULL, 10), factor);
    TRACE (TRACE_DATA, "readSizeT(): returnning %u for %s", *val, option);
    return true;
}



/* Allocate and initialize a new config struct.  */
static inline struct config *
new_config (void)
{
    struct config *new = xcalloc (1, sizeof (struct config));

    TRACE (TRACE_FLOW, "new_config ()");

    new->logHistory = ALL_HISTORY_REC_TYPES;
    new->RereadLogAfterVerify = LOGMSG_REREAD_ALWAYS;
    new->UserAdminOptions = xstrdup ("k");
    new->MaxCommentLeaderLength = 20;
#ifdef PROXY_SUPPORT
    new->MaxProxyBufferSize = (size_t)(8 * 1024 * 1024); /* 8 megabytes,
                                                          * by default.
                                                          */
#endif /* PROXY_SUPPORT */
#ifdef AUTH_SERVER_SUPPORT
    new->system_auth = true;
#endif /* AUTH_SERVER_SUPPORT */

    return new;
}



void
free_config (struct config *data)
{
    if (data->keywords) free_keywords (data->keywords);
    free (data);
}



/* Parse the CVS config file.  The syntax right now is a bit ad hoc
 * but tries to draw on the best or more common features of the other
 * *info files and various unix (or non-unix) config file syntaxes.
 * Lines starting with # are comments.  Settings are lines of the form
 * KEYWORD=VALUE.  There is currently no way to have a multi-line
 * VALUE (would be nice if there was, probably).
 *
 * CVSROOT is the $CVSROOT directory
 * (current_parsed_root->directory might not be set yet, so this
 * function takes the cvsroot as a function argument).
 *
 * RETURNS
 *   Always returns a fully initialized config struct, which on error may
 *   contain only the defaults.
 *
 * ERRORS
 *   Calls error(0, ...) on errors in addition to the return value.
 *
 *   xmalloc() failures are fatal, per usual.
 */
struct config *
parse_config (const char *cvsroot)
{
    char *infopath;
    FILE *fp_info;
    char *line = NULL;
    size_t line_allocated = 0;
    size_t len;
    char *p;
    struct config *retval;

    TRACE (TRACE_FUNCTION, "parse_config (%s)", cvsroot);

    retval = new_config ();

    infopath = Xasprintf ("%s/%s/%s", cvsroot, CVSROOTADM, CVSROOTADM_CONFIG);

    fp_info = CVS_FOPEN (infopath, "r");
    if (fp_info == NULL)
    {
	/* If no file, don't do anything special.  */
	if (!existence_error (errno))
	{
	    /* Just a warning message; doesn't affect return
	       value, currently at least.  */
	    error (0, errno, "cannot open %s", infopath);
	}
	free (infopath);
	return retval;
    }

    while (getline (&line, &line_allocated, fp_info) >= 0)
    {
	/* Skip comments.  */
	if (line[0] == '#')
	    continue;

	/* At least for the moment we don't skip whitespace at the start
	   of the line.  Too picky?  Maybe.  But being insufficiently
	   picky leads to all sorts of confusion, and it is a lot easier
	   to start out picky and relax it than the other way around.

	   Is there any kind of written standard for the syntax of this
	   sort of config file?  Anywhere in POSIX for example (I guess
	   makefiles are sort of close)?  Red Hat Linux has a bunch of
	   these too (with some GUI tools which edit them)...

	   Along the same lines, we might want a table of keywords,
	   with various types (boolean, string, &c), as a mechanism
	   for making sure the syntax is consistent.  Any good examples
	   to follow there (Apache?)?  */

	/* Strip the trailing newline.  There will be one unless we
	   read a partial line without a newline, and then got end of
	   file (or error?).  */

	len = strlen (line) - 1;
	if (line[len] == '\n')
	    line[len] = '\0';

	/* Skip blank lines.  */
	if (line[0] == '\0')
	    continue;

	TRACE (TRACE_DATA, "parse_info() examining line: `%s'", line);

	/* The first '=' separates keyword from value.  */
	p = strchr (line, '=');
	if (p == NULL)
	{
	    /* Probably should be printing line number.  */
	    error (0, 0, "syntax error in %s: line '%s' is missing '='",
		   infopath, line);
	    continue;
	}

	*p++ = '\0';

	if (strcmp (line, "RCSBIN") == 0)
	{
	    /* This option used to specify the directory for RCS
	       executables.  But since we don't run them any more,
	       this is a noop.  Silently ignore it so that a
	       repository can work with either new or old CVS.  */
	    ;
	}
	else if (strcmp (line, "SystemAuth") == 0)
#ifdef AUTH_SERVER_SUPPORT
	    readBool (infopath, "SystemAuth", p, &retval->system_auth);
#else
	{
	    /* Still parse the syntax but ignore the option.  That way the same
	     * config file can be used for local and server.
	     */
	    bool dummy;
	    readBool (infopath, "SystemAuth", p, &dummy);
	}
#endif
	else if (strcmp (line, "LocalKeyword") == 0)
	    RCS_setlocalid (&retval->keywords, p);
	else if (strcmp (line, "KeywordExpand") == 0)
	    RCS_setincexc (&retval->keywords, p);
	else if (strcmp (line, "PreservePermissions") == 0)
#ifdef PRESERVE_PERMISSIONS_SUPPORT
	    readBool (infopath, "PreservePermissions", p,
		      &retval->preserve_perms);
#else
	    error (0, 0, "\
warning: this CVS does not support PreservePermissions");
#endif
	else if (strcmp (line, "TopLevelAdmin") == 0)
	    readBool (infopath, "TopLevelAdmin", p, &retval->top_level_admin);
	else if (strcmp (line, "LockDir") == 0)
	{
	    if (retval->lock_dir != NULL)
		free (retval->lock_dir);
	    retval->lock_dir = xstrdup (p);
	    /* Could try some validity checking, like whether we can
	       opendir it or something, but I don't see any particular
	       reason to do that now rather than waiting until lock.c.  */
	}
	else if (strcmp (line, "LogHistory") == 0)
	{
	    if (strcmp (p, "all") != 0)
		retval->logHistory = xstrdup (p);
	}
	else if (strcmp (line, "RereadLogAfterVerify") == 0)
	{
	    if (!strcasecmp (p, "never"))
	      retval->RereadLogAfterVerify = LOGMSG_REREAD_NEVER;
	    else if (!strcasecmp (p, "always"))
	      retval->RereadLogAfterVerify = LOGMSG_REREAD_ALWAYS;
	    else if (!strcasecmp (p, "stat"))
	      retval->RereadLogAfterVerify = LOGMSG_REREAD_STAT;
	    else
	    {
		bool tmp;
		if (readBool (infopath, "RereadLogAfterVerify", p, &tmp))
		{
		    if (tmp)
			retval->RereadLogAfterVerify = LOGMSG_REREAD_ALWAYS;
		    else
			retval->RereadLogAfterVerify = LOGMSG_REREAD_NEVER;
		}
	    }
	}
	else if (strcmp (line, "UserAdminOptions") == 0)
	    retval->UserAdminOptions = xstrdup (p);
	else if (strcmp (line, "UseNewInfoFmtStrings") == 0)
#ifdef SUPPORT_OLD_INFO_FMT_STRINGS
	    readBool (infopath, "UseNewInfoFmtStrings", p,
		      &retval->UseNewInfoFmtStrings);
#else /* !SUPPORT_OLD_INFO_FMT_STRINGS */
	{
	    bool dummy;
	    if (readBool (infopath, "UseNewInfoFmtStrings", p, &dummy)
		&& !dummy)
		error (1, 0,
"%s: Old style info format strings not supported by this executable.",
		       infopath);
	}
#endif /* SUPPORT_OLD_INFO_FMT_STRINGS */
	else if (strcmp (line, "ImportNewFilesToVendorBranchOnly") == 0)
	    readBool (infopath, "ImportNewFilesToVendorBranchOnly", p,
		      &retval->ImportNewFilesToVendorBranchOnly);
#ifdef PROXY_SUPPORT
	else if (strcmp (line, "PrimaryServer") == 0)
	{
	    retval->PrimaryServer = parse_cvsroot (p);
	    if (retval->PrimaryServer->method != fork_method
		&& retval->PrimaryServer->method != ext_method)
	    {
		/* I intentionally neglect to mention :fork: here.  It is
	         * really only useful for testing.
		 */
	        error (1, 0,
"%s: Only PrimaryServers with :ext: methods are valid, not `%s'.",
		       infopath, p);
	    }
	}
	else if (!strcmp (line, "MaxProxyBufferSize"))
	    readSizeT (infopath, "MaxProxyBufferSize", p,
		       &retval->MaxProxyBufferSize);
#endif /* PROXY_SUPPORT */
	else if (!strcmp (line, "MaxCommentLeaderLength"))
	    readSizeT (infopath, "MaxCommentLeaderLength", p,
		       &retval->MaxCommentLeaderLength);
	else if (!strcmp (line, "UseArchiveCommentLeader"))
	    readBool (infopath, "UseArchiveCommentLeader", p,
		      &retval->UseArchiveCommentLeader);
	else
	    /* We may be dealing with a keyword which was added in a
	       subsequent version of CVS.  In that case it is a good idea
	       to complain, as (1) the keyword might enable a behavior like
	       alternate locking behavior, in which it is dangerous and hard
	       to detect if some CVS's have it one way and others have it
	       the other way, (2) in general, having us not do what the user
	       had in mind when they put in the keyword violates the
	       principle of least surprise.  Note that one corollary is
	       adding new keywords to your CVSROOT/config file is not
	       particularly recommended unless you are planning on using
	       the new features.  */
	    error (0, 0, "%s: unrecognized keyword '%s'",
		   infopath, line);
    }
    if (ferror (fp_info))
	error (0, errno, "cannot read %s", infopath);
    if (fclose (fp_info) < 0)
	error (0, errno, "cannot close %s", infopath);
    free (infopath);
    if (line != NULL)
	free (line);
    return retval;
}
