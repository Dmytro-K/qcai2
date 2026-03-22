/*! @file
    @brief Declares a lightweight debugger status widget for the agent dock.
*/
#pragma once

#include "../debugger/debugger_session_service.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QWidget>

namespace qcai2
{

class debugger_status_widget_t final : public QWidget
{
    Q_OBJECT

public:
    explicit debugger_status_widget_t(i_debugger_session_service_t *debugger_service,
                                      QWidget *parent = nullptr);

    void refresh();

private:
    i_debugger_session_service_t *debugger_service = nullptr;
    QLabel *session_summary_label = nullptr;
    QLabel *frame_summary_label = nullptr;
    QLabel *breakpoint_summary_label = nullptr;
    QPlainTextEdit *output_view = nullptr;
};

}  // namespace qcai2
