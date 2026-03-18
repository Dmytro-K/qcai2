#pragma once

#include "output_capture_support.h"

#include <QSet>
#include <QTimer>

namespace ProjectExplorer
{
class BuildConfiguration;
class BuildStep;
class BuildStepList;
class Project;
class RunControl;
class Target;
class Task;
}  // namespace ProjectExplorer

namespace qcai2
{

class ide_output_capture_t final : public QObject
{
    Q_OBJECT

public:
    explicit ide_output_capture_t(QObject *parent = nullptr);

    void initialize();

    QString compile_output_snapshot(int max_lines = 200) const;
    QString application_output_snapshot(int max_lines = 200) const;
    QString diagnostics_snapshot(int max_items = 50) const;

    void ingest_external_build_output(const QString &output);

private:
    void attach_project(ProjectExplorer::Project *project);
    void attach_target(ProjectExplorer::Target *target);
    void attach_build_configuration(ProjectExplorer::BuildConfiguration *build_configuration);
    void attach_build_step_list(ProjectExplorer::BuildStepList *step_list);
    void attach_build_step(ProjectExplorer::BuildStep *step);
    void attach_run_control(ProjectExplorer::RunControl *run_control);
    void track_object(QObject *object, QSet<const QObject *> &tracked_set);
    void refresh_run_controls();

    void begin_compile_capture(const QString &banner);
    void append_compile_banner(const QString &banner);
    void append_application_banner(const QString &banner);
    void record_compile_task(const ProjectExplorer::Task &task);

    QSet<const QObject *> projects;
    QSet<const QObject *> targets;
    QSet<const QObject *> build_configurations;
    QSet<const QObject *> build_step_lists;
    QSet<const QObject *> build_steps;
    QSet<const QObject *> run_controls;

    bounded_text_buffer_t compile_buffer;
    bounded_text_buffer_t application_buffer;
    QList<captured_diagnostic_t> compile_diagnostics;
    QString external_build_output;
    QTimer run_control_scan_timer;
    bool build_in_progress = false;
};

}  // namespace qcai2
