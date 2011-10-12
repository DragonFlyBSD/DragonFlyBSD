/*
 * Works around wcwidth recursive problem while maintaining UTF-8
 * handling functionality.
 */

#ifndef _GNUWIDECHAR_H_
#define _GNUWIDECHAR_H_

int special_wcwidth(wchar_t);

#endif /* !_GNUWIDECHAR_H_ */
