/*
 *      Copyright (c) 1987,1997, Prentice Hall
 *      All rights reserved.
 *
 *      Redistribution and use of the MINIX operating system in source and
 *      binary forms, with or without modification, are permitted provided
 *      that the following conditions are met:
 *
 *         * Redistributions of source code must retain the above copyright
 *           notice, this list of conditions and the following disclaimer.
 *
 *         * Redistributions in binary form must reproduce the above
 *           copyright notice, this list of conditions and the following
 *           disclaimer in the documentation and/or other materials provided
 *           with the distribution.
 *
 *         * Neither the name of Prentice Hall nor the names of the software
 *           authors or contributors may be used to endorse or promote
 *           products derived from this software without specific prior
 *           written permission.
 *
 *      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS, AUTHORS, AND
 *      CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *      INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *      MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *      IN NO EVENT SHALL PRENTICE HALL OR ANY AUTHORS OR CONTRIBUTORS BE
 *      LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *      CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *      SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *      BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *      WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *      OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *      EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * [original code from minix codebase]
 */
/*========================================================================*
 *				Mined.h					  *
 *========================================================================*/

#define INTEL	1
#define CHIP	INTEL
#define ASSUME_CONS25
#define ASSUME_XTERM

#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#ifndef YMAX
#ifdef UNIX
#include <stdio.h>
#undef putchar
#undef getchar
#undef NULL
#undef EOF
extern char *CE, *VS, *SO, *SE, *CL, *AL, *CM;
#define YMAX		49
#else
#define YMAX		24		/* Maximum y coordinate starting at 0 */
/* Escape sequences. */
extern const char *enter_string;	/* String printed on entering mined */
extern const char *rev_video;		/* String for starting reverse video */
extern const char *normal_video;	/* String for leaving reverse video */
extern const char *rev_scroll;		/* String for reverse scrolling */
extern const char *pos_string;		/* Absolute cursor positioning */
#define X_PLUS	' '		/* To be added to x for cursor sequence */
#define Y_PLUS	' '		/* To be added to y for cursor sequence */
#endif /* UNIX */

#define XMAX		79		/* Maximum x coordinate starting at 0*/
#define SCREENMAX	(YMAX - 1)	/* Number of lines displayed */
#define XBREAK		(XMAX - 1)	/* Line shift at this coordinate */
#define SHIFT_SIZE	25		/* Number of chars to shift */
#define SHIFT_MARK	'!'		/* Char indicating line continues */
#define MAX_CHARS	1024		/* Maximum chars on one line */

/* LINE_START must be rounded up to the lowest SHIFT_SIZE */
#define LINE_START	(((-MAX_CHARS - 1) / SHIFT_SIZE) * SHIFT_SIZE \
  				   - SHIFT_SIZE)
#define LINE_END	(MAX_CHARS + 1)	/* Highest x-coordinate for line */

#define LINE_LEN	(XMAX + 1)	/* Number of characters on line */
#define SCREEN_SIZE	(XMAX * YMAX)	/* Size of I/O buffering */
#define BLOCK_SIZE	1024

/* Return values of functions */
#define ERRORS		-1
#define NO_LINE		(ERRORS - 1)	/* Must be < 0 */
#define FINE	 	(ERRORS + 1)
#define NO_INPUT	(ERRORS + 2)

#define STD_OUT	 	1		/* File descriptor for terminal */

#if (CHIP == INTEL)
#define MEMORY_SIZE	(50 * 1024)	/* Size of data space to malloc */
#endif

#define REPORT	2			/* Report change of lines on # lines */

typedef int FLAG;

/* General flags */
#define	FALSE		0
#define	TRUE		1
#define	NOT_VALID	2
#define	VALID		3
#define	OFF		4
#define	ON		5

/* Expression flags */
#define	FORWARD		6
#define	REVERSE		7

/* Yank flags */
#define	SMALLER		8
#define	BIGGER		9
#define	SAME		10
#define	EMPTY		11
#define	NO_DELETE	12
#define	DELETE		13
#define	READ		14
#define	WRITE		15

/*
 * The Line structure.  Each line entry contains a pointer to the next line,
 * a pointer to the previous line, a pointer to the text and an unsigned char
 * telling at which offset of the line printing should start (usually 0).
 */
struct Line {
  struct Line *next;
  struct Line *prev;
  char *text;
  unsigned char shift_count;
};

typedef struct Line LINE;

/* Dummy line indicator */
#define DUMMY		0x80
#define DUMMY_MASK	0x7F

/* Expression definitions */
#define NO_MATCH	0
#define MATCH		1
#define REG_ERROR	2

#define BEGIN_LINE	(2 * REG_ERROR)
#define END_LINE	(2 * BEGIN_LINE)

/*
 * The regex structure. Status can be any of 0, BEGIN_LINE or REG_ERROR. In
 * the last case, the result.err_mess field is assigned. Start_ptr and end_ptr
 * point to the match found. For more details see the documentation file.
 */
struct regex {
  union {
  	const char *err_mess;
  	int *expression;
  } result;
  char status;
  char *start_ptr;
  char *end_ptr;
};

typedef struct regex REGEX;

/* NULL definitions */
#define NIL_PTR		((char *) 0)
#define NIL_LINE	((LINE *) 0)
#define NIL_REG		((REGEX *) 0)
#define NIL_INT		((int *) 0)

/*
 * Forward declarations
 */
extern int nlines;		/* Number of lines in file */
extern LINE *header;		/* Head of line list */
extern LINE *tail;		/* Last line in line list */
extern LINE *top_line;		/* First line of screen */
extern LINE *bot_line;		/* Last line of screen */
extern LINE *cur_line;		/* Current line in use */
extern char *cur_text;		/* Pointer to char on current line in use */
extern int last_y;		/* Last y of screen. Usually SCREENMAX */
extern int ymax;
extern int screenmax;
extern char screen[SCREEN_SIZE];/* Output buffer for "writes" and "reads" */

extern int x, y;			/* x, y coordinates on screen */
extern FLAG modified;			/* Set when file is modified */
extern FLAG stat_visible;		/* Set if status_line is visible */
extern FLAG writable;			/* Set if file cannot be written */
extern FLAG quit;			/* Set when quit character is typed */
extern FLAG rpipe;		/* Set if file should be read from stdin */
extern int input_fd;			/* Fd for command input */
extern FLAG loading;			/* Set if we're loading a file */
extern int out_count;			/* Index in output buffer */
extern char file_name[LINE_LEN];	/* Name of file in use */
extern char text_buffer[MAX_CHARS];	/* Buffer for modifying text */
extern const char *blank_line;		/* Clear line to end */

extern char yank_file[];		/* Temp file for buffer */
extern FLAG yank_status;		/* Status of yank_file */
extern long chars_saved;		/* Nr of chars saved in buffer */

/*
 * Empty output buffer
 */
#define clear_buffer()			(out_count = 0)

/*
 * Print character on terminal
 */
#define putchar(c)			write_char(STD_OUT, (c))

/*
 * Ring bell on terminal
 */
#define ring_bell()			putchar('\07')

/*
 * Print string on terminal
 */
#define string_print(str)		writeline(STD_OUT, (str))

/*
 * Flush output buffer
 */
#define flush()				flush_buffer(STD_OUT)

/*
 * Convert cnt to nearest tab position
 */
#define tab(cnt)			(((cnt) + 8) & ~07)
#define is_tab(c)			((c) == '\t')

/*
 * Word defenitions
 */
#define white_space(c)	((c) == ' ' || (c) == '\t')
#define alpha(c)	((c) != ' ' && (c) != '\t' && (c) != '\n')

/*
 * Print line on terminal at offset 0 and clear tail of line
 */
#define line_print(line)		put_line(line, 0, TRUE)

/*
 * Move to coordinates and set textp. (Don't use address)
 */
#define move_to(nx, ny)			move((nx), NIL_PTR, (ny))

/*
 * Move to coordinates on screen as indicated by textp.
 */
#define move_address(address)		move(0, (address), y)

/*
 * Functions handling status_line. ON means in reverse video.
 */
#define status_line(str1, str2)	bottom_line(ON, (str1), (str2), NIL_PTR, FALSE)
#define error(str1, str2)	bottom_line(ON, (str1), (str2), NIL_PTR, FALSE)
#define get_string(str1,str2, fl) bottom_line(ON, (str1), NIL_PTR, (str2), fl)
#define clear_status()		bottom_line(OFF, NIL_PTR, NIL_PTR,	\
					    NIL_PTR, FALSE)

/*
 * Print info about current file and buffer.
 */
#define fstatus(mess, cnt)	file_status((mess), (cnt), file_name, \
					     nlines, writable, modified)

/*
 * Get real shift value.
 */
#define get_shift(cnt)		((cnt) & DUMMY_MASK)

#endif /* YMAX */

/* mined1.c */

void	 FS(int);
void	 VI(int);
int	 WT(void);
void	 XWT(int);
void	 SH(int);
LINE	*proceed(LINE *line, int count);
int	 bottom_line(FLAG revfl, const char *s1, const char *s2, char *inbuf, FLAG statfl);
int	 count_chars(LINE *line);
void	 move(int new_x, char *new_address, int new_y);
int	 find_x(LINE *line, char *address);
char	*find_address(LINE *line, int x_coord, int *old_x);
int	 length_of(char *string);
void	 copy_string(char *to, const char *from);
void	 reset(LINE *head_line, int screen_y);
void	 set_cursor(int nx, int ny);
void	 open_device(void);
int	 getchar(void);
void	 display(int x_coord, int y_coord, LINE *line, int count);
int	 write_char(int fd, char c);
int	 writeline(int fd, const char *text);
void	 put_line(LINE *line, int offset, FLAG clear_line);
int	 flush_buffer(int fd);
void	 bad_write(int fd);
void	 catch(int sig);
void	 abort_mined(void);
void	 raw_mode(FLAG state);
void	 panic(const char *message) __dead2;
void	*alloc(int bytes);
void	 free_space(char *p);
void	 initialize(void);
char	*basename(char *path);
void	 load_file(const char *file);
int	 get_line(int fd, char *buffer);
LINE	*install_line(const char *buffer, int length);
void	 RD(int);
void	 I(int);
void	 XT(int);
void	 ESC(int);
int	 ask_save(void);
int	 line_number(void);
void	 file_status(const char *message, long count, char *file, int lines,
		     FLAG writefl, FLAG changed);
void	 build_string(char *buf, const char *fmt, ...);
char	*num_out(long number);
int	 get_number(const char *message, int *result);
int	 input(char *inbuf, FLAG clearfl);
int	 get_file(const char *message, char *file);
int	 _getchar(void);
void	 _flush(void);
void	 _putchar(int c);
void	 get_term(void);

/* mined2.c */

void	 UP(int);
void	 DN(int);
void	 LF(int);
void	 RT(int);
void	 HIGH(int);
void	 LOW(int);
void	 BL(int);
void	 EL(int);
void	 GOTO(int);
void	 HLP(int);
void	 ST(int);
void	 PD(int);
void	 PU(int);
void	 HO(int);
void	 EF(int);
void	 SU(int);
void	 SD(int);
int	 forward_scroll(void);
int	 reverse_scroll(void);
void	 MP(int);
void	 move_previous_word(FLAG remove);
void	 MN(int);
void	 move_next_word(FLAG remove);
void	 DCC(int);
void	 DPC(int);
void	 DLN(int);
void	 DNW(int);
void	 DPW(int);
void	 S(int character);
void	 CTL(int);
void	 LIB(int);
LINE	*line_insert(LINE *line, const char *string, int len);
int	 insert(LINE *line, char *location, char *string);
LINE	*line_delete(LINE *line);
void	 delete(LINE *start_line, char *start_textp,
		LINE *end_line, char *end_textp);
void	 PT(int);
void	 IF(int);
void	 file_insert(int fd, FLAG old_pos);
void	 WB(int);
void	 MA(int);
void	 YA(int);
void	 DT(int);
void	 set_up(FLAG remove);
FLAG	 checkmark(void);
int	 legal(void);
void	 yank(LINE *start_line, char *start_textp,
	      LINE *end_line, char *end_textp, FLAG remove);
int	 scratch_file(FLAG mode);
void	 SF(int);
void	 SR(int);
REGEX	*get_expression(const char *message);
void	 GR(int);
void	 LR(int);
void	 change(const char *message, FLAG file);
char	*substitute(LINE *line, REGEX *program, char *replacement);
void	 search(const char *message, FLAG method);
int	 find_y(LINE *match_line);
void	 finished(REGEX *program, int *last_exp);
void	 compile(char *pattern, REGEX *program);
LINE	*match(REGEX *program, char *string, FLAG method);
int	 line_check(REGEX *program, char *string, FLAG method);
int	 check_string(REGEX *program, char *string, int *expression);
int	 star(REGEX *program, char *end_position, char *string,
	      int *expression);
int	 in_list(int *list, char c, int list_length, int opcode);
void	 dummy_line(void);
