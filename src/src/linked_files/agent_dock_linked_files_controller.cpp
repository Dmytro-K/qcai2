/*! Implements linked-files behavior extracted from agent_dock_widget_t. */

#include "agent_dock_linked_files_controller.h"

#include "../session/agent_dock_session_controller.h"
#include "../ui/agent_dock_widget.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonArray>
#include <QListWidgetItem>
#include <QRegularExpression>

namespace qcai2
{

namespace
{

QSize linked_file_item_size(const QFontMetrics &metrics, const QString &text, int icon_width)
{
    return QSize(metrics.horizontalAdvance(text) + icon_width + 26,
                 qMax(metrics.height(), 16) + 8);
}

bool has_ignored_linked_path_prefix(const QStringList &ignored_prefixes, const QString &path)
{
    for (const QString &prefix : ignored_prefixes)
    {
        if (!prefix.isEmpty() && path.startsWith(prefix))
        {
            return true;
        }
    }

    return false;
}

}  // namespace

agent_dock_linked_files_controller_t::agent_dock_linked_files_controller_t(
    agent_dock_widget_t &dock)
    : dock(dock)
{
}

QString agent_dock_linked_files_controller_t::current_project_dir() const
{
    if (auto *ctx = this->dock.controller->editor_context(); ((ctx != nullptr) == true))
    {
        return ctx->capture().project_dir;
    }
    return {};
}

QString agent_dock_linked_files_controller_t::normalize_linked_file_path(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() == true)
    {
        return {};
    }

    QFileInfo info(trimmed);
    QString absolute_path =
        info.isAbsolute() ? info.absoluteFilePath()
                          : QFileInfo(this->linked_file_absolute_path(trimmed)).absoluteFilePath();
    if (absolute_path.isEmpty() == true)
    {
        absolute_path = QFileInfo(trimmed).absoluteFilePath();
    }

    const QFileInfo absolute_info(absolute_path);
    if (((!absolute_info.exists() || !absolute_info.isFile()) == true))
    {
        return {};
    }

    const QString project_dir = this->current_project_dir();
    if (project_dir.isEmpty() == false)
    {
        const QString relative =
            QDir(project_dir).relativeFilePath(absolute_info.absoluteFilePath());
        if (((!relative.startsWith(QStringLiteral("../"))) == true))
        {
            return QDir::cleanPath(relative);
        }
    }

    return QDir::cleanPath(absolute_info.absoluteFilePath());
}

QString agent_dock_linked_files_controller_t::linked_file_absolute_path(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() == true)
    {
        return {};
    }

    QFileInfo info(trimmed);
    if (info.isAbsolute() == true)
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    const QString project_dir = this->current_project_dir();
    if (project_dir.isEmpty() == true)
    {
        return {};
    }

    return QDir(project_dir).absoluteFilePath(trimmed);
}

QStringList agent_dock_linked_files_controller_t::linked_file_candidates() const
{
    const QString project_dir = this->current_project_dir();
    if (project_dir.isEmpty() == true)
    {
        return {};
    }

    if ((this->cached_linked_file_root == project_dir &&
         !this->cached_linked_file_candidates.isEmpty()) == true)
    {
        return this->cached_linked_file_candidates;
    }

    QStringList candidates;
    QDirIterator it(project_dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir root_dir(project_dir);
    while (it.hasNext() == true)
    {
        const QString absolute_path = it.next();
        const QString relative_path = QDir::cleanPath(root_dir.relativeFilePath(absolute_path));
        if (((relative_path.startsWith(QStringLiteral(".qcai2/")) ||
              relative_path.startsWith(QStringLiteral(".git/"))) == true))
        {
            continue;
        }
        if (has_ignored_linked_path_prefix(this->ignored_linked_file_paths, relative_path))
        {
            continue;
        }
        candidates.append(relative_path);
    }

    candidates.sort(Qt::CaseInsensitive);
    this->cached_linked_file_root = project_dir;
    this->cached_linked_file_candidates = candidates;
    return this->cached_linked_file_candidates;
}

QStringList agent_dock_linked_files_controller_t::linked_files_from_goal_text() const
{
    QStringList files;
    static const QRegularExpression re(QStringLiteral(R"((?:^|\s)#([^\s#]+))"));
    QRegularExpressionMatchIterator it = re.globalMatch(this->dock.goal_edit->toPlainText());
    while (it.hasNext() == true)
    {
        const QRegularExpressionMatch match = it.next();
        const QString normalized = this->normalize_linked_file_path(match.captured(1));
        if (!normalized.isEmpty() && !files.contains(normalized))
        {
            files.append(normalized);
        }
    }
    return files;
}

QString agent_dock_linked_files_controller_t::current_editor_linked_file() const
{
    if (((this->dock.controller == nullptr ||
          this->dock.controller->editor_context() == nullptr) == true))
    {
        return {};
    }

    return this->normalize_linked_file_path(
        this->dock.controller->editor_context()->capture().file_path);
}

QString agent_dock_linked_files_controller_t::default_linked_file() const
{
    const QString normalized = this->current_editor_linked_file();
    if (((normalized.isEmpty() ||
          has_ignored_linked_path_prefix(this->ignored_linked_file_paths, normalized) ||
          this->hidden_default_linked_files.contains(normalized)) == true))
    {
        return {};
    }

    return normalized;
}

QStringList agent_dock_linked_files_controller_t::effective_linked_files() const
{
    QStringList files = this->manual_linked_file_paths;
    const QString default_file = this->default_linked_file();
    if (!default_file.isEmpty() && !files.contains(default_file))
    {
        files.append(default_file);
    }
    for (const QString &path : this->linked_files_from_goal_text())
    {
        if (!files.contains(path))
        {
            files.append(path);
        }
    }
    return files;
}

void agent_dock_linked_files_controller_t::add_linked_files(const QStringList &paths)
{
    bool changed = false;
    for (const QString &path : paths)
    {
        const QString normalized = this->normalize_linked_file_path(path);
        if (normalized.isEmpty())
        {
            continue;
        }
        changed = this->hidden_default_linked_files.removeAll(normalized) > 0 || changed;
        if (this->manual_linked_file_paths.contains(normalized))
        {
            continue;
        }
        this->manual_linked_file_paths.append(normalized);
        changed = true;
    }

    if (changed == false)
    {
        return;
    }

    this->refresh_ui();
    this->dock.session_controller->save_chat();
}

void agent_dock_linked_files_controller_t::remove_selected_linked_files()
{
    if (((this->dock.linked_files_view == nullptr) == true))
    {
        return;
    }

    const QList<QListWidgetItem *> selected_items = this->dock.linked_files_view->selectedItems();
    if (selected_items.isEmpty())
    {
        return;
    }

    QString goal_text = this->dock.goal_edit->toPlainText();
    bool goal_changed = false;
    bool manual_changed = false;
    bool hidden_changed = false;
    const QString current_default_file = this->current_editor_linked_file();

    for (QListWidgetItem *item : selected_items)
    {
        const QString path = item->data(Qt::UserRole).toString();
        manual_changed = this->manual_linked_file_paths.removeAll(path) > 0 || manual_changed;
        if (path == current_default_file && !this->hidden_default_linked_files.contains(path))
        {
            this->hidden_default_linked_files.append(path);
            hidden_changed = true;
        }

        const QRegularExpression token_re(
            QStringLiteral(R"((^|\s)#%1(?=\s|$))").arg(QRegularExpression::escape(path)));
        const QString updated_goal = goal_text.replace(token_re, QStringLiteral("\\1"));
        goal_changed = goal_changed || (updated_goal != goal_text);
        goal_text = updated_goal;
    }

    if (goal_changed == true)
    {
        this->dock.goal_edit->setPlainText(goal_text);
    }

    if (((manual_changed || goal_changed || hidden_changed) == true))
    {
        this->refresh_ui();
        this->dock.session_controller->save_chat();
    }
}

void agent_dock_linked_files_controller_t::refresh_ui()
{
    if (((this->dock.linked_files_view == nullptr) == true))
    {
        return;
    }

    const QStringList linked_files = this->effective_linked_files();
    this->dock.linked_files_view->clear();
    this->dock.linked_files_view->setVisible(!linked_files.isEmpty());

    if (linked_files.isEmpty())
    {
        this->dock.linked_files_view->sync_to_contents();
        return;
    }

    static QFileIconProvider icon_provider;
    const QFontMetrics metrics(this->dock.linked_files_view->font());
    const int icon_width = this->dock.linked_files_view->iconSize().width();
    for (const QString &path : linked_files)
    {
        const QString absolute_path = this->linked_file_absolute_path(path);
        const QFileInfo file_info(absolute_path.isEmpty() ? path : absolute_path);
        const QString label = file_info.fileName().isEmpty() ? path : file_info.fileName();
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, path);
        item->setToolTip(absolute_path);
        item->setIcon(icon_provider.icon(file_info));
        item->setSizeHint(linked_file_item_size(metrics, label, icon_width));
        this->dock.linked_files_view->addItem(item);
    }
    this->dock.linked_files_view->sync_to_contents();
}

QString agent_dock_linked_files_controller_t::linked_files_prompt_context() const
{
    const QStringList linked_files = this->effective_linked_files();
    if (linked_files.isEmpty())
    {
        return {};
    }

    QString context = QStringLiteral("Linked files for this request:\n");
    for (const QString &path : linked_files)
    {
        context += QStringLiteral("- %1\n").arg(path);
    }

    context += QStringLiteral("\nUse these linked files as explicit request context.\n");

    static constexpr qsizetype total_chars_max = 60000;
    qsizetype used_chars = 0;
    for (const QString &path : linked_files)
    {
        const QString absolute_path = this->linked_file_absolute_path(path);
        QFile file(absolute_path);
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }

        QString content = QString::fromUtf8(file.readAll());
        const qsizetype remaining = total_chars_max - used_chars;
        if (remaining <= 0)
        {
            break;
        }
        if (content.size() > remaining)
        {
            content = content.left(remaining) + QStringLiteral("\n... [truncated]");
        }

        context += QStringLiteral("\nFile: %1\n~~~~text\n%2\n~~~~\n").arg(path, content);
        used_chars += content.size();
    }

    return context.trimmed();
}

void agent_dock_linked_files_controller_t::clear_state()
{
    this->manual_linked_file_paths.clear();
    this->ignored_linked_file_paths.clear();
    this->hidden_default_linked_files.clear();
    this->invalidate_candidates();
}

const QStringList &agent_dock_linked_files_controller_t::manual_linked_files() const
{
    return this->manual_linked_file_paths;
}

const QStringList &agent_dock_linked_files_controller_t::ignored_linked_files() const
{
    return this->ignored_linked_file_paths;
}

void agent_dock_linked_files_controller_t::set_manual_linked_files(const QStringList &paths)
{
    this->manual_linked_file_paths = paths;
}

void agent_dock_linked_files_controller_t::set_ignored_linked_files(const QStringList &paths)
{
    this->ignored_linked_file_paths = paths;
}

void agent_dock_linked_files_controller_t::invalidate_candidates()
{
    this->cached_linked_file_root.clear();
    this->cached_linked_file_candidates.clear();
}

}  // namespace qcai2
