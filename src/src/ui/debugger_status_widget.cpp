/*! @file
    @brief Implements a lightweight debugger status widget for the agent dock.
*/

#include "debugger_status_widget.h"

#include <QVBoxLayout>

namespace qcai2
{

debugger_status_widget_t::debugger_status_widget_t(i_debugger_session_service_t *debugger_service,
                                                   QWidget *parent)
    : QWidget(parent), debugger_service(debugger_service)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    this->session_summary_label = new QLabel(this);
    this->session_summary_label->setObjectName(QStringLiteral("session_summary_label"));
    this->session_summary_label->setWordWrap(true);
    layout->addWidget(this->session_summary_label);

    this->frame_summary_label = new QLabel(this);
    this->frame_summary_label->setObjectName(QStringLiteral("frame_summary_label"));
    this->frame_summary_label->setWordWrap(true);
    layout->addWidget(this->frame_summary_label);

    this->breakpoint_summary_label = new QLabel(this);
    this->breakpoint_summary_label->setObjectName(QStringLiteral("breakpoint_summary_label"));
    this->breakpoint_summary_label->setWordWrap(true);
    layout->addWidget(this->breakpoint_summary_label);

    this->output_view = new QPlainTextEdit(this);
    this->output_view->setObjectName(QStringLiteral("debugger_output_view"));
    this->output_view->setReadOnly(true);
    this->output_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    this->output_view->setMinimumHeight(this->fontMetrics().height() * 12);
    layout->addWidget(this->output_view, 1);

    if (this->debugger_service != nullptr)
    {
        connect(this->debugger_service, &i_debugger_session_service_t::snapshot_changed, this,
                &debugger_status_widget_t::refresh);
        connect(this->debugger_service, &i_debugger_session_service_t::debugger_output_changed,
                this, &debugger_status_widget_t::refresh);
        connect(this->debugger_service, &i_debugger_session_service_t::managed_breakpoints_changed,
                this, &debugger_status_widget_t::refresh);
    }

    this->refresh();
}

void debugger_status_widget_t::refresh()
{
    if (this->debugger_service == nullptr)
    {
        this->session_summary_label->setText(tr("Debugger integration is not available."));
        this->frame_summary_label->clear();
        this->breakpoint_summary_label->clear();
        this->output_view->clear();
        return;
    }

    const debugger_session_snapshot_t snapshot = this->debugger_service->session_snapshot();
    if (snapshot.active == false)
    {
        this->session_summary_label->setText(tr("No active debugger session."));
        this->frame_summary_label->setText(
            tr("Start a Qt Creator debug session to inspect stack, locals, and output here."));
        this->breakpoint_summary_label->setText(
            tr("Managed breakpoints: %1")
                .arg(this->debugger_service->managed_breakpoints().size()));
        this->output_view->setPlainText(this->debugger_service->debugger_output_snapshot(200));
        return;
    }

    this->session_summary_label->setText(
        tr("%1 — %2 (%3)")
            .arg(snapshot.engine_display_name.isEmpty() ? tr("Debugger")
                                                        : snapshot.engine_display_name,
                 snapshot.state_name, snapshot.paused ? tr("paused") : tr("running")));
    this->frame_summary_label->setText(
        tr("Location: %1:%2 — %3 | Frames: %4 | Threads: %5 | Variables: %6")
            .arg(snapshot.location.file_path.isEmpty() ? tr("<unknown>")
                                                       : snapshot.location.file_path)
            .arg(snapshot.location.line)
            .arg(snapshot.location.function_name.isEmpty() ? tr("<unknown>")
                                                           : snapshot.location.function_name)
            .arg(snapshot.stack_frame_count)
            .arg(snapshot.thread_count)
            .arg(snapshot.variable_count));
    this->breakpoint_summary_label->setText(
        tr("Breakpoints: %1 total | %2 managed")
            .arg(snapshot.breakpoint_count)
            .arg(this->debugger_service->managed_breakpoints().size()));
    this->output_view->setPlainText(this->debugger_service->debugger_output_snapshot(200));
}

}  // namespace qcai2
