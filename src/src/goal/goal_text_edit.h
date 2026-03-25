/*! Declares the specialized goal editor with shared popup completion support. */
#pragma once

#include "goal_special_handler.h"

#include <QImage>
#include <QStringList>
#include <QTextEdit>

#include <memory>
#include <vector>

class QCompleter;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QModelIndex;
class QKeyEvent;
class QMimeData;
class QStandardItemModel;

namespace qcai2
{

/**
 * QTextEdit prepared for shared special-token completions and colored spans.
 */
class goal_text_edit_t : public QTextEdit
{
    Q_OBJECT
public:
    explicit goal_text_edit_t(QWidget *parent = nullptr);

    /**
     * Adds one special-token handler to the editor.
     */
    void add_special_handler(std::unique_ptr<goal_special_handler_t> handler);

    /**
     * Returns true while the completion popup is visible.
     */
    bool has_completion_popup() const;

signals:
    void files_dropped(const QStringList &paths);
    void image_received(const QImage &image);

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void refresh_special_state();
    void refresh_completion_popup();
    void refresh_highlighting();
    void apply_completion_index(const QModelIndex &index);

    std::vector<std::unique_ptr<goal_special_handler_t>> special_handlers;
    QStandardItemModel *completion_model = nullptr;
    QCompleter *completer = nullptr;
    goal_completion_session_t active_completion;
};

}  // namespace qcai2
