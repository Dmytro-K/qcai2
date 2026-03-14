/*! Declares the main dock widget that presents chat, plans, diffs, and approvals. */
#pragma once

#include "../AgentController.h"
#include "../commands/SlashCommandRegistry.h"
#include "../diff/InlineDiffManager.h"
#include "../goal/LinkedFilesListWidget.h"
#include "../goal/GoalTextEdit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui
{
class AgentDockWidget;
}
QT_END_NAMESPACE

namespace qcai2
{

class AgentDockLinkedFilesController;
class AgentDockSessionController;

/**
 * Main dock widget for goal entry, logs, diff review, and approval prompts.
 */
class AgentDockWidget : public QWidget
{
    Q_OBJECT
public:
    /**
     * Creates the dock widget for one controller instance.
     * @param controller Agent controller that drives the UI.
     * @param parent Optional parent widget.
     */
    explicit AgentDockWidget(AgentController *controller, QWidget *parent = nullptr);

    /**
     * Persists the current chat session before the widget is destroyed.
     */
    ~AgentDockWidget() override;

private slots:
    /**
     * Starts a run using the current goal, model, and dry-run settings.
     */
    void onRunClicked();

    /**
     * Stops the active run and re-enables editing controls.
     */
    void onStopClicked();

    /**
     * Applies the currently approved subset of the diff preview.
     */
    void onApplyPatchClicked();

    /**
     * Reverts the last applied diff preview.
     */
    void onRevertPatchClicked();

    /**
     * Copies the rendered plan list to the clipboard.
     */
    void onCopyPlanClicked();

    /**
     * Appends a timestamped log entry to the log view.
     * @param msg Log message text.
     */
    void onLogMessage(const QString &msg);

    /**
     * Rebuilds the plan tab from the latest plan steps.
     * @param steps Plan steps to display.
     */
    void onPlanUpdated(const QList<PlanStep> &steps);

    /**
     * Refreshes diff preview widgets and inline diff markers.
     * @param diff Unified diff text.
     */
    void onDiffAvailable(const QString &diff);

    /**
     * Records and prompts for a tool approval request.
     * @param id Identifier value.
     * @param action Action description shown to the user.
     * @param reason Reason the approval is required.
     * @param preview Preview text shown with the approval request.
     */
    void onApprovalRequested(int id, const QString &action, const QString &reason,
                             const QString &preview);

    /**
     * Updates the status summary for the latest iteration.
     * @param iteration Current iteration number.
     */
    void onIterationChanged(int iteration);

    /**
     * Restores idle UI state after a run finishes.
     * @param summary Short completion summary.
     */
    void onStopped(const QString &summary);

private:
    friend class AgentDockLinkedFilesController;
    friend class AgentDockSessionController;

    /**
     * Builds the dock widget layout and wires local UI actions.
     */
    void setupUi();

    /**
     * Enables or disables controls based on controller run state.
     * @param running True when the UI should show a running state.
     */
    void updateRunState(bool running);

    /**
     * Executes a slash command locally when the goal starts with `/`.
     * @param goal Trimmed goal text from the editor.
     * @return True when the input was treated as a slash command.
     */
    bool tryExecuteSlashCommand(const QString &goal);

    /**
     * Handles keyboard shortcuts from the goal editor.
     * @param obj JSON object to convert.
     * @param event Event being filtered.
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

    /**
     * Flushes buffered log text into the visible log widget.
     */
    void renderLog();

    /**
     * Returns the combined committed and streaming markdown for the Actions Log.
     */
    QString currentLogMarkdown() const;

    /**
     * Clears the current in-memory chat, plan, diff, and approval state.
     */
    void clearChatState();

    /**
     * Refreshes the diff tab state and optionally re-renders inline editor markers.
     */
    void syncDiffUi(const QString &diff, bool focusDiffTab, bool refreshInlineMarkers);

    /** Designer-generated UI wrapper. */
    std::unique_ptr<Ui::AgentDockWidget> m_ui;

    /** Controller that owns the active agent run. */
    AgentController *m_controller;

    /** Input controls shown beside the goal editor. */
    QComboBox *m_projectCombo;
    LinkedFilesListWidget *m_linkedFilesView = nullptr;
    GoalTextEdit *m_goalEdit;
    QComboBox *m_modeCombo;
    QComboBox *m_modelCombo;
    QComboBox *m_reasoningCombo;
    QComboBox *m_thinkingCombo;
    QPushButton *m_runBtn;
    QPushButton *m_stopBtn;
    QCheckBox *m_dryRunCheck;

    /** Views hosted in the tab widget. */
    QTabWidget *m_tabs;
    QListWidget *m_planList;
    QTextEdit *m_logView;
    QPlainTextEdit *m_rawMarkdownView;
    QPlainTextEdit *m_diffView;
    QListWidget *m_diffFileList;
    QListWidget *m_approvalList;
    QPlainTextEdit *m_debugLogView;

    /** Persistent action buttons and status label. */
    QPushButton *m_applyPatchBtn;
    QPushButton *m_revertPatchBtn;
    QPushButton *m_copyPlanBtn;
    QLabel *m_statusLabel;

    /** Full diff currently shown in the preview tab. */
    QString m_currentDiff;

    /** Diff most recently applied so it can be reverted. */
    QString m_appliedDiff;

    /** Completed log text already committed to the log view. */
    QString m_logMarkdown;

    /** Streaming log text buffered until the throttle timer fires. */
    QString m_streamingMarkdown;

    /** Approval list items keyed by controller approval id. */
    QMap<int, QListWidgetItem *> m_approvalItems;

    /** Short timer that batches frequent log renders while streaming. */
    QTimer *m_renderThrottle;

    /** Manages inline editor markers for diff hunks. */
    InlineDiffManager *m_inlineDiffManager;

    /** Registry of dock-local slash commands. */
    SlashCommandRegistry m_slashCommands;

    /** Non-UI linked-files behavior extracted out of the widget. */
    std::unique_ptr<AgentDockLinkedFilesController> m_linkedFilesController;

    /** Non-UI project session persistence extracted out of the widget. */
    std::unique_ptr<AgentDockSessionController> m_sessionController;

};

}  // namespace qcai2
