/*
 * Copyright (c) 2017 by Dmitry V. Levin
 * Copyright (c) 2017 by Oleg Solovyov
 * See LICENSE
 */

#ifndef PASSWDQC_I18N_H__
#define PASSWDQC_I18N_H__

#ifdef ENABLE_NLS
#ifndef PACKAGE
#define PACKAGE "passwdqc"
#endif
#include <libintl.h>
#define _(msgid) dgettext(PACKAGE, msgid)
#define P2_(msgid, count) (dngettext(PACKAGE, (msgid), (msgid), (count)))
#define P3_(msgid, msgid_plural, count) (dngettext(PACKAGE, (msgid), (msgid_plural), (count)))
#define N_(msgid) msgid
#else
#define _(msgid) (msgid)
#define P2_(msgid, count) (msgid)
#define P3_(msgid, msgid_plural, count) ((count) == 1 ? (msgid) : (msgid_plural))
#define N_(msgid) msgid
#endif

#endif /* PASSWDQC_I18N_H__ */
