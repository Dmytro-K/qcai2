#include "vector_search_tools.h"

#include "../vector_search/text_embedder.h"
#include "../vector_search/vector_search_service.h"

#include <QDir>
#include <QFileInfo>

namespace qcai2
{

namespace
{

QString normalize_workspace_root(const QString &work_dir)
{
    if (work_dir.trimmed().isEmpty() == true)
    {
        return {};
    }
    const QFileInfo file_info(work_dir);
    const QString canonical = file_info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(file_info.absoluteFilePath()) : canonical;
}

int sanitize_limit(const QJsonObject &args)
{
    const int limit = args.value(QStringLiteral("limit")).toInt(8);
    return qBound(1, limit, 20);
}

QString format_file_matches(const QList<vector_search_match_t> &matches)
{
    if (matches.isEmpty() == true)
    {
        return QStringLiteral("No semantic file matches found.");
    }

    QStringList lines;
    lines.append(QStringLiteral("Semantic file matches:"));
    int index = 1;
    for (const vector_search_match_t &match : matches)
    {
        const QString relative_path =
            match.payload.value(QStringLiteral("relative_path")).toString(match.file_path);
        lines.append(QStringLiteral("%1. [%2] %3:%4-%5")
                         .arg(index++)
                         .arg(QString::number(match.score, 'f', 3))
                         .arg(relative_path)
                         .arg(match.line_start)
                         .arg(match.line_end));
        lines.append(QStringLiteral("   %1").arg(match.content.simplified().left(280)));
    }
    return lines.join(QLatin1Char('\n'));
}

QString format_history_matches(const QList<vector_search_match_t> &matches)
{
    if (matches.isEmpty() == true)
    {
        return QStringLiteral("No semantic history matches found.");
    }

    QStringList lines;
    lines.append(QStringLiteral("Semantic history matches:"));
    int index = 1;
    for (const vector_search_match_t &match : matches)
    {
        const QString role = match.payload.value(QStringLiteral("role")).toString();
        const QString conversation_id =
            match.payload.value(QStringLiteral("conversation_id")).toString();
        lines.append(QStringLiteral("%1. [%2] %3 in conversation %4")
                         .arg(index++)
                         .arg(QString::number(match.score, 'f', 3))
                         .arg(role.isEmpty() ? QStringLiteral("message") : role)
                         .arg(conversation_id));
        lines.append(QStringLiteral("   %1").arg(match.content.simplified().left(280)));
    }
    return lines.join(QLatin1Char('\n'));
}

QString execute_vector_query(const QJsonObject &args, const QString &work_dir,
                             const QString &entity_kind)
{
    const QString query = args.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty() == true)
    {
        return QStringLiteral("Error: 'query' argument is required.");
    }

    const QString workspace_root = normalize_workspace_root(work_dir);
    if (workspace_root.isEmpty() == true)
    {
        return QStringLiteral("Error: vector search requires a valid workspace root.");
    }

    auto &service = vector_search_service();
    if (service.is_available() == false)
    {
        return QStringLiteral("Error: vector search is unavailable. %1")
            .arg(service.status_message());
    }

    text_embedder_t embedder;
    QString error;
    const QList<vector_search_match_t> matches =
        service.search(embedder.embed_text(query),
                       QJsonObject{
                           {QStringLiteral("entity_kind"), entity_kind},
                           {QStringLiteral("workspace_root"), workspace_root},
                       },
                       sanitize_limit(args), &error);
    if (error.isEmpty() == false)
    {
        return QStringLiteral("Error: %1").arg(error);
    }

    return entity_kind == QStringLiteral("file") ? format_file_matches(matches)
                                                 : format_history_matches(matches);
}

}  // namespace

QJsonObject vector_search_tool_t::args_schema() const
{
    return QJsonObject{
        {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                              {QStringLiteral("required"), true}}},
        {QStringLiteral("limit"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}};
}

QString vector_search_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    return execute_vector_query(args, work_dir, QStringLiteral("file"));
}

QJsonObject vector_search_history_tool_t::args_schema() const
{
    return QJsonObject{
        {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                              {QStringLiteral("required"), true}}},
        {QStringLiteral("limit"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}};
}

QString vector_search_history_tool_t::execute(const QJsonObject &args, const QString &work_dir)
{
    return execute_vector_query(args, work_dir, QStringLiteral("history"));
}

}  // namespace qcai2
