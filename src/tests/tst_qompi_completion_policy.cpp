/*! @file
    @brief Regression tests for qompi completion normalization policy helpers.
*/

#include <qompi/completion_policy.h>

#include <QtTest>

namespace qompi
{

class tst_qompi_completion_policy_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Markdown fences should be stripped while preserving indentation.
     */
    void normalize_response_strips_fences();

    /**
     * Existing prefix and suffix text must not be duplicated.
     */
    void normalize_response_removes_context_overlap();

    /**
     * A model may rewrite the full current line; normalization should keep only the insertion.
     */
    void normalize_response_removes_full_line_prefix_rewrite();

    /**
     * FIM-style completions may add extra leading indentation before inline punctuation.
     */
    void normalize_response_accepts_multiline_fim_insertion();

    /**
     * Echoed FIM markers should be stripped so only inserted text remains.
     */
    void normalize_response_strips_echoed_fim_markers();

    /**
     * Explanatory text with a fenced block should still yield the fenced completion body.
     */
    void normalize_response_extracts_fenced_completion_body();

    /**
     * A suffix match may consume the full generated completion without underflowing normalization.
     */
    void normalize_response_handles_full_suffix_overlap();

    /**
     * Inline completion expansion must preserve the current-line suffix.
     */
    void build_inline_completion_text_preserves_line_context();

    /**
     * Streaming output must not end in the middle of an identifier.
     */
    void stable_streaming_completion_prefix_trims_identifier_tail();

    /**
     * Stop policy should trim an exact trailing stop word.
     */
    void apply_completion_stop_policy_trims_trailing_stop_word();

    /**
     * Stream aggregation should emit only new stable chunks.
     */
    void aggregate_completion_stream_emits_stable_chunks();
};

void tst_qompi_completion_policy_t::normalize_response_strips_fences()
{
    QCOMPARE(
        QString::fromStdString(normalize_completion_response({}, {}, "```cpp\n    foo();\n```")),
        QStringLiteral("    foo();"));
}

void tst_qompi_completion_policy_t::normalize_response_removes_context_overlap()
{
    QCOMPARE(QString::fromStdString(
                 normalize_completion_response("call", ");\nnext_line", "(arg1, arg2);")),
             QStringLiteral("(arg1, arg2"));
}

void tst_qompi_completion_policy_t::normalize_response_removes_full_line_prefix_rewrite()
{
    QCOMPARE(
        QString::fromStdString(normalize_completion_response(
            "previous_line\n    for", "\nnext_line", "    for (const auto &message : messages)")),
        QStringLiteral(" (const auto &message : messages)"));
}

void tst_qompi_completion_policy_t::normalize_response_accepts_multiline_fim_insertion()
{
    QCOMPARE(QString::fromStdString(normalize_completion_response(
                 "previous_line\n    for", "\nnext_line",
                 "        (const auto &message : messages)\n"
                 "    {\n"
                 "        req[QStringLiteral(\"message\")] = message.to_json();\n"
                 "    }")),
             QStringLiteral(" (const auto &message : messages)\n"
                            "    {\n"
                            "        req[QStringLiteral(\"message\")] = message.to_json();\n"
                            "    }"));
}

void tst_qompi_completion_policy_t::normalize_response_strips_echoed_fim_markers()
{
    QCOMPARE(QString::fromStdString(normalize_completion_response(
                 "previous_line\n    for", "\nnext_line",
                 "<fim_prefix>\nprevious_line\n    for\n<fim_suffix>\n\nnext_line\n<fim_middle>\n"
                 " (const auto &message : messages)")),
             QStringLiteral(" (const auto &message : messages)"));
}

void tst_qompi_completion_policy_t::normalize_response_extracts_fenced_completion_body()
{
    QCOMPARE(QString::fromStdString(normalize_completion_response(
                 "previous_line\n    for", "\nnext_line",
                 "Here is the completion:\n```cpp\n (const auto &message : messages)\n```")),
             QStringLiteral(" (const auto &message : messages)"));
}

void tst_qompi_completion_policy_t::normalize_response_handles_full_suffix_overlap()
{
    QCOMPARE(QString::fromStdString(normalize_completion_response("call(", ");", ");")),
             QString());
}

void tst_qompi_completion_policy_t::build_inline_completion_text_preserves_line_context()
{
    QCOMPARE(QString::fromStdString(build_inline_completion_text(
                 "previous_line\n    for", " {\nnext_line", " (size_t i = 0; i < count; ++i)")),
             QStringLiteral("    for (size_t i = 0; i < count; ++i) {"));
}

void tst_qompi_completion_policy_t::stable_streaming_completion_prefix_trims_identifier_tail()
{
    QCOMPARE(
        QString::fromStdString(stable_streaming_completion_prefix(" (size_t i = 0; i < mess")),
        QStringLiteral(" (size_t i = 0; i < "));
}

void tst_qompi_completion_policy_t::apply_completion_stop_policy_trims_trailing_stop_word()
{
    completion_stop_policy_t stop_policy;
    stop_policy.stop_words = {" {"};

    bool matched_stop_word = false;
    const QString completion = QString::fromStdString(
        apply_completion_stop_policy(" (size_t i = 0) {", stop_policy, &matched_stop_word));

    QCOMPARE(completion, QStringLiteral(" (size_t i = 0)"));
    QCOMPARE(matched_stop_word, true);
}

void tst_qompi_completion_policy_t::aggregate_completion_stream_emits_stable_chunks()
{
    completion_request_t request;
    request.prefix = "    for";
    request.suffix = " {\nnext_line";

    completion_stop_policy_t stop_policy;
    stop_policy.stop_words = {" {"};

    completion_stream_state_t state;
    const completion_stream_update_t first_update =
        aggregate_completion_stream(state, request, " (size_t i = 0; i < cou", stop_policy);
    const completion_stream_update_t second_update =
        aggregate_completion_stream(state, request, "nt; ++i) {", stop_policy);

    QCOMPARE(first_update.has_chunk, true);
    QCOMPARE(QString::fromStdString(first_update.chunk.stable_text),
             QStringLiteral(" (size_t i = 0; i < "));
    QCOMPARE(second_update.has_chunk, true);
    QCOMPARE(QString::fromStdString(second_update.chunk.stable_text),
             QStringLiteral(" (size_t i = 0; i < count; ++i)"));
    QCOMPARE(QString::fromStdString(second_update.normalized_completion),
             QStringLiteral(" (size_t i = 0; i < count; ++i)"));
}

}  // namespace qompi

QTEST_MAIN(qompi::tst_qompi_completion_policy_t)

#include "tst_qompi_completion_policy.moc"
