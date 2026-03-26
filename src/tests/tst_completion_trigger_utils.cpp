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
     * Pure whitespace insertions must not start autocomplete work.
     */
    void typed_insertion_ignores_whitespace_only_text();

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
};

void tst_completion_trigger_utils_t::identifier_fragment_length_counts_identifier_suffix()
{
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("for"), 3), 3);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("foo.bar"), 7), 3);
    QCOMPARE(identifier_fragment_length_before_cursor(QStringLiteral("foo("), 4), 0);
}

void tst_completion_trigger_utils_t::typed_insertion_ignores_whitespace_only_text()
{
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("f")), true);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("(")), true);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral(" ")), false);
    QCOMPARE(is_non_whitespace_typed_insertion(QStringLiteral("\n")), false);
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
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("for")), 3);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("for\n")), 3);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("for\n    ")), 3);
    QCOMPARE(typed_fragment_end_offset(QStringLiteral("    for\n")), 7);
}

}  // namespace qcai2

QTEST_MAIN(qcai2::tst_completion_trigger_utils_t)

#include "tst_completion_trigger_utils.moc"
