/*
 * Copyright (c)2004 Cat's Eye Technologies.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Cat's Eye Technologies nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * dfui.h
 * $Id: dfui.h,v 1.28 2005/02/07 06:39:59 cpressey Exp $
 */

#ifndef __DFUI_H
#define __DFUI_H

#ifdef __cplusplus
#define DFUI_BEGINDECLS     extern "C" {
#define DFUI_ENDDECLS       }
#else
#define DFUI_BEGINDECLS
#define DFUI_ENDDECLS
#endif

DFUI_BEGINDECLS

#include <libaura/buffer.h>

/*
 * TYPEDEFS
 */

typedef	int		dfui_err_t;

/*
 * CONSTANTS
 */

#define	DFUI_SUCCESS	(dfui_err_t)1
#define	DFUI_FAILURE	(dfui_err_t)0

/*
 * Transports.
 */

#define DFUI_TRANSPORT_CAPS	1
#define DFUI_TRANSPORT_NPIPE	2
#define DFUI_TRANSPORT_TCP	3

/*
 * Message types.
 */

#define DFUI_BE_MSG_READY	'r'	/* send me back a reply please */

#define DFUI_BE_MSG_STOP	'X'	/* shut down please, we're done */
#define DFUI_BE_MSG_PRESENT	'P'	/* present this form to the user */
#define DFUI_BE_MSG_PROG_BEGIN	'b'	/* begin showing a progress bar */
#define DFUI_BE_MSG_PROG_UPDATE	'u'	/* update the progress bar */
#define DFUI_BE_MSG_PROG_END	'e'	/* stop showing the progress bar */

#define DFUI_BE_MSG_SET_GLOBAL	'G'	/* set a global setting in the f/e */

#define DFUI_FE_MSG_READY	'r'	/* send me a form or something */

#define DFUI_FE_MSG_SUBMIT	'S'	/* submit results of a form */
#define DFUI_FE_MSG_CONTINUE	'c'	/* nothing stopping a progress bar */
#define DFUI_FE_MSG_CANCEL	'C'	/* user cancelled a progress bar */
#define DFUI_FE_MSG_ABORT	'X'	/* something catastrophic (^C?) */

/*
 * STRUCTURE PROTOTYPES
 */

struct dfui_connection;
struct dfui_payload;
struct dfui_info;
struct dfui_property;
struct dfui_dataset;
struct dfui_celldata;
struct dfui_form;
struct dfui_field;
struct dfui_option;
struct dfui_action;
struct dfui_response;
struct dfui_progress;

/*
 * STRUCTURE DEFINITIONS
 */

#ifdef NEEDS_DFUI_STRUCTURE_DEFINITIONS

/* Connections */

struct dfui_connection {
	int transport;		  /* transport layer: CAPS, NPIPE, or TCP */
	char *rendezvous;	  /* rendezvous point */
	struct aura_buffer *ebuf; /* last message recvd */
	int is_connected;	  /* was a connection actually established? */
	void *t_data;		  /* transport-specific connection data */

	dfui_err_t (*be_start)(struct dfui_connection *);
	dfui_err_t (*be_stop)(struct dfui_connection *);
	dfui_err_t (*be_ll_exchange)(struct dfui_connection *, char, const char *);

	dfui_err_t (*fe_connect)(struct dfui_connection *);
	dfui_err_t (*fe_disconnect)(struct dfui_connection *);

	dfui_err_t (*fe_ll_request)(struct dfui_connection *, char, const char *);
};

/* Common structures on objects */

struct dfui_info {
	char *name;
	char *short_desc;
	char *long_desc;
};

struct dfui_dataset {
	struct dfui_dataset *next;
	struct dfui_celldata *celldata_head;
};

struct dfui_celldata {
	struct dfui_celldata *next;
	char *field_id;
	char *value;
};

/*
 * Properties may be either strong (imply a behavioural guarantee from
 * the frontend) or weak (may be ignored or interpreted subjectively
 * by the frontend) depending on their name.  Currently only the
 * following properties are strong:
 *
 *   editable
 *   obscured
 */
struct dfui_property {
	struct dfui_property *next;
	char *name;
	char *value;
};

/* Form objects */

struct dfui_form {
	char *id;
	struct dfui_info *info;
	int multiple;
	int extensible;
	struct dfui_field *field_head;
	struct dfui_action *action_head;
	struct dfui_dataset *dataset_head;
	struct dfui_property *property_head;
};

struct dfui_field {
	char *id;
	struct dfui_info *info;
	struct dfui_field *next;
	struct dfui_option *option_head;
	struct dfui_property *property_head;
};

struct dfui_option {
	char *value;
	struct dfui_option *next;
};

struct dfui_action {
	char *id;
	struct dfui_info *info;
	struct dfui_action *next;
	struct dfui_property *property_head;
};

/* Progress objects */

struct dfui_progress {
	struct dfui_info *info;
	int amount;
	int streaming;		/* if 1, msg will stream in line by line */
	char *msg_line;		/* next line of message if streaming=1 */
};

/* Response objects */

struct dfui_response {
	char *form_id;
	char *action_id;
	struct dfui_dataset *dataset_head;
};

/* Payload objects */

struct dfui_payload {
	char msgtype;
	struct dfui_form *form;
	struct dfui_progress *progress;
	struct dfui_property *global_setting;
};

#endif /* NEEDS_DFUI_STRUCTURE_DEFINITIONS */

/*
 * PROTOTYPES
 */

/*
 * UTILITY (form/field creation, etc)
 */

struct dfui_info		*dfui_info_new(const char *, const char *, const char *);
void				 dfui_info_free(struct dfui_info *);
const char			*dfui_info_get_name(const struct dfui_info *);
const char			*dfui_info_get_short_desc(const struct dfui_info *);
const char			*dfui_info_get_long_desc(const struct dfui_info *);
void				 dfui_info_set_name(struct dfui_info *, const char *);
void				 dfui_info_set_short_desc(struct dfui_info *, const char *);
void				 dfui_info_set_long_desc(struct dfui_info *, const char *);

struct dfui_property		*dfui_property_new(const char *, const char *);
void				 dfui_property_free(struct dfui_property *);
void				 dfui_properties_free(struct dfui_property *);
struct dfui_property		*dfui_property_find(struct dfui_property *, const char *);
const char			*dfui_property_get(struct dfui_property *, const char *);
struct dfui_property		*dfui_property_set(struct dfui_property **, const char *, const char *);
const char			*dfui_property_get_name(const struct dfui_property *);
const char			*dfui_property_get_value(const struct dfui_property *);

struct dfui_celldata		*dfui_celldata_new(const char *, const char *);
void				 dfui_celldata_free(struct dfui_celldata *);
void				 dfui_celldatas_free(struct dfui_celldata *);
struct dfui_celldata		*dfui_celldata_find(struct dfui_celldata *, const char *);
struct dfui_celldata		*dfui_celldata_get_next(const struct dfui_celldata *);
const char *			 dfui_celldata_get_field_id(const struct dfui_celldata *);
const char *			 dfui_celldata_get_value(const struct dfui_celldata *);

struct dfui_dataset		*dfui_dataset_new(void);
struct dfui_dataset		*dfui_dataset_dup(const struct dfui_dataset *);
void				 dfui_dataset_free(struct dfui_dataset *);
void				 dfui_datasets_free(struct dfui_dataset *);
struct dfui_celldata		*dfui_dataset_celldata_add(struct dfui_dataset *,
					const char *, const char *);
struct dfui_celldata		*dfui_dataset_celldata_get_first(const struct dfui_dataset *);
struct dfui_celldata		*dfui_dataset_celldata_find(const struct dfui_dataset *, const char *);
struct dfui_dataset		*dfui_dataset_get_next(const struct dfui_dataset *);
const char			*dfui_dataset_get_value(const struct dfui_dataset *, const char *);
char				*dfui_dataset_dup_value(const struct dfui_dataset *, const char *);

struct dfui_field		*dfui_field_new(const char *, struct dfui_info *);
void				 dfui_field_free(struct dfui_field *);
void				 dfui_fields_free(struct dfui_field *);
struct dfui_option		*dfui_field_option_add(struct dfui_field *, const char *);
struct dfui_option		*dfui_field_option_get_first(const struct dfui_field *);
struct dfui_property		*dfui_field_property_set(struct dfui_field *, const char *, const char *);
const char			*dfui_field_property_get(const struct dfui_field *, const char *);
int				 dfui_field_property_is(const struct dfui_field *, const char *, const char *);
struct dfui_field		*dfui_field_get_next(const struct dfui_field *);
const char			*dfui_field_get_id(const struct dfui_field *);
struct dfui_info		*dfui_field_get_info(const struct dfui_field *);

struct dfui_option		*dfui_option_new(const char *);
void				 dfui_option_free(struct dfui_option *);
void				 dfui_options_free(struct dfui_option *);
struct dfui_option		*dfui_option_get_next(const struct dfui_option *);
const char			*dfui_option_get_value(const struct dfui_option *);

struct dfui_action		*dfui_action_new(const char *, struct dfui_info *);
void				 dfui_action_free(struct dfui_action *);
void				 dfui_actions_free(struct dfui_action *);
struct dfui_action		*dfui_action_get_next(const struct dfui_action *);
struct dfui_property		*dfui_action_property_set(struct dfui_action *, const char *, const char *);
const char			*dfui_action_property_get(const struct dfui_action *, const char *);
int				 dfui_action_property_is(const struct dfui_action *, const char *, const char *);
const char			*dfui_action_get_id(const struct dfui_action *);
struct dfui_info		*dfui_action_get_info(const struct dfui_action *);

struct dfui_form		*dfui_form_new(const char *, struct dfui_info *);
struct dfui_form		*dfui_form_create(const char *, const char *, const char *, const char *, ...);
void				 dfui_form_free(struct dfui_form *);
struct dfui_field		*dfui_form_field_add(struct dfui_form *,
					const char *, struct dfui_info *);
struct dfui_field		*dfui_form_field_attach(struct dfui_form *,
					struct dfui_field *);
struct dfui_action		*dfui_form_action_add(struct dfui_form *,
					const char *, struct dfui_info *);
struct dfui_action		*dfui_form_action_attach(struct dfui_form *,
					struct dfui_action *);
void				 dfui_form_dataset_add(struct dfui_form *,
					struct dfui_dataset *);
struct dfui_dataset		*dfui_form_dataset_get_first(const struct dfui_form *);
int				 dfui_form_dataset_count(const struct dfui_form *);
void				 dfui_form_datasets_free(struct dfui_form *);
struct dfui_property		*dfui_form_property_set(struct dfui_form *, const char *, const char *);
const char			*dfui_form_property_get(const struct dfui_form *, const char *);
int				 dfui_form_property_is(const struct dfui_form *, const char *, const char *);
struct dfui_field		*dfui_form_field_find(const struct dfui_form *, const char *);
struct dfui_field		*dfui_form_field_get_first(const struct dfui_form *);
int				 dfui_form_field_count(const struct dfui_form *);
struct dfui_action		*dfui_form_action_find(const struct dfui_form *, const char *);
struct dfui_action		*dfui_form_action_get_first(const struct dfui_form *);
int				 dfui_form_action_count(const struct dfui_form *);
const char			*dfui_form_get_id(const struct dfui_form *);
struct dfui_info		*dfui_form_get_info(const struct dfui_form *);
void				 dfui_form_set_multiple(struct dfui_form *, int);
int				 dfui_form_is_multiple(const struct dfui_form *);
void				 dfui_form_set_extensible(struct dfui_form *, int);
int				 dfui_form_is_extensible(const struct dfui_form *);

struct dfui_response		*dfui_response_new(const char *, const char *);
void				 dfui_response_free(struct dfui_response *);
void				 dfui_response_dataset_add(struct dfui_response *,
					struct dfui_dataset *);
struct dfui_dataset		*dfui_response_dataset_get_first(const struct dfui_response *);
int				 dfui_response_dataset_count(const struct dfui_response *);
const char			*dfui_response_get_form_id(const struct dfui_response *);
const char			*dfui_response_get_action_id(const struct dfui_response *);

struct dfui_progress		*dfui_progress_new(struct dfui_info *, int);
void				 dfui_progress_free(struct dfui_progress *);
struct dfui_info		*dfui_progress_get_info(const struct dfui_progress *);
void				 dfui_progress_set_amount(struct dfui_progress *, int);
int				 dfui_progress_get_amount(const struct dfui_progress *);
void				 dfui_progress_set_streaming(struct dfui_progress *, int);
int				 dfui_progress_get_streaming(const struct dfui_progress *);
void				 dfui_progress_set_msg_line(struct dfui_progress *, const char *);
const char			*dfui_progress_get_msg_line(const struct dfui_progress *);

void				 dfui_payload_free(struct dfui_payload *);
char				 dfui_payload_get_msg_type(const struct dfui_payload *);
struct dfui_form		*dfui_payload_get_form(const struct dfui_payload *);
struct dfui_progress		*dfui_payload_get_progress(const struct dfui_payload *);

/*
 * PROTOCOL
 */

struct dfui_connection	*dfui_connection_new(int, const char *);
void			 dfui_connection_free(struct dfui_connection *);

/*
 * BACKEND VERY HIGH LEVEL INTERFACE
 */

int			 dfui_be_present_dialog(struct dfui_connection *,
			     const char *, const char *, const char *, ...)
			     __printflike(4, 5);

/*
 * BACKEND HIGH LEVEL INTERFACE
 */
dfui_err_t		dfui_be_start(struct dfui_connection *);
dfui_err_t		dfui_be_stop(struct dfui_connection *);

dfui_err_t		dfui_be_present(struct dfui_connection *,
					struct dfui_form *, struct dfui_response **);
dfui_err_t		dfui_be_progress_begin(struct dfui_connection *,
						struct dfui_progress *);
dfui_err_t		dfui_be_progress_update(struct dfui_connection *,
						struct dfui_progress *, int *);
dfui_err_t		dfui_be_progress_end(struct dfui_connection *);

dfui_err_t		dfui_be_set_global_setting(struct dfui_connection *,
						   const char *, const char *, int *);

/*
 * FRONTEND HIGH LEVEL INTERFACE
 */
dfui_err_t		dfui_fe_connect(struct dfui_connection *);
dfui_err_t		dfui_fe_disconnect(struct dfui_connection *);

dfui_err_t		dfui_fe_receive(struct dfui_connection *, char *, void **);
struct dfui_payload    *dfui_fe_receive_payload(struct dfui_connection *);
dfui_err_t		dfui_fe_submit(struct dfui_connection *, struct dfui_response *);
dfui_err_t		dfui_fe_progress_continue(struct dfui_connection *);
dfui_err_t		dfui_fe_progress_cancel(struct dfui_connection *);
dfui_err_t		dfui_fe_confirm_set_global(struct dfui_connection *);
dfui_err_t		dfui_fe_cancel_set_global(struct dfui_connection *);
dfui_err_t		dfui_fe_confirm_stop(struct dfui_connection *);
dfui_err_t		dfui_fe_abort(struct dfui_connection *);

DFUI_ENDDECLS

#endif /* !__DFUI_H */
