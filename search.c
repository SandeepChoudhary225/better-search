/*
 * Search Directory in Local
 * -------------------------
 * This program:
 *   1. Accepts commands: search <name> [path], exit.
 *   2. Recursively searches all local drives, or one optional path.
 *   3. Finds files and folders with similar names (case-insensitive match).
 *   4. Displays results with name, path, size, and type.
 *   5. Lets the user click a result path to open File Explorer
 *      with that file selected.
 *
 * Compile (MinGW): gcc search_dir_in_local.c -o search_dir_in_local.exe -lshell32
 * Usage:           search_dir_in_local.exe
 *                  search_dir_in_local.exe <name> [path]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <shellapi.h>

/* ── Console colours ──────────────────────────────────────────────── */
#define CLR_RESET   7
#define CLR_DIR     11   /* cyan */
#define CLR_FILE    15   /* bright white */
#define CLR_META    8    /* dark grey */
#define CLR_TREE    10   /* green */
#define CLR_HEADER  14   /* yellow */
#define CLR_SIZE    13   /* magenta */
#define CLR_MATCH   12   /* red – matched portion */
#define CLR_INDEX   9    /* red – result number */

static void *hConsole;

static void set_color(int color)
{
    SetConsoleTextAttribute(hConsole, (unsigned short)color);
}

static int g_vt_enabled = 0;

static void enable_virtual_terminal_output(void)
{
    DWORD mode = 0;
    if (GetConsoleMode(hConsole, &mode) &&
        SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        g_vt_enabled = 1;
    }
}

/* ── Helpers ──────────────────────────────────────────────────────── */

/* Case-insensitive substring search – returns pointer to match or NULL */
static const char *stristr(const char *haystack, const char *needle)
{
    if (!needle[0]) return haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

/* Format file size into a human-friendly string */
static void format_size(unsigned long long size, char *buf, size_t buflen)
{
    if (size < 1024ULL)
        snprintf(buf, buflen, "%llu B", size);
    else if (size < 1024ULL * 1024)
        snprintf(buf, buflen, "%.2f KB", size / 1024.0);
    else if (size < 1024ULL * 1024 * 1024)
        snprintf(buf, buflen, "%.2f MB", size / (1024.0 * 1024));
    else if (size < 1024ULL * 1024 * 1024 * 1024)
        snprintf(buf, buflen, "%.2f GB", size / (1024.0 * 1024 * 1024));
    else
        snprintf(buf, buflen, "%.2f TB", size / (1024.0 * 1024 * 1024 * 1024));
}

/* Get file type description from Windows Shell */
static void get_type_description(const char *filepath, char *buf, size_t buflen)
{
    SHFILEINFOA sfi;
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoA(filepath, 0, &sfi, sizeof(sfi), SHGFI_TYPENAME)) {
        strncpy(buf, sfi.szTypeName, buflen - 1);
        buf[buflen - 1] = '\0';
    } else {
        strncpy(buf, "Unknown", buflen - 1);
        buf[buflen - 1] = '\0';
    }
}

static void normalize_windows_path(char *path)
{
    char out[1024];
    int i = 0;
    int j = 0;

    while (path[i] && j < (int)sizeof(out) - 1) {
        char c = path[i] == '/' ? '\\' : path[i];
        int previous_slash = (j > 0 && out[j - 1] == '\\');
        int keep_unc_prefix = (j == 1 && out[0] == '\\');

        if (c == '\\' && previous_slash && !keep_unc_prefix) {
            i++;
            continue;
        }

        out[j++] = c;
        i++;
    }

    out[j] = '\0';
    strncpy(path, out, 1023);
    path[1023] = '\0';
}

static void join_path(char *out, size_t outlen, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    if (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/'))
        snprintf(out, outlen, "%s%s", dir, name);
    else
        snprintf(out, outlen, "%s\\%s", dir, name);

    normalize_windows_path(out);
}

/* ── Search result storage ────────────────────────────────────────── */

#define MAX_RESULTS 500

typedef struct {
    char name[260];
    char path[1024];
    unsigned long long size;
    char type_desc[128];
    int is_dir;
    SHORT path_row_start;
    SHORT path_row_end;
} SearchResult;

static SearchResult g_results[MAX_RESULTS];
static int g_result_count = 0;
static unsigned long g_files_scanned = 0;
static unsigned long g_dirs_scanned = 0;
static int g_result_limit_reached = 0;

static void add_result(const char *name, const char *path,
                       unsigned long long size, int is_dir)
{
    SearchResult *r;

    if (g_result_count >= MAX_RESULTS) {
        g_result_limit_reached = 1;
        return;
    }

    r = &g_results[g_result_count];
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->name[sizeof(r->name) - 1] = '\0';
    strncpy(r->path, path, sizeof(r->path) - 1);
    r->path[sizeof(r->path) - 1] = '\0';
    r->size = size;
    r->type_desc[0] = '\0';
    r->is_dir = is_dir;
    r->path_row_start = -1;
    r->path_row_end = -1;
    g_result_count++;
}

/* ── Recursive search ─────────────────────────────────────────────── */

static void search_recursive(const char *dirpath, const char *query)
{
    WIN32_FIND_DATAA fd;
    void *hFind;
    char search_path[1024];
    char full_path[1024];

    if (g_result_limit_reached) return;

    join_path(search_path, sizeof(search_path), dirpath, "*");
    hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        join_path(full_path, sizeof(full_path), dirpath, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            g_dirs_scanned++;
            if (stristr(fd.cFileName, query) != NULL)
                add_result(fd.cFileName, full_path, 0, 1);
            if (g_result_limit_reached) break;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;
            search_recursive(full_path, query);
        } else {
            g_files_scanned++;

            /* Check if filename contains the search query (case-insensitive) */
            if (stristr(fd.cFileName, query) != NULL) {
                unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32)
                                          | fd.nFileSizeLow;
                add_result(fd.cFileName, full_path, size, 0);
                if (g_result_limit_reached) break;
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

/* ── Print filename with matched portion highlighted ──────────────── */

static void print_highlighted_name(const char *name, const char *query)
{
    const char *match_start = stristr(name, query);
    int query_len = (int)strlen(query);

    if (!match_start) {
        set_color(CLR_FILE);
        printf("%s", name);
        return;
    }

    /* Print part before match */
    int before_len = (int)(match_start - name);
    set_color(CLR_FILE);
    printf("%.*s", before_len, name);

    /* Print matched portion in highlight colour */
    set_color(CLR_MATCH);
    printf("%.*s", query_len, match_start);

    /* Print part after match */
    set_color(CLR_FILE);
    printf("%s", match_start + query_len);
}

/* Store the console buffer rows occupied by a path line for mouse clicks. */
static void remember_path_position(SearchResult *r, size_t path_len)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        r->path_row_start = -1;
        r->path_row_end = -1;
        return;
    }

    int width = csbi.dwSize.X > 0 ? csbi.dwSize.X : 80;
    int start_x = csbi.dwCursorPosition.X;
    int start_y = csbi.dwCursorPosition.Y;
    int end_y = start_y;

    if (path_len > 0)
        end_y = start_y + (start_x + (int)path_len - 1) / width;

    r->path_row_start = (SHORT)start_y;
    r->path_row_end = (SHORT)end_y;
}

static int uri_safe_char(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
           c == ':' || c == '/';
}

static void path_to_file_uri(const char *path, char *uri, size_t urilen)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;

    if (path[0] == '\\' && path[1] == '\\')
        snprintf(uri, urilen, "file:");
    else
        snprintf(uri, urilen, "file:///");

    j = strlen(uri);

    for (size_t i = 0; path[i] && j + 4 < urilen; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c == '\\') c = '/';

        if (uri_safe_char(c)) {
            uri[j++] = (char)c;
        } else {
            uri[j++] = '%';
            uri[j++] = hex[(c >> 4) & 0x0F];
            uri[j++] = hex[c & 0x0F];
        }
    }

    uri[j] = '\0';
}

static void print_clickable_path(const char *path)
{
    char uri[2048];

    if (!g_vt_enabled) {
        printf("%s", path);
        return;
    }

    path_to_file_uri(path, uri, sizeof(uri));
    printf("\x1b]8;;%s\x1b\\%s\x1b]8;;\x1b\\", uri, path);
}

static void ensure_type_description(SearchResult *r)
{
    if (r->is_dir) {
        strncpy(r->type_desc, "Folder", sizeof(r->type_desc) - 1);
        r->type_desc[sizeof(r->type_desc) - 1] = '\0';
        return;
    }

    if (r->type_desc[0] == '\0')
        get_type_description(r->path, r->type_desc, sizeof(r->type_desc));
}

/* ── Display results ──────────────────────────────────────────────── */

static void display_results(const char *query)
{
    printf("\n");
    set_color(CLR_HEADER);
    printf("  ──────────────────────────────────────────────────\n");
    printf("  Search Results\n");
    printf("  ──────────────────────────────────────────────────\n\n");

    if (g_result_count == 0) {
        set_color(CLR_META);
        printf("  No files found matching \"%s\"\n", query);
        set_color(CLR_RESET);
        return;
    }

    for (int i = 0; i < g_result_count; i++) {
        SearchResult *r = &g_results[i];
        char size_str[64];
        if (r->is_dir)
            snprintf(size_str, sizeof(size_str), "-");
        else
            format_size(r->size, size_str, sizeof(size_str));
        ensure_type_description(r);

        /* Result number */
        set_color(CLR_INDEX);
        printf("  [%d] ", i + 1);

        /* Filename with highlight */
        print_highlighted_name(r->name, query);
        printf("\n");

        /* Path */
        set_color(CLR_TREE);
        printf("      ");
        set_color(CLR_META);
        printf("Path: ");
        set_color(CLR_DIR);
        remember_path_position(r, strlen(r->path));
        print_clickable_path(r->path);
        printf("\n");

        /* Type and Size */
        set_color(CLR_TREE);
        printf("      ");
        set_color(CLR_META);
        printf("Type: ");
        set_color(CLR_FILE);
        printf("%-20s", r->type_desc);
        set_color(CLR_META);
        printf(" | Size: ");
        set_color(CLR_SIZE);
        printf("%s\n", size_str);

        printf("\n");
    }

    /* Summary line */
    set_color(CLR_HEADER);
    printf("  ──────────────────────────────────────────────────\n");
    set_color(CLR_META);
    printf("  Found ");
    set_color(CLR_FILE);
    printf("%d", g_result_count);
    set_color(CLR_META);
    printf(" item(s) matching \"%s\"", query);
    if (g_result_count >= MAX_RESULTS) {
        set_color(CLR_MATCH);
        printf(" (results capped at %d)", MAX_RESULTS);
    }
    printf("\n");
    set_color(CLR_META);
    printf("  Scanned ");
    set_color(CLR_FILE);
    printf("%lu", g_files_scanned);
    set_color(CLR_META);
    printf(" files across ");
    set_color(CLR_DIR);
    printf("%lu", g_dirs_scanned);
    set_color(CLR_META);
    printf(" directories\n");
    set_color(CLR_RESET);
}

/* ── Open file in Explorer with the file selected ─────────────────── */

static void open_in_explorer(const char *filepath)
{
    char clean_path[1024];
    char params[1200];
    unsigned long attrs;

    strncpy(clean_path, filepath, sizeof(clean_path) - 1);
    clean_path[sizeof(clean_path) - 1] = '\0';
    normalize_windows_path(clean_path);

    attrs = GetFileAttributesA(clean_path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        snprintf(params, sizeof(params), "\"%s\"", clean_path);
    else
        snprintf(params, sizeof(params), "/select,\"%s\"", clean_path);

    set_color(CLR_META);
    printf("\n  Opening: ");
    set_color(CLR_DIR);
    printf("%s\n", clean_path);
    set_color(CLR_RESET);

    ShellExecuteA(NULL, "open", "explorer.exe", params, NULL, SW_SHOWNORMAL);
}

static int result_at_mouse_row(SHORT row)
{
    for (int i = 0; i < g_result_count; i++) {
        if (g_results[i].path_row_start >= 0 &&
            row >= g_results[i].path_row_start &&
            row <= g_results[i].path_row_end)
        {
            return i;
        }
    }
    return -1;
}

static int visible_console_rows(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return 25;
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

static void scroll_console_lines(int lines)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    SMALL_RECT win;
    int height;
    int max_top;
    int new_top;

    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return;

    win = csbi.srWindow;
    height = win.Bottom - win.Top + 1;
    max_top = csbi.dwSize.Y - height;
    if (max_top < 0) max_top = 0;

    new_top = win.Top + lines;
    if (new_top < 0) new_top = 0;
    if (new_top > max_top) new_top = max_top;

    win.Top = (SHORT)new_top;
    win.Bottom = (SHORT)(new_top + height - 1);
    SetConsoleWindowInfo(hConsole, TRUE, &win);
}

static void wait_for_path_clicks(void)
{
    void *hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD old_mode = 0;
    DWORD mode = 0;

    printf("\n");
    set_color(CLR_HEADER);
    printf("  Click a Path line to open it in File Explorer");
    set_color(CLR_META);
    printf(" (mouse wheel/PageUp/PageDown scroll, Enter returns): ");
    set_color(CLR_RESET);

    if (!GetConsoleMode(hInput, &old_mode)) {
        printf("\n");
        return;
    }

    mode = old_mode;
    mode |= ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT;
    mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

    if (!SetConsoleMode(hInput, mode)) {
        SetConsoleMode(hInput, old_mode);
        printf("\n");
        return;
    }

    FlushConsoleInputBuffer(hInput);

    int left_was_down = 0;
    while (1) {
        INPUT_RECORD rec;
        DWORD read = 0;

        if (!ReadConsoleInputA(hInput, &rec, 1, &read) || read == 0)
            break;

        if (rec.EventType == KEY_EVENT) {
            KEY_EVENT_RECORD key = rec.Event.KeyEvent;
            if (key.bKeyDown) {
                if (key.wVirtualKeyCode == VK_RETURN ||
                    key.wVirtualKeyCode == VK_ESCAPE)
                {
                    break;
                } else if (key.wVirtualKeyCode == VK_UP) {
                    scroll_console_lines(-1);
                } else if (key.wVirtualKeyCode == VK_DOWN) {
                    scroll_console_lines(1);
                } else if (key.wVirtualKeyCode == VK_PRIOR) {
                    scroll_console_lines(-visible_console_rows());
                } else if (key.wVirtualKeyCode == VK_NEXT) {
                    scroll_console_lines(visible_console_rows());
                } else if (key.wVirtualKeyCode == VK_HOME) {
                    scroll_console_lines(-32767);
                } else if (key.wVirtualKeyCode == VK_END) {
                    scroll_console_lines(32767);
                }
            }
        } else if (rec.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD mouse = rec.Event.MouseEvent;
            int left_down =
                (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;

            if (mouse.dwEventFlags == MOUSE_WHEELED) {
                int wheel = (SHORT)HIWORD(mouse.dwButtonState);
                int notches = abs(wheel) / WHEEL_DELTA;
                int lines;

                if (notches < 1) notches = 1;
                lines = notches * 3;
                scroll_console_lines(wheel > 0 ? -lines : lines);
                left_was_down = 0;
                continue;
            }

            if (mouse.dwEventFlags == 0 && left_down && !left_was_down) {
                int idx = result_at_mouse_row(mouse.dwMousePosition.Y);
                if (idx >= 0)
                    open_in_explorer(g_results[idx].path);
            }

            left_was_down = left_down;
        }
    }

    SetConsoleMode(hInput, old_mode);
    printf("\n");
}

static void clear_screen(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD written = 0;
    COORD home = {0, 0};
    DWORD cells;

    if (g_vt_enabled) {
        printf("\x1b[2J\x1b[H");
        return;
    }

    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return;

    cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
    FillConsoleOutputCharacterA(hConsole, ' ', cells, home, &written);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(hConsole, home);
}

static int results_per_page(void)
{
    int rows = visible_console_rows();
    int usable = rows - 9;
    int per_page = usable / 4;

    if (per_page < 1) per_page = 1;
    if (per_page > 30) per_page = 30;
    return per_page;
}

static int clamp_result_top(int top)
{
    int per_page = results_per_page();
    int max_top = g_result_count - per_page;

    if (max_top < 0) max_top = 0;
    if (top < 0) return 0;
    if (top > max_top) return max_top;
    return top;
}

static void reset_click_rows(void)
{
    for (int i = 0; i < g_result_count; i++) {
        g_results[i].path_row_start = -1;
        g_results[i].path_row_end = -1;
    }
}

static void draw_result_page(const char *query, int top)
{
    int per_page = results_per_page();
    int end = top + per_page;

    if (end > g_result_count) end = g_result_count;

    reset_click_rows();
    clear_screen();

    set_color(CLR_HEADER);
    printf("  Search Results\n");
    printf("  --------------------------------------------------\n");
    set_color(CLR_META);
    printf("  Query: ");
    set_color(CLR_FILE);
    printf("%s", query);
    set_color(CLR_META);
    printf(" | Showing ");
    set_color(CLR_FILE);
    printf("%d-%d", top + 1, end);
    set_color(CLR_META);
    printf(" of ");
    set_color(CLR_FILE);
    printf("%d", g_result_count);
    set_color(CLR_META);
    printf(" | Scanned %lu files, %lu folders\n", g_files_scanned, g_dirs_scanned);
    printf("  Wheel/Up/Down scroll | PgUp/PgDn page | Home/End jump | Enter/Esc back\n");
    set_color(CLR_HEADER);
    printf("  --------------------------------------------------\n\n");

    for (int i = top; i < end; i++) {
        SearchResult *r = &g_results[i];
        char size_str[64];
        if (r->is_dir)
            snprintf(size_str, sizeof(size_str), "-");
        else
            format_size(r->size, size_str, sizeof(size_str));
        ensure_type_description(r);

        set_color(CLR_INDEX);
        printf("  [%d] ", i + 1);
        print_highlighted_name(r->name, query);
        printf("\n");

        set_color(CLR_TREE);
        printf("      ");
        set_color(CLR_META);
        printf("Path: ");
        set_color(CLR_DIR);
        remember_path_position(r, strlen(r->path));
        print_clickable_path(r->path);
        printf("\n");

        set_color(CLR_TREE);
        printf("      ");
        set_color(CLR_META);
        printf("Type: ");
        set_color(CLR_FILE);
        printf("%-20s", r->type_desc);
        set_color(CLR_META);
        printf(" | Size: ");
        set_color(CLR_SIZE);
        printf("%s\n\n", size_str);
    }

    set_color(CLR_HEADER);
    printf("  Click a visible Path line to open it in File Explorer.");
    set_color(CLR_RESET);
}

static void browse_results(const char *query)
{
    void *hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD old_mode = 0;
    DWORD mode = 0;
    int top = 0;

    if (g_result_count == 0) {
        set_color(CLR_META);
        printf("\n  No files found matching \"%s\"\n", query);
        set_color(CLR_RESET);
        return;
    }

    if (!GetConsoleMode(hInput, &old_mode)) {
        display_results(query);
        return;
    }

    mode = old_mode;
    mode |= ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT;
    mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

    if (!SetConsoleMode(hInput, mode)) {
        SetConsoleMode(hInput, old_mode);
        display_results(query);
        return;
    }

    top = clamp_result_top(top);
    draw_result_page(query, top);
    FlushConsoleInputBuffer(hInput);

    while (1) {
        INPUT_RECORD rec;
        DWORD read = 0;
        int redraw = 0;

        if (!ReadConsoleInputA(hInput, &rec, 1, &read) || read == 0)
            break;

        if (rec.EventType == KEY_EVENT) {
            KEY_EVENT_RECORD key = rec.Event.KeyEvent;
            if (!key.bKeyDown)
                continue;

            if (key.wVirtualKeyCode == VK_RETURN ||
                key.wVirtualKeyCode == VK_ESCAPE ||
                key.uChar.AsciiChar == 'q' ||
                key.uChar.AsciiChar == 'Q')
            {
                break;
            } else if (key.wVirtualKeyCode == VK_UP ||
                       key.uChar.AsciiChar == 'k' ||
                       key.uChar.AsciiChar == 'K')
            {
                top -= 1;
                redraw = 1;
            } else if (key.wVirtualKeyCode == VK_DOWN ||
                       key.uChar.AsciiChar == 'j' ||
                       key.uChar.AsciiChar == 'J')
            {
                top += 1;
                redraw = 1;
            } else if (key.wVirtualKeyCode == VK_PRIOR ||
                       key.uChar.AsciiChar == 'p' ||
                       key.uChar.AsciiChar == 'P')
            {
                top -= results_per_page();
                redraw = 1;
            } else if (key.wVirtualKeyCode == VK_NEXT ||
                       key.uChar.AsciiChar == 'n' ||
                       key.uChar.AsciiChar == 'N' ||
                       key.uChar.AsciiChar == ' ')
            {
                top += results_per_page();
                redraw = 1;
            } else if (key.wVirtualKeyCode == VK_HOME) {
                top = 0;
                redraw = 1;
            } else if (key.wVirtualKeyCode == VK_END) {
                top = g_result_count;
                redraw = 1;
            }
        } else if (rec.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD mouse = rec.Event.MouseEvent;
            int left_down =
                (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;

            if (mouse.dwEventFlags == MOUSE_WHEELED) {
                int wheel = (SHORT)HIWORD(mouse.dwButtonState);
                int notches = abs(wheel) / WHEEL_DELTA;
                if (notches < 1) notches = 1;
                top += (wheel > 0) ? -notches : notches;
                redraw = 1;
            } else if (mouse.dwEventFlags == 0 && left_down) {
                int idx = result_at_mouse_row(mouse.dwMousePosition.Y);
                if (idx >= 0) {
                    open_in_explorer(g_results[idx].path);
                    draw_result_page(query, top);
                }
            }
        } else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            redraw = 1;
        }

        if (redraw) {
            top = clamp_result_top(top);
            draw_result_page(query, top);
        }
    }

    SetConsoleMode(hInput, old_mode);
    clear_screen();
}

/* ── Strip trailing whitespace from input ─────────────────────────── */

static void strip_trailing(char *str)
{
    int len = (int)strlen(str);
    while (len > 0 && (str[len - 1] == '\n' ||
                       str[len - 1] == '\r' ||
                       str[len - 1] == ' '))
    {
        str[--len] = '\0';
    }
}

static char *skip_spaces(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int starts_with_command(const char *line, const char *cmd)
{
    int i = 0;
    while (cmd[i]) {
        if (tolower((unsigned char)line[i]) !=
            tolower((unsigned char)cmd[i]))
            return 0;
        i++;
    }
    return line[i] == '\0' || line[i] == ' ' || line[i] == '\t';
}

static void copy_trimmed(char *dst, size_t dstlen, const char *src)
{
    size_t len;
    while (*src == ' ' || *src == '\t') src++;
    len = strlen(src);
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t'))
        len--;
    if (len >= dstlen) len = dstlen - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int path_is_directory(const char *path)
{
    char resolved[1024];
    unsigned long attrs;

    if (!path || !path[0]) return 0;

    GetFullPathNameA(path, sizeof(resolved), resolved, NULL);
    normalize_windows_path(resolved);
    attrs = GetFileAttributesA(resolved);
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int looks_like_path(const char *s)
{
    if (!s || !s[0]) return 0;
    return strstr(s, ":\\") != NULL ||
           strstr(s, ":/") != NULL ||
           strchr(s, '\\') != NULL ||
           strchr(s, '/') != NULL ||
           s[0] == '.';
}

static void read_quoted_or_rest(char **cursor, char *out, size_t outlen)
{
    char *p = skip_spaces(*cursor);
    size_t i = 0;

    if (*p == '"') {
        p++;
        while (*p && *p != '"' && i < outlen - 1)
            out[i++] = *p++;
        if (*p == '"') p++;
    } else {
        while (*p && i < outlen - 1)
            out[i++] = *p++;
    }

    out[i] = '\0';
    strip_trailing(out);
    *cursor = p;
}

static int parse_search_command(char *line, char *query, size_t query_len,
                                char *path, size_t path_len, int *has_path)
{
    char *p = skip_spaces(line);
    char rest[2048];

    query[0] = '\0';
    path[0] = '\0';
    *has_path = 0;

    if (!starts_with_command(p, "search"))
        return 0;

    p += 6;
    p = skip_spaces(p);
    if (*p == '\0')
        return -1;

    if (*p == '"') {
        read_quoted_or_rest(&p, query, query_len);
        p = skip_spaces(p);
        if (*p) {
            read_quoted_or_rest(&p, path, path_len);
            *has_path = path[0] != '\0';
        }
    } else {
        copy_trimmed(rest, sizeof(rest), p);

        char *quote = strchr(rest, '"');
        if (quote) {
            char *end_quote = strchr(quote + 1, '"');
            if (end_quote) {
                char candidate_query[256];
                char candidate_path[1024];
                size_t path_chars = (size_t)(end_quote - quote - 1);

                *quote = '\0';
                copy_trimmed(candidate_query, sizeof(candidate_query), rest);
                if (path_chars >= sizeof(candidate_path))
                    path_chars = sizeof(candidate_path) - 1;
                memcpy(candidate_path, quote + 1, path_chars);
                candidate_path[path_chars] = '\0';

                if (candidate_query[0]) {
                    strncpy(query, candidate_query, query_len - 1);
                    query[query_len - 1] = '\0';
                    strncpy(path, candidate_path, path_len - 1);
                    path[path_len - 1] = '\0';
                    *has_path = 1;
                    return 1;
                }
            }
        }

        char pathlike_query[256] = {0};
        char pathlike_path[1024] = {0};
        int saw_pathlike = 0;

        for (char *space = rest; *space; space++) {
            if (*space == ' ' || *space == '\t') {
                char candidate_path[1024];
                char candidate_query[256];
                char saved = *space;

                *space = '\0';
                copy_trimmed(candidate_query, sizeof(candidate_query), rest);
                *space = saved;
                copy_trimmed(candidate_path, sizeof(candidate_path), space + 1);

                if (candidate_query[0] && path_is_directory(candidate_path)) {
                    strncpy(query, candidate_query, query_len - 1);
                    query[query_len - 1] = '\0';
                    strncpy(path, candidate_path, path_len - 1);
                    path[path_len - 1] = '\0';
                    *has_path = 1;
                    return 1;
                }

                if (candidate_query[0] && looks_like_path(candidate_path)) {
                    strncpy(pathlike_query, candidate_query,
                            sizeof(pathlike_query) - 1);
                    pathlike_query[sizeof(pathlike_query) - 1] = '\0';
                    strncpy(pathlike_path, candidate_path,
                            sizeof(pathlike_path) - 1);
                    pathlike_path[sizeof(pathlike_path) - 1] = '\0';
                    saw_pathlike = 1;
                }
            }
        }

        if (saw_pathlike) {
            strncpy(query, pathlike_query, query_len - 1);
            query[query_len - 1] = '\0';
            strncpy(path, pathlike_path, path_len - 1);
            path[path_len - 1] = '\0';
            *has_path = 1;
            return 1;
        }

        strncpy(query, rest, query_len - 1);
        query[query_len - 1] = '\0';
    }

    return query[0] ? 1 : -1;
}

static void reset_search_state(void)
{
    g_result_count = 0;
    g_files_scanned = 0;
    g_dirs_scanned = 0;
    g_result_limit_reached = 0;
}

static int resolve_search_path(const char *input, char *resolved, size_t len)
{
    unsigned long attrs;

    GetFullPathNameA(input, (DWORD)len, resolved, NULL);
    normalize_windows_path(resolved);
    attrs = GetFileAttributesA(resolved);

    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void search_all_local_drives(const char *query)
{
    DWORD drives = GetLogicalDrives();
    int found_drive = 0;

    for (int i = 0; i < 26; i++) {
        char root[] = "A:\\";

        if (!(drives & (1u << i)))
            continue;

        root[0] = (char)('A' + i);
        if (GetDriveTypeA(root) != DRIVE_FIXED)
            continue;

        found_drive = 1;
        set_color(CLR_META);
        printf("  Scanning ");
        set_color(CLR_DIR);
        printf("%s\n", root);
        set_color(CLR_RESET);

        search_recursive(root, query);
        if (g_result_limit_reached)
            break;
    }

    if (!found_drive) {
        search_recursive("C:\\", query);
    }
}

static int run_search_command(const char *query, const char *path, int has_path)
{
    char resolved[1024];

    reset_search_state();

    set_color(CLR_META);
    printf("\n  Searching for \"");
    set_color(CLR_FILE);
    printf("%s", query);
    set_color(CLR_META);
    printf("\" ...\n");
    set_color(CLR_RESET);

    if (has_path) {
        if (!resolve_search_path(path, resolved, sizeof(resolved))) {
            set_color(CLR_MATCH);
            printf("  [ERROR] Search path is not a valid directory: %s\n", path);
            set_color(CLR_RESET);
            return 0;
        }

        set_color(CLR_META);
        printf("  Search in: ");
        set_color(CLR_DIR);
        printf("%s\n", resolved);
        set_color(CLR_RESET);

        search_recursive(resolved, query);
    } else {
        set_color(CLR_META);
        printf("  Search in: all fixed local drives\n");
        set_color(CLR_RESET);
        search_all_local_drives(query);
    }

    if (g_result_limit_reached) {
        set_color(CLR_MATCH);
        printf("  Result limit reached at %d items. Narrow the search for more precision.\n",
               MAX_RESULTS);
        set_color(CLR_RESET);
    }

    browse_results(query);
    return 1;
}

static void print_command_help(void)
{
    set_color(CLR_HEADER);
    printf("\n  Commands\n");
    printf("  --------------------------------------------------\n");
    set_color(CLR_FILE);
    printf("  search <name>\n");
    printf("  search <name> <path>\n");
    printf("  search \"name with spaces\" \"D:\\path with spaces\"\n");
    printf("  exit\n");
    set_color(CLR_META);
    printf("\n  Without a path, search scans all fixed local drives.\n\n");
    set_color(CLR_RESET);
}

static void build_command_from_args(int argc, char *argv[],
                                    char *out, size_t outlen)
{
    int start = 1;

    out[0] = '\0';

    if (argc > 1 && equals_ignore_case(argv[1], "search"))
        start = 1;
    else
        snprintf(out, outlen, "search");

    for (int i = start; i < argc; i++) {
        if (out[0] && strlen(out) + 1 < outlen)
            strncat(out, " ", outlen - strlen(out) - 1);

        if (strchr(argv[i], ' ') || strchr(argv[i], '\t')) {
            strncat(out, "\"", outlen - strlen(out) - 1);
            strncat(out, argv[i], outlen - strlen(out) - 1);
            strncat(out, "\"", outlen - strlen(out) - 1);
        } else {
            strncat(out, argv[i], outlen - strlen(out) - 1);
        }
    }
}

static int command_loop(int argc, char *argv[])
{
    char command[2048];
    char query[256];
    char path[1024];
    int has_path;
    int parsed;

    if (argc > 1) {
        build_command_from_args(argc, argv, command, sizeof(command));
        parsed = parse_search_command(command, query, sizeof(query),
                                      path, sizeof(path), &has_path);
        if (parsed == 1)
            return run_search_command(query, path, has_path) ? 0 : 1;

        print_command_help();
        return 1;
    }

    print_command_help();

    while (1) {
        set_color(CLR_HEADER);
        printf("  > ");
        set_color(CLR_FILE);

        if (fgets(command, sizeof(command), stdin) == NULL)
            break;
        strip_trailing(command);

        if (command[0] == '\0')
            continue;
        if (equals_ignore_case(command, "exit") ||
            equals_ignore_case(command, "quit"))
            break;
        if (equals_ignore_case(command, "help") ||
            equals_ignore_case(command, "?"))
        {
            print_command_help();
            continue;
        }

        parsed = parse_search_command(command, query, sizeof(query),
                                      path, sizeof(path), &has_path);
        if (parsed == 1) {
            run_search_command(query, path, has_path);
        } else if (parsed == -1) {
            set_color(CLR_MATCH);
            printf("  Usage: search <name> [path]\n");
            set_color(CLR_RESET);
        } else {
            set_color(CLR_MATCH);
            printf("  Unknown command. Type 'help' or use: search <name> [path]\n");
            set_color(CLR_RESET);
        }
    }

    set_color(CLR_RESET);
    printf("\n");
    return 0;
}

/* ── Print banner ─────────────────────────────────────────────────── */

static void print_banner(void)
{
    set_color(CLR_HEADER);
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║          SEARCH DIRECTORY IN LOCAL              ║\n");
    printf("  ║         (Find Similar Files by Name)            ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n");
    set_color(CLR_RESET);
    printf("\n");
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    char search_dir[1024];
    char query[256];

    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(65001);
    enable_virtual_terminal_output();

    print_banner();
    return command_loop(argc, argv);

    /* ── Get search directory ── */
    if (argc >= 2) {
        strncpy(search_dir, argv[1], sizeof(search_dir) - 1);
        search_dir[sizeof(search_dir) - 1] = '\0';
    } else {
        set_color(CLR_HEADER);
        printf("  Enter directory to search (or press Enter for all drives): ");
        set_color(CLR_FILE);

        if (fgets(search_dir, sizeof(search_dir), stdin) == NULL) {
            set_color(CLR_MATCH);
            printf("\n  [ERROR] Failed to read input.\n");
            set_color(CLR_RESET);
            printf("  Press Enter to exit...");
            getchar();
            return 1;
        }
        strip_trailing(search_dir);
    }

    /* If empty, default to C:\ */
    if (search_dir[0] == '\0') {
        strcpy(search_dir, "C:\\");
    }

    /* Resolve full path */
    char resolved_dir[1024];
    GetFullPathNameA(search_dir, sizeof(resolved_dir), resolved_dir, NULL);
    normalize_windows_path(resolved_dir);

    /* Validate */
    unsigned long dir_attrs = GetFileAttributesA(resolved_dir);
    if (dir_attrs == INVALID_FILE_ATTRIBUTES) {
        set_color(CLR_MATCH);
        printf("  [ERROR] Path does not exist: %s\n\n", resolved_dir);
        set_color(CLR_RESET);
        printf("  Press Enter to exit...");
        getchar();
        return 1;
    }
    if (!(dir_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        set_color(CLR_MATCH);
        printf("  [ERROR] Path is not a directory: %s\n\n", resolved_dir);
        set_color(CLR_RESET);
        printf("  Press Enter to exit...");
        getchar();
        return 1;
    }

    /* ── Search loop (allows multiple searches) ── */
    while (1) {
        set_color(CLR_HEADER);
        printf("\n  ──────────────────────────────────────────────────\n");
        printf("  Search in: ");
        set_color(CLR_DIR);
        printf("%s\n", resolved_dir);
        set_color(CLR_HEADER);
        printf("  ──────────────────────────────────────────────────\n");

        set_color(CLR_HEADER);
        printf("  Enter filename to search (or 'quit' to exit): ");
        set_color(CLR_FILE);

        if (fgets(query, sizeof(query), stdin) == NULL) break;
        strip_trailing(query);

        if (query[0] == '\0') continue;
        if (strcmp(query, "quit") == 0 || strcmp(query, "exit") == 0) break;

        /* Reset counters */
        g_result_count = 0;
        g_files_scanned = 0;
        g_dirs_scanned = 0;

        /* Show scanning message */
        set_color(CLR_META);
        printf("\n  Searching for \"%s\" ...\n", query);
        set_color(CLR_RESET);

        /* Perform recursive search */
        search_recursive(resolved_dir, query);

        /* Display results in the paged viewer. */
        browse_results(query);

        /* ── Let user open files ── */
    }

    /* ── Exit ── */
    set_color(CLR_RESET);
    printf("\n");
    set_color(CLR_META);
    printf("  Press Enter to exit...");
    set_color(CLR_RESET);
    getchar();
    printf("\n");
    return 0;
}
