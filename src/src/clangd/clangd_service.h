/*! Declares a reusable ClangCodeModel/clangd integration layer. */
#pragma once

#include <languageserverprotocol/lsptypes.h>
#include <utils/filepath.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace qcai2
{

struct clangd_range_t
{
    int start_line = -1;
    int start_column = -1;
    int end_line = -1;
    int end_column = -1;

    bool is_valid() const
    {
        return this->start_line > 0 && this->start_column > 0 && this->end_line > 0 &&
               this->end_column > 0;
    }

    bool contains(int line, int column) const
    {
        if (this->is_valid() == false)
        {
            return false;
        }

        if (line < this->start_line || line > this->end_line)
        {
            return false;
        }
        if (line == this->start_line && column < this->start_column)
        {
            return false;
        }
        if (line == this->end_line && column > this->end_column)
        {
            return false;
        }
        return true;
    }
};

struct clangd_location_t
{
    Utils::FilePath file_path;
    LanguageServerProtocol::Position position;
    int line = -1;
    int column = -1;

    bool is_valid() const
    {
        return this->file_path.isEmpty() == false && this->position.isValid() == true &&
               this->line > 0 && this->column > 0;
    }
};

struct clangd_link_t
{
    Utils::FilePath file_path;
    int line = -1;
    int column = -1;
    QString title;

    bool is_valid() const
    {
        return this->file_path.isEmpty() == false && this->line > 0 && this->column > 0;
    }
};

struct clangd_status_t
{
    QString file_path;
    QString client_state;
    QString version;
    QString error;
    bool available = false;
    bool reachable = false;
    bool fully_indexed = false;
    bool document_open = false;
};

struct clangd_symbol_info_t
{
    QString file_path;
    QString name;
    QString prefix;
    QString error;
    int line = -1;
    int column = -1;
    bool success = false;
};

struct clangd_diagnostic_t
{
    QString severity;
    QString message;
    QString source;
    QString code;
    clangd_range_t range;
};

struct clangd_reference_t
{
    clangd_link_t link;
    QString kind_name;
    QString preview;
    int kind = 0;
};

struct clangd_symbol_match_t
{
    QString name;
    QString detail;
    QString container_name;
    QString kind_name;
    int kind = 0;
    clangd_link_t link;
};

struct clangd_document_symbol_t
{
    QString name;
    QString detail;
    QString kind_name;
    int kind = 0;
    clangd_range_t range;
    clangd_range_t selection_range;
    QList<clangd_document_symbol_t> children;
};

struct clangd_completion_item_t
{
    QString label;
    QString detail;
    QString documentation;
    QString insert_text;
    QString kind_name;
    int kind = 0;
};

struct clangd_signature_t
{
    QString label;
    QStringList parameters;
    QString documentation;
    int active_parameter = -1;
};

struct clangd_hierarchy_item_t
{
    QString name;
    QString detail;
    QString kind_name;
    int kind = 0;
    clangd_link_t link;
    QList<clangd_range_t> source_ranges;
};

struct clangd_ast_path_entry_t
{
    QString role;
    QString kind;
    QString detail;
    QString arcana;
    QString type_name;
    clangd_range_t range;
};

class clangd_service_t : public QObject
{
public:
    explicit clangd_service_t(QObject *parent = nullptr);

    std::optional<clangd_location_t> current_editor_location(QString *error = nullptr) const;
    std::optional<clangd_location_t> resolve_location(const Utils::FilePath &file_path, int line,
                                                      int column, QString *error = nullptr) const;

    clangd_status_t status_for_current_editor(QString *error = nullptr) const;
    clangd_status_t status_for_file(const Utils::FilePath &file_path) const;
    clangd_symbol_info_t symbol_info_at(const clangd_location_t &location,
                                        int timeout_ms = 1500) const;
    QList<clangd_diagnostic_t> diagnostics_at(const clangd_location_t &location,
                                              QString *error = nullptr) const;
    QList<clangd_diagnostic_t> file_diagnostics(const Utils::FilePath &file_path,
                                                int max_scan_chars = 40000,
                                                QString *error = nullptr) const;

    std::optional<clangd_link_t> follow_symbol(const clangd_location_t &location,
                                               QString *error = nullptr) const;
    std::optional<clangd_link_t> switch_decl_def(const clangd_location_t &location,
                                                 QString *error = nullptr) const;
    QList<clangd_link_t> find_references(const clangd_location_t &location,
                                         bool include_declaration = false, int max_results = 100,
                                         QString *error = nullptr) const;
    QList<clangd_link_t> find_implementations(const clangd_location_t &location,
                                              int max_results = 100,
                                              QString *error = nullptr) const;
    QList<clangd_symbol_match_t> search_symbols(const QString &query, int max_results = 50,
                                                QString *error = nullptr) const;
    QList<clangd_document_symbol_t> document_symbols(const Utils::FilePath &file_path,
                                                     QString *error = nullptr) const;
    QList<clangd_ast_path_entry_t> ast_path_at(const clangd_location_t &location,
                                               QString *error = nullptr) const;
    QString hover_text_at(const clangd_location_t &location, QString *error = nullptr) const;
    QList<clangd_signature_t> signature_help_at(const clangd_location_t &location,
                                                int *active_signature = nullptr,
                                                QString *error = nullptr) const;
    QList<clangd_completion_item_t> completions_at(const clangd_location_t &location,
                                                   int max_results = 50,
                                                   QString *error = nullptr) const;
    QList<clangd_reference_t> document_highlights(const clangd_location_t &location,
                                                  QString *error = nullptr) const;
    QList<clangd_hierarchy_item_t> incoming_calls(const clangd_location_t &location,
                                                  int max_results = 50,
                                                  QString *error = nullptr) const;
    QList<clangd_hierarchy_item_t> outgoing_calls(const clangd_location_t &location,
                                                  int max_results = 50,
                                                  QString *error = nullptr) const;

    QString line_text_at(const Utils::FilePath &file_path, int line,
                         QString *error = nullptr) const;
    QString source_snippet(const Utils::FilePath &file_path, int start_line, int end_line,
                           QString *error = nullptr) const;
    QString active_build_dir_for_file(const Utils::FilePath &file_path) const;
};

}  // namespace qcai2
