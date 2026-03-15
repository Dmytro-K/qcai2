/*! Implements # file reference popup completions and highlighting. */

#include "FileReferenceGoalHandler.h"

#include <QColor>

namespace qcai2
{

namespace
{

struct HashTokenState
{
    bool active = false;
    int start = 0;
    int end = 0;
};

bool isTokenBoundary(const QString &text, int index)
{
    return index <= 0 || text.at(index - 1).isSpace();
}

HashTokenState hashTokenAtCursor(const QString &text, int cursorPosition)
{
    for (int i = 0; i < text.size(); ++i)
    {
        if (text.at(i) != QLatin1Char('#') || !isTokenBoundary(text, i))
        {
            continue;
        }

        int end = i + 1;
        while (end < text.size() && !text.at(end).isSpace())
        {
            ++end;
        }

        if (((cursorPosition < i + 1 || cursorPosition > end) == true))
        {
            continue;
        }

        return {true, i, end};
    }

    return {};
}

QList<HashTokenState> allHashTokens(const QString &text)
{
    QList<HashTokenState> tokens;
    for (int i = 0; i < text.size(); ++i)
    {
        if (text.at(i) != QLatin1Char('#') || !isTokenBoundary(text, i))
        {
            continue;
        }

        int end = i + 1;
        while (end < text.size() && !text.at(end).isSpace())
        {
            ++end;
        }
        tokens.append({true, i, end});
        i = end - 1;
    }
    return tokens;
}

QColor fileReferenceColor(const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Highlight);
    return palette.color(QPalette::Base).lightness() < 128 ? base.lighter(130) : base.darker(120);
}

}  // namespace

FileReferenceGoalHandler::FileReferenceGoalHandler(CandidateProvider candidateProvider)
    : m_candidateProvider(std::move(candidateProvider))
{
}

GoalCompletionSession
FileReferenceGoalHandler::completionSession(const GoalCompletionRequest &request) const
{
    if (((!m_candidateProvider) == true))
    {
        return {};
    }

    const HashTokenState token = hashTokenAtCursor(request.text, request.cursorPosition);
    if (token.active == false)
    {
        return {};
    }

    GoalCompletionSession session;
    session.active = true;
    session.replaceStart = token.start;
    session.replaceEnd = token.end;
    session.prefix = request.text.mid(token.start, request.cursorPosition - token.start);

    const QString filePrefix = session.prefix.mid(1);
    const QStringList candidates = m_candidateProvider();
    QList<GoalCompletionItem> caseSensitiveItems;
    QList<GoalCompletionItem> caseInsensitiveItems;
    for (const QString &candidate : candidates)
    {
        if (!candidate.contains(filePrefix, Qt::CaseInsensitive))
        {
            continue;
        }

        GoalCompletionItem item{QStringLiteral("#%1").arg(candidate),
                                QStringLiteral("#%1").arg(candidate), candidate};
        if (candidate.contains(filePrefix, Qt::CaseSensitive))
        {
            caseSensitiveItems.append(item);
        }
        else
        {
            caseInsensitiveItems.append(item);
        }
    }

    session.items = caseSensitiveItems;
    session.items.append(caseInsensitiveItems);

    return session;
}

QList<GoalHighlightSpan> FileReferenceGoalHandler::highlightSpans(const QString &text,
                                                                  const QPalette &palette) const
{
    QList<GoalHighlightSpan> spans;
    QTextCharFormat format;
    format.setForeground(fileReferenceColor(palette));

    for (const HashTokenState &token : allHashTokens(text))
    {
        spans.append({token.start, token.end - token.start, format});
    }

    return spans;
}

}  // namespace qcai2
