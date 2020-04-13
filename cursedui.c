#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ncurses.h>
#include <regex.h>
#include <locale.h>

#define _POSIX_C_SOURCE 200809L

typedef struct {
    char *value;
    char *display;
} Item;

typedef struct {
    Item *items;
    size_t buflen;
    size_t length;
    int curitem;
} Menu;

int readMenu(Menu *menu, FILE *fin);
int rendermenu(Menu *menu, WINDOW *win, int useValues);
int filterMenu(Menu *menu, Menu *fmenu, char *filter);
char *runui(Menu *menu);

int
readMenu(Menu *menu, FILE *fin)
{
    char *readbuf = NULL;
    size_t buflen = 0;
    ssize_t readlen;
    char *tab;
    menu->buflen = 1024;
    menu->items = calloc(sizeof(Item), menu->buflen);
    if (menu->items == NULL) return -1;
    menu->length = 0;
    menu->curitem = 0;
    while ((readlen = getline(&readbuf, &buflen, fin)) > 0) {
        if (readlen == 0 || readbuf[0] == '\n') continue;
        if (readbuf[readlen - 1] == '\n') readbuf[readlen - 1] = 0;
        if (menu->length == menu->buflen) {
            menu->buflen += 1024;
            menu->items = realloc(menu->items, sizeof(Item) * menu->buflen);
            if (menu->items == NULL) return -1;
        }
        if ((tab = strchr(readbuf, '\t')) == NULL) {
            errno = EBADMSG;
            return -1;
        }
        *tab = 0;
        tab++;
        menu->items[menu->length].value = strdup(readbuf);
        menu->items[menu->length].display = strdup(tab);
        menu->length++;
    }
    return menu->length;
}

int
rendermenu(Menu *menu, WINDOW *win, int useValues)
{
    int winwidth, winheight, startln;
    getmaxyx(win, winheight, winwidth);
    startln = menu->curitem - winheight / 2;
    startln = startln < 0 ? 0 : startln > menu->length - winheight ? menu->length - winheight : startln;
    for (int i = startln; i  < startln + winheight; i++) {
        wmove(win, i - startln, 0);
        if (i == menu->curitem) wbkgdset(win, A_STANDOUT);
        wclrtoeol(win);
        if (i < menu->length)
            if (useValues)
                wprintw(win, ">%s", menu->items[i].value);
            else
                wprintw(win, "%s", menu->items[i].display);
        if (i == menu->curitem) wbkgdset(win, 0);
    }
    wrefresh(win);
    return winheight;
}

int
filterMenu(Menu *menu, Menu *fmenu, char *filter)
{
    if (filter == NULL || *filter == 0) {
        fmenu->length = 0;
        fmenu->curitem = 0;
        for (int i = 0; i < menu->length; i++) {
            fmenu->items[i] = menu->items[i];
            fmenu->length++;
        }
        return 0;
    }
    regex_t rx;
    int regerr;
    if ((regerr = regcomp(&rx, filter, REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0)
        return regerr;
    fmenu->length = 0;
    fmenu->curitem = 0;
    for (int i = 0; i < menu->length; i++) {
        if (regexec(&rx, menu->items[i].display, 0, NULL, 0) == 0) {
            fmenu->items[fmenu->length] = menu->items[i];
            fmenu->length++;
        }
    }
    return 0;
}

char
*runui(Menu *menu)
{
    Menu fmenu;
    fmenu.buflen = menu->length;
    fmenu.items = calloc(sizeof(Item), fmenu.buflen);
    filterMenu(menu, &fmenu, NULL);
    int running = 1;
    int useValues = 0;
    int lines;
    int regret;
    char *retval = NULL;
    char filterstr[2048];
    FILE *tty = fopen("/dev/tty", "r+"); // Bypass I/O redirection
    setlocale(LC_ALL, "");
    SCREEN *scr = newterm(NULL, tty, tty);
    WINDOW *winmenu = newwin(LINES - 1, COLS, 0, 0);
    WINDOW *winprompt = newwin(1, 0, LINES - 1, 0);
    cbreak();
    noecho();
    keypad(winmenu, 1);
    while (running) {
        lines = rendermenu(&fmenu, winmenu, useValues);
        curs_set(0);
        switch(wgetch(winmenu)) {
            case 'j': /* FALLTHROUGH */
            case KEY_DOWN:
                if (fmenu.curitem < fmenu.length - 1) fmenu.curitem++;
                break;
            case 'k': /* FALLTHROUGH */
            case KEY_UP:
                if (fmenu.curitem != 0) fmenu.curitem--;
                break;
            case 'g': /* FALLTHROUGH */
            case '^':
                fmenu.curitem = 0;
                break;
            case 'G': /* FALLTHROUGH */
            case '$':
                fmenu.curitem = fmenu.length - 1;
                break;
            case 'd': /* FALLTHROUGH */
            case KEY_NPAGE:
                fmenu.curitem += lines / 2;
                if (fmenu.curitem > fmenu.length - 1)
                    fmenu.curitem = fmenu.length - 1;
                break;
            case 'u': /* FALLTHROUGH */
            case KEY_PPAGE:
                fmenu.curitem -= lines / 2;
                if (fmenu.curitem < 0)
                    fmenu.curitem = 0;
                break;
            case 'l': /* FALLTHROUGH */
            case ' ':
            case '\n':
            case KEY_RIGHT:
                retval = fmenu.items[fmenu.curitem].value;
                running = 0;
                break;
            case 'q': /* FALLTHROUGH */
            case 'h':
            case KEY_BACKSPACE:
            case KEY_LEFT:
                running = 0;
                break;
            case '=': /* FALLTHROUGH */
            case '\t':
                useValues = !useValues;
                break;
            case KEY_RESIZE:
                mvwin(winprompt, LINES - 1, 0);
                wresize(winmenu, LINES - 1, COLS);
                if (filterstr[0] != 0) {
                    mvwaddstr(winprompt, 0, 0, "/");
                    mvwaddnstr(winprompt, 0, 1, filterstr, COLS - 1);
                } else {
                    werase(winprompt);
                }
                wrefresh(winprompt);
                wrefresh(winmenu);
                break;
            case '/':
                werase(winprompt);
                mvwaddstr(winprompt, 0, 0, "/");
                wrefresh(winprompt);
                echo();
                curs_set(1);
                wgetnstr(winprompt, filterstr, 2048);
                curs_set(0);
                noecho();
                if (filterstr[0] == 0) {
                    werase(winprompt);
                    wrefresh(winprompt);
                }
                regret = filterMenu(menu, &fmenu, filterstr);
                if (regret != 0) {
                    werase(winprompt);
                    regerror(regret, NULL, filterstr, 2048);
                    mvwprintw(winprompt, 0, 0, "/%s", filterstr);
                    wrefresh(winprompt);
                }
                break;
        }
    }
    delwin(winprompt);
    delwin(winmenu);
    endwin();
    delscreen(scr);
    return retval;
}

int
main()
{
    Menu menu;
    if (readMenu(&menu, stdin) < 0) {
        perror("readMenu");
        return 5;
    }

    char *str = runui(&menu);
    if (str == NULL) return 1;
    printf("%s\n", str);
}
