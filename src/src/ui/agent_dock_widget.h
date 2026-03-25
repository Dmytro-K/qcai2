/*! Declares the main dock widget that presents chat, plans, diffs, and approvals. */
#pragma once

#include "../agent_controller.h"
#include "../commands/slash_command_registry.h"
#include "../debugger/debugger_session_service.h"
#include "../diff/inline_diff_manager.h"
#include "../goal/goal_text_edit.h"
#include "../goal/linked_files_list_widget.h"
#include "actions_log_delegate.h"
#include "actions_log_model.h"
#include "request_queue.h"

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
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
class auto_hiding_list_widget_t;
class chat_context_manager_t;
class decision_request_widget_t;
class debugger_status_widget_t;
class image_attachment_preview_strip_t;

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
                                 i_debugger_session_service_t *debugger_service,
                                 QWidget *parent = nullptr);

    /**
     * Persists the current chat session before the widget is destroyed.
     */
    ~agent_dock_widget_t() override;

    /**
     * Focuses the goal editor so the user can immediately type a new request.
     */
    void focus_goal_input();

    /**
     * Queues the current request using the same behavior as the dock Queue button.
     */
    void queue_current_request();

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
     * Queues the current request to run after the active one fully finishes.
     */
    void on_queue_clicked();

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
     * Clears the Debug Log view and retained in-memory logger entries.
     */
    void on_clear_debug_clicked();

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
     * Renders one structured user-decision card and pauses normal run controls around it.
     * @param request Structured decision payload emitted by the controller.
     */
    void on_decision_requested(const agent_decision_request_t &request);

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
     * Captures the current UI state as one executable request.
     */
    queued_request_t capture_current_request(const QString &goal) const;

    /**
     * Starts one captured request, applying auto-compact when needed.
     */
    void start_request(const queued_request_t &request,
                       const agent_controller_t::run_options_t &options =
                           agent_controller_t::run_options_t(false));

    /**
     * Starts an internal compaction run using the current model settings.
     */
    void start_compaction_request(const QString &compact_goal,
                                  const queued_request_t &base_request);

    /**
     * Appends one request to the local FIFO queue and refreshes the pending-items UI.
     */
    bool enqueue_request(const queued_request_t &request);

    /**
     * Starts the next queued request when the controller is idle.
     */
    void maybe_start_next_queued_request();

    /**
     * Rebuilds the generic pending-items list from the current queue state.
     */
    void refresh_pending_items_view();

    /**
     * Imports one pasted clipboard image into the active conversation draft.
     */
    void import_image_attachment_from_image(const QImage &image);

    /**
     * Imports dropped image files while forwarding non-image files to Linked Files.
     */
    void import_image_attachments_from_paths(const QStringList &paths);

    /**
     * Removes one draft image attachment and deletes its stored file.
     */
    void remove_image_attachment(const QString &attachment_id);

    /**
     * Opens one stored image attachment in the system viewer or fallback dialog.
     */
    void open_image_attachment(const QString &attachment_id);

    /**
     * Rebuilds the image-attachment preview strip from the current draft state.
     */
    void refresh_image_attachment_ui();

    /**
     * Resolves stored draft attachment paths into readable absolute file paths for providers.
     */
    QList<image_attachment_t>
    resolve_request_image_attachments(const QList<image_attachment_t> &attachments,
                                      QString *error = nullptr) const;

    /**
     * Submits one predefined decision option chosen from the decision card.
     * @param request_id Identifier of the pending decision request.
     * @param option_id Identifier of the chosen option.
     */
    void submit_decision_option(const QString &request_id, const QString &option_id);

    /**
     * Submits one freeform decision answer from the decision card.
     * @param request_id Identifier of the pending decision request.
     * @param freeform_text Custom answer text.
     */
    void submit_decision_freeform(const QString &request_id, const QString &freeform_text);

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
     * Commits buffered streaming markdown into the persistent log buffer.
     */
    void flush_streaming_markdown();

    /**
     * Drops buffered streaming markdown without committing it to the Actions Log.
     */
    void discard_streaming_markdown();

    /**
     * Returns the combined committed and streaming markdown for the Actions Log.
     */
    QString current_log_markdown() const;

    /**
     * Returns the committed log blocks followed by the current streaming preview block, if any.
     */
    QStringList current_log_markdown_blocks() const;

    /**
     * Replaces the raw-markdown debug tab with the current combined Actions Log markdown.
     */
    void sync_raw_markdown_view();

    /**
     * Appends one raw markdown block to the raw-markdown debug tab.
     * @param markdown_block Block to append.
     */
    void append_raw_markdown_block(const QString &markdown_block);

    /**
     * Scrolls both Actions Log views to the newest entry.
     */
    void scroll_log_views_to_bottom();

    /**
     * Switches the one interactive Actions Log row that hosts a selectable rich-text editor.
     * @param current Newly current row.
     * @param previous Previously current row.
     */
    void set_actions_log_active_row(const QModelIndex &current, const QModelIndex &previous);

    /**
     * Restores the interactive Actions Log row after model resets or row insertions.
     */
    void restore_actions_log_active_row();

    /**
     * Resets the bottom usage label for a new run or cleared chat state.
     */
    void reset_usage_display(const QString &provider_id = {}, const QString &model_name = {});

    /**
     * Refreshes the bottom usage value from the accumulated run usage state.
     */
    void update_usage_display();

    /**
     * Starts the live elapsed-seconds label for the active request.
     */
    void start_request_duration_display();

    /**
     * Stops and hides the live elapsed-seconds label when no request is active.
     */
    void stop_request_duration_display();

    /**
     * Refreshes the elapsed-seconds label from the current request start time.
     */
    void update_request_duration_display();

    /**
     * Clears the current in-memory chat, plan, diff, and approval state.
     */
    void clear_chat_state();

    /**
     * Refreshes the diff tab state and optionally re-renders inline editor markers.
     */
    void sync_diff_ui(const QString &diff, bool focus_diff_tab, bool refresh_inline_markers);
    void update_approval_selection_ui();
    void resolve_selected_approval(bool approved);
    void refresh_conversation_list();
    void rename_selected_conversation();
    void delete_selected_conversation();

    /** Designer-generated UI wrapper. */
    std::unique_ptr<Ui::agent_dock_widget_t> ui;

    /** Controller that owns the active agent run. */
    agent_controller_t *controller;

    /** Shared debugger-session service used by the optional debugger tab. */
    i_debugger_session_service_t *debugger_service = nullptr;

    /** Input controls shown beside the goal editor. */
    QComboBox *project_combo;
    QComboBox *conversation_combo = nullptr;
    linked_files_list_widget_t *linked_files_view = nullptr;
    image_attachment_preview_strip_t *image_attachment_strip = nullptr;
    goal_text_edit_t *goal_edit;
    QComboBox *mode_combo;
    QComboBox *model_combo;
    QComboBox *reasoning_combo;
    QComboBox *thinking_combo;
    QPushButton *run_btn;
    QPushButton *queue_btn = nullptr;
    QPushButton *stop_btn;
    QCheckBox *dry_run_check;
    decision_request_widget_t *decision_request_widget = nullptr;
    debugger_status_widget_t *debugger_status_widget = nullptr;
    auto_hiding_list_widget_t *pending_items_view = nullptr;

    /** Views hosted in the tab widget. */
    QTabWidget *tabs;
    QWidget *plan_page = nullptr;
    QWidget *actions_log_page = nullptr;
    QListWidget *plan_list;
    QListView *log_view;
    QPlainTextEdit *raw_markdown_view;
    QPlainTextEdit *diff_view;
    QListWidget *diff_file_list;
    QListWidget *approval_list;
    QPlainTextEdit *approval_preview_view = nullptr;
    QPlainTextEdit *debug_log_view;

    /** Persistent action buttons and status label. */
    QPushButton *apply_patch_btn;
    QPushButton *revert_patch_btn;
    QPushButton *copy_plan_btn;
    QPushButton *approve_action_btn = nullptr;
    QPushButton *deny_action_btn = nullptr;
    QLabel *status_label;
    QLabel *request_duration_label = nullptr;
    QLabel *usage_value_label;
    QPushButton *rename_conversation_btn = nullptr;
    QPushButton *delete_conversation_btn = nullptr;

    /** Full diff currently shown in the preview tab. */
    QString current_diff;

    /** Diff most recently applied so it can be reverted. */
    QString applied_diff;

    /** Completed log text already committed to the log view. */
    QString log_markdown;

    /** Committed markdown blocks shown as separate Actions Log rows. */
    QStringList log_markdown_blocks;

    /** Streaming log text buffered until the throttle timer fires. */
    QString streaming_markdown;

    /** Raw provider stream buffered so JSON envelopes can be decoded for display. */
    QString streaming_response_raw;

    /** Most recent streamed answer already committed into the actions log. */
    QString last_committed_streaming_markdown;

    /** True while the conversations combo is being repopulated programmatically. */
    bool conversation_list_refreshing = false;

    /** True while streaming tokens are being appended incrementally. */
    bool is_streaming = false;

    /** Row that should keep the selectable Actions Log editor open, or -1 when none is active. */
    int actions_log_active_row = -1;

    /** Provider identifier and model name for the current run's usage display. */
    QString usage_provider_id;
    QString usage_model_name;

    /** Accumulated usage and request count shown in the bottom usage row. */
    provider_usage_t displayed_usage;
    int displayed_provider_request_count = 0;

    /** Auto-compact: original user request queued to run after an auto-compact pass completes. */
    queued_request_t deferred_request;
    bool has_deferred_request = false;
    /** Set to true after auto-compact finishes to skip re-triggering on the follow-up run. */
    bool skip_auto_compact_once = false;

    /** FIFO queue of full requests waiting to run after the active one. */
    request_queue_t request_queue;

    /** Draft image attachments currently shown below Linked Files. */
    QList<image_attachment_t> image_attachments;

    /** Approval list items keyed by controller approval id. */
    QMap<int, QListWidgetItem *> approval_items;

    /** Pending `apply_patch` approval currently being reviewed via inline editor annotations. */
    int active_inline_approval_id = -1;

    /** Short timer that batches frequent log renders while streaming. */
    QTimer *render_throttle;

    /** Per-entry model used by the Actions Log list view. */
    actions_log_model_t *actions_log_model = nullptr;

    /** Delegate that renders one markdown-backed Actions Log row. */
    actions_log_delegate_t *actions_log_delegate = nullptr;

    /** One-second timer that refreshes the live request duration label. */
    QTimer *request_duration_timer = nullptr;

    /** Start timestamp for the currently running request. */
    QElapsedTimer request_elapsed_timer;

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
