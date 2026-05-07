/*
 * calendar.cpp
 *
 * Full-screen calendar renderer for FastEPD (C API).
 * Supports a 7-column month grid and a 7-row ISO-week planner.
 *
 * Compiled as C++ so that #include "FastEPD.h" takes the C++ path and does
 * NOT inline the .inl implementation files a second time (they live in main.c).
 */

#include "calendar.h"   /* → FastEPD.h (C++ path: types only, no .inl inlining) */

#include <stdio.h>
#include <string.h>

/*
 * Font data arrays are #defined in the font headers as non-extern const arrays,
 * so they get defined once in main.c.  Declare them here as extern so the linker
 * resolves them from main.c.obj without a duplicate-definition error.
 */
extern "C" {
    extern const uint8_t Roboto_Black_80[];
    extern const uint8_t Roboto_Black_40[];
    extern const uint8_t Courier_Prime_16[];

    /* FastEPD C-API functions (implementations are inlined into main.c). */
    void bbepFillScreen(FASTEPDSTATE *pState, uint8_t u8Color);
    int  bbepFullUpdate(FASTEPDSTATE *pState, int iClearMode, bool bKeepOn, BB_RECT *pRect);
    void bbepDrawLine(FASTEPDSTATE *pBBEP, int x1, int y1, int x2, int y2, uint8_t ucColor);
    int  bbepWriteStringCustom(FASTEPDSTATE *pBBEP, const void *pFont, int x, int y, char *szMsg, int iColor);
    int  bbepGetStringBox(FASTEPDSTATE *pBBEP, const char *szMsg, BB_RECT *pRect);
    void bbepSetCursor(FASTEPDSTATE *pBBEP, int x, int y);
    void bbepRectangle(FASTEPDSTATE *pBBEP, int x1, int y1, int x2, int y2, uint8_t ucColor, uint8_t bFilled);
    void bbepRoundRect(FASTEPDSTATE *pBBEP, int x, int y, int w, int h, int r, uint8_t iColor, int bFilled);
    void bbepInvertRect(FASTEPDSTATE *pBBEP, int x, int y, int w, int h);
    int  bbepSetRotation(FASTEPDSTATE *pState, int iAngle);
}
/* =========================================================================
 * String tables
 * ======================================================================= */

static const char *MONTH_NAMES[] = {
    "", "JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
    "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"
};
static const char *MONTH_ABBR[] = {
    "", "JAN","FEB","MAR","APR","MAY","JUN",
    "JUL","AUG","SEP","OCT","NOV","DEC"
};
/* Monday-first order (index 0 = Monday) */
static const char *DAY_ABBR[] = { "MON","TUE","WED","THU","FRI","SAT","SUN" };

/* =========================================================================
 * Screen & layout constants  (tweak these to re-layout)
 * ======================================================================= */

#define MARGIN      20

/* ------ Month view ------ */
/* Roboto_Black_80:  cap height ≈ 70 px above baseline */
#define MV_TITLE_BASE    140   /* y-baseline of "MONTH YEAR" title       */
#define MV_SEP1_Y        165   /* horizontal rule below title             */
/* Roboto_Black_40:  cap height ≈ 35 px above baseline */
#define MV_DAYNAMES_BASE 210   /* y-baseline of "MON TUE …" header row   */
#define MV_SEP2_Y        225   /* horizontal rule below day-names         */
#define MV_GRID_TOP      235   /* top edge of the day-cell grid           */
#define MV_GRID_ROWS_MAX   6   /* max rows (fits months starting Sun–Mon) */
#define MV_GRID_COLS       7
/* MV_COL_W and MV_ROW_H are computed as local vars in draw_month_view */
#define MV_NUM_PAD_TOP    30   /* gap from cell top to top of glyph       */
#define MV_TODAY_PAD       8   /* extra padding around today's number     */
#define MV_TODAY_R        10   /* corner radius of today highlight        */

/* ------ Week view ------ */
/* Roboto_Black_80:  "WEEK N  YEAR" title */
#define WV_TITLE_BASE    140
/* Roboto_Black_40:  date-range sub-title */
#define WV_RANGE_BASE    210
#define WV_SEP_Y         230   /* horizontal rule below sub-title         */
#define WV_ROWS_TOP      242   /* top edge of the 7 day rows              */
/* WV_ROW_H is computed as a local var in draw_week_view */
#define WV_LEFT_COL_W    230   /* width of left day-label column          */
#define WV_TEXT_INDENT    16   /* left indent of text inside label column */
/* Roboto_Black_40 metrics */
#define WV_FONT_CAP_H     35   /* cap height above baseline               */
#define WV_FONT_LINE_GAP  70   /* baseline-to-baseline spacing (larger = more gap) */

/* =========================================================================
 * Date-math helpers
 * ======================================================================= */

static int cal_is_leap(int y)
{
    return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
}

static int cal_days_in_month(int y, int m)
{
    static const int mdays[] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
    return (m == 2 && cal_is_leap(y)) ? 29 : mdays[m];
}

/* Tomohiko Sakamoto's algorithm: returns 0=Sun, 1=Mon … 6=Sat */
static int cal_dow(int y, int m, int d)
{
    static const int t[] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

/* ISO day-of-week: 1=Mon … 7=Sun */
static int cal_iso_dow(int y, int m, int d)
{
    int dow = cal_dow(y, m, d);
    return (dow == 0) ? 7 : dow;
}

/* ISO week number for a given date */
static int cal_iso_week(int y, int m, int d)
{
    /* ordinal day of year */
    int doy = d;
    for (int i = 1; i < m; i++) doy += cal_days_in_month(y, i);

    int idow = cal_iso_dow(y, m, d);          /* 1=Mon … 7=Sun */
    int w = (doy - idow + 10) / 7;

    if (w == 0) {
        /* last week of previous year: recurse on Dec 31 */
        return cal_iso_week(y - 1, 12, 31);
    }
    if (w == 53) {
        /* week 53 is valid only when Dec 31 falls Thu–Sun */
        int dec31 = cal_iso_dow(y, 12, 31);
        return (dec31 >= 4) ? 53 : 1;
    }
    return w;
}

/*
 * Return the Monday (out_y, out_m, out_d) of a given ISO year+week.
 * Uses the rule: Jan 4 is always in week 1.
 */
static void cal_week_monday(int iso_year, int iso_week,
                             int *out_y, int *out_m, int *out_d)
{
    int dow_jan4  = cal_iso_dow(iso_year, 1, 4);    /* 1=Mon..7=Sun */
    int monday_doy = 4 - (dow_jan4 - 1) + (iso_week - 1) * 7;

    int y = iso_year;
    if (monday_doy < 1) {
        y--;
        monday_doy += cal_is_leap(y) ? 366 : 365;
    } else {
        int total = cal_is_leap(y) ? 366 : 365;
        if (monday_doy > total) {
            monday_doy -= total;
            y++;
        }
    }

    *out_y = y;
    *out_m = 1;
    while (*out_m <= 12) {
        int dm = cal_days_in_month(y, *out_m);
        if (monday_doy <= dm) break;
        monday_doy -= dm;
        (*out_m)++;
    }
    *out_d = monday_doy;
}

/* Add n days to a date */
static void cal_date_add(int y, int m, int d, int n,
                          int *oy, int *om, int *od)
{
    d += n;
    while (d > cal_days_in_month(y, m)) {
        d -= cal_days_in_month(y, m);
        if (++m > 12) { m = 1; y++; }
    }
    *oy = y;  *om = m;  *od = d;
}

/* =========================================================================
 * Drawing helpers
 * ======================================================================= */

/*
 * Measure the pixel width of a string rendered with a custom font.
 * Temporarily sets pBBEP->pFont, then restores it.
 */
static int cal_text_w(FASTEPDSTATE *bbep, const void *font, const char *text)
{
    BB_RECT r;
    void *saved = bbep->pFont;
    bbep->pFont = (void *)font;
    bbepSetCursor(bbep, 0, 0);
    bbepGetStringBox(bbep, text, &r);
    bbep->pFont = saved;
    return r.w;
}

/* Draw text with a custom font, baseline at (x, base_y). */
static void cal_draw(FASTEPDSTATE *bbep, const void *font,
                     int x, int base_y, const char *text, int color)
{
    bbepWriteStringCustom(bbep, font, x, base_y, (char *)text, color);
}

/* Draw text centred horizontally around cx, baseline at base_y. */
static void cal_draw_c(FASTEPDSTATE *bbep, const void *font,
                       int cx, int base_y, const char *text, int color)
{
    int w = cal_text_w(bbep, font, text);
    cal_draw(bbep, font, cx - w / 2, base_y, text, color);
}

/* Horizontal rule spanning x1..x2 */
static void cal_hline(FASTEPDSTATE *bbep, int y, int x1, int x2)
{
    bbepDrawLine(bbep, x1, y, x2, y, BBEP_BLACK);
}

/* =========================================================================
 * Month view
 * ======================================================================= */

static void draw_month_view(FASTEPDSTATE *bbep, const calendar_params_t *p)
{
    char buf[64];
    int mv_col_w  = (bbep->width - 2 * MARGIN) / MV_GRID_COLS;

    /* Compute rows actually needed for this month (5 or 6) */
    int dow_sun   = cal_dow(p->year, p->month, 1);   /* 0=Sun … 6=Sat */
    int first_col = (dow_sun == 0) ? 6 : dow_sun - 1; /* convert to Mon=0 */
    int total_days = cal_days_in_month(p->year, p->month);
    int rows_needed = (first_col + total_days + MV_GRID_COLS - 1) / MV_GRID_COLS;

    int mv_row_h  = (bbep->height - MV_GRID_TOP - MARGIN) / rows_needed;
    /* Centre the grid: split the integer-division remainder evenly left/right */
    int grid_left  = (bbep->width - mv_col_w * MV_GRID_COLS) / 2;
    int grid_right = grid_left + mv_col_w * MV_GRID_COLS;
    int grid_cx    = (grid_left + grid_right) / 2;

    /* ---- Title: "MAY 2026" ---- */
    snprintf(buf, sizeof(buf), "%s %d", MONTH_NAMES[p->month], p->year);
    cal_draw_c(bbep, Roboto_Black_80, grid_cx, MV_TITLE_BASE, buf, BBEP_BLACK);

    cal_hline(bbep, MV_SEP1_Y, grid_left, grid_right);

    /* ---- Day-of-week headers (Mon–Sun) ---- */
    for (int col = 0; col < MV_GRID_COLS; col++) {
        int cx = grid_left + col * mv_col_w + mv_col_w / 2;
        cal_draw_c(bbep, Roboto_Black_40, cx, MV_DAYNAMES_BASE,
                   DAY_ABBR[col], BBEP_BLACK);
    }

    cal_hline(bbep, MV_SEP2_Y, grid_left, grid_right);

    /* ---- Grid lines ---- */
    /* Horizontal lines (top of each row + bottom of last row) */
    for (int row = 0; row <= rows_needed; row++) {
        int y = MV_GRID_TOP + row * mv_row_h;
        bbepDrawLine(bbep, grid_left, y, grid_right, y, BBEP_BLACK);
    }
    /* Vertical lines (left of each column + right edge) */
    for (int col = 0; col <= MV_GRID_COLS; col++) {
        int x   = grid_left + col * mv_col_w;
        int bot = MV_GRID_TOP + rows_needed * mv_row_h;
        bbepDrawLine(bbep, x, MV_GRID_TOP, x, bot, BBEP_BLACK);
    }

    /* ---- Day numbers ---- */
    /* first_col / total_days / rows_needed computed above */
    /* Font metrics for Roboto_Black_40: cap-height ≈ 35 px, descender ≈ 9 px */
    int glyph_h   = 44;   /* total glyph bounding box height   */
    int cap_h     = 35;   /* height above baseline             */
    /* Vertical centre of number in each cell */
    int pad_top   = (mv_row_h - glyph_h) / 2;

    for (int day = 1; day <= total_days; day++) {
        int slot = first_col + day - 1;
        int row  = slot / MV_GRID_COLS;
        int col  = slot % MV_GRID_COLS;

        int cell_x   = grid_left + col * mv_col_w;
        int cell_y   = MV_GRID_TOP + row * mv_row_h;

        snprintf(buf, sizeof(buf), "%d", day);

        /* Centre number horizontally in the column */
        int tw     = cal_text_w(bbep, Roboto_Black_40, buf);
        int num_x  = cell_x + (mv_col_w - tw) / 2;
        /* Vertically centre the glyph in the cell */
        int base_y = cell_y + pad_top + cap_h;

        int is_today = (p->today != 0 &&
                        p->today == day);

        if (is_today) {
            /* Highlight: fill most of the cell, leaving a small inset */
            int hi_pad = 6;
            int rx = cell_x + hi_pad;
            int ry = cell_y + hi_pad;
            int rw = mv_col_w - hi_pad * 2;
            int rh = mv_row_h - hi_pad * 2;
            bbepRoundRect(bbep, rx, ry, rw, rh, MV_TODAY_R, BBEP_BLACK, 1);
            /* Draw number in white over the filled rect */
            bbepWriteStringCustom(bbep, Roboto_Black_40, num_x, base_y, buf, BBEP_WHITE);
        } else {
            bbepWriteStringCustom(bbep, Roboto_Black_40, num_x, base_y, buf, BBEP_BLACK);
        }
    }
}

/* =========================================================================
 * Week view
 * ======================================================================= */

static void draw_week_view(FASTEPDSTATE *bbep, const calendar_params_t *p)
{
    char buf[80];
    int wv_row_h  = (bbep->height - WV_ROWS_TOP - MARGIN) / 7;
    int wv_left   = MARGIN;
    int wv_right  = bbep->width - MARGIN;
    int wv_cx     = (wv_left + wv_right) / 2;

    /* ---- Find Monday of this ISO week ---- */
    int iso_week = cal_iso_week(p->year, p->month, p->today > 0 ? p->today : 1);
    int mon_y, mon_m, mon_d;
    cal_week_monday(p->year, iso_week, &mon_y, &mon_m, &mon_d);

    /* Sunday = Monday + 6 */
    int sun_y, sun_m, sun_d;
    cal_date_add(mon_y, mon_m, mon_d, 6, &sun_y, &sun_m, &sun_d);

    /* ---- Title: "WEEK 19  2026" ---- */
    snprintf(buf, sizeof(buf), "WEEK %d  %d", iso_week, p->year);
    cal_draw_c(bbep, Roboto_Black_80, wv_cx, WV_TITLE_BASE, buf, BBEP_BLACK);

    /* ---- Date-range subtitle ---- */
    if (mon_m == sun_m) {
        snprintf(buf, sizeof(buf), "%d - %d %s %d",
                 mon_d, sun_d, MONTH_NAMES[mon_m], mon_y);
    } else if (mon_y == sun_y) {
        snprintf(buf, sizeof(buf), "%d %s - %d %s %d",
                 mon_d, MONTH_ABBR[mon_m],
                 sun_d, MONTH_ABBR[sun_m], mon_y);
    } else {
        snprintf(buf, sizeof(buf), "%d %s %d - %d %s %d",
                 mon_d, MONTH_ABBR[mon_m], mon_y,
                 sun_d, MONTH_ABBR[sun_m], sun_y);
    }
    cal_draw_c(bbep, Roboto_Black_40, wv_cx, WV_RANGE_BASE, buf, BBEP_BLACK);

    cal_hline(bbep, WV_SEP_Y, wv_left, wv_right);

    /* ---- Day rows ---- */
    for (int i = 0; i < 7; i++) {
        int dy, dm, dd;
        cal_date_add(mon_y, mon_m, mon_d, i, &dy, &dm, &dd);

        int row_top = WV_ROWS_TOP + i * wv_row_h;
        int row_bot = row_top + wv_row_h;           /* exclusive */

        /* today: year+month from params identify the full date */
        int is_today = (p->today != 0 &&
                        p->today == dd &&
                        p->month == dm &&
                        p->year  == dy);

        /* Always draw content in black first */

        /* Vertically centre the two-line block (abbr + number) in the row.
         * Block spans from top-of-abbr-glyph to bottom-of-num-glyph:
         *   height = WV_FONT_CAP_H + WV_FONT_LINE_GAP + (44 - WV_FONT_CAP_H)
         *          = WV_FONT_LINE_GAP + 44
         */
        int block_h    = WV_FONT_LINE_GAP + 44;
        int block_top  = row_top + (wv_row_h - block_h) / 2;
        int abbr_base  = block_top + WV_FONT_CAP_H;
        int num_base   = abbr_base + WV_FONT_LINE_GAP;

        /* Day abbreviation  (e.g. "MON") */
        cal_draw(bbep, Roboto_Black_40,
                 wv_left + WV_TEXT_INDENT, abbr_base,
                 DAY_ABBR[i], BBEP_BLACK);

        /* Date number — same font as day name */
        snprintf(buf, sizeof(buf), "%d", dd);
        cal_draw(bbep, Roboto_Black_40,
                 wv_left + WV_TEXT_INDENT, num_base,
                 buf, BBEP_BLACK);

        /* Vertical separator: day-label | content area */
        bbepDrawLine(bbep,
                     wv_left + WV_LEFT_COL_W, row_top,
                     wv_left + WV_LEFT_COL_W, row_bot - 1,
                     BBEP_BLACK);

        /* Horizontal separator at the bottom of every row */
        bbepDrawLine(bbep,
                     wv_left, row_bot,
                     wv_right, row_bot,
                     BBEP_BLACK);

        /* Today: invert the left label column so background→black, text→white */
        if (is_today) {
            int hi_pad = 4;
            bbepInvertRect(bbep,
                           wv_left + hi_pad, row_top + hi_pad,
                           WV_LEFT_COL_W - hi_pad * 2, wv_row_h - hi_pad * 2);
        }
    }

    /* Outer border around the whole week grid */
    int grid_bot = WV_ROWS_TOP + 7 * wv_row_h;
    bbepRectangle(bbep,
                  wv_left, WV_ROWS_TOP,
                  wv_right - 1, grid_bot,
                  BBEP_BLACK, 0);
}

/* =========================================================================
 * Public API
 * ======================================================================= */

extern "C" void calendar_draw(FASTEPDSTATE *bbep, const calendar_params_t *params)
{
    bbepFillScreen(bbep, BBEP_WHITE);

    if (params->view == CALENDAR_VIEW_MONTH) {
        draw_month_view(bbep, params);
    } else {
        draw_week_view(bbep, params);
    }
}

extern "C" void calendar_demo_month(FASTEPDSTATE *bbep)
{
    calendar_params_t p = {
        .view  = CALENDAR_VIEW_MONTH,
        .year  = 2026,
        .month = 5,
        .today = 4,
    };
    calendar_draw(bbep, &p);
    bbepFullUpdate(bbep, CLEAR_NONE, 0, NULL);
}

extern "C" void calendar_demo_week(FASTEPDSTATE *bbep)
{
    calendar_params_t p = {
        .view  = CALENDAR_VIEW_WEEK,
        .year  = 2026,
        .month = 5,
        .today = 4,
    };
    calendar_draw(bbep, &p);
    bbepFullUpdate(bbep, CLEAR_NONE, 0, NULL);
}
