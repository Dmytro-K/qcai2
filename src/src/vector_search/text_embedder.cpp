#include "text_embedder.h"

#include <QCryptographicHash>
#include <QRegularExpression>

#include <cmath>

namespace qcai2
{

namespace
{

quint64 token_hash(const QString &token)
{
    const QByteArray digest = QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha1);
    quint64 value = 0;
    const int bytes_to_use = qMin(8, static_cast<int>(digest.size()));
    for (int index = 0; index < bytes_to_use; ++index)
    {
        value = (value << 8) | static_cast<quint8>(digest.at(index));
    }
    return value;
}

QStringList token_ngrams(const QString &token)
{
    QStringList ngrams;
    if (token.size() < 5)
    {
        return ngrams;
    }

    for (int index = 0; index + 3 <= token.size(); ++index)
    {
        ngrams.append(token.sliced(index, 3));
    }
    return ngrams;
}

}  // namespace

QStringList text_embedder_t::tokenize(const QString &text)
{
    static const QRegularExpression token_pattern(QStringLiteral("[\\p{L}\\p{N}_]+"));

    QStringList tokens;
    auto match_iterator = token_pattern.globalMatch(text.toLower());
    while (match_iterator.hasNext() == true)
    {
        const QRegularExpressionMatch match = match_iterator.next();
        const QString token = match.captured(0).trimmed();
        if (token.isEmpty() == false)
        {
            tokens.append(token);
        }
    }
    return tokens;
}

QList<float> text_embedder_t::embed_text(const QString &text) const
{
    QList<float> embedding;
    embedding.fill(0.0F, embedding_dimensions);

    const QStringList tokens = tokenize(text);
    if (tokens.isEmpty() == true)
    {
        return embedding;
    }

    for (const QString &token : tokens)
    {
        const QList<QString> features = [&token]() {
            QList<QString> values;
            values.append(token);
            const QStringList ngrams = token_ngrams(token);
            for (const QString &ngram : ngrams)
            {
                values.append(ngram);
            }
            return values;
        }();

        for (const QString &feature : features)
        {
            const quint64 hash = token_hash(feature);
            const int index = static_cast<int>(hash % static_cast<quint64>(embedding_dimensions));
            const float direction = (hash & (1ULL << 8)) == 0 ? 1.0F : -1.0F;
            embedding[index] += direction;
        }
    }

    double norm = 0.0;
    for (const float value : embedding)
    {
        norm += static_cast<double>(value) * static_cast<double>(value);
    }
    norm = std::sqrt(norm);

    if (norm <= 0.0)
    {
        return embedding;
    }

    for (float &value : embedding)
    {
        value = static_cast<float>(static_cast<double>(value) / norm);
    }
    return embedding;
}

QList<text_chunk_t> text_embedder_t::chunk_text(const QString &text, int max_chunk_chars,
                                                int overlap_lines) const
{
    QList<text_chunk_t> chunks;
    if (text.trimmed().isEmpty() == true)
    {
        return chunks;
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    int start_line = 0;

    while (start_line < lines.size())
    {
        QStringList chunk_lines;
        int line_index = start_line;
        int chars = 0;

        while (line_index < lines.size())
        {
            const QString &line = lines.at(line_index);
            const int next_chars = chars + static_cast<int>(line.size()) + 1;
            if ((chunk_lines.isEmpty() == false) && (next_chars > max_chunk_chars))
            {
                break;
            }
            chunk_lines.append(line);
            chars = next_chars;
            ++line_index;
        }

        if (chunk_lines.isEmpty() == true)
        {
            chunk_lines.append(lines.at(start_line));
            line_index = start_line + 1;
        }

        text_chunk_t chunk;
        chunk.line_start = start_line + 1;
        chunk.line_end = line_index;
        chunk.content = chunk_lines.join(QLatin1Char('\n')).trimmed();
        if (chunk.content.isEmpty() == false)
        {
            chunks.append(chunk);
        }

        if (line_index >= lines.size())
        {
            break;
        }

        start_line = qMax(start_line + 1, line_index - overlap_lines);
    }

    return chunks;
}

}  // namespace qcai2
