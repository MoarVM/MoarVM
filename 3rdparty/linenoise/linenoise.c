/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2013, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2013, Henry Rawas <henryr@at schakra dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * CHA (Cursor Horizontal Absolute)
 *    Sequence: ESC [ n G
 *    Effect: moves cursor to column n
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward of n chars
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * The following are used to clear the screen: ESC [ H ESC [ 2 J
 * This is actually composed of two sequences:
 *
 * cursorhome
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED2 (Clear entire screen)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include "linenoise.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define isatty _isatty
#define snprintf _snprintf
#define __NOTUSED(V) ((void) V)
#endif

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
static const char *unsupported_term[] = {"dumb","cons25",NULL};
static linenoiseCompletionCallback *completionCallback = NULL;

#ifndef _WIN32
static struct termios orig_termios; /* In order to restore at exit.*/
#endif

static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

/* for ctrl-r */
static char        *search_buf = NULL;
static unsigned int search_pos = -1;


/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    int fd;             /* Terminal file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    const char *prompt; /* Prompt to display. */
    size_t plen;        /* Prompt length. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t len;         /* Current edited line length. */
    size_t cols;        /* Number of columns in terminal. */
    size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
};

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char *line);
static void linenoiseHistoryCacheSearchBuf(char **cache, char *buf_data);
static int linenoiseHistorySearch(char *str);

static void refreshLine(struct linenoiseState *l);

#ifdef _WIN32
#ifndef STDIN_FILENO
#define STDIN_FILENO (_fileno(stdin))
#endif

static HANDLE hOut;
static HANDLE hIn;
static DWORD consolemode;

static int win32read(char *c) {

    DWORD foo;
    INPUT_RECORD b;
    KEY_EVENT_RECORD e;

    while (1) {
        if (!ReadConsoleInput(hIn, &b, 1, &foo)) return 0;
        if (!foo) return 0;

        if (b.EventType == KEY_EVENT && b.Event.KeyEvent.bKeyDown) {

            e = b.Event.KeyEvent;
            *c = e.wVirtualKeyCode;

            //if (e.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
                /* Alt+key ignored */
            //} else
            if (e.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
                /* Ctrl+Key */
                switch (*c) {
                    case 'D':
                    case 'C':
                    case 'H':
                    case 'R': /* ctrl-r, history search */
                    case 'T':
                    case 'B': /* ctrl-b, left_arrow */
                    case 'F': /* ctrl-f right_arrow*/
                    case 'P': /* ctrl-p up_arrow*/
                    case 'N': /* ctrl-n down_arrow*/
                    case 'U': /* Ctrl+u, delete the whole line. */
                    case 'K': /* Ctrl+k, delete from current to end of line. */
                    case 'A': /* Ctrl+a, go to the start of the line */
                    case 'E': /* ctrl+e, go to the end of the line */
                    case 'L': /* ctrl+l, clear screen */
                    case 'W': /* ctrl+w, delete previous word */
                        *c = *c - 'A' + 1;
                        return 1;
                }

                /* Other Ctrl+KEYs ignored */
            } else {
                switch (*c) {

                    case VK_ESCAPE: /* ignore - send ctrl-c, will return -1 */
                        *c = 3;
                        return 1;
                    case VK_RETURN:  /* enter */
                        *c = 13;
                        return 1;
                    case VK_LEFT:   /* left */
                        *c = 2;
                        return 1;
                    case VK_RIGHT: /* right */
                        *c = 6;
                        return 1;
                    case VK_UP:   /* up */
                        *c = 16;
                        return 1;
                    case VK_DOWN:  /* down */
                        *c = 14;
                        return 1;
                    case VK_HOME:
                        *c = 1;
                        return 1;
                    case VK_END:
                        *c = 5;
                        return 1;
                    case VK_BACK:
                        *c = 8;
                        return 1;
                    case VK_DELETE:
                        *c = 4;
                        return 1;
                    default:
                        if (*c = e.uChar.AsciiChar) return 1;
                }
            }
        }
    }

    return -1; /* Makes compiler happy */
}

/* ======================= Clear Screen API for Windows ====================== */

/* see http://support.microsoft.com/kb/99261 */

/* Standard error macro for reporting API errors */
#define PERR(bSuccess, api) if(!(bSuccess)) \
    printf("%s:Error %d from %s on line %d\n", __FILE__, GetLastError(), api, __LINE__);

static void cls( HANDLE hConsole )
 {
    COORD coordScreen = { 0, 0 };    /* here's where we'll home the
                                        cursor */
    BOOL bSuccess;
    DWORD cCharsWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */
    DWORD dwConSize;                 /* number of character cells in
                                        the current buffer */

    /* get the number of character cells in the current buffer */
    bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
#ifdef LN_DEBUG
    PERR( bSuccess, "GetConsoleScreenBufferInfo" );
#endif
    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

    /* fill the entire screen with blanks */
    bSuccess = FillConsoleOutputCharacter( hConsole, (TCHAR) ' ',
       dwConSize, coordScreen, &cCharsWritten );
#ifdef LN_DEBUG
    PERR( bSuccess, "FillConsoleOutputCharacter" );
#endif

    /* get the current text attribute */
    bSuccess = GetConsoleScreenBufferInfo( hConsole, &csbi );
#ifdef LN_DEBUG
    PERR( bSuccess, "ConsoleScreenBufferInfo" );
#endif

    /* now set the buffer's attributes accordingly */
    bSuccess = FillConsoleOutputAttribute( hConsole, csbi.wAttributes,
       dwConSize, coordScreen, &cCharsWritten );
#ifdef LN_DEBUG
    PERR( bSuccess, "FillConsoleOutputAttribute" );
#endif

    /* put the cursor at (0, 0) */
    bSuccess = SetConsoleCursorPosition( hConsole, coordScreen );
#ifdef LN_DEBUG
    PERR( bSuccess, "SetConsoleCursorPosition" );
#endif
    return;
}

#ifdef __STRICT_ANSI__
static char *strdup(const char *s) {
    size_t l = strlen(s)+1;
    char *p = malloc(l);

    memcpy(p,s,l);
    return p;
}
#endif /*   __STRICT_ANSI__   */

#endif /*   _WIN32    */

/* ======================= Low level terminal handling ====================== */

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
#ifndef _WIN32
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
#endif
    return 0;
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd) {
#ifndef _WIN32
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode = 1;
#else
    __NOTUSED(fd);

    if (!atexit_registered) {
        /* Init windows console handles only once */
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut==INVALID_HANDLE_VALUE) goto fatal;

        if (!GetConsoleMode(hOut, &consolemode)) {
            CloseHandle(hOut);
            errno = ENOTTY;
            return -1;
        };

        hIn = GetStdHandle(STD_INPUT_HANDLE);
        if (hIn == INVALID_HANDLE_VALUE) {
            CloseHandle(hOut);
            errno = ENOTTY;
            return -1;
        }

        GetConsoleMode(hIn, &consolemode);
        SetConsoleMode(hIn, ENABLE_LINE_INPUT);

        /* Cleanup them at exit */
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }

    rawmode = 1;
#endif
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
#ifdef _WIN32
    __NOTUSED(fd);
    rawmode = 0;
#else
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
#endif
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(void) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO b;

    if (!GetConsoleScreenBufferInfo(hOut, &b)) return 80;
    return b.srWindow.Right - b.srWindow.Left;
#else
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) return 80;
    return ws.ws_col;
#endif
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
#ifdef _WIN32
    cls(hOut);
#else
    if (write(STDIN_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
#endif
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void) {
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, NULL };
    int nread, nwritten;
    char c = 0;

    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        linenoiseBeep();
    } else {
        size_t stop = 0, i = 0;

        while(!stop) {
            /* Show completion or original buffer */
            if (i < lc.len) {
                struct linenoiseState saved = *ls;

                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            } else {
                refreshLine(ls);
            }

#ifdef _WIN32
            nread = win32read(&c);
#else
            nread = read(ls->fd,&c,1);
#endif
            if (nread <= 0) {
                freeCompletions(&lc);
                return -1;
            }

            switch(c) {
                case 9: /* tab */
                    i = (i+1) % (lc.len+1);
                    if (i == lc.len) linenoiseBeep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if (i < lc.len) refreshLine(ls);
                    stop = 1;
                    break;
                default:
                    /* Update buffer and return */
                    if (i < lc.len) {
                        nwritten = snprintf(ls->buf,ls->buflen,"%s",lc.cvec[i]);
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }

    freeCompletions(&lc);
    return c; /* Return last read character */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, char *str) {
    size_t len = strlen(str);
    char *copy = (char*)malloc(len+1);
    memcpy(copy,str,len+1);
    lc->cvec = (char**)realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState *l) {
    char seq[64];
#ifdef _WIN32
    DWORD pl, bl, w;
    CONSOLE_SCREEN_BUFFER_INFO b;
    COORD coord;
#endif
    const size_t plen = strlen(l->prompt);
    int fd = l->fd;
    char *buf = l->buf;
    size_t len = l->len;
    size_t pos = l->pos;

    while((plen+pos) >= l->cols) {
        buf++;
        len--;
        pos--;
    }
    while (plen+len > l->cols) {
        len--;
    }

#ifdef _WIN32
    __NOTUSED(seq);
    __NOTUSED(fd);

    /* Get buffer console info */
    if (!GetConsoleScreenBufferInfo(hOut, &b)) return;
    /* Erase Line */
    coord.X = 0;
    coord.Y = b.dwCursorPosition.Y;
    FillConsoleOutputCharacterA(hOut, ' ', b.dwSize.X, coord, &w);
    /*  Cursor to the left edge */
    SetConsoleCursorPosition(hOut, coord);
    /* Write the prompt and the current buffer content */
    WriteConsole(hOut, l->prompt, (DWORD)plen, &pl, NULL);
    WriteConsole(hOut, buf, (DWORD)len, &bl, NULL);
    /* Move cursor to original position. */
    coord.X = (int)(pos+plen);
    coord.Y = b.dwCursorPosition.Y;
    SetConsoleCursorPosition(hOut, coord);
#else

    /* Cursor to left edge */
    snprintf(seq,64,"\x1b[0G");
    if (write(fd,seq,strlen(seq)) == -1) return;
    /* Write the prompt and the current buffer content */
    if (write(fd,l->prompt,plen) == -1) return;
    if (write(fd,buf,len) == -1) return;
    /* Erase to right */
    snprintf(seq,64,"\x1b[0K");
    if (write(fd,seq,strlen(seq)) == -1) return;
    /* Move cursor to original position. */
    snprintf(seq,64,"\x1b[0G\x1b[%dC", (int)(pos+plen));
    if (write(fd,seq,strlen(seq)) == -1) return;
#endif
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.
 * TODO: refreshMultiLine didn't work on Windows yet. */
static void refreshMultiLine(struct linenoiseState *l) {
    char seq[64];
    int plen = strlen(l->prompt);
    int rows = (plen+l->len+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int old_rows = l->maxrows;
    int fd = l->fd, j;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

#ifdef LN_DEBUG
    FILE *fp = fopen("/tmp/debug.txt","a");
    fprintf(fp,"[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d",
        (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos,(int)l->maxrows,old_rows);
#endif

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    if (old_rows-rpos > 0) {
#ifdef LN_DEBUG
        fprintf(fp,", go down %d", old_rows-rpos);
#endif
        snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
        if (write(fd,seq,strlen(seq)) == -1) return;
    }

    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows-1; j++) {
#ifdef LN_DEBUG
        fprintf(fp,", clear+up");
#endif
        snprintf(seq,64,"\x1b[0G\x1b[0K\x1b[1A");
        if (write(fd,seq,strlen(seq)) == -1) return;
    }

    /* Clean the top line. */
#ifdef LN_DEBUG
    fprintf(fp,", clear");
#endif
    snprintf(seq,64,"\x1b[0G\x1b[0K");
    if (write(fd,seq,strlen(seq)) == -1) return;

    /* Write the prompt and the current buffer content */
    if (write(fd,l->prompt,strlen(l->prompt)) == -1) return;
    if (write(fd,l->buf,l->len) == -1) return;

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
        l->pos == l->len &&
        (l->pos+plen) % l->cols == 0)
    {
#ifdef LN_DEBUG
        fprintf(fp,", <newline>");
#endif
        if (write(fd,"\n",1) == -1) return;
        snprintf(seq,64,"\x1b[0G");
        if (write(fd,seq,strlen(seq)) == -1) return;
        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }

    /* Move cursor to right position. */
    rpos2 = (plen+l->pos+l->cols)/l->cols; /* current cursor relative row. */
#ifdef LN_DEBUG
    fprintf(fp,", rpos2 %d", rpos2);
#endif
    /* Go up till we reach the expected positon. */
    if (rows-rpos2 > 0) {
#ifdef LN_DEBUG
        fprintf(fp,", go-up %d", rows-rpos2);
#endif
        snprintf(seq,64,"\x1b[%dA", rows-rpos2);
        if (write(fd,seq,strlen(seq)) == -1) return;
    }
    /* Set column. */
#ifdef LN_DEBUG
    fprintf(fp,", set col %d", 1+((plen+(int)l->pos) % (int)l->cols));
#endif
    snprintf(seq,64,"\x1b[%dG", 1+((plen+(int)l->pos) % (int)l->cols));
    if (write(fd,seq,strlen(seq)) == -1) return;

    l->oldpos = l->pos;

#ifdef LN_DEBUG
    fprintf(fp,"\n");
    fclose(fp);
#endif
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refreshLine(struct linenoiseState *l) {
    if (mlmode)
        refreshMultiLine(l);
    else
        refreshSingleLine(l);
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
static int linenoiseEditInsert(struct linenoiseState *l, int c) {
#ifdef _WIN32
    DWORD foo;
#endif
    if (l->len < l->buflen) {
        if (l->len == l->pos) {
            l->buf[l->pos] = c;
            l->pos++;
            l->len++;
            l->buf[l->len] = '\0';
            if ((!mlmode && l->plen+l->len < l->cols) /* || mlmode */) {
                /* Avoid a full update of the line in the
                 * trivial case. */
#ifdef _WIN32
                if (!WriteConsole(hOut, &c, 1, &foo, NULL)) return -1;
#else
                if (write(l->fd,&c,1) == -1) return -1;
#endif
            } else {
                refreshLine(l);
            }
        } else {
            memmove(l->buf+l->pos+1,l->buf+l->pos,l->len-l->pos);
            l->buf[l->pos] = c;
            l->len++;
            l->pos++;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

/* Move cursor on the left. */
static void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos--;
        refreshLine(l);
    }
}

/* Move cursor on the right. */
static void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos++;
        refreshLine(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
static void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len-1;
            return;
        }
        strncpy(l->buf,history[history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
static void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        memmove(l->buf+l->pos,l->buf+l->pos+1,l->len-l->pos-1);
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
static void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        memmove(l->buf+l->pos-1,l->buf+l->pos,l->len-l->pos);
        l->pos--;
        l->len--;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
static void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0 && l->buf[l->pos-1] == ' ')
        l->pos--;
    while (l->pos > 0 && l->buf[l->pos-1] != ' ')
        l->pos--;
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    refreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(int fd, char *buf, size_t buflen, const char *prompt)
{
    struct linenoiseState l;
#ifdef _WIN32
    DWORD foo;
#endif

    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l.fd = fd;
    l.buf = buf;
    l.buflen = buflen;
    l.prompt = prompt;
    l.plen = strlen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.cols = getColumns();
    l.maxrows = 0;
    l.history_index = 0;

    /* Buffer starts empty. */
    buf[0] = '\0';
    buflen--; /* Make sure there is always space for the nulterm */

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("");

#ifdef _WIN32
    if (!WriteConsole(hOut, prompt, (DWORD)l.plen, &foo, NULL)) return -1;
#else
    if (write(fd,prompt,l.plen) == -1) return -1;
#endif
    while(1) {
        char c;
        int nread;
        char seq[2], seq2[2];

#ifdef _WIN32
        nread = win32read(&c);
#else
        nread = read(fd,&c,1);
#endif
        if (nread <= 0) return l.len;

        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (c == 9 && completionCallback != NULL) {
            c = completeLine(&l);
            /* Return on errors */
            if (c < 0) return l.len;
            /* Read next character when 0 */
            if (c == 0) continue;
        }

        /* Only keep searching buf if it's ctrl-r, or ctrl-r and then press tab, destroy otherwise */
        if (search_buf && c != 9 && c != 18) {
            free(search_buf);
            search_buf = NULL;
            search_pos = -1;
        }

        switch(c) {
        case 13:    /* enter */
            history_len--;
            free(history[history_len]);
            return (int)l.len;
        case 3:     /* ctrl-c */
            errno = EAGAIN;
            exit(0);
            return -1;
        case 127:   /* backspace */
        case 8:     /* ctrl-h */
            linenoiseEditBackspace(&l);
            break;
        case 4:     /* delete, ctrl-d, remove char at right of cursor, or of the
                       line is empty, act as end-of-file. */
            if (l.len > 0) {
                linenoiseEditDelete(&l);
            } else {
                history_len--;
                free(history[history_len]);
                return -1;
            }
            break;
        case 18: {   /* ctrl-r, search the history */
            int h_pos;

            if(!search_buf)
                linenoiseHistoryCacheSearchBuf(&search_buf, buf);

            if ((h_pos = linenoiseHistorySearch(search_buf)) != -1) {
                strcpy(buf, history[h_pos]);
                l.len = l.pos = strlen(buf);
                refreshLine(&l);
            }
            break;
        }
        case 9: {   /* tab */
            int h_pos;

            if (search_buf && (h_pos = linenoiseHistorySearch(search_buf)) != -1) {
                strcpy(buf, history[h_pos]);
                l.len = l.pos = strlen(buf);
                refreshLine(&l);
            }
            break;
        }
        case 20:    /* ctrl-t, swaps current character with previous. */
            if (l.pos > 0 && l.pos < l.len) {
                int aux = buf[l.pos-1];
                buf[l.pos-1] = buf[l.pos];
                buf[l.pos] = aux;
                if (l.pos != l.len-1) l.pos++;
                refreshLine(&l);
            }
            break;
        case 2:     /* ctrl-b */
            linenoiseEditMoveLeft(&l);
            break;
        case 6:     /* ctrl-f */
            linenoiseEditMoveRight(&l);
            break;
        case 16:    /* ctrl-p */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
        case 14:    /* ctrl-n */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
        case 27:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence. */
            if (read(fd,seq,2) == -1) break;

            if (seq[0] == 91 && seq[1] == 68) {
                /* Left arrow */
                linenoiseEditMoveLeft(&l);
            } else if (seq[0] == 91 && seq[1] == 67) {
                /* Right arrow */
                linenoiseEditMoveRight(&l);
            } else if (seq[0] == 79 && seq[1] == 72) {
                /* Home button */
                l.pos = 0;
                refreshLine(&l);
            } else if (seq[0] == 79 && seq[1] == 70) {
                /* End button */
                l.pos = l.len;
                refreshLine(&l);
            } else if (seq[0] == 91 && (seq[1] == 65 || seq[1] == 66)) {
                /* Up and Down arrows */
                linenoiseEditHistoryNext(&l,
                    (seq[1] == 65) ? LINENOISE_HISTORY_PREV :
                                     LINENOISE_HISTORY_NEXT);
            } else if (seq[0] == 91 && seq[1] > 48 && seq[1] < 55) {
                /* extended escape, read additional two bytes. */
                if (read(fd,seq2,2) == -1) break;
                if (seq[1] == 51 && seq2[0] == 126) {
                    /* Delete key. */
                    linenoiseEditDelete(&l);
                }
            }
            break;
        default:
            if (linenoiseEditInsert(&l,c)) return -1;
            break;
        case 21: /* Ctrl+u, delete the whole line. */
            buf[0] = '\0';
            l.pos = l.len = 0;
            refreshLine(&l);
            break;
        case 11: /* Ctrl+k, delete from current to end of line. */
            buf[l.pos] = '\0';
            l.len = l.pos;
            refreshLine(&l);
            break;
        case 1: /* Ctrl+a, go to the start of the line */
            l.pos = 0;
            refreshLine(&l);
            break;
        case 5: /* ctrl+e, go to the end of the line */
            l.pos = l.len;
            refreshLine(&l);
            break;
        case 12: /* ctrl+l, clear screen */
            linenoiseClearScreen();
            refreshLine(&l);
            break;
        case 23: /* ctrl+w, delete previous word */
            linenoiseEditDeletePrevWord(&l);
            break;
        }
    }
    return l.len;
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(char *buf, size_t buflen, const char *prompt) {
    int fd = STDIN_FILENO;
    int count;

    if (buflen == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!isatty(STDIN_FILENO)) {
        if (fgets(buf, buflen, stdin) == NULL) return -1;
        count = strlen(buf);
        if (count && buf[count-1] == '\n') {
            count--;
            buf[count] = '\0';
        }
    } else {
        if (enableRawMode(fd) == -1) return -1;
        count = linenoiseEdit(fd, buf, buflen, prompt);
        disableRawMode(fd);
        printf("\n");
    }
    return count;
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise(const char *prompt) {
    char buf[LINENOISE_MAX_LINE];
    int count;

    if (isUnsupportedTerm()) {
        size_t len;

        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,LINENOISE_MAX_LINE,stdin) == NULL) return NULL;
        len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    } else {
        count = linenoiseRaw(buf,LINENOISE_MAX_LINE,prompt);
        if (count == -1) return NULL;
        return strdup(buf);
    }
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void freeHistory(void) {
    if (history) {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
#ifdef _WIN32
    SetConsoleMode(hIn, consolemode);
    CloseHandle(hOut);
    CloseHandle(hIn);
#else
    disableRawMode(STDIN_FILENO);
#endif
    if(search_buf)
        free(search_buf);
    freeHistory();
}

/* Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line) {
    char *linecopy;

    if (history_max_len == 0) return 0;
    if (history == NULL) {
        history = (char**)malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char*)*history_max_len));
    }
    linecopy = strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }

    //do not insert duplicate lines into history
    if (history_len > 0 && !strcmp(line, history[history_len - 1]))
        return 0;
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len) {
    char **new_history;

    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;

        new_history = (char**)malloc(sizeof(char*)*len);
        if (new_history == NULL) return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(history[j]);
            tocopy = len;
        }
        memset(new_history,0,sizeof(char*)*len);
        memcpy(new_history,history+(history_len-tocopy), sizeof(char*)*tocopy);
        free(history);
        history = new_history;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(char *filename) {
#ifdef _WIN32
    FILE *fp = fopen(filename,"wb");
#else
    FILE *fp = fopen(filename,"w");
#endif
    int j;

    if (fp == NULL) return -1;
    for (j = 0; j < history_len; j++)
        fprintf(fp,"%s\n",history[j]);
    fclose(fp);
    return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_MAX_LINE];

    if (fp == NULL) return -1;

    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *p;

        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}

static void linenoiseHistoryCacheSearchBuf(char **cache, char *buf_data) {
    if (*cache)
        free(*cache);

    *cache = strdup(buf_data);
}

static int linenoiseHistorySearch(char *str) {
    int i;

    if (str == NULL)
        return -1;

    for (i = (search_pos == -1 ? history_len - 1 : search_pos); i >= 0; i--) {
        if (strstr(history[i], str) != NULL) {
            search_pos = i - 1;
            return i;
        }
    }

    /* if it's end, search from start again */
    search_pos = -1 ;

    return -1;
}