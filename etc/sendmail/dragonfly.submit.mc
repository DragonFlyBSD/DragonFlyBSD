divert(-1)
#
# Copyright (c) 2001-2003 Sendmail, Inc. and its suppliers.
#	All rights reserved.
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the sendmail distribution.
#
#

#
#  This is the DragonFly BSD template configuration for a set-group-ID
#  sm-msp sendmail that acts as a initial mail submission program.
#
#  If you want sendmail.submit.cf to be based on a customized version of
#  this file, copy it to /etc/mail/<hostname>.submit.mc and modify -or-
#  copy it to any location and set SENDMAIL_SUBMIT_MC in /etc/make.conf
#  to its path, then modify it as desired.
#

divert(0)dnl
VERSIONID(`$DragonFly: 10 March 2015')
define(`confCF_VERSION', `Submit')
define(`__OSTYPE__',`')dnl dirty hack to keep proto.m4 from complaining
define(`_USE_DECNET_SYNTAX_', `1')dnl support DECnet
define(`confTIME_ZONE', `USE_TZ')
define(`confDONT_INIT_GROUPS', `True')
define(`confBIND_OPTS', `WorkAroundBrokenAAAA')
dnl
dnl If you use IPv6 only, change [127.0.0.1] to [IPv6:::1]
FEATURE(`msp', `[127.0.0.1]')dnl
dnl
dnl To deliver all local mail to your mailhub
dnl FEATURE(`msp','[mailhub.do.main]`)
