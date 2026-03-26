/*! @file
    @brief Declares shared helper functions for autocomplete trigger decisions.
*/

#pragma once

#include <QString>

namespace qcai2
{

/**
 * Returns the identifier-fragment length immediately before the cursor.
 * @param line_text Current line text.
 * @param cursor_column Zero-based cursor column within the line.
 * @return Number of consecutive identifier characters directly before the cursor.
 */
int identifier_fragment_length_before_cursor(const QString &line_text, int cursor_column);

/**
 * Checks whether typed text contains any non-whitespace content worth triggering on.
 * @param inserted_text Text inserted by the editor change event.
 * @return True when the insertion is not empty and not pure whitespace.
 */
bool is_non_whitespace_typed_insertion(const QString &inserted_text);

/**
 * Returns the offset right after the last non-whitespace character in one insertion.
 * @param inserted_text Text inserted by the editor change event.
 * @return Positive offset from the insertion start to the meaningful fragment end, or 0 when the
 * insertion has no non-whitespace characters.
 */
int typed_fragment_end_offset(const QString &inserted_text);

/**
 * Returns the effective end column of the meaningful typed fragment inside one insertion.
 * @param inserted_text Text inserted by the editor change event.
 * @param trigger_column_after_insert Zero-based column after the full insertion finished.
 * @return Column to use for auto-trigger evaluation, ignoring trailing whitespace/newlines.
 */
int typed_fragment_trigger_column(const QString &inserted_text, int trigger_column_after_insert);

/**
 * Checks whether the cursor position should auto-trigger autocomplete.
 * @param line_text Current line text.
 * @param cursor_column Zero-based cursor column within the line.
 * @param completion_min_chars Minimum identifier length required for word triggers.
 * @return True for traditional trigger characters or identifier fragments at/above threshold.
 */
bool should_auto_trigger_completion(const QString &line_text, int cursor_column,
                                    int completion_min_chars);

}  // namespace qcai2
