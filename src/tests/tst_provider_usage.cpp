/*! Unit tests for shared provider token-usage parsing and formatting. */

#include <QtTest>

#include "src/providers/ProviderUsage.h"

using namespace qcai2;

class provider_usage_test_t : public QObject
{
    Q_OBJECT

private slots:
    void provider_usage_from_response_object_reads_open_ai_usage_shapes();
    void provider_usage_from_response_object_reads_direct_top_level_fields();
    void format_provider_usage_summary_omits_missing_fields_and_derives_total();
    void format_provider_usage_summary_appends_duration();
    void format_provider_usage_summary_supports_duration_only();
};

void provider_usage_test_t::provider_usage_from_response_object_reads_open_ai_usage_shapes()
{
    const QJsonObject response{
        {QStringLiteral("usage"),
         QJsonObject{{QStringLiteral("prompt_tokens"), 120},
                     {QStringLiteral("completion_tokens"), 34},
                     {QStringLiteral("total_tokens"), 154},
                     {QStringLiteral("prompt_tokens_details"),
                      QJsonObject{{QStringLiteral("cached_tokens"), 16}}},
                     {QStringLiteral("completion_tokens_details"),
                      QJsonObject{{QStringLiteral("reasoning_tokens"), 9}}}}}};

    const provider_usage_t usage = provider_usage_from_response_object(response);

    QCOMPARE(usage.input_tokens, 120);
    QCOMPARE(usage.output_tokens, 34);
    QCOMPARE(usage.total_tokens, 154);
    QCOMPARE(usage.reasoning_tokens, 9);
    QCOMPARE(usage.cached_input_tokens, 16);
    QVERIFY(usage.has_any());
}

void provider_usage_test_t::provider_usage_from_response_object_reads_direct_top_level_fields()
{
    const QJsonObject response{{QStringLiteral("input_tokens"), 80},
                               {QStringLiteral("output_tokens"), 25},
                               {QStringLiteral("reasoning_tokens"), 4},
                               {QStringLiteral("cached_input_tokens"), 10}};

    const provider_usage_t usage = provider_usage_from_response_object(response);

    QCOMPARE(usage.input_tokens, 80);
    QCOMPARE(usage.output_tokens, 25);
    QCOMPARE(usage.total_tokens, -1);
    QCOMPARE(usage.reasoning_tokens, 4);
    QCOMPARE(usage.cached_input_tokens, 10);
    QCOMPARE(usage.resolved_total_tokens(), 105);
}

void provider_usage_test_t::format_provider_usage_summary_omits_missing_fields_and_derives_total()
{
    provider_usage_t usage;
    usage.input_tokens = 50;
    usage.output_tokens = 12;
    usage.cached_input_tokens = 5;

    QCOMPARE(format_provider_usage_summary(usage),
             QStringLiteral("input 50 | output 12 | total 62 | cached input 5"));
}

void provider_usage_test_t::format_provider_usage_summary_appends_duration()
{
    provider_usage_t usage;
    usage.input_tokens = 50;
    usage.output_tokens = 12;

    QCOMPARE(format_provider_usage_summary(usage, 1534),
             QStringLiteral("input 50 | output 12 | total 62 | time 1.53 s"));
}

void provider_usage_test_t::format_provider_usage_summary_supports_duration_only()
{
    QCOMPARE(format_provider_usage_summary(provider_usage_t{}, 87), QStringLiteral("time 87 ms"));
}

QTEST_APPLESS_MAIN(provider_usage_test_t)

#include "tst_provider_usage.moc"
