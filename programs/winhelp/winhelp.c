/*
 * Help Viewer
 *
 * Copyright    1996 Ulrich Schmid <uschmid@mail.hh.provi.de>
 *              2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *              2002, 2008 Eric Pouech <eric.pouech@wanadoo.fr>
 *              2004 Ken Belleau <jamez@ivic.qc.ca>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "commdlg.h"
#include "winhelp.h"
#include "winhelp_res.h"
#include "shellapi.h"
#include "richedit.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winhelp);

static BOOL    WINHELP_RegisterWinClasses(void);
static LRESULT CALLBACK WINHELP_MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WINHELP_TextWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WINHELP_ButtonBoxWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WINHELP_ButtonWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WINHELP_HistoryWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WINHELP_ShadowWndProc(HWND, UINT, WPARAM, LPARAM);
static void    WINHELP_CheckPopup(UINT);
static BOOL    WINHELP_SplitLines(HWND hWnd, LPSIZE);
static void    WINHELP_InitFonts(HWND hWnd);
static void    WINHELP_DeleteLines(WINHELP_WINDOW*);
static void    WINHELP_DeleteWindow(WINHELP_WINDOW*);
static void    WINHELP_DeleteButtons(WINHELP_WINDOW*);
static void    WINHELP_SetupText(HWND hWnd, WINHELP_WINDOW *win, ULONG relative);
static void    WINHELP_DeletePageLinks(HLPFILE_PAGE* page);
static WINHELP_LINE_PART* WINHELP_IsOverLink(WINHELP_WINDOW*, WPARAM, LPARAM);

WINHELP_GLOBALS Globals = {3, NULL, TRUE, NULL, NULL, NULL, NULL, NULL, {{{NULL,NULL}},0}};

static BOOL use_richedit;

#define CTL_ID_BUTTON   0x700
#define CTL_ID_TEXT     0x701

/***********************************************************************
 *
 *           WINHELP_GetOpenFileName
 */
BOOL WINHELP_GetOpenFileName(LPSTR lpszFile, int len)
{
    OPENFILENAME openfilename;
    CHAR szDir[MAX_PATH];
    CHAR szzFilter[2 * MAX_STRING_LEN + 100];
    LPSTR p = szzFilter;

    WINE_TRACE("()\n");

    LoadString(Globals.hInstance, STID_HELP_FILES_HLP, p, MAX_STRING_LEN);
    p += strlen(p) + 1;
    lstrcpy(p, "*.hlp");
    p += strlen(p) + 1;
    LoadString(Globals.hInstance, STID_ALL_FILES, p, MAX_STRING_LEN);
    p += strlen(p) + 1;
    lstrcpy(p, "*.*");
    p += strlen(p) + 1;
    *p = '\0';

    GetCurrentDirectory(sizeof(szDir), szDir);

    lpszFile[0]='\0';

    openfilename.lStructSize       = sizeof(OPENFILENAME);
    openfilename.hwndOwner         = NULL;
    openfilename.hInstance         = Globals.hInstance;
    openfilename.lpstrFilter       = szzFilter;
    openfilename.lpstrCustomFilter = 0;
    openfilename.nMaxCustFilter    = 0;
    openfilename.nFilterIndex      = 1;
    openfilename.lpstrFile         = lpszFile;
    openfilename.nMaxFile          = len;
    openfilename.lpstrFileTitle    = 0;
    openfilename.nMaxFileTitle     = 0;
    openfilename.lpstrInitialDir   = szDir;
    openfilename.lpstrTitle        = 0;
    openfilename.Flags             = 0;
    openfilename.nFileOffset       = 0;
    openfilename.nFileExtension    = 0;
    openfilename.lpstrDefExt       = 0;
    openfilename.lCustData         = 0;
    openfilename.lpfnHook          = 0;
    openfilename.lpTemplateName    = 0;

    return GetOpenFileName(&openfilename);
}

static char* WINHELP_GetCaption(WINHELP_WNDPAGE* wpage)
{
    if (wpage->wininfo->caption[0]) return wpage->wininfo->caption;
    return wpage->page->file->lpszTitle;
}

/***********************************************************************
 *
 *           WINHELP_LookupHelpFile
 */
HLPFILE* WINHELP_LookupHelpFile(LPCSTR lpszFile)
{
    HLPFILE*        hlpfile;
    char szFullName[MAX_PATH];
    char szAddPath[MAX_PATH];
    char *p;

    /*
     * NOTE: This is needed by popup windows only.
     * In other cases it's not needed but does not hurt though.
     */
    if (Globals.active_win && Globals.active_win->page && Globals.active_win->page->file)
    {
        strcpy(szAddPath, Globals.active_win->page->file->lpszPath);
        p = strrchr(szAddPath, '\\');
        if (p) *p = 0;
    }

    /*
     * FIXME: Should we swap conditions?
     */
    if (!SearchPath(NULL, lpszFile, ".hlp", MAX_PATH, szFullName, NULL) &&
        !SearchPath(szAddPath, lpszFile, ".hlp", MAX_PATH, szFullName, NULL))
    {
        if (WINHELP_MessageBoxIDS_s(STID_FILE_NOT_FOUND_s, lpszFile, STID_WHERROR,
                                    MB_YESNO|MB_ICONQUESTION) != IDYES)
            return NULL;
        if (!WINHELP_GetOpenFileName(szFullName, MAX_PATH))
            return NULL;
    }
    hlpfile = HLPFILE_ReadHlpFile(szFullName);
    if (!hlpfile)
        WINHELP_MessageBoxIDS_s(STID_HLPFILE_ERROR_s, lpszFile,
                                STID_WHERROR, MB_OK|MB_ICONSTOP);
    return hlpfile;
}

/******************************************************************
 *		WINHELP_GetWindowInfo
 *
 *
 */
HLPFILE_WINDOWINFO*     WINHELP_GetWindowInfo(HLPFILE* hlpfile, LPCSTR name)
{
    static      HLPFILE_WINDOWINFO      mwi;
    unsigned int     i;

    if (!name || !name[0])
        name = Globals.active_win->lpszName;

    if (hlpfile)
        for (i = 0; i < hlpfile->numWindows; i++)
            if (!strcmp(hlpfile->windows[i].name, name))
                return &hlpfile->windows[i];

    if (strcmp(name, "main") != 0)
    {
        WINE_FIXME("Couldn't find window info for %s\n", name);
        assert(0);
        return NULL;
    }
    if (!mwi.name[0])
    {
        strcpy(mwi.type, "primary");
        strcpy(mwi.name, "main");
        if (!LoadString(Globals.hInstance, STID_WINE_HELP, 
                        mwi.caption, sizeof(mwi.caption)))
            strcpy(mwi.caption, hlpfile->lpszTitle);
        mwi.origin.x = mwi.origin.y = mwi.size.cx = mwi.size.cy = CW_USEDEFAULT;
        mwi.style = SW_SHOW;
        mwi.win_style = WS_OVERLAPPEDWINDOW;
        mwi.sr_color = mwi.sr_color = 0xFFFFFF;
    }
    return &mwi;
}

/******************************************************************
 *		HLPFILE_GetPopupWindowInfo
 *
 *
 */
static HLPFILE_WINDOWINFO*     WINHELP_GetPopupWindowInfo(HLPFILE* hlpfile,
                                                          WINHELP_WINDOW* parent, LPARAM mouse)
{
    static      HLPFILE_WINDOWINFO      wi;

    RECT parent_rect;
    
    wi.type[0] = wi.name[0] = wi.caption[0] = '\0';

    /* Calculate horizontal size and position of a popup window */
    GetWindowRect(parent->hMainWnd, &parent_rect);
    wi.size.cx = (parent_rect.right  - parent_rect.left) / 2;
    wi.size.cy = 10; /* need a non null value, so that border are taken into account while computing */

    wi.origin.x = (short)LOWORD(mouse);
    wi.origin.y = (short)HIWORD(mouse);
    ClientToScreen(parent->hMainWnd, &wi.origin);
    wi.origin.x -= wi.size.cx / 2;
    wi.origin.x  = min(wi.origin.x, GetSystemMetrics(SM_CXSCREEN) - wi.size.cx);
    wi.origin.x  = max(wi.origin.x, 0);

    wi.style = SW_SHOW;
    wi.win_style = WS_POPUP | WS_BORDER;
    wi.sr_color = parent->info->sr_color;
    wi.nsr_color = 0xFFFFFF;

    return &wi;
}

/***********************************************************************
 *
 *           WinMain
 */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG                 msg;
    LONG                lHash = 0;
    HLPFILE*            hlpfile;
    static CHAR         default_wndname[] = "main";
    LPSTR               wndname = default_wndname;
    WINHELP_DLL*        dll;

    Globals.hInstance = hInstance;

    use_richedit = getenv("WINHELP_RICHEDIT") != NULL;
    if (use_richedit && LoadLibrary("riched20.dll") == NULL)
        return MessageBox(0, MAKEINTRESOURCE(STID_NO_RICHEDIT),
                          MAKEINTRESOURCE(STID_WHERROR), MB_OK);

    /* Get options */
    while (*cmdline && (*cmdline == ' ' || *cmdline == '-'))
    {
        CHAR   option;
        LPCSTR topic_id;
        if (*cmdline++ == ' ') continue;

        option = *cmdline;
        if (option) cmdline++;
        while (*cmdline && *cmdline == ' ') cmdline++;
        switch (option)
	{
	case 'i':
	case 'I':
            topic_id = cmdline;
            while (*cmdline && *cmdline != ' ') cmdline++;
            if (*cmdline) *cmdline++ = '\0';
            lHash = HLPFILE_Hash(topic_id);
            break;

	case '3':
	case '4':
            Globals.wVersion = option - '0';
            break;

        case 'x':
            show = SW_HIDE; 
            Globals.isBook = FALSE;
            break;

        default:
            WINE_FIXME("Unsupported cmd line: %s\n", cmdline);
            break;
	}
    }

    /* Create primary window */
    if (!WINHELP_RegisterWinClasses())
    {
        WINE_FIXME("Couldn't register classes\n");
        return 0;
    }

    if (*cmdline)
    {
        char*   ptr;
        if ((*cmdline == '"') && (ptr = strchr(cmdline+1, '"')))
        {
            cmdline++;
            *ptr = '\0';
        }
        if ((ptr = strchr(cmdline, '>')))
        {
            *ptr = '\0';
            wndname = ptr + 1;
        }
        hlpfile = WINHELP_LookupHelpFile(cmdline);
        if (!hlpfile) return 0;
    }
    else hlpfile = NULL;
    WINHELP_OpenHelpWindow(HLPFILE_PageByHash, hlpfile, lHash,
                           WINHELP_GetWindowInfo(hlpfile, wndname), show);

    /* Message loop */
    while (GetMessage(&msg, 0, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    for (dll = Globals.dlls; dll; dll = dll->next)
    {
        if (dll->class & DC_INITTERM) dll->handler(DW_TERM, 0, 0);
    }
    return 0;
}

/***********************************************************************
 *
 *           RegisterWinClasses
 */
static BOOL WINHELP_RegisterWinClasses(void)
{
    WNDCLASS class_main, class_button_box, class_text, class_shadow, class_history;

    class_main.style               = CS_HREDRAW | CS_VREDRAW;
    class_main.lpfnWndProc         = WINHELP_MainWndProc;
    class_main.cbClsExtra          = 0;
    class_main.cbWndExtra          = sizeof(LONG);
    class_main.hInstance           = Globals.hInstance;
    class_main.hIcon               = LoadIcon(Globals.hInstance, MAKEINTRESOURCE(IDI_WINHELP));
    class_main.hCursor             = LoadCursor(0, IDC_ARROW);
    class_main.hbrBackground       = (HBRUSH)(COLOR_WINDOW+1);
    class_main.lpszMenuName        = 0;
    class_main.lpszClassName       = MAIN_WIN_CLASS_NAME;

    class_button_box               = class_main;
    class_button_box.lpfnWndProc   = WINHELP_ButtonBoxWndProc;
    class_button_box.cbWndExtra    = 0;
    class_button_box.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    class_button_box.lpszClassName = BUTTON_BOX_WIN_CLASS_NAME;

    class_text                     = class_main;
    class_text.lpfnWndProc         = WINHELP_TextWndProc;
    class_text.hbrBackground       = 0;
    class_text.lpszClassName       = TEXT_WIN_CLASS_NAME;

    class_shadow                   = class_main;
    class_shadow.lpfnWndProc       = WINHELP_ShadowWndProc;
    class_shadow.cbWndExtra        = 0;
    class_shadow.hbrBackground     = (HBRUSH)(COLOR_3DDKSHADOW+1);
    class_shadow.lpszClassName     = SHADOW_WIN_CLASS_NAME;

    class_history                  = class_main;
    class_history.lpfnWndProc      = WINHELP_HistoryWndProc;
    class_history.lpszClassName    = HISTORY_WIN_CLASS_NAME;

    return (RegisterClass(&class_main) &&
            RegisterClass(&class_button_box) &&
            RegisterClass(&class_text) &&
            RegisterClass(&class_shadow) &&
            RegisterClass(&class_history));
}

typedef struct
{
    WORD size;
    WORD command;
    LONG data;
    LONG reserved;
    WORD ofsFilename;
    WORD ofsData;
} WINHELP,*LPWINHELP;

/******************************************************************
 *		WINHELP_HandleCommand
 *
 *
 */
static LRESULT  WINHELP_HandleCommand(HWND hSrcWnd, LPARAM lParam)
{
    COPYDATASTRUCT*     cds = (COPYDATASTRUCT*)lParam;
    WINHELP*            wh;

    if (cds->dwData != 0xA1DE505)
    {
        WINE_FIXME("Wrong magic number (%08lx)\n", cds->dwData);
        return 0;
    }

    wh = (WINHELP*)cds->lpData;

    if (wh)
    {
        char*   ptr = (wh->ofsFilename) ? (LPSTR)wh + wh->ofsFilename : NULL;

        WINE_TRACE("Got[%u]: cmd=%u data=%08x fn=%s\n",
                   wh->size, wh->command, wh->data, ptr);
        switch (wh->command)
        {
        case HELP_CONTEXT:
            if (ptr)
            {
                MACRO_JumpContext(ptr, "main", wh->data);
            }
            break;
        case HELP_QUIT:
            MACRO_Exit();
            break;
        case HELP_CONTENTS:
            if (ptr)
            {
                MACRO_JumpContents(ptr, "main");
            }
            break;
        case HELP_HELPONHELP:
            MACRO_HelpOn();
            break;
        /* case HELP_SETINDEX: */
        case HELP_SETCONTENTS:
            if (ptr)
            {
                MACRO_SetContents(ptr, wh->data);
            }
            break;
        case HELP_CONTEXTPOPUP:
            if (ptr)
            {
                MACRO_PopupContext(ptr, wh->data);
            }
            break;
        /* case HELP_FORCEFILE:*/
        /* case HELP_CONTEXTMENU: */
        case HELP_FINDER:
            /* in fact, should be the topic dialog box */
            WINE_FIXME("HELP_FINDER: stub\n");
            if (ptr)
            {
                MACRO_JumpHash(ptr, "main", 0);
            }
            break;
        /* case HELP_WM_HELP: */
        /* case HELP_SETPOPUP_POS: */
        /* case HELP_KEY: */
        /* case HELP_COMMAND: */
        /* case HELP_PARTIALKEY: */
        /* case HELP_MULTIKEY: */
        /* case HELP_SETWINPOS: */
        default:
            WINE_FIXME("Unhandled command (%x) for remote winhelp control\n", wh->command);
            break;
        }
    }
    /* Always return success for now */
    return 1;
}

void            WINHELP_LayoutMainWindow(WINHELP_WINDOW* win)
{
    RECT        rect, button_box_rect;
    INT         text_top = 0;
    HWND        hButtonBoxWnd = GetDlgItem(win->hMainWnd, CTL_ID_BUTTON);
    HWND        hTextWnd = GetDlgItem(win->hMainWnd, CTL_ID_TEXT);

    GetClientRect(win->hMainWnd, &rect);

    /* Update button box and text Window */
    SetWindowPos(hButtonBoxWnd, HWND_TOP,
                 rect.left, rect.top,
                 rect.right - rect.left,
                 rect.bottom - rect.top, 0);

    if (GetWindowRect(hButtonBoxWnd, &button_box_rect))
        text_top = rect.top + button_box_rect.bottom - button_box_rect.top;

    SetWindowPos(hTextWnd, HWND_TOP,
                 rect.left, text_top,
                 rect.right - rect.left,
                 rect.bottom - text_top, 0);

}

static void     WINHELP_RememberPage(WINHELP_WINDOW* win, WINHELP_WNDPAGE* wpage)
{
    unsigned        num;

    if (!Globals.history.index || Globals.history.set[0].page != wpage->page)
    {
        num = sizeof(Globals.history.set) / sizeof(Globals.history.set[0]);
        /* we're full, remove latest entry */
        if (Globals.history.index == num)
        {
            HLPFILE_FreeHlpFile(Globals.history.set[num - 1].page->file);
            Globals.history.index--;
        }
        memmove(&Globals.history.set[1], &Globals.history.set[0],
                Globals.history.index * sizeof(Globals.history.set[0]));
        Globals.history.set[0] = *wpage;
        Globals.history.index++;
        wpage->page->file->wRefCount++;
    }
    if (win->hHistoryWnd) InvalidateRect(win->hHistoryWnd, NULL, TRUE);

    num = sizeof(win->back.set) / sizeof(win->back.set[0]);
    if (win->back.index == num)
    {
        /* we're full, remove latest entry */
        HLPFILE_FreeHlpFile(win->back.set[0].page->file);
        memmove(&win->back.set[0], &win->back.set[1],
                (num - 1) * sizeof(win->back.set[0]));
        win->back.index--;
    }
    win->back.set[win->back.index++] = *wpage;
    wpage->page->file->wRefCount++;
}

/***********************************************************************
 *
 *           WINHELP_CreateHelpWindow
 */
BOOL WINHELP_CreateHelpWindow(WINHELP_WNDPAGE* wpage, int nCmdShow, BOOL remember)
{
    WINHELP_WINDOW*     win = NULL;
    BOOL                bPrimary, bPopup, bReUsed = FALSE;
    LPSTR               name;
    HICON               hIcon;
    HWND                hTextWnd = NULL;

    bPrimary = !lstrcmpi(wpage->wininfo->name, "main");
    bPopup = !bPrimary && (wpage->wininfo->win_style & WS_POPUP);

    if (wpage->page && !wpage->page->first_paragraph) HLPFILE_BrowsePage(wpage->page, NULL);

    if (!bPopup)
    {
        for (win = Globals.win_list; win; win = win->next)
        {
            if (!lstrcmpi(win->lpszName, wpage->wininfo->name))
            {
                POINT   pt = {0, 0};
                SIZE    sz = {0, 0};
                DWORD   flags = SWP_NOSIZE | SWP_NOMOVE;

                WINHELP_DeleteButtons(win);
                bReUsed = TRUE;
                SetWindowText(win->hMainWnd, WINHELP_GetCaption(wpage));
                if (wpage->wininfo->origin.x != CW_USEDEFAULT &&
                    wpage->wininfo->origin.y != CW_USEDEFAULT)
                {
                    pt = wpage->wininfo->origin;
                    flags &= ~SWP_NOSIZE;
                }
                if (wpage->wininfo->size.cx != CW_USEDEFAULT &&
                    wpage->wininfo->size.cy != CW_USEDEFAULT)
                {
                    sz = wpage->wininfo->size;
                    flags &= ~SWP_NOMOVE;
                }
                SetWindowPos(win->hMainWnd, HWND_TOP, pt.x, pt.y, sz.cx, sz.cy, flags);

                if (wpage->page && wpage->page->file != win->page->file)
                    WINHELP_DeleteBackSet(win);
                WINHELP_InitFonts(win->hMainWnd);

                win->page = wpage->page;
                win->info = wpage->wininfo;
                hTextWnd = GetDlgItem(win->hMainWnd, CTL_ID_TEXT);
                WINHELP_SetupText(hTextWnd, win, wpage->relative);

                InvalidateRect(win->hMainWnd, NULL, TRUE);
                if (win->hHistoryWnd) InvalidateRect(win->hHistoryWnd, NULL, TRUE);
                break;
            }
        }
    }

    if (!win)
    {
        /* Initialize WINHELP_WINDOW struct */
        win = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        sizeof(WINHELP_WINDOW) + strlen(wpage->wininfo->name) + 1);
        if (!win) return FALSE;
        win->next = Globals.win_list;
        Globals.win_list = win;

        name = (char*)win + sizeof(WINHELP_WINDOW);
        lstrcpy(name, wpage->wininfo->name);
        win->lpszName = name;
        win->hHandCur = LoadCursorW(0, (LPWSTR)IDC_HAND);
        win->back.index = 0;
    }
    win->page = wpage->page;
    win->info = wpage->wininfo;

    win->hArrowCur = LoadCursorA(0, (LPSTR)IDC_ARROW);
    win->hHandCur = LoadCursorA(0, (LPSTR)IDC_HAND);

    if (!bPopup && wpage->page && remember)
    {
        WINHELP_RememberPage(win, wpage);
    }

    if (bPopup)
        Globals.active_popup = win;
    else
        Globals.active_win = win;

    /* Initialize default pushbuttons */
    if (bPrimary && wpage->page)
    {
        CHAR    buffer[MAX_STRING_LEN];

        LoadString(Globals.hInstance, STID_CONTENTS, buffer, sizeof(buffer));
        MACRO_CreateButton("BTN_CONTENTS", buffer, "Contents()");
        LoadString(Globals.hInstance, STID_SEARCH,buffer, sizeof(buffer));
        MACRO_CreateButton("BTN_SEARCH", buffer, "Search()");
        LoadString(Globals.hInstance, STID_BACK, buffer, sizeof(buffer));
        MACRO_CreateButton("BTN_BACK", buffer, "Back()");
        if (win->back.index <= 1) MACRO_DisableButton("BTN_BACK");
        LoadString(Globals.hInstance, STID_HISTORY, buffer, sizeof(buffer));
        MACRO_CreateButton("BTN_HISTORY", buffer, "History()");
        LoadString(Globals.hInstance, STID_TOPICS, buffer, sizeof(buffer));
        MACRO_CreateButton("BTN_TOPICS", buffer, "Finder()");
    }

    if (!bReUsed)
    {
        win->hMainWnd = CreateWindowEx((bPopup) ? WS_EX_TOOLWINDOW : 0, MAIN_WIN_CLASS_NAME,
                                       WINHELP_GetCaption(wpage),
                                       bPrimary ? WS_OVERLAPPEDWINDOW : wpage->wininfo->win_style,
                                       wpage->wininfo->origin.x, wpage->wininfo->origin.y,
                                       wpage->wininfo->size.cx, wpage->wininfo->size.cy,
                                       bPopup ? Globals.active_win->hMainWnd : NULL,
                                       bPrimary ? LoadMenu(Globals.hInstance, MAKEINTRESOURCE(MAIN_MENU)) : 0,
                                       Globals.hInstance, win);
        if (!bPopup)
            /* Create button box and text Window */
            CreateWindow(BUTTON_BOX_WIN_CLASS_NAME, "", WS_CHILD | WS_VISIBLE,
                         0, 0, 0, 0, win->hMainWnd, (HMENU)CTL_ID_BUTTON, Globals.hInstance, NULL);

        if (!use_richedit)
            hTextWnd = CreateWindow(TEXT_WIN_CLASS_NAME, "", WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0, win->hMainWnd, (HMENU)CTL_ID_TEXT, Globals.hInstance, win);
        else
        {
            hTextWnd = CreateWindow(RICHEDIT_CLASS, NULL,
                                    ES_MULTILINE | ES_READONLY | WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_VISIBLE,
                                    0, 0, 0, 0, win->hMainWnd, (HMENU)CTL_ID_TEXT, Globals.hInstance, NULL);
            SendMessage(hTextWnd, EM_SETEVENTMASK, 0,
                        SendMessage(hTextWnd, EM_GETEVENTMASK, 0, 0) | ENM_MOUSEEVENTS);
        }
    }

    hIcon = (wpage->page) ? wpage->page->file->hIcon : NULL;
    if (!hIcon) hIcon = LoadIcon(Globals.hInstance, MAKEINTRESOURCE(IDI_WINHELP));
    SendMessage(win->hMainWnd, WM_SETICON, ICON_SMALL, (DWORD_PTR)hIcon);

    /* Initialize file specific pushbuttons */
    if (!(wpage->wininfo->win_style & WS_POPUP) && wpage->page)
    {
        HLPFILE_MACRO  *macro;
        for (macro = wpage->page->file->first_macro; macro; macro = macro->next)
            MACRO_ExecuteMacro(macro->lpszMacro);

        for (macro = wpage->page->first_macro; macro; macro = macro->next)
            MACRO_ExecuteMacro(macro->lpszMacro);
    }

    WINHELP_LayoutMainWindow(win);

    ShowWindow(win->hMainWnd, nCmdShow);
    WINHELP_SetupText(hTextWnd, win, wpage->relative);

    return TRUE;
}

/******************************************************************
 *             WINHELP_OpenHelpWindow
 * Main function to search for a page and display it in a window
 */
BOOL WINHELP_OpenHelpWindow(HLPFILE_PAGE* (*lookup)(HLPFILE*, LONG, ULONG*),
                            HLPFILE* hlpfile, LONG val, HLPFILE_WINDOWINFO* wi,
                            int nCmdShow)
{
    WINHELP_WNDPAGE     wpage;

    wpage.page = lookup(hlpfile, val, &wpage.relative);
    if (wpage.page) wpage.page->file->wRefCount++;
    wpage.wininfo = wi;
    return WINHELP_CreateHelpWindow(&wpage, nCmdShow, TRUE);
}

/***********************************************************************
 *
 *           WINHELP_FindLink
 */
static HLPFILE_LINK* WINHELP_FindLink(WINHELP_WINDOW* win, LPARAM pos)
{
    HLPFILE_LINK*           link;
    POINTL                  mouse_ptl, char_ptl, char_next_ptl;
    DWORD                   cp;

    if (!win->page) return NULL;

    mouse_ptl.x = (short)LOWORD(pos);
    mouse_ptl.y = (short)HIWORD(pos);
    cp = SendMessageW(GetDlgItem(win->hMainWnd, CTL_ID_TEXT), EM_CHARFROMPOS,
                      0, (LPARAM)&mouse_ptl);

    for (link = win->page->first_link; link; link = link->next)
    {
        if (link->cpMin <= cp && cp <= link->cpMax)
        {
            /* check whether we're at end of line */
            SendMessageW(GetDlgItem(win->hMainWnd, CTL_ID_TEXT), EM_POSFROMCHAR,
                         (LPARAM)&char_ptl, cp);
            SendMessageW(GetDlgItem(win->hMainWnd, CTL_ID_TEXT), EM_POSFROMCHAR,
                         (LPARAM)&char_next_ptl, cp + 1);
            if (char_next_ptl.y != char_ptl.y || mouse_ptl.x >= char_next_ptl.x)
                link = NULL;
            break;
        }
    }
    return link;
}

/******************************************************************
 *             WINHELP_HandleTextMouse
 *
 */
static BOOL WINHELP_HandleTextMouse(WINHELP_WINDOW* win, const MSGFILTER* msgf)
{
    HLPFILE*                hlpfile;
    HLPFILE_LINK*           link;
    BOOL                    ret = FALSE;

    switch (msgf->msg)
    {
    case WM_MOUSEMOVE:
        if (WINHELP_FindLink(win, msgf->lParam))
            SetCursor(win->hHandCur);
        else
            SetCursor(LoadCursor(0, IDC_ARROW));
        break;

     case WM_LBUTTONDOWN:
         if ((win->current_link = WINHELP_FindLink(win, msgf->lParam)))
             ret = TRUE;
         break;

    case WM_LBUTTONUP:
        if ((link = WINHELP_FindLink(win, msgf->lParam)) && link == win->current_link)
        {
            HLPFILE_WINDOWINFO*     wi;

            switch (link->cookie)
            {
            case hlp_link_link:
                if ((hlpfile = WINHELP_LookupHelpFile(link->string)))
                {
                    if (link->window == -1)
                        wi = win->info;
                    else if ((link->window >= 0) && (link->window < hlpfile->numWindows))
                        wi = &hlpfile->windows[link->window];
                    else
                    {
                        WINE_WARN("link to window %d/%d\n", link->window, hlpfile->numWindows);
                        break;
                    }
                    WINHELP_OpenHelpWindow(HLPFILE_PageByHash, hlpfile, link->hash, wi, SW_NORMAL);
                }
                break;
            case hlp_link_popup:
                if ((hlpfile = WINHELP_LookupHelpFile(link->string)))
                    WINHELP_OpenHelpWindow(HLPFILE_PageByHash, hlpfile, link->hash,
                                           WINHELP_GetPopupWindowInfo(hlpfile, win, msgf->lParam),
                                           SW_NORMAL);
                break;
            case hlp_link_macro:
                MACRO_ExecuteMacro(link->string);
                break;
            default:
                WINE_FIXME("Unknown link cookie %d\n", link->cookie);
            }
            ret = TRUE;
        }
        win->current_link = NULL;
        break;
    }
    return ret;
}

/***********************************************************************
 *
 *           WINHELP_MainWndProc
 */
static LRESULT CALLBACK WINHELP_MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WINHELP_WINDOW *win;
    WINHELP_BUTTON *button;
    RECT rect;
    INT  curPos, min, max, dy, keyDelta;
    HWND hTextWnd;

    WINHELP_CheckPopup(msg);

    switch (msg)
    {
    case WM_NCCREATE:
        win = (WINHELP_WINDOW*) ((LPCREATESTRUCT) lParam)->lpCreateParams;
        SetWindowLongPtr(hWnd, 0, (ULONG_PTR) win);
        if (!win->page && Globals.isBook)
            PostMessage(hWnd, WM_COMMAND, MNID_FILE_OPEN, 0);
        win->hMainWnd = hWnd;
        break;

    case WM_WINDOWPOSCHANGED:
        WINHELP_LayoutMainWindow((WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0));
        break;

    case WM_COMMAND:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
        switch (wParam)
	{
            /* Menu FILE */
	case MNID_FILE_OPEN:    MACRO_FileOpen();       break;
	case MNID_FILE_PRINT:	MACRO_Print();          break;
	case MNID_FILE_SETUP:	MACRO_PrinterSetup();   break;
	case MNID_FILE_EXIT:	MACRO_Exit();           break;

            /* Menu EDIT */
	case MNID_EDIT_COPYDLG: MACRO_CopyDialog();     break;
	case MNID_EDIT_ANNOTATE:MACRO_Annotate();       break;

            /* Menu Bookmark */
	case MNID_BKMK_DEFINE:  MACRO_BookmarkDefine(); break;

            /* Menu Help */
	case MNID_HELP_HELPON:	MACRO_HelpOn();         break;
	case MNID_HELP_HELPTOP: MACRO_HelpOnTop();      break;
	case MNID_HELP_ABOUT:	MACRO_About();          break;
	case MNID_HELP_WINE:    ShellAbout(hWnd, "WINE", "Help", 0); break;

	default:
            /* Buttons */
            for (button = win->first_button; button; button = button->next)
                if (wParam == button->wParam) break;
            if (button)
                MACRO_ExecuteMacro(button->lpszMacro);
            else if (!HIWORD(wParam))
                MessageBox(0, MAKEINTRESOURCE(STID_NOT_IMPLEMENTED),
                           MAKEINTRESOURCE(STID_WHERROR), MB_OK);
            break;
	}
        break;
/* EPP     case WM_DESTROY: */
/* EPP         if (Globals.hPopupWnd) DestroyWindow(Globals.hPopupWnd); */
/* EPP         break; */
    case WM_COPYDATA:
        return WINHELP_HandleCommand((HWND)wParam, lParam);

    case WM_KEYDOWN:
        keyDelta = 0;

        switch (wParam)
        {
        case VK_UP:
        case VK_DOWN:
            keyDelta = GetSystemMetrics(SM_CXVSCROLL);
            if (wParam == VK_UP)
                keyDelta = -keyDelta;

        case VK_PRIOR:
        case VK_NEXT:
            win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
            hTextWnd = GetDlgItem(win->hMainWnd, CTL_ID_TEXT);
            curPos = GetScrollPos(hTextWnd, SB_VERT);
            GetScrollRange(hTextWnd, SB_VERT, &min, &max);

            if (keyDelta == 0)
            {            
                GetClientRect(hTextWnd, &rect);
                keyDelta = (rect.bottom - rect.top) / 2;
                if (wParam == VK_PRIOR)
                    keyDelta = -keyDelta;
            }

            curPos += keyDelta;
            if (curPos > max)
                 curPos = max;
            else if (curPos < min)
                 curPos = min;

            dy = GetScrollPos(hTextWnd, SB_VERT) - curPos;
            SetScrollPos(hTextWnd, SB_VERT, curPos, TRUE);
            ScrollWindow(hTextWnd, 0, dy, NULL, NULL);
            UpdateWindow(hTextWnd);
            return 0;

        case VK_ESCAPE:
            MACRO_Exit();
            return 0;
        }
        break;

    case WM_NOTIFY:
        if (wParam == CTL_ID_TEXT)
        {
            return WINHELP_HandleTextMouse((WINHELP_WINDOW*)GetWindowLong(hWnd, 0),
                                           (const MSGFILTER*)lParam);
        }
        break;

    case WM_NCDESTROY:
        {
            BOOL bExit;
            win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
            bExit = (Globals.wVersion >= 4 && !lstrcmpi(win->lpszName, "main"));
            WINHELP_DeleteWindow(win);

            if (bExit) MACRO_Exit();
            if (!Globals.win_list)
                PostQuitMessage(0);
        }
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static DWORD CALLBACK WINHELP_RtfStreamIn(DWORD_PTR cookie, BYTE* buff,
                                          LONG cb, LONG* pcb)
{
    struct RtfData*     rd = (struct RtfData*)cookie;

    if (rd->where >= rd->ptr) return 1;
    if (rd->where + cb > rd->ptr)
        cb = rd->ptr - rd->where;
    memcpy(buff, rd->where, cb);
    rd->where += cb;
    *pcb = cb;
    return 0;
}

static void WINHELP_FillRichEdit(HWND hTextWnd, WINHELP_WINDOW *win, ULONG relative)
{
    SendMessage(hTextWnd, WM_SETREDRAW, FALSE, 0);
    SendMessage(hTextWnd, EM_SETBKGNDCOLOR, 0, (LPARAM)win->info->sr_color);
    /* set word-wrap to window size (undocumented) */
    SendMessage(hTextWnd, EM_SETTARGETDEVICE, 0, 0);
    if (win->page)
    {
        struct RtfData  rd;
        EDITSTREAM      es;

        if (HLPFILE_BrowsePage(win->page, &rd))
        {
            rd.where = rd.data;
            es.dwCookie = (DWORD_PTR)&rd;
            es.dwError = 0;
            es.pfnCallback = WINHELP_RtfStreamIn;

            SendMessageW(hTextWnd, EM_STREAMIN, SF_RTF, (LPARAM)&es);
        }
        /* FIXME: else leaking potentially the rd.first_link chain */
        HeapFree(GetProcessHeap(), 0, rd.data);
    }
    else
    {
        SendMessage(hTextWnd, WM_SETTEXT, 0, (LPARAM)"");
    }
    SendMessage(hTextWnd, WM_SETREDRAW, TRUE, 0);
    SendMessage(hTextWnd, EM_SETSEL, 0, 0);
    SendMessage(hTextWnd, EM_SCROLLCARET, 0, 0);
    InvalidateRect(hTextWnd, NULL, TRUE);
}

/***********************************************************************
 *
 *           WINHELP_ButtonBoxWndProc
 */
static LRESULT CALLBACK WINHELP_ButtonBoxWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WINDOWPOS      *winpos;
    WINHELP_WINDOW *win;
    WINHELP_BUTTON *button;
    SIZE button_size;
    INT  x, y;

    WINHELP_CheckPopup(msg);

    switch (msg)
    {
    case WM_WINDOWPOSCHANGING:
        winpos = (WINDOWPOS*) lParam;
        win = (WINHELP_WINDOW*) GetWindowLongPtr(GetParent(hWnd), 0);

        /* Update buttons */
        button_size.cx = 0;
        button_size.cy = 0;
        for (button = win->first_button; button; button = button->next)
	{
            HDC  hDc;
            SIZE textsize;
            if (!button->hWnd)
            {
                button->hWnd = CreateWindow(STRING_BUTTON, button->lpszName,
                                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                            0, 0, 0, 0,
                                            hWnd, (HMENU) button->wParam,
                                            Globals.hInstance, 0);
                if (button->hWnd) {
                    if (Globals.button_proc == NULL)
                        Globals.button_proc = (WNDPROC) GetWindowLongPtr(button->hWnd, GWLP_WNDPROC);
                    SetWindowLongPtr(button->hWnd, GWLP_WNDPROC, (LONG_PTR) WINHELP_ButtonWndProc);
                }
            }
            hDc = GetDC(button->hWnd);
            GetTextExtentPoint(hDc, button->lpszName,
                               lstrlen(button->lpszName), &textsize);
            ReleaseDC(button->hWnd, hDc);

            button_size.cx = max(button_size.cx, textsize.cx + BUTTON_CX);
            button_size.cy = max(button_size.cy, textsize.cy + BUTTON_CY);
	}

        x = 0;
        y = 0;
        for (button = win->first_button; button; button = button->next)
	{
            SetWindowPos(button->hWnd, HWND_TOP, x, y, button_size.cx, button_size.cy, 0);

            if (x + 2 * button_size.cx <= winpos->cx)
                x += button_size.cx;
            else
                x = 0, y += button_size.cy;
	}
        winpos->cy = y + (x ? button_size.cy : 0);
        break;

    case WM_COMMAND:
        SendMessage(GetParent(hWnd), msg, wParam, lParam);
        break;

    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_ESCAPE:
            return SendMessage(GetParent(hWnd), msg, wParam, lParam);
        }
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/***********************************************************************
 *
 *           WINHELP_ButtonWndProc
 */
static LRESULT CALLBACK WINHELP_ButtonWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN)
    {
        switch (wParam)
        {
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_ESCAPE:
            return SendMessage(GetParent(hWnd), msg, wParam, lParam);
        }
    }

    return CallWindowProc(Globals.button_proc, hWnd, msg, wParam, lParam);
}

/***********************************************************************
 *
 *           WINHELP_TextWndProc
 */
static LRESULT CALLBACK WINHELP_TextWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WINHELP_WINDOW    *win;
    WINHELP_LINE      *line;
    WINHELP_LINE_PART *part;
    WINDOWPOS         *winpos;
    PAINTSTRUCT        ps;
    HDC   hDc;
    INT   scroll_pos;
    HWND  hPopupWnd;

    if (msg != WM_LBUTTONDOWN)
        WINHELP_CheckPopup(msg);

    switch (msg)
    {
    case WM_NCCREATE:
        win = (WINHELP_WINDOW*) ((LPCREATESTRUCT) lParam)->lpCreateParams;
        SetWindowLongPtr(hWnd, 0, (ULONG_PTR) win);
        win->hBrush = CreateSolidBrush(win->info->sr_color);
        WINHELP_InitFonts(hWnd);
        break;

    case WM_CREATE:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);

        /* Calculate vertical size and position of a popup window */
        if (win->info->win_style & WS_POPUP)
	{
            POINT origin;
            RECT old_window_rect;
            RECT old_client_rect;
            SIZE old_window_size;
            SIZE old_client_size;
            SIZE new_client_size;
            SIZE new_window_size;

            GetWindowRect(GetParent(hWnd), &old_window_rect);
            origin.x = old_window_rect.left;
            origin.y = old_window_rect.top;
            old_window_size.cx = old_window_rect.right  - old_window_rect.left;
            old_window_size.cy = old_window_rect.bottom - old_window_rect.top;

            WINHELP_LayoutMainWindow(win);
            GetClientRect(hWnd, &old_client_rect);
            old_client_size.cx = old_client_rect.right  - old_client_rect.left;
            old_client_size.cy = old_client_rect.bottom - old_client_rect.top;

            new_client_size = old_client_size;
            WINHELP_SplitLines(hWnd, &new_client_size);

            if (origin.y + POPUP_YDISTANCE + new_client_size.cy <= GetSystemMetrics(SM_CYSCREEN))
                origin.y += POPUP_YDISTANCE;
            else
                origin.y -= POPUP_YDISTANCE + new_client_size.cy;

            new_window_size.cx = old_window_size.cx - old_client_size.cx + new_client_size.cx;
            new_window_size.cy = old_window_size.cy - old_client_size.cy + new_client_size.cy;

            win->hShadowWnd =
                CreateWindowEx(WS_EX_TOOLWINDOW, SHADOW_WIN_CLASS_NAME, "", WS_POPUP,
                             origin.x + SHADOW_DX, origin.y + SHADOW_DY,
                             new_window_size.cx, new_window_size.cy,
                             0, 0, Globals.hInstance, 0);

            SetWindowPos(GetParent(hWnd), HWND_TOP, origin.x, origin.y,
                         new_window_size.cx, new_window_size.cy,
                         0);
            SetWindowPos(win->hShadowWnd, hWnd, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
            ShowWindow(win->hShadowWnd, SW_NORMAL);
            SetActiveWindow(hWnd);
	}
        break;

    case WM_WINDOWPOSCHANGED:
        winpos = (WINDOWPOS*) lParam;

        if (!(winpos->flags & SWP_NOSIZE)) WINHELP_SetupText(hWnd, NULL, 0);
        break;

    case WM_MOUSEWHEEL:
    {
       int wheelDelta = 0;
       UINT scrollLines = 3;
       int curPos = GetScrollPos(hWnd, SB_VERT);
       int min, max;

       GetScrollRange(hWnd, SB_VERT, &min, &max);

       SystemParametersInfo(SPI_GETWHEELSCROLLLINES,0, &scrollLines, 0);
       if (wParam & (MK_SHIFT | MK_CONTROL))
           return DefWindowProc(hWnd, msg, wParam, lParam);
       wheelDelta -= GET_WHEEL_DELTA_WPARAM(wParam);
       if (abs(wheelDelta) >= WHEEL_DELTA && scrollLines) {
           int dy;

           curPos += wheelDelta;
           if (curPos > max)
                curPos = max;
           else if (curPos < min)
                curPos = min;

           dy = GetScrollPos(hWnd, SB_VERT) - curPos;
           SetScrollPos(hWnd, SB_VERT, curPos, TRUE);
           ScrollWindow(hWnd, 0, dy, NULL, NULL);
           UpdateWindow(hWnd);
       }
    }
    break;

    case WM_VSCROLL:
    {
	BOOL  update = TRUE;
	RECT  rect;
	INT   Min, Max;
	INT   CurPos = GetScrollPos(hWnd, SB_VERT);
	INT   dy;
	
	GetScrollRange(hWnd, SB_VERT, &Min, &Max);
	GetClientRect(hWnd, &rect);

	switch (wParam & 0xffff)
        {
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: CurPos  = wParam >> 16;                   break;
        case SB_TOP:           CurPos  = Min;                            break;
        case SB_BOTTOM:        CurPos  = Max;                            break;
        case SB_PAGEUP:        CurPos -= (rect.bottom - rect.top) / 2;   break;
        case SB_PAGEDOWN:      CurPos += (rect.bottom - rect.top) / 2;   break;
        case SB_LINEUP:        CurPos -= GetSystemMetrics(SM_CXVSCROLL); break;
        case SB_LINEDOWN:      CurPos += GetSystemMetrics(SM_CXVSCROLL); break;
        default: update = FALSE;
        }
	if (update)
        {
	    if (CurPos > Max)
                CurPos = Max;
	    else if (CurPos < Min)
                CurPos = Min;
	    dy = GetScrollPos(hWnd, SB_VERT) - CurPos;
	    SetScrollPos(hWnd, SB_VERT, CurPos, TRUE);
	    ScrollWindow(hWnd, 0, dy, NULL, NULL);
	    UpdateWindow(hWnd);
        }
    }
    break;

    case WM_PAINT:
        hDc = BeginPaint(hWnd, &ps);
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
        scroll_pos = GetScrollPos(hWnd, SB_VERT);

        /* No DPtoLP needed - MM_TEXT map mode */
        if (ps.fErase) FillRect(hDc, &ps.rcPaint, win->hBrush);
        for (line = win->first_line; line; line = line->next)
        {
            for (part = &line->first_part; part; part = part->next)
            {
                switch (part->cookie)
                {
                case hlp_line_part_text:
                    SelectObject(hDc, part->u.text.hFont);
                    SetTextColor(hDc, part->u.text.color);
                    SetBkColor(hDc, win->info->sr_color);
                    TextOut(hDc, part->rect.left, part->rect.top - scroll_pos,
                            part->u.text.lpsText, part->u.text.wTextLen);
                    if (part->u.text.wUnderline)
                    {
                        HPEN    hPen;

                        switch (part->u.text.wUnderline)
                        {
                        case 1: /* simple */
                        case 2: /* double */
                            hPen = CreatePen(PS_SOLID, 1, part->u.text.color);
                            break;
                        case 3: /* dotted */
                            hPen = CreatePen(PS_DOT, 1, part->u.text.color);
                            break;
                        default:
                            WINE_FIXME("Unknow underline type\n");
                            continue;
                        }

                        SelectObject(hDc, hPen);
                        MoveToEx(hDc, part->rect.left, part->rect.bottom - scroll_pos - 1, NULL);
                        LineTo(hDc, part->rect.right, part->rect.bottom - scroll_pos - 1);
                        if (part->u.text.wUnderline == 2)
                        {
                            MoveToEx(hDc, part->rect.left, part->rect.bottom - scroll_pos + 1, NULL);
                            LineTo(hDc, part->rect.right, part->rect.bottom - scroll_pos + 1);
                        }
                        DeleteObject(hPen);
                    }
                    break;
                case hlp_line_part_bitmap:
                    {
                        HDC hMemDC;

                        hMemDC = CreateCompatibleDC(hDc);
                        SelectObject(hMemDC, part->u.bitmap.hBitmap);
                        BitBlt(hDc, part->rect.left, part->rect.top - scroll_pos,
                               part->rect.right - part->rect.left, part->rect.bottom - part->rect.top,
                               hMemDC, 0, 0, SRCCOPY);
                        DeleteDC(hMemDC);
                    }
                    break;
                case hlp_line_part_metafile:
                    {
                        HDC hMemDC;
                        HBITMAP hBitmap;
                        SIZE sz;
                        RECT rc;

                        sz.cx = part->rect.right - part->rect.left;
                        sz.cy = part->rect.bottom - part->rect.top;
                        hMemDC = CreateCompatibleDC(hDc);
                        hBitmap = CreateCompatibleBitmap(hDc, sz.cx, sz.cy);
                        SelectObject(hMemDC, hBitmap);
                        SelectObject(hMemDC, win->hBrush);
                        rc.left = 0;
                        rc.top = 0;
                        rc.right = sz.cx;
                        rc.bottom = sz.cy;
                        FillRect(hMemDC, &rc, win->hBrush);
                        SetMapMode(hMemDC, part->u.metafile.mm);
                        SetWindowExtEx(hMemDC, sz.cx, sz.cy, 0);
                        SetViewportExtEx(hMemDC, sz.cx, sz.cy, 0);
                        SetWindowOrgEx(hMemDC, 0, 0, 0);
                        SetViewportOrgEx(hMemDC, 0, 0, 0);
                        PlayMetaFile(hMemDC, part->u.metafile.hMetaFile);
                        SetMapMode(hMemDC, MM_TEXT);
                        SetWindowOrgEx(hMemDC, 0, 0, 0);
                        SetViewportOrgEx(hMemDC, 0, 0, 0);
                        BitBlt(hDc, part->rect.left, part->rect.top - scroll_pos,
                               sz.cx, sz.cy, hMemDC, 0, 0, SRCCOPY);
                        DeleteDC(hMemDC);
                        DeleteObject(hBitmap);
                    }
                    break;
                }
            }
        }

        EndPaint(hWnd, &ps);
        break;

    case WM_MOUSEMOVE:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);

        if (WINHELP_IsOverLink(win, wParam, lParam))
            SetCursor(win->hHandCur); /* set to hand pointer cursor to indicate a link */
        else
            SetCursor(win->hArrowCur); /* set to hand pointer cursor to indicate a link */

        break;

    case WM_LBUTTONDOWN:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);

        if (Globals.active_popup)
        {
            hPopupWnd = Globals.active_popup->hMainWnd;
            Globals.active_popup = NULL;
        }
        else hPopupWnd = NULL;

        part = WINHELP_IsOverLink(win, wParam, lParam);
        if (part)
        {
            HLPFILE*            hlpfile;
            HLPFILE_WINDOWINFO* wi;

            if (part->link) switch (part->link->cookie)
            {
            case hlp_link_link:
                hlpfile = WINHELP_LookupHelpFile(part->link->string);
                if (part->link->window == -1)
                    wi = win->info;
                else if (part->link->window < hlpfile->numWindows)
                    wi = &hlpfile->windows[part->link->window];
                else
                {
                    WINE_WARN("link to window %d/%d\n", part->link->window, hlpfile->numWindows);
                    break;
                }
                WINHELP_OpenHelpWindow(HLPFILE_PageByHash, hlpfile, part->link->hash,
                                       wi, SW_NORMAL);
                break;
            case hlp_link_popup:
                hlpfile = WINHELP_LookupHelpFile(part->link->string);
                if (hlpfile) WINHELP_OpenHelpWindow(HLPFILE_PageByHash, hlpfile, part->link->hash,
                                                    WINHELP_GetPopupWindowInfo(hlpfile, win, lParam),
                                                    SW_NORMAL);
                break;
            case hlp_link_macro:
                MACRO_ExecuteMacro(part->link->string);
                break;
            default:
                WINE_FIXME("Unknown link cookie %d\n", part->link->cookie);
            }
        }

        if (hPopupWnd)
            DestroyWindow(hPopupWnd);
        break;

    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/******************************************************************
 *		WINHELP_HistoryWndProc
 *
 *
 */
static LRESULT CALLBACK WINHELP_HistoryWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WINHELP_WINDOW*     win;
    PAINTSTRUCT         ps;
    HDC                 hDc;
    TEXTMETRIC          tm;
    unsigned int        i;
    RECT                r;

    switch (msg)
    {
    case WM_NCCREATE:
        win = (WINHELP_WINDOW*)((LPCREATESTRUCT)lParam)->lpCreateParams;
        SetWindowLongPtr(hWnd, 0, (ULONG_PTR)win);
        win->hHistoryWnd = hWnd;
        break;
    case WM_CREATE:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
        hDc = GetDC(hWnd);
        GetTextMetrics(hDc, &tm);
        GetWindowRect(hWnd, &r);

        r.right = r.left + 30 * tm.tmAveCharWidth;
        r.bottom = r.top + (sizeof(Globals.history.set) / sizeof(Globals.history.set[0])) * tm.tmHeight;
        AdjustWindowRect(&r, GetWindowLong(hWnd, GWL_STYLE), FALSE);
        if (r.left < 0) {r.right -= r.left; r.left = 0;}
        if (r.top < 0) {r.bottom -= r.top; r.top = 0;}

        MoveWindow(hWnd, r.left, r.top, r.right, r.bottom, TRUE);
        ReleaseDC(hWnd, hDc);
        break;
    case WM_LBUTTONDOWN:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
        hDc = GetDC(hWnd);
        GetTextMetrics(hDc, &tm);
        i = HIWORD(lParam) / tm.tmHeight;
        if (i < Globals.history.index)
            WINHELP_CreateHelpWindow(&Globals.history.set[i], SW_SHOW, TRUE);
        ReleaseDC(hWnd, hDc);
        break;
    case WM_PAINT:
        hDc = BeginPaint(hWnd, &ps);
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
        GetTextMetrics(hDc, &tm);

        for (i = 0; i < Globals.history.index; i++)
        {
            if (Globals.history.set[i].page->file == Globals.active_win->page->file)
            {
                TextOut(hDc, 0, i * tm.tmHeight,
                        Globals.history.set[i].page->lpszTitle,
                        strlen(Globals.history.set[i].page->lpszTitle));
            }
            else
            {
                char        buffer[1024];
                const char* ptr1;
                const char* ptr2;
                unsigned    len;

                ptr1 = strrchr(Globals.history.set[i].page->file->lpszPath, '\\');
                if (!ptr1) ptr1 = Globals.history.set[i].page->file->lpszPath;
                else ptr1++;
                ptr2 = strrchr(ptr1, '.');
                len = ptr2 ? ptr2 - ptr1 : strlen(ptr1);
                if (len > sizeof(buffer)) len = sizeof(buffer);
                memcpy(buffer, ptr1, len);
                if (len < sizeof(buffer)) buffer[len++] = ':';
                strncpy(&buffer[len], Globals.history.set[i].page->lpszTitle, sizeof(buffer) - len);
                buffer[sizeof(buffer) - 1] = '\0';
                TextOut(hDc, 0, i * tm.tmHeight, buffer, strlen(buffer));
            }
        }
        EndPaint(hWnd, &ps);
        break;
    case WM_DESTROY:
        win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
        if (hWnd == win->hHistoryWnd)
            win->hHistoryWnd = 0;
        break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/***********************************************************************
 *
 *           WINHELP_ShadowWndProc
 */
static LRESULT CALLBACK WINHELP_ShadowWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WINHELP_CheckPopup(msg);
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/***********************************************************************
 *
 *           SetupText
 */
static void WINHELP_SetupText(HWND hWnd, WINHELP_WINDOW* win, ULONG relative)
{
    HDC  hDc;
    RECT rect;
    SIZE newsize;

    if (!win) win = (WINHELP_WINDOW*) GetWindowLong(hWnd, 0);
    if (use_richedit) return WINHELP_FillRichEdit(hWnd, win, relative);
    hDc = GetDC(hWnd);
    ShowScrollBar(hWnd, SB_VERT, FALSE);
    if (!WINHELP_SplitLines(hWnd, NULL))
    {
        ShowScrollBar(hWnd, SB_VERT, TRUE);
        GetClientRect(hWnd, &rect);

        WINHELP_SplitLines(hWnd, &newsize);
        SetScrollRange(hWnd, SB_VERT, 0, rect.top + newsize.cy - rect.bottom, TRUE);
    }
    else
    {
        SetScrollPos(hWnd, SB_VERT, 0, FALSE);
        SetScrollRange(hWnd, SB_VERT, 0, 0, FALSE);
    }

    ReleaseDC(hWnd, hDc);
}

/***********************************************************************
 *
 *           WINHELP_AppendText
 */
static BOOL WINHELP_AppendText(WINHELP_LINE ***linep, WINHELP_LINE_PART ***partp,
			       LPSIZE space, LPSIZE textsize,
			       INT *line_ascent, INT ascent,
			       LPCSTR text, UINT textlen,
			       HFONT font, COLORREF color, HLPFILE_LINK *link,
                               unsigned underline)
{
    WINHELP_LINE      *line;
    WINHELP_LINE_PART *part;
    LPSTR ptr;

    if (!*partp) /* New line */
    {
        *line_ascent  = ascent;

        line = HeapAlloc(GetProcessHeap(), 0,
                         sizeof(WINHELP_LINE) + textlen);
        if (!line) return FALSE;

        line->next    = 0;
        part          = &line->first_part;
        ptr           = (char*)line + sizeof(WINHELP_LINE);

        line->rect.top    = (**linep ? (**linep)->rect.bottom : 0) + space->cy;
        line->rect.bottom = line->rect.top;
        line->rect.left   = space->cx;
        line->rect.right  = space->cx;

        if (**linep) *linep = &(**linep)->next;
        **linep = line;
        space->cy = 0;
    }
    else /* Same line */
    {
        line = **linep;

        if (*line_ascent < ascent)
	{
            WINHELP_LINE_PART *p;
            for (p = &line->first_part; p; p = p->next)
	    {
                p->rect.top    += ascent - *line_ascent;
                p->rect.bottom += ascent - *line_ascent;
	    }
            line->rect.bottom += ascent - *line_ascent;
            *line_ascent = ascent;
	}

        part = HeapAlloc(GetProcessHeap(), 0,
                         sizeof(WINHELP_LINE_PART) + textlen);
        if (!part) return FALSE;
        **partp = part;
        ptr     = (char*)part + sizeof(WINHELP_LINE_PART);
    }

    memcpy(ptr, text, textlen);
    part->cookie            = hlp_line_part_text;
    part->rect.left         = line->rect.right + (*partp ? space->cx : 0);
    part->rect.right        = part->rect.left + textsize->cx;
    line->rect.right        = part->rect.right;
    part->rect.top          =
        ((*partp) ? line->rect.top : line->rect.bottom) + *line_ascent - ascent;
    part->rect.bottom       = part->rect.top + textsize->cy;
    line->rect.bottom       = max(line->rect.bottom, part->rect.bottom);
    part->u.text.lpsText    = ptr;
    part->u.text.wTextLen   = textlen;
    part->u.text.hFont      = font;
    part->u.text.color      = color;
    part->u.text.wUnderline = underline;

    WINE_TRACE("Appended text '%*.*s'[%d] @ (%d,%d-%d,%d)\n",
               part->u.text.wTextLen,
               part->u.text.wTextLen,
               part->u.text.lpsText,
               part->u.text.wTextLen,
               part->rect.left, part->rect.top, part->rect.right, part->rect.bottom);

    part->link = link;
    if (link) link->wRefCount++;

    part->next          = 0;
    *partp              = &part->next;

    space->cx = 0;

    return TRUE;
}

/***********************************************************************
 *
 *           WINHELP_AppendGfxObject
 */
static WINHELP_LINE_PART* WINHELP_AppendGfxObject(WINHELP_LINE ***linep, WINHELP_LINE_PART ***partp,
                                                  LPSIZE space, LPSIZE gfxSize,
                                                  HLPFILE_LINK *link, unsigned pos)
{
    WINHELP_LINE      *line;
    WINHELP_LINE_PART *part;
    LPSTR              ptr;

    if (!*partp || pos == 1) /* New line */
    {
        line = HeapAlloc(GetProcessHeap(), 0, sizeof(WINHELP_LINE));
        if (!line) return NULL;

        line->next    = NULL;
        part          = &line->first_part;

        line->rect.top    = (**linep ? (**linep)->rect.bottom : 0) + space->cy;
        line->rect.bottom = line->rect.top;
        line->rect.left   = space->cx;
        line->rect.right  = space->cx;

        if (**linep) *linep = &(**linep)->next;
        **linep = line;
        space->cy = 0;
        ptr = (char*)line + sizeof(WINHELP_LINE);
    }
    else /* Same line */
    {
        if (pos == 2) WINE_FIXME("Left alignment not handled\n");
        line = **linep;

        part = HeapAlloc(GetProcessHeap(), 0, sizeof(WINHELP_LINE_PART));
        if (!part) return NULL;
        **partp = part;
        ptr = (char*)part + sizeof(WINHELP_LINE_PART);
    }

    /* part->cookie should be set by caller (image or metafile) */
    part->rect.left       = line->rect.right + (*partp ? space->cx : 0);
    part->rect.right      = part->rect.left + gfxSize->cx;
    line->rect.right      = part->rect.right;
    part->rect.top        = (*partp) ? line->rect.top : line->rect.bottom;
    part->rect.bottom     = part->rect.top + gfxSize->cy;
    line->rect.bottom     = max(line->rect.bottom, part->rect.bottom);

    WINE_TRACE("Appended gfx @ (%d,%d-%d,%d)\n",
               part->rect.left, part->rect.top, part->rect.right, part->rect.bottom);

    part->link = link;
    if (link) link->wRefCount++;

    part->next            = NULL;
    *partp                = &part->next;

    space->cx = 0;

    return part;
}


/***********************************************************************
 *
 *           WINHELP_SplitLines
 */
static BOOL WINHELP_SplitLines(HWND hWnd, LPSIZE newsize)
{
    WINHELP_WINDOW     *win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
    HLPFILE_PARAGRAPH  *p;
    WINHELP_LINE      **line = &win->first_line;
    WINHELP_LINE_PART **part = 0;
    INT                 line_ascent = 0;
    SIZE                space;
    RECT                rect;
    HDC                 hDc;

    if (newsize) newsize->cx = newsize->cy = 0;

    if (!win->page) return TRUE;

    WINHELP_DeleteLines(win);

    GetClientRect(hWnd, &rect);

    rect.top    += INTERNAL_BORDER_WIDTH;
    rect.left   += INTERNAL_BORDER_WIDTH;
    rect.right  -= INTERNAL_BORDER_WIDTH;
    rect.bottom -= INTERNAL_BORDER_WIDTH;

    space.cy = rect.top;
    space.cx = rect.left;

    hDc = GetDC(hWnd);

    for (p = win->page->first_paragraph; p; p = p->next)
    {
        switch (p->cookie)
        {
        case para_normal_text:
        case para_debug_text:
            {
                TEXTMETRIC tm;
                SIZE textsize    = {0, 0};
                LPCSTR text      = p->u.text.lpszText;
                UINT indent      = 0;
                UINT len         = strlen(text);
                unsigned underline = 0;

                HFONT hFont = 0;
                COLORREF color = RGB(0, 0, 0);

                if (p->u.text.wFont < win->page->file->numFonts)
                {
                    HLPFILE*    hlpfile = win->page->file;

                    if (!hlpfile->fonts[p->u.text.wFont].hFont)
                        hlpfile->fonts[p->u.text.wFont].hFont = CreateFontIndirect(&hlpfile->fonts[p->u.text.wFont].LogFont);
                    hFont = hlpfile->fonts[p->u.text.wFont].hFont;
                    color = hlpfile->fonts[p->u.text.wFont].color;
                }
                else
                {
                    UINT  wFont = (p->u.text.wFont < win->fonts_len) ? p->u.text.wFont : 0;

                    hFont = win->fonts[wFont];
                }

                if (p->link && p->link->bClrChange)
                {
                    underline = (p->link->cookie == hlp_link_popup) ? 3 : 1;
                    color = RGB(0, 0x80, 0);
                }
                if (p->cookie == para_debug_text) color = RGB(0xff, 0, 0);

                SelectObject(hDc, hFont);

                GetTextMetrics(hDc, &tm);

                if (p->u.text.wIndent)
                {
                    indent = p->u.text.wIndent * 5 * tm.tmAveCharWidth;
                    if (!part)
                        space.cx = rect.left + indent - 2 * tm.tmAveCharWidth;
                }

                if (p->u.text.wVSpace)
                {
                    part = 0;
                    space.cx = rect.left + indent;
                    space.cy += (p->u.text.wVSpace - 1) * tm.tmHeight;
                }

                if (p->u.text.wHSpace)
                {
                    space.cx += p->u.text.wHSpace * 2 * tm.tmAveCharWidth;
                }

                WINE_TRACE("splitting text %s\n", text);

                while (len)
                {
                    INT free_width = rect.right - (part ? (*line)->rect.right : rect.left) - space.cx;
                    UINT low = 0, curr = len, high = len, textlen = 0;

                    if (free_width > 0)
                    {
                        while (1)
                        {
                            GetTextExtentPoint(hDc, text, curr, &textsize);

                            if (textsize.cx <= free_width) low = curr;
                            else high = curr;

                            if (high <= low + 1) break;

                            if (textsize.cx) curr = (curr * free_width) / textsize.cx;
                            if (curr <= low) curr = low + 1;
                            else if (curr >= high) curr = high - 1;
                        }
                        textlen = low;
                        while (textlen && text[textlen] && text[textlen] != ' ') textlen--;
                    }
                    if (!part && !textlen) textlen = max(low, 1);

                    if (free_width <= 0 || !textlen)
                    {
                        part = 0;
                        space.cx = rect.left + indent;
                        space.cx = min(space.cx, rect.right - rect.left - 1);
                        continue;
                    }

                    WINE_TRACE("\t => %d %*s\n", textlen, textlen, text);

                    if (!WINHELP_AppendText(&line, &part, &space, &textsize,
                                            &line_ascent, tm.tmAscent,
                                            text, textlen, hFont, color, p->link, underline) ||
                        (!newsize && (*line)->rect.bottom > rect.bottom))
                    {
                        ReleaseDC(hWnd, hDc);
                        return FALSE;
                    }

                    if (newsize)
                        newsize->cx = max(newsize->cx, (*line)->rect.right + INTERNAL_BORDER_WIDTH);

                    len -= textlen;
                    text += textlen;
                    if (text[0] == ' ') text++, len--;
                }
            }
            break;
        case para_bitmap:
        case para_metafile:
            {
                SIZE                    gfxSize;
                INT                     free_width;
                WINHELP_LINE_PART*      ref_part;

                if (p->u.gfx.pos & 0x8000)
                {
                    space.cx = rect.left;
                    if (*line)
                        space.cy += (*line)->rect.bottom - (*line)->rect.top;
                    part = 0;
                }

                if (p->cookie == para_bitmap)
                {
                    DIBSECTION              dibs;
                    
                    GetObject(p->u.gfx.u.bmp.hBitmap, sizeof(dibs), &dibs);
                    gfxSize.cx = dibs.dsBm.bmWidth;
                    gfxSize.cy = dibs.dsBm.bmHeight;
                }
                else
                {
                    LPMETAFILEPICT lpmfp = &p->u.gfx.u.mfp;
                    if (lpmfp->mm == MM_ANISOTROPIC || lpmfp->mm == MM_ISOTROPIC)
                    {
                        gfxSize.cx = MulDiv(lpmfp->xExt, GetDeviceCaps(hDc, HORZRES),
                                            100*GetDeviceCaps(hDc, HORZSIZE));
                        gfxSize.cy = MulDiv(lpmfp->yExt, GetDeviceCaps(hDc, VERTRES),
                                            100*GetDeviceCaps(hDc, VERTSIZE));
                    }
                    else
                    {
                        gfxSize.cx = lpmfp->xExt;
                        gfxSize.cy = lpmfp->yExt;
                    }
                }

                free_width = rect.right - ((part && *line) ? (*line)->rect.right : rect.left) - space.cx;
                if (free_width <= 0)
                {
                    part = NULL;
                    space.cx = rect.left;
                    space.cx = min(space.cx, rect.right - rect.left - 1);
                }
                ref_part = WINHELP_AppendGfxObject(&line, &part, &space, &gfxSize,
                                                   p->link, p->u.gfx.pos);
                if (!ref_part || (!newsize && (*line)->rect.bottom > rect.bottom))
                {
                    return FALSE;
                }
                if (p->cookie == para_bitmap)
                {
                    ref_part->cookie = hlp_line_part_bitmap;
                    ref_part->u.bitmap.hBitmap = p->u.gfx.u.bmp.hBitmap;
                }
                else
                {
                    ref_part->cookie = hlp_line_part_metafile;
                    ref_part->u.metafile.hMetaFile = p->u.gfx.u.mfp.hMF;
                    ref_part->u.metafile.mm = p->u.gfx.u.mfp.mm;
                }
            }
            break;
        }
    }

    if (newsize)
        newsize->cy = (*line)->rect.bottom + INTERNAL_BORDER_WIDTH;

    ReleaseDC(hWnd, hDc);
    return TRUE;
}

/***********************************************************************
 *
 *           WINHELP_CheckPopup
 */
static void WINHELP_CheckPopup(UINT msg)
{
    HWND        hPopup;

    if (!Globals.active_popup) return;

    switch (msg)
    {
    case WM_COMMAND:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
        hPopup = Globals.active_popup->hMainWnd;
        Globals.active_popup = NULL;
        DestroyWindow(hPopup);
    }
}

/***********************************************************************
 *
 *           WINHELP_DeleteLines
 */
static void WINHELP_DeleteLines(WINHELP_WINDOW *win)
{
    WINHELP_LINE      *line, *next_line;
    WINHELP_LINE_PART *part, *next_part;
    for (line = win->first_line; line; line = next_line)
    {
        next_line = line->next;
        for (part = &line->first_part; part; part = next_part)
	{
            next_part = part->next;
            HLPFILE_FreeLink(part->link);
            HeapFree(GetProcessHeap(), 0, part);
	}
    }
    win->first_line = 0;
}

/******************************************************************
 *		WINHELP_DeleteButtons
 *
 */
static void WINHELP_DeleteButtons(WINHELP_WINDOW* win)
{
    WINHELP_BUTTON*     b;
    WINHELP_BUTTON*     bp;

    for (b = win->first_button; b; b = bp)
    {
        DestroyWindow(b->hWnd);
        bp = b->next;
        HeapFree(GetProcessHeap(), 0, b);
    }
    win->first_button = NULL;
}

/******************************************************************
 *		WINHELP_DeleteBackSet
 *
 */
void WINHELP_DeleteBackSet(WINHELP_WINDOW* win)
{
    unsigned int i;

    for (i = 0; i < win->back.index; i++)
    {
        HLPFILE_FreeHlpFile(win->back.set[i].page->file);
        win->back.set[i].page = NULL;
    }
    win->back.index = 0;
}

/******************************************************************
 *             WINHELP_DeletePageLinks
 *
 */
static void WINHELP_DeletePageLinks(HLPFILE_PAGE* page)
{
    HLPFILE_LINK*       curr;
    HLPFILE_LINK*       next;

    for (curr = page->first_link; curr; curr = next)
    {
        next = curr->next;
        HeapFree(GetProcessHeap(), 0, curr);
    }
}

/***********************************************************************
 *
 *           WINHELP_DeleteWindow
 */
static void WINHELP_DeleteWindow(WINHELP_WINDOW* win)
{
    WINHELP_WINDOW**    w;

    for (w = &Globals.win_list; *w; w = &(*w)->next)
    {
        if (*w == win)
        {
            *w = win->next;
            break;
        }
    }

    if (Globals.active_win == win)
    {
        Globals.active_win = Globals.win_list;
        if (Globals.win_list)
            SetActiveWindow(Globals.win_list->hMainWnd);
    }

    if (win == Globals.active_popup)
        Globals.active_popup = NULL;

    WINHELP_DeleteButtons(win);

    if (win->page) WINHELP_DeletePageLinks(win->page);
    if (win->hShadowWnd) DestroyWindow(win->hShadowWnd);
    if (win->hHistoryWnd) DestroyWindow(win->hHistoryWnd);

    DeleteObject(win->hBrush);

    WINHELP_DeleteBackSet(win);

    if (win->page) HLPFILE_FreeHlpFile(win->page->file);
    WINHELP_DeleteLines(win);
    HeapFree(GetProcessHeap(), 0, win);
}

/***********************************************************************
 *
 *           WINHELP_InitFonts
 */
static void WINHELP_InitFonts(HWND hWnd)
{
    WINHELP_WINDOW *win = (WINHELP_WINDOW*) GetWindowLongPtr(hWnd, 0);
    LOGFONT logfontlist[] = {
        {-10, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"},
        {-12, 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"},
        {-12, 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"},
        {-12, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"},
        {-12, 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"},
        {-10, 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"},
        { -8, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 32, "Helv"}};
#define FONTS_LEN (sizeof(logfontlist)/sizeof(*logfontlist))

    static HFONT fonts[FONTS_LEN];
    static BOOL init = 0;

    win->fonts_len = FONTS_LEN;
    win->fonts = fonts;

    if (!init)
    {
        UINT i;

        for (i = 0; i < FONTS_LEN; i++)
	{
            fonts[i] = CreateFontIndirect(&logfontlist[i]);
	}

        init = 1;
    }
}

/***********************************************************************
 *
 *           WINHELP_MessageBoxIDS_s
 */
INT WINHELP_MessageBoxIDS_s(UINT ids_text, LPCSTR str, UINT ids_title, WORD type)
{
    CHAR text[MAX_STRING_LEN];
    CHAR newtext[MAX_STRING_LEN + MAX_PATH];

    LoadString(Globals.hInstance, ids_text, text, sizeof(text));
    wsprintf(newtext, text, str);

    return MessageBox(0, newtext, MAKEINTRESOURCE(ids_title), type);
}

/******************************************************************
 *		WINHELP_IsOverLink
 *
 *
 */
WINHELP_LINE_PART* WINHELP_IsOverLink(WINHELP_WINDOW* win, WPARAM wParam, LPARAM lParam)
{
    POINT mouse;
    WINHELP_LINE      *line;
    WINHELP_LINE_PART *part;
    int scroll_pos = GetScrollPos(GetDlgItem(win->hMainWnd, CTL_ID_TEXT), SB_VERT);

    mouse.x = LOWORD(lParam);
    mouse.y = HIWORD(lParam);
    for (line = win->first_line; line; line = line->next)
    {
        for (part = &line->first_part; part; part = part->next)
        {
            if (part->link && 
                part->link->string &&
                part->rect.left   <= mouse.x &&
                part->rect.right  >= mouse.x &&
                part->rect.top    <= mouse.y + scroll_pos &&
                part->rect.bottom >= mouse.y + scroll_pos)
            {
                return part;
            }
        }
    }

    return NULL;
}

/**************************************************************************
 * cb_KWBTree
 *
 * HLPFILE_BPTreeCallback enumeration function for '|KWBTREE' internal file.
 *
 */
static void cb_KWBTree(void *p, void **next, void *cookie)
{
    HWND hListWnd = (HWND)cookie;
    int count;

    WINE_TRACE("Adding '%s' to search list\n", (char *)p);
    SendMessage(hListWnd, LB_INSERTSTRING, -1, (LPARAM)p);
    count = SendMessage(hListWnd, LB_GETCOUNT, 0, 0);
    SendMessage(hListWnd, LB_SETITEMDATA, count-1, (LPARAM)p);
    *next = (char*)p + strlen((char*)p) + 7;
}

/**************************************************************************
 * WINHELP_IndexDlgProc
 *
 * Index dialog callback function.
 *
 * nResult passed to EndDialog:
 *   1: CANCEL button
 *  >1: valid offset value +2.
 *  EndDialog itself can return 0 (error).
 */
INT_PTR CALLBACK WINHELP_SearchDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HLPFILE *file;
    int sel;
    ULONG offset = 1;

    switch (msg)
    {
    case WM_INITDIALOG:
        file = (HLPFILE *)lParam;
        HLPFILE_BPTreeEnum(file->kwbtree, cb_KWBTree,
                           GetDlgItem(hWnd, IDC_INDEXLIST));
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            sel = SendDlgItemMessage(hWnd, IDC_INDEXLIST, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR)
            {
                BYTE *p;
                int count;

                p = (BYTE*)SendDlgItemMessage(hWnd, IDC_INDEXLIST,
                                              LB_GETITEMDATA, sel, 0);
                count = *(short*)((char *)p + strlen((char *)p) + 1);
                if (count > 1)
                {
                    MessageBox(hWnd, "count > 1 not supported yet", "Error", MB_OK | MB_ICONSTOP);
                    return TRUE;
                }
                offset = *(ULONG*)((char *)p + strlen((char *)p) + 3);
                offset = *(long*)(file->kwdata + offset + 9);
                if (offset == 0xFFFFFFFF)
                {
                    MessageBox(hWnd, "macro keywords not supported yet", "Error", MB_OK | MB_ICONSTOP);
                    return TRUE;
                }
                offset += 2;
            }
            /* Fall through */
        case IDCANCEL:
            EndDialog(hWnd, offset);
            return TRUE;
        default:
            break;
        }
    default:
        break;
    }
    return FALSE;
}

/**************************************************************************
 * WINHELP_CreateIndexWindow
 *
 * Displays a dialog with keywords of current help file.
 *
 */
BOOL WINHELP_CreateIndexWindow(void)
{
    int ret;
    HLPFILE *hlpfile;

    if (Globals.active_win && Globals.active_win->page && Globals.active_win->page->file)
        hlpfile = Globals.active_win->page->file;
    else
        return FALSE;

    if (hlpfile->kwbtree == NULL)
    {
        WINE_TRACE("No index provided\n");
        return FALSE;
    }

    ret = DialogBoxParam(Globals.hInstance, MAKEINTRESOURCE(IDD_INDEX),
                         Globals.active_win->hMainWnd, WINHELP_SearchDlgProc,
                         (LPARAM)hlpfile);
    if (ret > 1)
    {
        ret -= 2;
        WINE_TRACE("got %d as an offset\n", ret);
        WINHELP_OpenHelpWindow(HLPFILE_PageByOffset, hlpfile, ret,
                               Globals.active_win->info, SW_NORMAL);
    }
    return TRUE;
}
