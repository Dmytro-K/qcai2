/*! Unit tests for shared provider token-usage parsing and formatting. */

#include <QtTest>

#include "src/providers/ProviderUsage.h"

using namespace qcai2;

class ProviderUsageTest : public QObject
{
    Q_OBJECT

private slots:
    void providerUsageFromResponseObject_readsOpenAiUsageShapes();
    void providerUsageFromResponseObject_readsDirectTopLevelFields();
    void formatProviderUsageSummary_omitsMissingFieldsAndDerivesTotal();
};

void ProviderUsageTest::providerUsageFromResponseObject_readsOpenAiUsageShapes()
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

    const ProviderUsage usage = providerUsageFromResponseObject(response);

    QCOMPARE(usage.inputTokens, 120);
    QCOMPARE(usage.outputTokens, 34);
    QCOMPARE(usage.totalTokens, 154);
    QCOMPARE(usage.reasoningTokens, 9);
    QCOMPARE(usage.cachedInputTokens, 16);
    QVERIFY(usage.hasAny());
}

void ProviderUsageTest::providerUsageFromResponseObject_readsDirectTopLevelFields()
{
    const QJsonObject response{{QStringLiteral("input_tokens"), 80},
                               {QStringLiteral("output_tokens"), 25},
                               {QStringLiteral("reasoning_tokens"), 4},
                               {QStringLiteral("cached_input_tokens"), 10}};

    const ProviderUsage usage = providerUsageFromResponseObject(response);

    QCOMPARE(usage.inputTokens, 80);
    QCOMPARE(usage.outputTokens, 25);
    QCOMPARE(usage.totalTokens, -1);
    QCOMPARE(usage.reasoningTokens, 4);
    QCOMPARE(usage.cachedInputTokens, 10);
    QCOMPARE(usage.resolvedTotalTokens(), 105);
}

void ProviderUsageTest::formatProviderUsageSummary_omitsMissingFieldsAndDerivesTotal()
{
    ProviderUsage usage;
    usage.inputTokens = 50;
    usage.outputTokens = 12;
    usage.cachedInputTokens = 5;

    QCOMPARE(formatProviderUsageSummary(usage),
             QStringLiteral("input 50 | output 12 | total 62 | cached input 5"));
}

QTEST_APPLESS_MAIN(ProviderUsageTest)

#include "tst_provider_usage.moc"
