#pragma once

#include "device_text.h"

#include <stdbool.h>
#include <stddef.h>

#include "version_info.h"

/* What the About screen says, worked out away from the screen that draws it.
 *
 * Here rather than in ui.c for the reason every other view derivation is:
 * this is a set of decisions - what to call a version that could not be read,
 * when to warn that the two halves disagree, which words each language uses -
 * and decisions are what a host test can check. ui.c is left drawing labels.
 *
 * The same lines feed the web page, through GET /api/about, so the panel and
 * the browser cannot end up describing the device differently.
 */

/* One line of "Name: value". The longest is the framework line at 480x320,
 * and this leaves room for a version string of full length beside its label
 * in either language. */
#define UI_ABOUT_LINE_MAX 63U

typedef struct {
    char firmware[UI_ABOUT_LINE_MAX + 1U];
    char built[UI_ABOUT_LINE_MAX + 1U];
    char web[UI_ABOUT_LINE_MAX + 1U];
    char idf[UI_ABOUT_LINE_MAX + 1U];
    /* Empty unless the two halves came from different builds, which is the
     * one thing on this screen that is a problem rather than a fact. */
    char notice[UI_ABOUT_LINE_MAX + 1U];
} ui_about_lines_t;

/* Fills every line. Never fails: a reading the device could not take comes
 * back as the word for "unknown" rather than as an empty row, because a blank
 * where a version should be reads as a bug in the screen. */
void ui_about_build(const version_info_t *info, device_language_t language,
                    ui_about_lines_t *lines);
