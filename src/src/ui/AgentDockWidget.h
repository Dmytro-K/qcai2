/*! Declares the main dock widget that presents chat, plans, diffs, and approvals. */
#pragma once

#include "../AgentController.h"
#include "../commands/SlashCommandRegistry.h"
#include "../diff/InlineDiffManager.h"
#include "../goal/GoalTextEdit.h"
#include "../goal/LinkedFilesListWidget.h"

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
class agent_dock_widget_t;
}
QT_END_NAMESPACE

namespace qcai2
{

class agent_dock_linked_files_controller_t;
class agent_dock_session_controller_t;
class chat_context_manager_t;

/**
 * Main dock widget for goal entry, logs, diff review, and approval prompts.
 */
class agent_dock_widget_t : public QWidget
{
    Q_OBJECT
public:
    /**
     * Creates the dock widget for one controller instance.
     * @param controller Agent controller that drives the UI.
     * @param parent Optional parent widget.
     */
    explicit agent_dock_widget_t(agent_controller_t *controller,
                                 chat_context_manager_t *chat_context_manager,
                                 QWidget *parent = nullptr);

    /**
     * Persists the current chat session before the widget is destroyed.
     */
    ~agent_dock_widget_t() override;

    /**
     * Focuses the goal editor so the user can immediately type a new request.
     */
    void focus_goal_input();

private:
    /**
     * Starts a run using the current goal, model, and dry-run settings.
     */
    void on_run_clicked();

    /**
     * Stops the active run and re-enables editing controls.
     */
    void on_stop_clicked();

    /**
     * Applies the currently approved subset of the diff preview.
     */
    void on_apply_patch_clicked();

    /**
     * Reverts the last applied diff preview.
     */
    void on_revert_patch_clicked();

    /**
     * Copies the rendered plan list to the clipboard.
     */
    void on_copy_plan_clicked();

    /**
     * Appends a timestamped log entry to the log view.
     * @param msg Log message text.
     */
    void on_log_message(const QString &msg);

    /**
     * Appends a highlighted provider usage summary line to the log view.
     * @param usage Usage counters returned by the provider.
     * @param durationMs Wall-clock duration of the completed request in milliseconds.
     */
    void on_provider_usage_available(const provider_usage_t &usage, qint64 duration_ms);

    /**
     * Rebuilds the plan tab from the latest plan steps.
     * @param steps Plan steps to display.
     */
    void on_plan_updated(const QList<plan_step_t> &steps);

    /**
     * Refreshes diff preview widgets and inline diff markers.
     * @param diff Unified diff text.
     */
    void on_diff_available(const QString &diff);

    /**
     * Records and prompts for a tool approval request.
     * @param id Identifier value.
     * @param action Action description shown to the user.
     * @param reason Reason the approval is required.
     * @param preview Preview text shown with the approval request.
     */
    void on_approval_requested(int id, const QString &action, const QString &reason,
                               const QString &preview);

    /**
     * Updates the status summary for the latest iteration.
     * @param iteration Current iteration number.
     */
    void on_iteration_changed(int iteration);

    /**
     * Restores idle UI state after a run finishes.
     * @param summary Short completion summary.
     */
    void on_stopped(const QString &summary);

    friend class agent_dock_linked_files_controller_t;
    friend class agent_dock_session_controller_t;

    /**
     * Builds the dock widget layout and wires local UI actions.
     */
    void setup_ui();

    /**
     * Enables or disables controls based on controller run state.
     * @param running True when the UI should show a running state.
     */
    void update_run_state(bool running);

    /**
     * Executes a slash command locally when the goal starts with `/`.
     * @param goal Trimmed goal text from the editor.
     * @return True when the input was treated as a slash command.
     */
    bool try_execute_slash_command(QString &goal);

    /**
     * Handles keyboard shortcuts from the goal editor.
     * @param obj JSON object to convert.
     * @param event Event being filtered.
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

    /**
     * Flushes buffered log text into the visible log widget.
     */
    void render_log();

    /**
     * Performs a full markdown re-render of the Actions Log.
     */
    void render_log_full();

    /**
     * Appends new streaming tokens to the log view incrementally.
     */
    void render_log_streaming(const QString &new_tokens);

    /**
     * Appends one timestamped markdown entry to the committed log buffer.
     */
    void append_stamped_log_entry(const QString &body);

    /**
     * Returns the combined committed and streaming markdown for the Actions Log.
     */
    QString current_log_markdown() const;

    /**
     * Clears the current in-memory chat, plan, diff, and approval state.
     */
    void clear_chat_state();

    /**
     * Refreshes the diff tab state and optionally re-renders inline editor markers.
     */
    void sync_diff_ui(const QString &diff, bool focus_diff_tab, bool refresh_inline_markers);

    /** Designer-generated UI wrapper. */
    std::unique_ptr<Ui::agent_dock_widget_t> ui;

    /** Controller that owns the active agent run. */
    agent_controller_t *controller;

    /** Input controls shown beside the goal editor. */
    QComboBox *project_combo;
    linked_files_list_widget_t *linked_files_view = nullptr;
    goal_text_edit_t *goal_edit;
    QComboBox *mode_combo;
    QComboBox *model_combo;
    QComboBox *reasoning_combo;
    QComboBox *thinking_combo;
    QPushButton *run_btn;
    QPushButton *stop_btn;
    QCheckBox *dry_run_check;

    /** Views hosted in the tab widget. */
    QTabWidget *tabs;
    QListWidget *plan_list;
    QTextEdit *log_view;
    QPlainTextEdit *raw_markdown_view;
    QPlainTextEdit *diff_view;
    QListWidget *diff_file_list;
    QListWidget *approval_list;
    QPlainTextEdit *debug_log_view;

    /** Persistent action buttons and status label. */
    QPushButton *apply_patch_btn;
    QPushButton *revert_patch_btn;
    QPushButton *copy_plan_btn;
    QLabel *status_label;

    /** Full diff currently shown in the preview tab. */
    QString current_diff;

    /** Diff most recently applied so it can be reverted. */
    QString applied_diff;

    /** Completed log text already committed to the log view. */
    QString log_markdown;

    /** Streaming log text buffered until the throttle timer fires. */
    QString streaming_markdown;

    /** Length of streaming markdown already appended to the log view. */
    qsizetype streaming_rendered_len = 0;

    /** True while streaming tokens are being appended incrementally. */
    bool is_streaming = false;

    /** Approval list items keyed by controller approval id. */
    QMap<int, QListWidgetItem *> approval_items;

    /** Short timer that batches frequent log renders while streaming. */
    QTimer *render_throttle;

    /** Manages inline editor markers for diff hunks. */
    inline_diff_manager_t *inline_diff_manager;

    /** Registry of dock-local slash commands. */
    slash_command_registry_t slash_commands;

    /** Non-UI linked-files behavior extracted out of the widget. */
    std::unique_ptr<agent_dock_linked_files_controller_t> linked_files_controller;

    /** Non-UI project session persistence extracted out of the widget. */
    std::unique_ptr<agent_dock_session_controller_t> session_controller;
};

}  // namespace qcai2
