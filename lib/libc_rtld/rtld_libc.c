#include <sys/param.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdint.h>

int __getcwd(char *, size_t);

char *
getcwd(char *buffer, size_t size)
{
	int allocated;
	int error;

	allocated = 0;
	if (buffer == NULL) {
		size = MAXPATHLEN;
		buffer = malloc(size);
		if (buffer == NULL)
			return (NULL);
		allocated = 1;
	}
	error = __getcwd(buffer, size);
	if (error == 0)
		return (buffer);
	if (allocated)
		free(buffer);
	return (NULL);
}

int
getpagesize(void)
{
	return (PAGE_SIZE);
}

void
abort(void)
{
	__builtin_trap();
}

void
exit(int status)
{
	(void)status;
	_exit(status);
	__builtin_unreachable();
}

unsigned long
strtoul(const char *string, char **endp, int base)
{
	const char *start;
	unsigned long value;
	unsigned long digit;
	int negative;
	int digit_value;

	while (*string == ' ' || *string == '\t' || *string == '\n' ||
	    *string == '\r' || *string == '\f' || *string == '\v')
		++string;
	start = string;
	negative = 0;
	if (*string == '+' || *string == '-') {
		negative = *string == '-';
		++string;
	}
	if ((base == 0 || base == 16) && string[0] == '0' &&
	    (string[1] == 'x' || string[1] == 'X')) {
		base = 16;
		string += 2;
	} else if (base == 0 && string[0] == '0') {
		base = 8;
	} else if (base == 0) {
		base = 10;
	}
	value = 0;
	while (*string != '\0') {
		if (*string >= '0' && *string <= '9')
			digit_value = *string - '0';
		else if (*string >= 'a' && *string <= 'z')
			digit_value = *string - 'a' + 10;
		else if (*string >= 'A' && *string <= 'Z')
			digit_value = *string - 'A' + 10;
		else
			break;
		if (digit_value >= base)
			break;
		digit = (unsigned long)digit_value;
		if (value > (ULONG_MAX - digit) / (unsigned long)base) {
			value = ULONG_MAX;
			while (*string != '\0')
				++string;
			break;
		}
		value = value * (unsigned long)base + digit;
		++string;
	}
	if (endp != NULL)
		*endp = (char *)(uintptr_t)(string == start ? start : string);
	return (negative ? (unsigned long)(-value) : value);
}
