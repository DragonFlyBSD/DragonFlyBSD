#include <stdio.h>

#include <string.h>
#include "dfui/dfui.h"

/*
 *
 *
 * Example for how to communicate with a DFUI backend from within a CGI.
 * Not currently working (missing the bit where we send back a response,)
 * but it shows the outline.
 *
 * Only seems to work with thttpd (where user= the user that the DFUI
 * backend is running as;) had no luck with Apache.
 *
 *
 */

static struct dfui_response *
create_response_from_posted_data(void)
{
	struct dfui_response *r;
		
	r = dfui_response_new("the_form", "the_button_was_clicked");
	/* add the contents of all fields to r's dataset(s) */

	return(r);
}

static void
display_HTML_form_based_on(struct dfui_form *f __unused)
{
	printf("<p>foo.</p>");
}

static int
request_method_is(const char *method)
{
	/* XXX */
	if (!strcasecmp(method, "plugh"))
		return(1);
	else
		return(0);
}

int
main(int argc __unused, char **argv __unused)
{
	struct dfui_connection *c;
	struct dfui_form *f;
	struct dfui_response *r;
	char msgtype;
	void *payload;

	printf("Content-type: text/html\r\n\r\n");
	fflush(stdout);

	c = dfui_connection_new(DFUI_TRANSPORT_CAPS, "test");
	dfui_fe_connect(c);

	printf("<html><h1>Hi!</h1>");
	fflush(stdout);

	if (request_method_is("POST")) {
		r = create_response_from_posted_data();
		dfui_fe_submit(c, r);
		dfui_response_free(r);
	}

	dfui_fe_receive(c, &msgtype, &payload);
	switch (msgtype) {
	case DFUI_BE_MSG_PRESENT:
		f = (struct dfui_form *)payload;
		display_HTML_form_based_on(f);
		dfui_form_free(f);
	}

	dfui_fe_disconnect(c);
	return(0);
}
