/*
 * Copyright (c) 2001, Derek Price and others
 * Copyright (c) 1992, Brian Berliner and Jeff Polk
 * Copyright (c) 1989-1992, Brian Berliner
 *
 * You may distribute under the terms of the GNU General Public License as
 * specified in the README file that comes with the CVS kit.
 */

/* CVSroot data structures */

/* Access method specified in CVSroot. */
typedef enum {
    null_method = 0,
    local_method,
    server_method,
    pserver_method,
    kserver_method,
    gserver_method,
    ext_method,
    fork_method
} CVSmethod;
extern const char method_names[][16];	/* change this in root.c if you change
					   the enum above */

typedef struct cvsroot_s {
    char *original;		/* The complete source CVSroot string. */
    CVSmethod method;		/* One of the enum values above. */
    char *directory;		/* The directory name. */
#ifdef CLIENT_SUPPORT
    char *username;		/* The username or NULL if method == local. */
    char *password;		/* The password or NULL if method == local. */
    char *hostname;		/* The hostname or NULL if method == local. */
    char *cvs_rsh;		/* The $CVS_RSH or NULL if method != ext. */
    char *cvs_server;		/* The $CVS_SERVER or NULL if
				 * method != ext and method != fork. */
    int port;			/* The port or zero if method == local. */
    char *proxy_hostname;	/* The hostname of the proxy server, or NULL
				 * when method == local or no proxy will be
				 * used.
				 */
    int proxy_port;		/* The port of the proxy or zero, as above. */
    unsigned char isremote;	/* Nonzero if we are doing remote access. */
    bool redirect;		/* False if we are to disable redirects. */
#endif /* CLIENT_SUPPORT */
} cvsroot_t;

extern cvsroot_t *current_parsed_root;
extern const cvsroot_t *original_parsed_root;

cvsroot_t *Name_Root (const char *dir, const char *update_dir);
void free_cvsroot_t (cvsroot_t *root_in);
cvsroot_t *parse_cvsroot (const char *root)
	__attribute__ ((__malloc__));
cvsroot_t *local_cvsroot (const char *dir)
	__attribute__ ((__malloc__));
void Create_Root (const char *dir, const char *rootdir);
void root_allow_add (const char *);
void root_allow_free (void);
bool root_allow_ok (const char *);
struct config *get_root_allow_config (const char *arg);
const char *primary_root_translate (const char *root_in);
const char *primary_root_inverse_translate (const char *root_in);
