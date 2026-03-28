#pragma once

#include <QList>
#include <QString>

namespace qcai2
{

struct text_chunk_t
{
    QString content;
    int line_start = 1;
    int line_end = 1;
};

class text_embedder_t
{
public:
    static constexpr int embedding_dimensions = 256;

    QList<float> embed_text(const QString &text) const;
    QList<text_chunk_t> chunk_text(const QString &text, int max_chunk_chars = 1400,
                                   int overlap_lines = 4) const;

private:
    static QStringList tokenize(const QString &text);
};

}  // namespace qcai2
