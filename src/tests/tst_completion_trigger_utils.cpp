/*! @file
    @brief Regression tests for shared autocomplete trigger helpers.
*/

#include "../src/completion/completion_trigger_utils.h"

#include <QtTest>

namespace qcai2
{

class tst_completion_trigger_utils_t : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Identifier fragments should count only contiguous identifier characters before the cursor.
     */
    void identifier_fragment_length_counts_identifier_suffix();

    /**
     * Identifier fragment counting should clamp invalid cursor positions and accept underscores.
     */
    void identifier_fragment_length_handles_boundaries_and_underscores();

    /**
     * Pure whitespace insertions must not start autocomplete work.
     */
    void typed_insertion_ignores_whitespace_only_text();

    /**
     * Typed insertions with any visible character should still count as meaningful.
     */
    void typed_insertion_accepts_mixed_whitespace_and_visible_text();

    /**
     * Word triggers should fire once the identifier length reaches or exceeds the threshold.
     */
    void auto_trigger_allows_word_lengths_at_or_above_threshold();

    /**
     * Traditional completion punctuation should still trigger immediately.
     */
    void auto_trigger_allows_traditional_trigger_characters();

    /**
     * Trailing newline or indent inserted with the typed fragment must not move the trigger end
     * past the last meaningful typed character.
     */
    void typed_fragment_end_offset_ignores_trailing_whitespace();

    /**
     * Trigger columns should be adjusted back from trailing whitespace inserted with the fragment.
     */
    void typed_fragment_trigger_column_ignores_trailing_whitespace();

    /**
     * Invalid cursor positions and non-identifier endings must not trigger word completion.
     */
    void auto_trigger_rejects_invalid_positions_and_non_identifier_chars();

    /**
     * Traditional trigger characters should win even when the word threshold is disabled.
     */
    void auto_trigger_keeps_traditional_punctuation_with_non_positive_threshold();
};

void tst_completion_trigger_utils_t::identifier_fragment_length_counts_identifier_suffix()
{
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("for"), 3), 3);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("foo.bar"), 7), 3);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("foo("), 4), 0);
}

void tst_completion_trigger_utils_t::
    identifier_fragment_length_handles_boundaries_and_underscores()
{
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral(""), 0), 0);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("alpha_beta9"), 11), 11);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("alpha_beta9"), 99), 11);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("value + name"), 7), 0);
}

void tst_completion_trigger_utils_t::typed_insertion_ignores_whitespace_only_text()
{
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("")), false);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("f")), true);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("(")), true);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral(" ")), false);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("\n")), false);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("\t  \n")), false);
}

void tst_completion_trigger_utils_t::typed_insertion_accepts_mixed_whitespace_and_visible_text()
{
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("  foo")), true);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("\n    bar\n")), true);
}

void tst_completion_trigger_utils_t::auto_trigger_allows_word_lengths_at_or_above_threshold()
{
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("fo"), 2, 3), false);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("for"), 3, 3), true);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("format"), 6, 3), true);
}

void tst_completion_trigger_utils_t::auto_trigger_allows_traditional_trigger_characters()
{
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("object."), 7, 3), true);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("ptr->"), 5, 3), true);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("value("), 6, 3), true);
}

void tst_completion_trigger_utils_t::typed_fragment_end_offset_ignores_trailing_whitespace()
{
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("")), 0);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("   \n\t")), 0);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("for")), 3);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("for\n")), 3);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("for\n    ")), 3);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("    for\n")), 7);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("  foo  ")), 5);
}

void tst_completion_trigger_utils_t::typed_fragment_trigger_column_ignores_trailing_whitespace()
{
    QCOMPARE(typed_fragment_trigger_column(QStringLiteral("for"), 12), 12);
    QCOMPARE(typed_fragment_trigger_column(QStringLiteral("for\n"), 12), 11);
    QCOMPARE(typed_fragment_trigger_column(QStringLiteral("for\n    "), 12), 7);
    QCOMPARE(typed_fragment_trigger_column(QStringLiteral("   \n"), 4), 4);
    QCOMPARE(typed_fragment_trigger_column(QStringLiteral("  foo  "), 20), 18);
}

void tst_completion_trigger_utils_t::
    auto_trigger_rejects_invalid_positions_and_non_identifier_chars()
{
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("for"), 0, 3), false);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("for"), -1, 3), false);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("for"), 4, 3), false);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("value "), 6, 3), false);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("::"), 2, 3), true);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("a"), 1, 3), false);
}

void tst_completion_trigger_utils_t::
    auto_trigger_keeps_traditional_punctuation_with_non_positive_threshold()
{
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("object."), 7, 0), true);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("ptr->"), 5, -1), true);
    QCOMPARE(should_auto_trigger_completion(QStringLiteral("word"), 4, 0), false);
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_completion_trigger_utils_t)

#include "tst_completion_trigger_utils.moc"
