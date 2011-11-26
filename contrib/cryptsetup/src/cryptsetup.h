#ifndef CRYPTSETUP_H
#define CRYPTSETUP_H

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "lib/nls.h"

#define MAX_CIPHER_LEN		32
#define MAX_CIPHER_LEN_STR	"32"
#define DEFAULT_CIPHER(type)	(DEFAULT_##type##_CIPHER "-" DEFAULT_##type##_MODE)

#define log_dbg(x...) clogger(NULL, CRYPT_LOG_DEBUG, __FILE__, __LINE__, x)
#define log_std(x...) clogger(NULL, CRYPT_LOG_NORMAL, __FILE__, __LINE__, x)
#define log_verbose(x...) clogger(NULL, CRYPT_LOG_VERBOSE, __FILE__, __LINE__, x)
#define log_err(x...) clogger(NULL, CRYPT_LOG_ERROR, __FILE__, __LINE__, x)

#endif /* CRYPTSETUP_H */
