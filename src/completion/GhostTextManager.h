#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

namespace TextEditor
{
class TextEditorWidget;
}

namespace qcai2
{

class IAIProvider;

// Manages ghost-text lifecycle: debounced AI requests, native TextSuggestion display,
// Tab to accept (works with FakeVim), Esc to dismiss.
class GhostTextManager : public QObject
{
    Q_OBJECT
public:
    explicit GhostTextManager(QObject *parent = nullptr);

    void setProvider(IAIProvider *provider)
    {
        m_provider = provider;
    }
    void setModel(const QString &model)
    {
        m_model = model;
    }
    void setEnabled(bool enabled);
    bool isEnabled() const
    {
        return m_enabled;
    }

    void attachToEditor(TextEditor::TextEditorWidget *editor);

private:
    void onContentsChanged(TextEditor::TextEditorWidget *editor);
    void requestCompletion(TextEditor::TextEditorWidget *editor);
    static QString cleanCompletion(const QString &raw);

    IAIProvider *m_provider = nullptr;
    QString m_model;
    bool m_enabled = true;
    QHash<TextEditor::TextEditorWidget *, QTimer *> m_debounceTimers;
    QHash<TextEditor::TextEditorWidget *, int> m_lastEditPositions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}  // namespace qcai2
