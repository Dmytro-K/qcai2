/*! @file
    @brief Regression tests for shared completion response normalization helpers.
*/

#include "../src/completion/completion_response_utils.h"

#include <QtTest>

namespace qcai2
{

class tst_completion_response_utils_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Markdown fences should be stripped while preserving meaningful indentation.
     */
    void normalize_response_strips_fences_and_preserves_indentation();

    /**
     * Returned text must not duplicate the text already typed before the cursor.
     */
    void normalize_response_removes_prefix_overlap();

    /**
     * Returned text must not duplicate the text that already exists after the cursor.
     */
    void normalize_response_removes_suffix_overlap();

    /**
     * Full-line replacement responses after control-flow keywords should be rejected.
     */
    void normalize_response_rejects_control_keyword_replacement();

    /**
     * Inline ghost-text suggestions must preserve the already typed line prefix and suffix.
     */
    void build_inline_completion_text_preserves_existing_line_text();

    /**
     * Streaming partial completions should hide the unfinished trailing identifier fragment.
     */
    void stable_streaming_completion_prefix_trims_trailing_identifier();

    /**
     * Streaming partial completions may keep punctuation and whitespace immediately.
     */
    void stable_streaming_completion_prefix_keeps_non_identifier_suffix();
};

void tst_completion_response_utils_t::normalize_response_strips_fences_and_preserves_indentation()
{
    QCOMPARE(normalize_completion_response(QStringLiteral("```cpp\n    foo();\n```"), {}, {}),
             QStringLiteral("    foo();"));
}

void tst_completion_response_utils_t::normalize_response_removes_prefix_overlap()
{
    const QString prefix = QStringLiteral("    for");
    QCOMPARE(normalize_completion_response(QStringLiteral("for (;;) {\n    \n}"), prefix, {}),
             QStringLiteral(" (;;) {\n    \n}"));
}

void tst_completion_response_utils_t::normalize_response_removes_suffix_overlap()
{
    const QString prefix = QStringLiteral("call");
    const QString suffix = QStringLiteral(");\nnext_line");
    QCOMPARE(normalize_completion_response(QStringLiteral("(arg1, arg2);"), prefix, suffix),
             QStringLiteral("(arg1, arg2"));
}

void tst_completion_response_utils_t::normalize_response_rejects_control_keyword_replacement()
{
    const QString prefix = QStringLiteral("    for");
    QCOMPARE(normalize_completion_response(
                 QStringLiteral("this->pending_requests[id] = {callback};"), prefix, {}),
             QString());
}

void tst_completion_response_utils_t::build_inline_completion_text_preserves_existing_line_text()
{
    const QString prefix = QStringLiteral("previous_line\n    for");
    const QString completion = QStringLiteral(" (size_t i = 0; i < count; ++i)");
    const QString suffix = QStringLiteral(" {\nnext_line");
    QCOMPARE(build_inline_completion_text(prefix, completion, suffix),
             QStringLiteral("    for (size_t i = 0; i < count; ++i) {"));
}

void tst_completion_response_utils_t::
    stable_streaming_completion_prefix_trims_trailing_identifier()
{
    QCOMPARE(stable_streaming_completion_prefix(QStringLiteral(" (size_t i = 0; i < mess")),
             QStringLiteral(" (size_t i = 0; i < "));
}

void tst_completion_response_utils_t::
    stable_streaming_completion_prefix_keeps_non_identifier_suffix()
{
    QCOMPARE(stable_streaming_completion_prefix(QStringLiteral(" (size_t ")),
             QStringLiteral(" (size_t "));
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_completion_response_utils_t)

#include "tst_completion_response_utils.moc"
