/* Generated from /home/eirikn/src/kerberos5/lib/libkrb5/../../../crypto/heimdal-0.6.3/lib/krb5/k524_err.et */
/* $DragonFly: src/kerberos5/include/k524_err.h,v 1.1 2005/01/23 18:55:26 eirikn Exp $ */
/* $Id: k524_err.et,v 1.1 2001/06/20 02:44:11 joda Exp $ */

#ifndef __k524_err_h__
#define __k524_err_h__

#include <com_right.h>

void initialize_k524_error_table_r(struct et_list **);

void initialize_k524_error_table(void);
#define init_k524_err_tbl initialize_k524_error_table

typedef enum k524_error_number{
	ERROR_TABLE_BASE_k524 = -1750206208,
	k524_err_base = -1750206208,
	KRB524_BADKEY = -1750206208,
	KRB524_BADADDR = -1750206207,
	KRB524_BADPRINC = -1750206206,
	KRB524_BADREALM = -1750206205,
	KRB524_V4ERR = -1750206204,
	KRB524_ENCFULL = -1750206203,
	KRB524_DECEMPTY = -1750206202,
	KRB524_NOTRESP = -1750206201,
	k524_num_errors = 8
} k524_error_number;

#endif /* __k524_err_h__ */
