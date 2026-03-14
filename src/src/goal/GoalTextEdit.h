/*! Declares the specialized goal editor with shared popup completion support. */
#pragma once

#include "GoalSpecialHandler.h"

#include <QTextEdit>

#include <memory>
#include <vector>

class QCompleter;
class QModelIndex;
class QKeyEvent;
class QStandardItemModel;

namespace qcai2
{

/**
 * QTextEdit prepared for shared special-token completions and colored spans.
 */
class GoalTextEdit : public QTextEdit
{
public:
    explicit GoalTextEdit(QWidget *parent = nullptr);

    /**
     * Adds one special-token handler to the editor.
     */
    void addSpecialHandler(std::unique_ptr<GoalSpecialHandler> handler);

    /**
     * Returns true while the completion popup is visible.
     */
    bool hasCompletionPopup() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void refreshSpecialState();
    void refreshCompletionPopup();
    void refreshHighlighting();
    void applyCompletionIndex(const QModelIndex &index);

    std::vector<std::unique_ptr<GoalSpecialHandler>> m_specialHandlers;
    QStandardItemModel *m_completionModel = nullptr;
    QCompleter *m_completer = nullptr;
    GoalCompletionSession m_activeCompletion;
};

}  // namespace qcai2
