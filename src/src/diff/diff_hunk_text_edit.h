/*! Declares helper types for applying one unified-diff hunk to a text document buffer. */
#pragma once

#include "inline_diff_manager.h"

#include <optional>

namespace qcai2
{

/**
 * Describes one contiguous text replacement derived from a parsed unified-diff hunk.
 */
struct hunk_text_edit_t
{
    /** Start offset of the replaced range in the current document text. */
    int start_position = 0;

    /** End offset of the replaced range in the current document text. */
    int end_position = 0;

    /** Exact text currently expected in the replaced range before applying the hunk. */
    QString expected_text;

    /** Exact replacement text that should be inserted into the replaced range. */
    QString replacement_text;
};

/**
 * Builds one document replacement operation for a parsed hunk in the current partially-applied
 * document state.
 * @param hunk Parsed unified-diff hunk.
 * @param prior_accepted_line_delta Sum of earlier accepted line-count deltas in the same file.
 * @param document_text Current plain-text contents of the editor document.
 * @param error_message Optional error output when the current document no longer matches the hunk.
 */
std::optional<hunk_text_edit_t> build_hunk_text_edit(const diff_hunk_t &hunk,
                                                     int prior_accepted_line_delta,
                                                     const QString &document_text,
                                                     QString *error_message);

}  // namespace qcai2
