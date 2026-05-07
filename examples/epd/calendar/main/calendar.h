#pragma once

#include "FastEPD.h"

/* Which view to render */
typedef enum {
    CALENDAR_VIEW_MONTH = 0,  /* Full month grid (7 cols × up to 6 rows) */
    CALENDAR_VIEW_WEEK,       /* Single ISO week, 7 horizontal day rows   */
} calendar_view_t;

/*
 * Input parameters for calendar_draw().
 *
 * Month view:  supply year + month.
 * Week view:   supply year + month + today (ISO week is computed automatically).
 *
 * today = 0  →  no highlight; for week view, draws the week containing day 1.
 */
typedef struct {
    calendar_view_t view;

    int year;        /* 4-digit year                         */
    int month;       /* 1–12                                 */
    int today;       /* day-of-month highlight (0 = none)    */
} calendar_params_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fill the display buffer with the calendar layout.
 * Calls bbepFillScreen internally; the caller must call bbepFullUpdate()
 * afterwards to push pixels to the e-ink panel.
 */
void calendar_draw(FASTEPDSTATE *bbep, const calendar_params_t *params);

/*
 * Self-contained demos: draw + full-update.
 * calendar_demo_month → May 2026, today = 4 May 2026
 * calendar_demo_week  → week containing 4 May 2026 (Week 19)
 */
void calendar_demo_month(FASTEPDSTATE *bbep);
void calendar_demo_week(FASTEPDSTATE *bbep);

#ifdef __cplusplus
}
#endif
