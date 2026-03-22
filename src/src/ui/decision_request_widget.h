/*! Declares an interactive card widget for structured user-decision requests. */
#pragma once

#include "../models/agent_messages.h"

#include <QFrame>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QObject;
class QResizeEvent;
class QWidget;

namespace qcai2
{

/**
 * Renders one structured decision request with clickable options and custom-answer support.
 */
class decision_request_widget_t : public QFrame
{
    Q_OBJECT

public:
    /**
     * Creates one initially hidden decision-request card.
     * @param parent Owning widget.
     */
    explicit decision_request_widget_t(QWidget *parent = nullptr);

    /**
     * Displays one pending decision request and resets the widget to the interactive state.
     * @param request Structured request payload to render.
     */
    void set_decision_request(const agent_decision_request_t &request);

    /**
     * Marks the current request as answered and disables further interaction.
     * @param answer_summary Short user-visible summary of the submitted answer.
     */
    void mark_resolved(const QString &answer_summary);

    /**
     * Hides the card and clears any rendered request state.
     */
    void clear_request();

    /**
     * Reports whether the widget currently displays one request.
     * @return True when a request is currently rendered.
     */
    bool has_request() const;

    /**
     * Returns the currently rendered request id, if any.
     * @return Request identifier or an empty string.
     */
    QString current_request_id() const;

signals:
    /**
     * Emitted when the user picks one predefined option.
     * @param request_id Decision-request identifier.
     * @param option_id Chosen option identifier.
     */
    void option_submitted(const QString &request_id, const QString &option_id);

    /**
     * Emitted when the user submits a custom freeform answer.
     * @param request_id Decision-request identifier.
     * @param freeform_text Custom answer text.
     */
    void freeform_submitted(const QString &request_id, const QString &freeform_text);

protected:
    /**
     * Handles Enter/Return on the option list consistently across platforms.
     * @param watched Object that received the event.
     * @param event Qt event being filtered.
     * @return True when the event was handled.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * Recalculates the option-list height when the card width changes and text wrapping changes.
     * @param event Resize event from Qt.
     */
    void resizeEvent(QResizeEvent *event) override;

private:
    /**
     * Expands the option list to the height required to show all rendered options without
     * clipping.
     */
    void update_options_list_height();

    /**
     * Handles activation of one option item from mouse click or keyboard Enter.
     * @param item Activated list item.
     */
    void submit_option_item(QListWidgetItem *item);

    /**
     * Submits the current freeform line-edit contents when non-empty.
     */
    void submit_freeform();

    /** Current request shown in the card. */
    agent_decision_request_t current_request;

    /** True once one request is currently rendered. */
    bool has_active_request = false;

    /** Title label shown at the top of the card. */
    QLabel *title_label = nullptr;

    /** Optional longer description label. */
    QLabel *description_label = nullptr;

    /** Option list that supports arrow-key navigation and Enter activation. */
    QListWidget *options_list = nullptr;

    /** Container for the freeform answer row. */
    QWidget *freeform_row = nullptr;

    /** Line edit used for a custom answer. */
    QLineEdit *freeform_edit = nullptr;

    /** Mouse-clickable send button for the custom answer. */
    QPushButton *submit_freeform_button = nullptr;

    /** Label shown after the request was resolved. */
    QLabel *resolved_label = nullptr;
};

}  // namespace qcai2
