/*
 * Copyright (c) 2004 Scott Ullrich <GeekGod@GeekGod.com> 
 * Portions Copyright (c) 2004 Chris Pressey <cpressey@catseye.mine.nu>
 *
 * Copyright (c) 2004 The DragonFly Project.
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Scott Ullrich and Chris Pressey (see above for e-mail addresses).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS, CONTRIBUTORS OR VOICES IN THE AUTHOR'S HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * dfuife_cgi.c
 * CGI frontend for DFUI.
 * $Id: dfuife_cgi.c,v 1.46 2005/03/09 22:55:55 cpressey Exp $
 *
 * NOTE: 
 * With CAPS, this CGI only seems to work with thttpd (where user =
 * the user that the DFUI backend is running as.)  No luck with Apache.
 */

#define SAVED_ENVIRONMENT "/tmp/cgicsave.env"
#define LINEMAX 1024   /* an arbitrary buffer size */
 
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "dfui/dfui.h"
#include "dfui/dump.h"

#include "cgic.h"

#ifndef DFUIFE_CSS
#define DFUIFE_CSS "dfuife.css"
#endif

#ifndef DFUIFE_CGI
#define DFUIFE_CGI "dfuife.cgi"
#endif

#ifndef DFUIFE_JS
#define DFUIFE_JS "dfuife.js"
#endif

static void	 signal_handler(int signo);
static void	 output_html_header(const char *title);
static void	 output_html_footer(void);
static void	 cl_progress_bar_begin(void);
static void	 create_html_from_dfui_form(const struct dfui_form *f);
static void	 create_html_from_dfui_form_single(const struct dfui_form *f);
static void	 create_html_from_dfui_form_multiple(const struct dfui_form *f);
static void	 cl_progress_bar_stop(void);
static void	 cl_progress_bar_update(int percent, const char *pbstatus);
static void	 convert_nl_to_br(const char *string_to_convert);
static int 	 field_name_has_type(const char *arrayFieldName);

int headers_outputted = 0;
int last_progress_amount = -1;
int is_streaming_progress_bar = 0;

volatile sig_atomic_t caught_signal;

static void
signal_handler(int signo)
{
	caught_signal = signo;
}

static void
output_html_header(const char *title)
{
	struct tm *tp;
	time_t now;

	if (headers_outputted == 1)
		return;

	headers_outputted = 1;

	setvbuf(cgiOut, NULL, _IONBF, 0);

	now = time(NULL);
	tp = localtime(&now); 
	fprintf(cgiOut, "Cache-Control: no-cache\n");
	fprintf(cgiOut, "Cache-Control: no-store\n");
	fprintf(cgiOut, "Pragma: no-cache\n");
	fprintf(cgiOut, "Content-type: text/html\n\n");
	fprintf(cgiOut, "\n<?xml version='1.0' encoding='UTF-8'?>\n");
	fprintf(cgiOut, "\n<!DOCTYPE html ");
	fprintf(cgiOut, "PUBLIC '-//W3C//Dtd XHTML 1.0 Strict//EN'");
	fprintf(cgiOut, " 'http://www.w3.org/tr/xhtml1/Dtd/xhtml1-strict.dtd'>\n");
	fprintf(cgiOut, "\n<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en' lang='en'>\n");
	fprintf(cgiOut, "\n<head>");
	fprintf(cgiOut, "\n<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />");
	fprintf(cgiOut, "\n<title>%s</title>", title);
	fprintf(cgiOut, "<link rel=\"stylesheet\" type=\"text/css\" href=\"" DFUIFE_CSS "\" /> \n ");
	fprintf(cgiOut, "\n</head>");
	fprintf(cgiOut, "\n<body onunload=\"if(loaded == 0) { event.returnValue = 'An install in progress.'; } \"> ");
	fprintf(cgiOut, "\n<form name='dfuife' id='dfuife' method='post' action='" DFUIFE_CGI "'>\n");

	/* javascript form helper functions */
	fprintf(cgiOut, "\n<script type=\"text/javascript\" language=\"javascript\" src=\"" DFUIFE_JS "\">\n</script>\n");

	fprintf(cgiOut, "\n<div class=\"main\">\n");
	
	fprintf(cgiOut, "<div class=\"maintitle\">\n");
	fprintf(cgiOut, "	DragonFly Installer\n");
	fprintf(cgiOut, "</div>\n");

	fprintf(cgiOut, "<div class=\"title\">\n");
	fprintf(cgiOut, "	%s\n",title);
	fprintf(cgiOut, "</div>\n");
}

static void
output_html_footer(void)
{
	fprintf(cgiOut, "\n</tbody>\n</table>\n");
	fprintf(cgiOut, "\n</div>\n");
	fprintf(cgiOut, "\n</div>\n");
	fprintf(cgiOut, "\n</form>\n");
	fprintf(cgiOut, "\n\n<script type=\"text/javascript\" language='javascript'>\nloaded = 1;\n</script>\n");
	fprintf(cgiOut, "\n<p />");
	fprintf(cgiOut, "\n</body>\n</html>\n");
}

static void
create_html_from_dfui_form_single(const struct dfui_form *f)
{
	struct dfui_action *a;
	struct dfui_celldata *cd;
	struct dfui_field *fi;
	const char *field_id;
	const char *field_name;
	const char *field_descr;
	const char *field_value;
	const char *field_type;
	const char *form_name;
	const char *form_short_description;
	struct dfui_option *o;
	const char *help_text;

	help_text = dfui_info_get_long_desc(dfui_form_get_info(f));
	form_name = dfui_info_get_name(dfui_form_get_info(f));
	form_short_description = dfui_info_get_short_desc(dfui_form_get_info(f));

	output_html_header(form_name);

	if (dfui_form_property_is(f, "monospaced", "true")) {
		/* output the short description as-is */
		fprintf(cgiOut, "<div class=\"monospaced\">\n");
		fprintf(cgiOut, "<pre>%s</pre>", form_short_description);
	} else {
		/* output the short desc with newlines converted to <br> */
		fprintf(cgiOut, "<div class=\"textbox\">\n");
		convert_nl_to_br(form_short_description);
	}
	fprintf(cgiOut, "</div>");

	fprintf(cgiOut, "	<div class=\"mainform\">\n");
	fprintf(cgiOut, "       <table class=\"maintable\" id=\"mainness\">");
	fprintf(cgiOut, "       <tbody>\n");

#if 0
	fprintf(cgiOut, "\n<table width=\"100%%\" border=\"0\"><tr><td colspan=\"15\"><center>");
	fprintf(cgiOut, "<div class=\"textbox\">\n");
	if (dfui_form_property_is(f, "monospaced", "true")) {
		fprintf(cgiOut, "<pre>");
	}
	/* ouput this text with newlines converted to <br> */
	convert_nl_to_br(form_short_description);
	if (dfui_form_property_is(f, "monospaced", "true")) {
		fprintf(cgiOut, "</pre>");
	}
	fprintf(cgiOut, "</div>\n<center>\n");
#endif

	if (dfui_form_is_extensible(f)) {
		fprintf(cgiOut, "<input type='hidden' name='isextensible' value='isextensible'></input>\n");		
	}

	/*
	 * TODO: * get accurate colspan
	 *       * size all columns in a grid pattern
	 */

	help_text = dfui_info_get_long_desc(dfui_form_get_info(f));

	for (fi = dfui_form_field_get_first(f); fi != NULL; fi = dfui_field_get_next(fi)) {
		field_id = dfui_field_get_id(fi);
		field_name = dfui_info_get_name(dfui_field_get_info(fi));
		field_descr = dfui_info_get_short_desc(dfui_field_get_info(fi));
		if (strlen(field_descr) < 3)
			field_descr = field_name;

		cd = dfui_dataset_celldata_find(dfui_form_dataset_get_first(f), dfui_field_get_id(fi));
		field_value = dfui_celldata_get_value(cd);
		field_type = "textbox";
		if (dfui_field_property_is(fi, "control", "checkbox"))
			field_type = "checkbox";
		if (dfui_field_property_is(fi, "control", "button"))
			field_type = "button";
		if (dfui_field_property_is(fi, "control", "radio"))
			field_type = "radio";
		if (dfui_field_property_is(fi, "control", "checkbox")) {
			fprintf(cgiOut, "<tr><td colspan=\"2\" align=middle><center>%s: "
					"<input type='checkbox' name='%s' title='%s'"
					"%s></input>\n",
			    field_name, field_id, field_descr,
			    strcmp(field_value, "Y") == 0 ? " CHECKED" : " ");
			fprintf(cgiOut, "<input type='hidden' name='%s_type' value='checkbox'></input></td></tr>\n",
			    field_id);
		} else {
                        o = dfui_field_option_get_first(fi);
                        if (o != NULL) {
			    fprintf(cgiOut, "<tr><td align=right>%s:</td>"
					    "<td align=left><select name='%s' "
					    "title='%s'>\n",
				field_name, field_id, field_descr);
			    for (o = dfui_field_option_get_first(fi); o != NULL; o = dfui_option_get_next(o)) {
                                /*
				 * Render this option as a choice
				 * in a drop-down list box.
				 */
				fprintf(cgiOut, "<option value='%s'>%s</option>\n",
				    dfui_option_get_value(o), dfui_option_get_value(o));
			    }
                            fprintf(cgiOut, "</select>\n</td></tr>\n");
                        } else {
                                fprintf(cgiOut, "<tr><td align=right>%s:</td>"
						"<td align='left'><input class='button' "
						"type='%s' name='%s' value='%s' %s"
						"title='%s'></input>\n",
                                    field_name,
				    dfui_field_property_is(fi, "obscured", "true") ?
					"password" : "textbox",
				    field_id, field_value,
				    dfui_field_property_is(fi, "editable", "false") ?
					" onFocus=\"this.blur();\" " : " ",
				    field_descr);
                                fprintf(cgiOut, "<input type='hidden' name='%s_type' value='%s'></input></td></tr>\n",
                                    field_id, field_type);
                        }
		}
	}
	fprintf(cgiOut, "\n<tr><td colspan=\"15\">&nbsp;<br /><center>");
	for (a = dfui_form_action_get_first(f); a != NULL;  a = dfui_action_get_next(a)) {
		const char *action_id, *action_name;

		action_id = dfui_action_get_id(a);
		action_name = dfui_info_get_name(dfui_action_get_info(a));
		fprintf(cgiOut, "<input class='button' type='submit' name='%s' value='%s'></input> \n",
		    action_id, action_name);
		fprintf(cgiOut, "<input type='hidden' name='%s_type' value='button'></input> \n",
		    action_id);
		if (dfui_form_property_is(f, "role", "menu")) {
			fprintf(cgiOut, "<br />&nbsp;<br />");
		}
	}
	fprintf(cgiOut, "</center>");
	fprintf(cgiOut, "</td></tr></table>");

        if (strlen(help_text) > 0) {
                fprintf(cgiOut, "<center><p><a href='#' onClick='javascript:alert(\"%s\");'>Help</a></p></center>", help_text);
        }

	output_html_footer();
}

static void
create_html_from_dfui_form_multiple(const struct dfui_form *f)
{
	struct dfui_action *a;
	struct dfui_celldata *cd;
	struct dfui_dataset *ds;
	struct dfui_field *fi;
	struct dfui_option *o;
	const char *field_descr;
	const char *field_name;
	const char *field_value;
	const char *field_type;
	const char *form_name;
	const char *form_short_description;
	int field_counter;
	int field_counter_js = 0;
	int rows = 0;
	const char *help_text;

	form_name = dfui_info_get_name(dfui_form_get_info(f));
	form_short_description = dfui_info_get_short_desc(dfui_form_get_info(f));

	output_html_header(form_name);

	fprintf(cgiOut, "       <div class=\"mainform\">\n");
	fprintf(cgiOut, "       <table width=\"100%%\" class=\"maintable\" id=\"mainness\">");

	fprintf(cgiOut, "       <tbody>\n<tr><td><center>");

	fprintf(cgiOut, "\n<center>\n<table border=\"0\"><tr><td colspan=\"15\"><center>");

	fprintf(cgiOut, "<input type='hidden' name='ismultiple' value='true'></input>\n");
	
	if (dfui_form_is_extensible(f))
		fprintf(cgiOut, "<input type='hidden' name='isextensible' value='isextensible'></input>\n");		

	if (dfui_form_property_is(f, "monospaced", "true")) {
		fprintf(cgiOut, "<div class=\"monospaced\">\n");
		fprintf(cgiOut, "<pre>%s</pre>", form_short_description);
	} else {
		/* output this text with newlines converted to <br> */
		fprintf(cgiOut, "<div class=\"textbox\">\n");
		convert_nl_to_br(form_short_description);
	}
	fprintf(cgiOut, "</div>\n<center>\n");

	fprintf(cgiOut, "<p></p>\n<table width=\"100%%\" name=\"maintablearea\" id=\"maintablearea\"><tr>");

	rows = 0;
	for (fi = dfui_form_field_get_first(f); fi != NULL;  fi = dfui_field_get_next(fi)) {
		field_descr = dfui_info_get_name(dfui_field_get_info(fi));
		fprintf(cgiOut, "<td><b>%s</b></td>", field_descr);
		fprintf(cgiOut, "\n<script type=\"text/javascript\" language='javascript'>\n");
		field_name = dfui_field_get_id(fi);
		fprintf(cgiOut, "rowname[%i] = \"%s\";\n", rows, field_name);

		field_type = "textbox";
		if (dfui_field_property_is(fi, "control", "checkbox"))
			field_type = "checkbox";
		if (dfui_field_property_is(fi, "control", "button"))
			field_type = "button";
		if (dfui_field_property_is(fi, "control", "textbox"))
			field_type = "textbox";
		if (dfui_field_property_is(fi, "control", "radio"))
			field_type = "radio";

		fprintf(cgiOut, "rowtype[%i] = \"%s\";\n", rows, field_type);
		fprintf(cgiOut, "</script>\n");
		fprintf(cgiOut, "<input type='hidden' name='%s_type' value='%s'></input>\n<p></p>\n",
		    field_name, field_type);
		field_counter_js++;
		rows++;
	}

	if (dfui_form_is_extensible(f))
	    fprintf(cgiOut, "\n<td>\n<input class='button' type=\"button\" "
			    "onclick=\"addRowTo('maintablearea')\" value=\"Add\">\n</td>\n");

	fprintf(cgiOut, "\n</tr>\n<tr>");
	rows = 0;
	field_counter = 0;
	for (ds = dfui_form_dataset_get_first(f); ds != NULL; ds = dfui_dataset_get_next(ds)) {
		fprintf(cgiOut, "<tr>");
		for (fi = dfui_form_field_get_first(f); fi != NULL;  fi = dfui_field_get_next(fi)) {
			cd = dfui_dataset_celldata_find(ds, dfui_field_get_id(fi));
			field_name = dfui_field_get_id(fi);
			field_value = dfui_celldata_get_value(cd);
			field_descr = dfui_info_get_short_desc(dfui_field_get_info(fi));

			if (dfui_field_property_is(fi, "control", "checkbox")) {
				fprintf(cgiOut, "<td><center><input type='checkbox' name='%s-%d'%s></input>\n",
				    field_name, field_counter, strcmp(field_value, "Y") == 0 ? " CHECKED" : " ");

				fprintf(cgiOut, "<input type='hidden' name='%s_row-%d' value='%d'></input><p></td>",
				    field_name, field_counter, field_counter);
			} else {
				o = dfui_field_option_get_first(fi);
				if (o != NULL) {
					fprintf(cgiOut, "<td><select name=\"%s-%d\">\n",
					    field_name, field_counter);
					for (o = dfui_field_option_get_first(fi); o != NULL; o = dfui_option_get_next(o)) {
						/*
						 * Render this option as a choice
						 * in a drop-down list box.
						 */
						fprintf(cgiOut, "<option value='%s'>%s</option>\n",
						    dfui_option_get_value(o), dfui_option_get_value(o));
					}
					fprintf(cgiOut, "</select>\n</td>");
				} else {
					fprintf(cgiOut, "<td><input class='button' type='%s' name='%s-%d' "
							"value='%s' %s title='%s'></input>\n",
					    dfui_field_property_is(fi, "obscured","true") ?
						"password" : "textbox",
					    field_name, field_counter,
					    field_value,
					    dfui_field_property_is(fi, "editable", "false") ?
						" onFocus=\"this.blur();\" " : " ",
					    field_descr);

					fprintf(cgiOut, "<input type='hidden' name='%s_row-%d' value='%d'></input>\n<p />\n</td>",
					    field_name, field_counter, field_counter);
				}
			}
		}
		field_counter++;
		rows++;
		if (dfui_form_is_extensible(f))
			fprintf(cgiOut, "\n<td>\n<input class='button' type='button' "
					"value='Delete' onclick='removeRow(this)'>\n</td>\n");

		fprintf(cgiOut, "\n</tr>");
	}
	fprintf(cgiOut, "</table><table>");
	fprintf(cgiOut, "<tr><td>\n");
	fprintf(cgiOut, "\n<script type=\"text/javascript\" language='javascript'>\n");
	fprintf(cgiOut, "field_counter_js = %d;\n", field_counter_js);
	fprintf(cgiOut, "rows = %d;\n", (field_counter-1));
	fprintf(cgiOut, "totalrows = %d;\n", (rows-1));
	fprintf(cgiOut, "</script>\n\n");
	for (a = dfui_form_action_get_first(f); a != NULL;  a = dfui_action_get_next(a)) {
		field_name = dfui_action_get_id(a);
		field_value = dfui_info_get_name(dfui_action_get_info(a));
		fprintf(cgiOut, "\n<input class='button' type='submit' name='%s' value='%s'></input> \n", field_name, field_value);
		fprintf(cgiOut, "\n<input type='hidden' name='%s_type' value='button'></input>\n", field_name);
		if (dfui_form_property_is(f, "role", "menu"))
			fprintf(cgiOut, "\n<br />&nbsp;<br />");
	}
	fprintf(cgiOut, "</td></tr></table>\n");

	help_text = dfui_info_get_long_desc(dfui_form_get_info(f));

        if (strlen(help_text) > 0) {
                fprintf(cgiOut, "<center><p><a href='#' onClick='javascript:alert(\"%s\");'>Help</a></p></center>", help_text);
        }

	output_html_footer();
}

static void
create_html_from_dfui_form(const struct dfui_form *f)
{
	if (dfui_form_is_multiple(f))
		create_html_from_dfui_form_multiple(f);
	else
		create_html_from_dfui_form_single(f);
}

static struct dfui_response *
create_response_from_posted_data_single(const char *form_id)
{
	struct dfui_dataset *ds;
	struct dfui_response *r;
	char **array, **arrayStep;
	char *field_name;
	char arrayFieldName[80];
	char field_type[80];
	char tmp[30];
	char value[80];

	if (cgiFormEntries(&array) != cgiFormSuccess)
		return(r);
	
	arrayStep = array;
	while (*arrayStep != NULL) {
		snprintf(tmp, 29, "%s_type", *arrayStep); 
		cgiFormStringNoNewlines(tmp, field_type, 80);
		if (cgiFormSubmitClicked(*arrayStep) == cgiFormSuccess &&
		    strcmp(field_type, "button") == 0) {
			field_name = *arrayStep;
			break;
		}
		arrayStep++;
	}

	r = dfui_response_new(form_id, field_name);

	/* Add the contents of all form inputs to r's dataset. */
	ds = dfui_dataset_new();

	#ifdef DEBUG
		output_html_header("DEBUG TURNED ON");
	#endif /* DEBUG */

	arrayStep = array;
	/*
	 * TODO: revise this since it doesn't seem to include
	 *       unchecked boxes.
	 */
	while (*arrayStep != NULL) {
		strncpy(arrayFieldName, *arrayStep, 30);
		if (field_name_has_type(arrayFieldName)) {
			arrayStep++;
			continue;
		}
		/* copy cgi value to value var */
		cgiFormStringNoNewlines(*arrayStep, value, 81); 
		/* we posted a extra hint about this field */
		snprintf(tmp, 29, "%s_type", *arrayStep); 
		/* copy the hint type to observe what kind of field we have. */
		cgiFormStringNoNewlines(tmp, field_type, 80);
		/* if it's a checkbox: */
		
		/* TODO: Check for _type at end of string */
		if (strcmp(field_type, "checkbox") == 0) {
			if (cgiFormCheckboxSingle(*arrayStep) ==
			    cgiFormSuccess) { 
				snprintf(value, 2, "Y");
				#ifdef DEBUG
					fprintf(cgiOut, "Adding %s %s<P>", 
					    arrayFieldName, value);
				#endif /* DEBUG */
				dfui_dataset_celldata_add(ds, 
				    arrayFieldName, value);
			} else {
				snprintf(value, 2, "N");
				#ifdef DEBUG
					fprintf(cgiOut, "Adding %s %s<P>", 
					    arrayFieldName, value);
				#endif /* DEBUG */
				dfui_dataset_celldata_add(ds, 
				    arrayFieldName, value);
			}
		} else {
			/* if it's a textbox or a checkbox: */
			if (strcmp(field_type, "textbox") == 0 || 
			    strcmp(field_type, "checkbox") == 0) {
				#ifdef DEBUG
					fprintf(cgiOut, "Adding %s %s<P>", 
					    arrayFieldName, value);
				#endif /* DEBUG */
				dfui_dataset_celldata_add(ds, arrayFieldName, value);
			}
		}
		arrayStep++;
	}
	cgiStringArrayFree(array);
	dfui_response_dataset_add(r, ds);
	return(r);
}

static int
field_name_has_type(const char *arrayFieldName)
{
	if (strstr(arrayFieldName, "_type") != NULL ||
	    strstr(arrayFieldName, "_row") != NULL) {
		return(1); /* field is okay to post to backend. */
	} else {
		return(0); /* field is not okay to post to backend. */
	}
}

static struct dfui_response *
create_response_from_posted_data_multiple(const char *form_id)
{
	struct dfui_dataset *ds;
	struct dfui_response *r;
	char **array, **arrayStep;
	char *field_name;
	char arrayFieldName[80];
	char field_type[80];
	char tmp[30];
	char value[80];
	int arrayFieldNumber;
	int ismultiple;
	size_t x;
	int row = 0, found = 0;

	ismultiple = 0;
	cgiFormStringNoNewlines("ismultiple", tmp, 20);
	if (strcmp(tmp, "true") == 0)
		ismultiple = 1;

	if (cgiFormEntries(&array) != cgiFormSuccess) {
		return(r);
	}

	arrayStep = array;
	while (*arrayStep != NULL) {
		snprintf(tmp, 29, "%s_type", *arrayStep);
		cgiFormStringNoNewlines(tmp, field_type, 80);
		if (cgiFormSubmitClicked(*arrayStep) == cgiFormSuccess &&
		    strcmp(field_type, "button") == 0) {
			field_name = *arrayStep;
			break;
		}
		arrayStep++;
	}

	r = dfui_response_new(form_id, field_name);

	#ifdef DEBUG
		output_html_header("DEBUG TURNED ON");
	#endif /* DEBUG */

	/*
	 * TODO: revise this since it doesn't seem to include
	 *       unchecked boxes.
	 */
	row = 0;
	do {
		ds = dfui_dataset_new();
		found = 0;
		for (arrayStep = array; *arrayStep != NULL; arrayStep++) {
			strncpy(arrayFieldName, *arrayStep, 30);
			if (field_name_has_type(arrayFieldName)) {
				continue;
			}
			/* copy cgi value to value var */
			cgiFormStringNoNewlines(*arrayStep, value, 81);
			/* walk arrayFieldName and remove -[0-1000] */
			arrayFieldNumber = -1;
			for (x = 0; x < strlen(arrayFieldName); x++) {
				if (arrayFieldName[x] == '-') {
					arrayFieldName[x] = '\0';
					arrayFieldNumber = atoi(&arrayFieldName[x+1]);
					break;
				}
			}
			#ifdef DEBUG
				fprintf(cgiOut, "HTML input name: %s, arrayFieldName: %s, "
						"arrayFieldNumber: %d<p></p>",
						*arrayStep, arrayFieldName, arrayFieldNumber);
				fflush(cgiOut);
			#endif /* DEBUG */
			/*
			 * In any given iteration of this (inner) loop, we only care about
			 * one set of fields, the fields which have an arrayFieldNumber
			 * (that is, the bit after the hyphen) which is the same as 'row'.
			 * So, if it's different, try again; if it's the same, count it.
			 */
			if (arrayFieldNumber != row || arrayFieldNumber == -1) {
				continue;
			} else {
				found++;
			}
			/* we posted a extra hint about this field */
			snprintf(tmp, 29, "%s_type", arrayFieldName);
			/* copy the hint type to observe what kind of field we have. */
			cgiFormStringNoNewlines(tmp, field_type, 80);
			/* XXX temporarily override */

			/* if it's a checkbox: */
			if (strcmp(field_type, "checkbox") == 0) {
				if (cgiFormCheckboxSingle(*arrayStep) ==
				    cgiFormSuccess) {
					snprintf(value, 2, "Y");
					#ifdef DEBUG
						fprintf(cgiOut, "Adding %s %s<P>",
						    arrayFieldName, value);
					#endif /* DEBUG */
					dfui_dataset_celldata_add(ds,
					    arrayFieldName, value);
				} else {
					snprintf(value, 2, "N");
					#ifdef DEBUG
						fprintf(cgiOut, "Adding %s %s<P>",
						    arrayFieldName, value);
					#endif /* DEBUG */
					dfui_dataset_celldata_add(ds,
					    arrayFieldName, value);
				}
			} else {
				/* if it's a textbox or a checkbox: */
				if (strcmp(field_type, "textbox") == 0 ||
				    strcmp(field_type, "checkbox") == 0) {
					#ifdef DEBUG
						fprintf(cgiOut, "Adding %s %s<P>",
						arrayFieldName, value);
					#endif /* DEBUG */
					if (strcmp(arrayFieldName, "") != 0)
						dfui_dataset_celldata_add(ds, arrayFieldName, value);
				}
			}
		}
		if (found > 0) {
			if (strcmp(arrayFieldName, "") != 0) {
			   cgiFormStringNoNewlines("isextensible", tmp, 80);
			   dfui_response_dataset_add(r, ds);
			}
		} else {
			dfui_dataset_free(ds);
		}
		row++;
	} while (row < 100);
	cgiStringArrayFree(array);
	return(r);
}

static int
request_method_is(const char *method)
{
	if (!strcasecmp(method, cgiRequestMethod))
		return(1);
	else
		return(0);
}

int 
cgiMain(void)
{
	struct dfui_connection *c;
	struct dfui_form *f;
	struct dfui_progress *pr;
	struct dfui_property *gp;
	struct dfui_response *r;
	void *payload;
	char msgtype;
	char tmp[30];
	int done = 0;
	char environmental_payload[256];

	int progress_amount;
	#ifdef DEBUG
		dfui_debug_file = fopen("/tmp/dfuife_cgi_debug.log", "a");
	#endif /* DEBUG */
	setvbuf(cgiOut, NULL, _IONBF, 0);

	/* Deterimine if the user is setting the transport via a environmental variable */

	snprintf(environmental_payload, 256, "%s", getenv("BSD_INSTALLER_TRANSPORT"));
	if (strcmp(environmental_payload,"CAPS") == 0) {
		c = dfui_connection_new(DFUI_TRANSPORT_CAPS, "test");
	} else if (strcmp(environmental_payload,"NPIPE") == 0) {
		c = dfui_connection_new(DFUI_TRANSPORT_NPIPE, "test");
	} else if (strcmp(environmental_payload,"TCP") == 0) {
		c = dfui_connection_new(DFUI_TRANSPORT_TCP, "9999");
	} else {
		c = dfui_connection_new(DFUI_TRANSPORT_TCP, "9999");
	}

	dfui_fe_connect(c);

	/* TODO: Additional error checking if we could not connect. */

	if (request_method_is("POST")) {
		char form_id[81];

		if (cgiFormSubmitClicked("cancelinstal") == cgiFormSuccess) {
			/* cancel install */
			dfui_fe_progress_cancel(c);
		}
		cgiFormStringNoNewlines("form_id", form_id, 80);
		cgiFormStringNoNewlines("ismultiple",tmp, 20);
		if (strcmp(tmp, "true") == 0) {
			 r = create_response_from_posted_data_multiple(form_id);
		} else {
			 r = create_response_from_posted_data_single(form_id);
		}
		dfui_fe_submit(c, r);
		dfui_response_free(r);
	}

	while (!done) {
		dfui_fe_receive(c, &msgtype, &payload);
		switch (msgtype) {
		case DFUI_BE_MSG_PRESENT:
			f = (struct dfui_form *)payload;
			create_html_from_dfui_form(f);
			dfui_form_free(f);
			done = 1;
			break;
		case DFUI_BE_MSG_PROG_BEGIN:
			caught_signal = 0;
			signal(SIGPIPE, signal_handler);
			pr = (struct dfui_progress *)payload;
			is_streaming_progress_bar = dfui_progress_get_streaming(pr);
			fprintf(cgiOut, "<script language=javascript>\n");
			fprintf(cgiOut, "is_streaming_progress_bar = 1; \n");
			fprintf(cgiOut, "</script>\n");
			cl_progress_bar_begin();
			dfui_progress_free(pr);
			dfui_fe_progress_continue(c);
			break;
		case DFUI_BE_MSG_PROG_UPDATE:
			pr = (struct dfui_progress *)payload;
			progress_amount = dfui_progress_get_amount(pr);
			if (last_progress_amount != progress_amount)
			      cl_progress_bar_update(progress_amount, dfui_info_get_short_desc(dfui_progress_get_info(pr)));
			dfui_progress_free(pr);
			if (caught_signal != 0) {
				dfui_fe_progress_cancel(c);
				done = 1;
			} else {
				dfui_fe_progress_continue(c);
			}
			break;
		case DFUI_BE_MSG_PROG_END:
			signal(SIGPIPE, SIG_DFL);
			cl_progress_bar_stop();
			dfui_fe_progress_continue(c);
			done = 1;
			break;
		case DFUI_BE_MSG_SET_GLOBAL:
			gp = (struct dfui_property *)payload;

			/*
			 * Check for a change to the "lang" setting...
			 */
			if (strcmp(dfui_property_get_name(gp), "lang") == 0) {
				/*
				 * TODO: call the appropriate gettext function.
				 */
			}

			dfui_fe_confirm_set_global(c);
			dfui_property_free(gp);
			break;
		case DFUI_BE_MSG_STOP:
			/* end of cycle, return to opening screen. */
			dfui_fe_confirm_stop(c);
			cgiHeaderLocation("/");
			done = 1;
			break;
		}
		/* sleep(1); */
	}
	fflush(cgiOut);
	dfui_fe_disconnect(c);
	#ifdef DEBUG
		fclose(dfui_debug_file);
	#endif /* DEBUG */	
	return(0);
}

static void
cl_progress_bar_stop(void)
{
	fprintf(cgiOut, "\n<script type=\"text/javascript\" language='javascript'>\n");
	fprintf(cgiOut, "document.progressbar.style.width='100%%';\n");
	fprintf(cgiOut, "</script>\n");
	fprintf(cgiOut, "\n\n<meta http-equiv='refresh' content='1;url=" DFUIFE_CGI "'>\n");
	fflush(cgiOut);
}

static void
cl_progress_bar_begin(void)
{
	int rows;
	int cols;

	if (is_streaming_progress_bar) {
		rows = 14;
		cols = 80;
	} else {
		rows = 5;
		cols = 40;
	}
	output_html_header("Operation in progress, please wait...");
	fprintf(cgiOut, "\n\n<p>&nbsp;</p>\n<center>\n\n");
	fprintf(cgiOut, "<table height='40' border='1' bordercolor='black' width='400' bordercolordark='#000000' bordercolorlight='#000000' style='border-collapse: collapse' colspacing='2' cellpadding='2' cellspacing='2'><tr><td>");
	fprintf(cgiOut, "\n<img border='0' src='dfly-pg.gif' width='280' height='43' name='progressbar' id='progressbar'>");
	fprintf(cgiOut, "\n</td>\n</tr>\n</table>\n<br />\n<form>\n<center>\n<textarea rows='%d' onFocus=\"this.blur();\" cols='%d' name='pbstatus' id='pbstatus'>\n</textarea>", rows, cols);
	fprintf(cgiOut, "\n<form method='post' action='" DFUIFE_CGI "'><p><input class='button' type='submit' name='cancelinstall' value='Cancel'></p>");
	fflush(cgiOut);
	output_html_footer();
}

static void
cl_progress_bar_update(int percent, const char *pbstatus)
{
	fprintf(cgiOut, "\n\n<script type=\"text/javascript\" language='javascript'>\n");
	fprintf(cgiOut, "document.progressbar.style.width='%d%%';\n", percent);
	if (is_streaming_progress_bar) {
		fprintf(cgiOut, "temp_streaming_text = document.forms[0].pbstatus.value;\n");
		fprintf(cgiOut, "document.forms[0].pbstatus.value = temp_streaming_text + \"%s\\n\";\n", pbstatus);
	} else {
		fprintf(cgiOut, "document.forms[0].pbstatus.value='%s';\n", pbstatus);
	}
	fprintf(cgiOut, "</script>\n\n");
	last_progress_amount = percent;
	fflush(cgiOut);
}

static void
convert_nl_to_br(const char *string_to_convert)
{
	size_t x;

	for (x = 0; x < strlen(string_to_convert); x++) {
		if (string_to_convert[x] == 10) {	/* newline character */
			fprintf(cgiOut, "&nbsp;<br />");
		} else if (string_to_convert[x] == '>') {
			fprintf(cgiOut, "&gt;");
		} else if (string_to_convert[x] == '<') {
			fprintf(cgiOut, "&lt;");
		} else {
			fprintf(cgiOut, "%c", string_to_convert[x]);
		}
	}
}
