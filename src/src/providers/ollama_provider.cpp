#include "ollama_provider.h"

namespace qcai2
{

/**
 * Creates an Ollama provider pointed at the default localhost endpoint.
 * @param parent Parent QObject that owns this instance.
 */
ollama_provider_t::ollama_provider_t(QObject *parent) : open_ai_compatible_provider_t(parent)
{
    // Default Ollama endpoint
    set_base_url(QStringLiteral("http://localhost:11434"));
}

QString ollama_provider_t::attachment_support_error(const file_attachment_t &attachment) const
{
    const QString mime_type = attachment.mime_type.trimmed();
    if (mime_type.startsWith(QStringLiteral("image/")) == true)
    {
        return {};
    }
    const QString display_name = attachment.file_name.trimmed().isEmpty() == false
                                     ? attachment.file_name.trimmed()
                                     : attachment.storage_path;
    return QStringLiteral("The current provider '%1' supports only image attachments, but '%2' "
                          "has MIME type '%3'.")
        .arg(this->display_name(), display_name,
             mime_type.isEmpty() == false ? mime_type : QStringLiteral("unknown"));
}

}  // namespace qcai2
