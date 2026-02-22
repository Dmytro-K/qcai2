#pragma once

#include "AgentController.h"

#include <QWidget>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QTimer>

namespace Qcai2 {

// The main dock widget UI for the AI Agent.
class AgentDockWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AgentDockWidget(AgentController *controller, QWidget *parent = nullptr);
    ~AgentDockWidget() override;

private slots:
    void onRunClicked();
    void onStopClicked();
    void onApplyPatchClicked();
    void onRevertPatchClicked();
    void onCopyPlanClicked();

    void onLogMessage(const QString &msg);
    void onPlanUpdated(const QList<PlanStep> &steps);
    void onDiffAvailable(const QString &diff);
    void onApprovalRequested(int id, const QString &action, const QString &reason, const QString &preview);
    void onIterationChanged(int iteration);
    void onStopped(const QString &summary);

private:
    void setupUi();
    void updateRunState(bool running);
    bool eventFilter(QObject *obj, QEvent *event) override;
    void renderLog();
    void saveChat();
    void restoreChat();

    AgentController *m_controller;

    // Input
    QTextEdit    *m_goalEdit;
    QComboBox    *m_modelCombo;
    QComboBox    *m_thinkingCombo;
    QPushButton  *m_runBtn;
    QPushButton  *m_stopBtn;
    QCheckBox    *m_dryRunCheck;

    // Views (in tabs)
    QTabWidget       *m_tabs;
    QListWidget      *m_planList;      // "Plan" tab
    QTextEdit        *m_logView;       // "Actions log" tab
    QPlainTextEdit   *m_diffView;      // "Diff preview" tab
    QListWidget      *m_approvalList;  // "Approvals" tab
    QPlainTextEdit   *m_debugLogView;  // "Debug Log" tab

    // Bottom bar
    QPushButton *m_applyPatchBtn;
    QPushButton *m_revertPatchBtn;
    QPushButton *m_copyPlanBtn;
    QLabel      *m_statusLabel;

    // State
    QString m_currentDiff;
    QString m_logMarkdown;
    QString m_streamingMarkdown;
    QMap<int, QListWidgetItem *> m_approvalItems;
    QTimer *m_renderThrottle;
};

} // namespace Qcai2
