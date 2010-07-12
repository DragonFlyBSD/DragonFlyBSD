#include <sys/types.h>
#include <sys/device.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/queue.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <libprop/proplib.h>
#include <sys/udev.h>

#define	LISTEN_SOCKET_FILE	"/tmp/udevd.socket"
#define SOCKFILE_NAMELEN	strlen(LISTEN_SOCKET_FILE)+1

int conn_local_server(const char *sockfile, int socktype, int nonblock,
		  int *retsock);
prop_dictionary_t udevd_get_command_dict(char *command);
void udevd_request_devs(int s);

struct udev {
	int	gp_fd;
	int	monitor_fd;
	int	refs;

	void	*userdata;
};

struct udev_enumerate {
	struct udev	*udev_ctx;
	prop_array_t	pa;
	int	refs;
	TAILQ_HEAD(, udev_list_entry)	list_entries;
};

struct udev_list_entry {
	prop_dictionary_t	dict;
	TAILQ_ENTRY(udev_list_entry)	link;
};

struct udev_monitor {
	struct udev	*udev_ctx;
	prop_array_t	ev_filt;
	int	socket;
	int	user_socket; /* maybe... one day... */
	int	refs;
};

struct udev_device {
	struct udev	*udev_ctx;
	prop_dictionary_t	dict;
	int	ev_type;
	int	refs;
};

struct udev *
udev_ref(struct udev *udev_ctx)
{
	atomic_add_int(&udev_ctx->refs, 1);

	return udev_ctx;
}

void
udev_unref(struct udev *udev_ctx)
{
	int refcount;

	refcount = atomic_fetchadd_int(&udev_ctx->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_ctx->refs, 0x400); /* in destruction */
		if (udev_ctx->gp_fd != -1)
			close (udev_ctx->gp_fd);
		if (udev_ctx->monitor_fd != -1)
			close (udev_ctx->monitor_fd);

		free(udev_ctx);
	}
}

struct udev *
udev_new()
{
	struct udev *udev_ctx;

	udev_ctx = malloc(sizeof(struct udev));

	udev_ctx->refs = 1;
	udev_ctx->gp_fd = -1;
	udev_ctx->monitor_fd = -1;
	udev_ctx->userdata = NULL;

	return udev_ctx;
}

const char *udev_get_dev_path(struct udev *udev_ctx __unused)
{
	return "/dev";
}

void *
udev_get_userdata(struct udev *udev_ctx)
{
	return udev_ctx->userdata;
}

void
udev_set_userdata(struct udev *udev_ctx, void *userdata)
{
	udev_ctx->userdata = userdata;
}

struct udev_enumerate *
udev_enumerate_new(struct udev *udev_ctx)
{
	struct udev_enumerate *udev_enum;

	udev_enum = malloc(sizeof(struct udev_enumerate));

	udev_enum->refs = 1;
	udev_enum->pa = NULL;
	TAILQ_INIT(&udev_enum->list_entries);
	udev_enum->udev_ctx = udev_ref(udev_ctx);
}

struct udev_enumerate *
udev_enumerate_ref(struct udev_enumerate *udev_enum)
{
	atomic_add_int(&udev_enum->refs, 1);

	return udev_enum;
}

void
udev_enumerate_unref(struct udev_enumerate *udev_enum)
{
	struct udev_list_entry	*le;
	int refcount;

	refcount = atomic_fetchadd_int(&udev_enum->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_enum->refs, 0x400); /* in destruction */
		if (udev_enum->pa != NULL)
			prop_object_release(udev_enum->pa);

		while (!TAILQ_EMPTY(&udev_enum->list_entries)) {
			le = TAILQ_FIRST(&udev_enum->list_entries);
			TAILQ_REMOVE(&udev_enum->list_entries, le, link);
			prop_object_release(le->dict);
			free(le);
		}
		udev_unref(udev_enum->udev_ctx);
		free(udev_enum);
	}
}

struct udev *
udev_enumerate_get_udev(struct udev_enumerate *udev_enum)
{
	return udev_enum->udev_ctx;
}

int
udev_enumerate_scan_devices(struct udev_enumerate *udev_enum)
{
	prop_array_t	pa;

	if (udev_enum->udev_ctx->gp_fd == -1)
		return -1;

	pa = udevd_request_devs(udev_enum->udev_ctx->gp_fd);
	if (pa == NULL)
		return -1;

	prop_object_retain(pa);

	if (udev_enum->pa != NULL)
		prop_object_release(udev_enum->pa);

	udev_enum->iter = NULL;
	udev_enum->pa = pa;

	return 0;
}

struct udev_list_entry *
udev_enumerate_get_list_entry(struct udev_enumerate *udev_enum)
{
	struct udev_list_entry *le;
	prop_object_iterator_t	iter;

	/* If the list is not empty, assume it was populated in an earlier call */
	if (!TAILQ_EMPTY(&udev_enum->list_entries))
		return TAILQ_FIRST(&udev_enum->list_entries);

	iter = prop_array_iterator(udev_enum->pa);
	if (iter == NULL)
		return NULL;

	while ((dict = prop_object_iterator_next(iter)) != NULL) {
		le = malloc(sizeof(struct udev_list_entry));
		if (le == NULL)
			goto out;

		prop_object_retain(dict);
		le->dict = dict;
		TAILQ_INSERT_TAIL(&udev_enum->list_entries, le, link);
	}

	le = TAILQ_FIRST(&udev_enum->list_entries);

out:
	prop_object_iterator_release(iter);
	return le;
}

prop_array_t
udev_enumerate_get_array(struct udev_enumerate *udev_enum)
{
	return udev_enum->pa;
}

struct udev_list_entry *
udev_list_entry_get_next(struct udev_list_entry *list_entry)
{
	return TAILQ_NEXT(list_entry, link);
}

prop_dictionary_t
udev_list_entry_get_dictionary(struct udev_list_entry *list_entry)
{
	return list_entry->dict;
}

#define	udev_list_entry_foreach(list_entry, first_entry) \
	for(list_entry = first_entry; \
	    list_entry != NULL; \
	    list_entry = udev_list_entry_get_next(list_entry))






struct udev_monitor *
udev_monitor_new(struct udev *udev_ctx)
{
	struct udev_monitor *udev_monitor;
	int ret, s;

	ret = conn_local_server(LISTEN_SOCKET_FILE, SOCK_STREAM, 0, &s);
	if (ret < 0)
		return NULL;

	udev_monitor = malloc(sizeof(struct udev_monitor));
	if (udev_monitor == NULL)
		return NULL;

	udev_monitor->refs = 1;
	udev_monitor->ev_filt = NULL;
	udev_monitor->socket = s;
	udev_monitor->user_socket = 1;
	udev_monitor->udev_ctx = udev_ref(udev_ctx);

	return udev_monitor;
}


struct udev_monitor *
udev_monitor_ref(struct udev_monitor *udev_monitor)
{
	atomic_add_int(&udev_monitor->refs, 1);

	return udev_monitor;
}

void
udev_monitor_unref(struct udev_monitor *udev_monitor)
{
	int refcount;

	refcount = atomic_fetchadd_int(&udev_monitor->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_monitor->refs, 0x400); /* in destruction */
		if (udev_monitor->ev_filt != NULL)
			prop_object_release(udev_monitor->ev_filt);

		if (udev_monitor->socket != -1)
			close(udev_monitor->socket);
		if (udev_monitor->user_socket != -1)
			close(udev_monitor->user_socket);

		udev_unref(udev_monitor->udev_ctx);
		free(udev_monitor);
	}
}

struct udev *
udev_monitor_get_udev(struct udev_monitor *udev_monitor)
{
	return udev_monitor->udev_ctx;
}

int
udev_monitor_get_fd(struct udev_monitor *udev_monitor)
{
	return udev_monitor->socket;
}

struct udev_device *
udev_monitor_receive_device(struct udev_monitor *udev_monitor)
{
	struct udev_device *udev_dev;
	prop_dictionary_t dict;
	prop_number_t	pn;
	char *xml;
	int n, evtype;

	xml = malloc(12*1024*1024);
	if (xml == NULL)
		return NULL;

	if ((n = read_xml(udev_monitor->socket, xml, 12*1024*1024)) <= 0) {
		free(xml);
		return NULL;
	}

	xml[n+1] = '\0';
	dict = prop_dictionary_internalize(xml);
	free(xml);
	if (dict == NULL)
		return NULL;

	pn = prop_dictionary_get(dict, "evtype");
	if (pn == NULL) {
		prop_object_release(dict);
		return NULL;
	}

	udev_dev = malloc(sizeof(struct udev_dev));
	if (udev_dev == NULL) {
		prop_object_release(dict);
		return NULL;
	}

	udev_dev->refs = 1;
	udev_dev->ev_type = prop_number_integer_value(pn);
	udev_dev->dict = prop_dictionary_get(dict, "evdict");
	if (udev_dev->dict == NULL) {
		free(udev_dev);
		return NULL;
	}
	udev_dev->udev_ctx = udev_ref(udev_monitor->udev_ctx);

out:
	return udev_dev;
}

int
udev_monitor_enable_receiving(struct udev_monitor *udev_monitor)
{
	prop_dictionary_t	dict;
	char *xml;
	/* ->socket, ->user_socket, ->ev_filt */

	dict = udevd_get_command_dict(__DECONST(char *, "monitor"));
	if (dict == NULL)
		return -1;

	/* Add event filters to message, if available */
	if (udev_monitor->ev_filt != NULL) {
		if (prop_dictionary_set(dict, "filters",
		    udev_monitor->ev_filt) == false) {
			prop_object_release(dict);
			return -1;
		}
	}

	xml = prop_dictionary_externalize(dict);
	prop_object_release(dict);
	if (xml == NULL)
		return -1;

	n = send_xml(udev_monitor->socket, xml);
	free(xml);
	if (n <= 0)
		return NULL;

	return 0;
}

int
udev_monitor_filter_add_match_subsystem_devtype(struct udev_monitor *udev_monitor,
						const char *subsystem,
						const char *devtype __unused)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_WILDCARD,
						 0,
						 "subsystem",
						 subsystem);

	return ret;
}

int
udev_monitor_filter_add_match_expr(struct udev_monitor *udev_monitor,
			      	   const char *key,
			      	   char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_WILDCARD,
						 0,
						 key,
						 expr);

	return ret;
}

int
udev_monitor_filter_add_nomatch_expr(struct udev_monitor *udev_monitor,
			      	     const char *key,
			      	     char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_WILDCARD,
						 1,
						 key,
						 expr);

	return ret;
}

int
udev_monitor_filter_add_match_regex(struct udev_monitor *udev_monitor,
			      	   const char *key,
			      	   char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_REGEX,
						 0,
						 key,
						 expr);

	return ret;
}

int
udev_monitor_filter_add_nomatch_regex(struct udev_monitor *udev_monitor,
			      	     const char *key,
			      	     char *expr)
{
	int ret;

	ret = _udev_monitor_filter_add_match_gen(udev_monitor,
						 EVENT_FILTER_TYPE_REGEX,
						 1,
						 key,
						 expr);

	return ret;
}

int
_udev_monitor_filter_add_match_gen(struct udev_monitor *udev_monitor,
				   int type,
				   int neg,
				   const char *key,
				   char *expr)
{
	prop_array_t		pa;
	prop_dictionary_t	dict;
	int error;

	if (subsystem == NULL)
		return NULL;

	dict = prop_dictionary_create();
	if (dict == NULL)
		return -1;

	error = _udev_dict_set_cstr(dict, "key", key);
	if (error != 0)
		goto error_out;
	error = _udev_dict_set_int(dict, "type", type);
	if (error != 0)
		goto error_out;
	error = _udev_dict_set_int(dict, "expr", expr);
	if (error != 0)
		goto error_out;

	if (neg) {
		error = _udev_dict_set_int(dict, "negative", 1);
		if (error != 0)
			goto error_out;
	}

	if (udev_monitor->ev_filt == NULL) {
		pa = prop_array_create();
		if (pa == NULL)
			goto error_out;

		udev_monitor->ev_filt = pa;
	}

	if (prop_array_add(udev_monitor->ev_filt, dict) == false)
		goto error_out;

	return 0;

error_out:
	prop_object_release(dict);
	return -1;
}

struct udev_device *
udev_device_ref(struct udev_device *udev_device)
{
	atomic_add_int(&udev_device->refs, 1);

	return udev_device;
}

void
udev_device_unref(struct udev_device *udev_device)
{
	int refcount;

	refcount = atomic_fetchadd_int(&udev_device->refs, -1);

	if (refcount == 1) {
		atomic_subtract_int(&udev_device->refs, 0x400); /* in destruction */
		if (udev_device->dict != NULL)
			prop_object_release(udev_device->dict);

		udev_unref(udev_device->udev_ctx);
		free(udev_device);
	}
}

prop_dictionary_t
udev_device_get_dictionary(struct udev_device *udev_device)
{
	return udev_device->dict;
}

struct udev *
udev_device_get_udev(struct udev_device *udev_device)
{
	return udev_device->udev_ctx;
}

int
send_xml(int s, char *xml)
{
	ssize_t r,n;
	size_t sz;

	sz = strlen(xml) + 1;

	r = send(s, &sz, sizeof(sz), 0);
	if (r <= 0)
		return r;

	r = 0;
	while (r < (ssize_t)sz) {
		n = send(s, xml+r, sz-r, 0);
		if (n <= 0)
			return n;
		r += n;
	}

	return r;
}

int
read_xml(int s, char *buf, size_t buf_sz)
{
	size_t sz;
	int n, r;

	n = recv(s, &sz, sizeof(sz), MSG_WAITALL);
	if (n <= 0)
		return n;

	r = 0;
	while ((r < (ssize_t)sz) && (r < (ssize_t)buf_sz)) {
		n = recv(s, buf+r, sz-r, MSG_WAITALL);
		if (n <= 0)
			return n;
		r += n;
	}

	return r;
}



static int
_udev_dict_set_cstr(prop_dictionary_t dict, const char *key, char *str)
{
	prop_string_t	ps;

	ps = prop_string_create_cstring(str);
	if (ps == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, ps) == false) {
		prop_object_release(ps);
		return ENOMEM;
	}

	prop_object_release(ps);
	return 0;
}

static int
_udev_dict_set_int(prop_dictionary_t dict, const char *key, int64_t val)
{
	prop_number_t	pn;

	pn = prop_number_create_integer(val);
	if (pn == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, pn) == false) {
		prop_object_release(pn);
		return ENOMEM;
	}

	prop_object_release(pn);
	return 0;
}

static int
_udev_dict_set_uint(prop_dictionary_t dict, const char *key, uint64_t val)
{
	prop_number_t	pn;

	pn = prop_number_create_unsigned_integer(val);
	if (pn == NULL)
		return ENOMEM;

	if (prop_dictionary_set(dict, key, pn) == false) {
		prop_object_release(pn);
		return ENOMEM;
	}

	prop_object_release(pn);
	return 0;
}

int
conn_local_server(const char *sockfile, int socktype, int nonblock,
		  int *retsock)
{
	int s;
	struct sockaddr_un serv_addr;

	*retsock = -1;
	if ((s = socket(AF_UNIX, socktype, 0)) < 0)
		return -1;

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;
	strncpy(serv_addr.sun_path, sockfile, SOCKFILE_NAMELEN);
	serv_addr.sun_path[SOCKFILE_NAMELEN - 1] = '\0';

	if (nonblock && unblock_descriptor(s) < 0) {
		close(s);
		return -1;
	}

	*retsock = s;
	return connect(s, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
}

prop_dictionary_t
udevd_get_command_dict(char *command)
{
	prop_dictionary_t	dict;
	int	error;

	dict = prop_dictionary_create();
	if (dict == NULL)
		return NULL;

	if ((error = _udev_dict_set_cstr(dict, "command", command)))
		goto error_out;

	return dict;

error_out:
	prop_object_release(dict);
	return NULL;
}

prop_array_t
udevd_request_devs(int s)
{
	prop_array_t	pa;
	prop_dictionary_t	dict;
	char *xml;

	int n, t;

	dict = udevd_get_command_dict(__DECONST(char *, "getdevs"));
	if (dict == NULL)
		return NULL;

	xml = prop_dictionary_externalize(dict);
	prop_object_release(dict);
	if (xml == NULL)
		return NULL;

	n = send_xml(s, xml);
	free(xml);

	if (n <= 0)
		return NULL;

	xml = malloc(12*1024*1024); /* generous 12 MB */
	if ((n = read_xml(s, xml, 12*1024*1024)) <= 0) {
		free(xml);
		return NULL;
	}

	xml[n+1] = '\0';
	pa = prop_array_internalize(xml);
	free(xml);
	return (pa);
}



int
main(void)
{
	int ret, s;

	ret = conn_local_server(LISTEN_SOCKET_FILE, SOCK_STREAM, 0, &s);
	if (ret < 0)
		err(1, "conn_local_server");

	udevd_request_devs(s);

	return 0;
}
