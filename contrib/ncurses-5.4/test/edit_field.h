/*
 * $Id: edit_field.h,v 1.1 2003/04/26 22:54:50 tom Exp $
 *
 * Interface of edit_field.c
 */

#ifndef EDIT_FORM_H_incl
#define EDIT_FORM_H_incl 1

#include <form.h>

#define EDIT_FIELD(c) (MAX_FORM_COMMAND + c)

extern void help_edit_field(void);
extern int edit_field(FORM * form, int *result);

#endif /* EDIT_FORM_H_incl */
