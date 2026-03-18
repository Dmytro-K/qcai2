#include "ide_output_capture.h"

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildstep.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/target.h>
#include <projectexplorer/task.h>

#include <QCoreApplication>

namespace qcai2
{

namespace
{

QString displayNameForRunControl(const ProjectExplorer::RunControl *runControl)
{
    if (((runControl == nullptr) == true))
    {
        return QStringLiteral("Application");
    }

    const QString displayName = runControl->displayName().trimmed();
    return displayName.isEmpty() ? QStringLiteral("Application") : displayName;
}

captured_diagnostic_t diagnosticFromTask(const ProjectExplorer::Task &task)
{
    captured_diagnostic_t diagnostic;

    switch (task.type())
    {
        case ProjectExplorer::Task::Error:
        case ProjectExplorer::Task::DisruptingError:
            diagnostic.severity = diagnostic_severity_t::ERROR;
            break;
        case ProjectExplorer::Task::Warning:
            diagnostic.severity = diagnostic_severity_t::WARNING;
            break;
        case ProjectExplorer::Task::Unknown:
            diagnostic.severity = diagnostic_severity_t::UNKNOWN;
            break;
    }

    diagnostic.summary = task.summary().trimmed();
    if (diagnostic.summary.isEmpty())
    {
        diagnostic.summary = task.description().trimmed();
    }
    diagnostic.details = task.details().join(QStringLiteral(" | ")).trimmed();
    diagnostic.file_path = task.file().toUserOutput();
    diagnostic.line = task.line();
    diagnostic.column = task.column();
    diagnostic.origin = task.origin().trimmed();

    return diagnostic;
}

}  // namespace

ide_output_capture_t::ide_output_capture_t(QObject *parent)
    : QObject(parent), compile_buffer(120000), application_buffer(120000),
      run_control_scan_timer(this)
{
    this->run_control_scan_timer.setInterval(1000);
    connect(&this->run_control_scan_timer, &QTimer::timeout, this,
            &ide_output_capture_t::refresh_run_controls);
}

void ide_output_capture_t::initialize()
{
    if (auto *projectManager = ProjectExplorer::ProjectManager::instance();
        ((projectManager != nullptr) == true))
    {
        for (ProjectExplorer::Project *project : ProjectExplorer::ProjectManager::projects())
        {
            attach_project(project);
        }

        connect(projectManager, &ProjectExplorer::ProjectManager::projectAdded, this,
                &ide_output_capture_t::attach_project);
        connect(projectManager, &ProjectExplorer::ProjectManager::buildConfigurationAdded, this,
                &ide_output_capture_t::attach_build_configuration);
    }

    if (auto *buildManager = ProjectExplorer::BuildManager::instance();
        ((buildManager != nullptr) == true))
    {
        connect(buildManager, &ProjectExplorer::BuildManager::buildStateChanged, this,
                [this](ProjectExplorer::Project *project) {
                    if (((ProjectExplorer::BuildManager::isBuilding() &&
                          !this->build_in_progress) == true))
                    {
                        const QString projectName =
                            project ? project->displayName() : QStringLiteral("Project");
                        begin_compile_capture(
                            QStringLiteral("=== Build started: %1 ===").arg(projectName));
                    }
                });
        connect(buildManager, &ProjectExplorer::BuildManager::buildQueueFinished, this,
                [this](bool success) {
                    this->build_in_progress = false;
                    this->append_compile_banner(
                        QStringLiteral("=== Build finished: %1 ===")
                            .arg(success ? QStringLiteral("success") : QStringLiteral("failed")));
                });
    }

    this->refresh_run_controls();
    this->run_control_scan_timer.start();
}

QString ide_output_capture_t::compile_output_snapshot(int maxLines) const
{
    if (this->compile_buffer.is_empty() == true)
    {
        return QStringLiteral("No Compile Output captured yet.");
    }

    return this->compile_buffer.last_lines(maxLines);
}

QString ide_output_capture_t::application_output_snapshot(int maxLines) const
{
    if (this->application_buffer.is_empty() == true)
    {
        return QStringLiteral("No Application Output captured yet.");
    }

    return this->application_buffer.last_lines(maxLines);
}

QString ide_output_capture_t::diagnostics_snapshot(int maxItems) const
{
    QList<captured_diagnostic_t> diagnostics = this->compile_diagnostics;
    if (diagnostics.isEmpty())
    {
        const QString source = !this->external_build_output.isEmpty()
                                   ? this->external_build_output
                                   : this->compile_buffer.text();
        diagnostics = extract_diagnostics_from_text(source);
    }

    return format_captured_diagnostics(diagnostics, maxItems);
}

void ide_output_capture_t::ingest_external_build_output(const QString &output)
{
    this->begin_compile_capture(QStringLiteral("=== External build tool output ==="));
    this->compile_buffer.append_chunk(output, !output.endsWith(QLatin1Char('\n')));
    this->external_build_output = output;

    const QList<captured_diagnostic_t> diagnostics = extract_diagnostics_from_text(output);
    if (((!diagnostics.isEmpty()) == true))
    {
        this->compile_diagnostics = diagnostics;
    }
}

void ide_output_capture_t::attach_project(ProjectExplorer::Project *project)
{
    if ((((project == nullptr) || this->projects.contains(project)) == true))
    {
        return;
    }

    this->track_object(project, this->projects);
    for (ProjectExplorer::Target *target : project->targets())
    {
        this->attach_target(target);
    }

    connect(project, &ProjectExplorer::Project::addedTarget, this,
            &ide_output_capture_t::attach_target);
}

void ide_output_capture_t::attach_target(ProjectExplorer::Target *target)
{
    if ((((target == nullptr) || this->targets.contains(target)) == true))
    {
        return;
    }

    this->track_object(target, this->targets);
    for (ProjectExplorer::BuildConfiguration *buildConfiguration : target->buildConfigurations())
    {
        this->attach_build_configuration(buildConfiguration);
    }

    connect(target, &ProjectExplorer::Target::addedBuildConfiguration, this,
            &ide_output_capture_t::attach_build_configuration);
}

void ide_output_capture_t::attach_build_configuration(
    ProjectExplorer::BuildConfiguration *buildConfiguration)
{
    if ((((buildConfiguration == nullptr) ||
          this->build_configurations.contains(buildConfiguration)) == true))
    {
        return;
    }

    this->track_object(buildConfiguration, this->build_configurations);
    this->attach_build_step_list(buildConfiguration->buildSteps());
    this->attach_build_step_list(buildConfiguration->cleanSteps());
}

void ide_output_capture_t::attach_build_step_list(ProjectExplorer::BuildStepList *stepList)
{
    if ((((stepList == nullptr) || this->build_step_lists.contains(stepList)) == true))
    {
        return;
    }

    this->track_object(stepList, this->build_step_lists);
    for (ProjectExplorer::BuildStep *step : stepList->steps())
    {
        this->attach_build_step(step);
    }

    connect(stepList, &ProjectExplorer::BuildStepList::stepInserted, this,
            [this, stepList](int position) { this->attach_build_step(stepList->at(position)); });
}

void ide_output_capture_t::attach_build_step(ProjectExplorer::BuildStep *step)
{
    if ((((step == nullptr) || this->build_steps.contains(step)) == true))
    {
        return;
    }

    this->track_object(step, this->build_steps);
    connect(step, &ProjectExplorer::BuildStep::addOutput, this,
            [this](const QString &text, ProjectExplorer::BuildStep::OutputFormat format,
                   ProjectExplorer::BuildStep::OutputNewlineSetting newlineSetting) {
                Q_UNUSED(format)
                if (this->build_in_progress == false)
                {
                    this->begin_compile_capture(QStringLiteral("=== Build output captured ==="));
                }

                this->compile_buffer.append_chunk(
                    text, newlineSetting == ProjectExplorer::BuildStep::DoAppendNewline);
            });
    connect(step, &ProjectExplorer::BuildStep::addTask, this,
            [this](const ProjectExplorer::Task &task, int linkedOutputLines, int skipLines) {
                Q_UNUSED(linkedOutputLines)
                Q_UNUSED(skipLines)
                this->record_compile_task(task);
            });
}

void ide_output_capture_t::attach_run_control(ProjectExplorer::RunControl *runControl)
{
    if ((((runControl == nullptr) || this->run_controls.contains(runControl)) == true))
    {
        return;
    }

    this->track_object(runControl, this->run_controls);
    connect(runControl, &ProjectExplorer::RunControl::started, this, [this, runControl]() {
        this->application_buffer.clear();
        this->append_application_banner(
            QStringLiteral("=== Run started: %1 ===").arg(displayNameForRunControl(runControl)));
    });
    connect(runControl, &ProjectExplorer::RunControl::appendMessage, this,
            [this](const QString &message, Utils::OutputFormat format) {
                Q_UNUSED(format)
                this->application_buffer.append_chunk(message,
                                                      !message.endsWith(QLatin1Char('\n')));
            });
    connect(runControl, &ProjectExplorer::RunControl::stopped, this, [this, runControl]() {
        this->append_application_banner(
            QStringLiteral("=== Run stopped: %1 ===").arg(displayNameForRunControl(runControl)));
    });
}

void ide_output_capture_t::track_object(QObject *object, QSet<const QObject *> &trackedSet)
{
    trackedSet.insert(object);
    auto *trackedSetPtr = &trackedSet;
    connect(object, &QObject::destroyed, this,
            [object, trackedSetPtr]() { trackedSetPtr->remove(object); });
}

void ide_output_capture_t::refresh_run_controls()
{
    if (((!qApp) == true))
    {
        return;
    }

    const auto run_controls = qApp->findChildren<ProjectExplorer::RunControl *>();
    for (ProjectExplorer::RunControl *runControl : run_controls)
    {
        this->attach_run_control(runControl);
    }
}

void ide_output_capture_t::begin_compile_capture(const QString &banner)
{
    this->build_in_progress = true;
    this->compile_buffer.clear();
    this->compile_diagnostics.clear();
    this->external_build_output.clear();
    this->append_compile_banner(banner);
}

void ide_output_capture_t::append_compile_banner(const QString &banner)
{
    this->compile_buffer.append_chunk(banner, true);
}

void ide_output_capture_t::append_application_banner(const QString &banner)
{
    this->application_buffer.append_chunk(banner, true);
}

void ide_output_capture_t::record_compile_task(const ProjectExplorer::Task &task)
{
    if (((!task.isError() && !task.isWarning() &&
          task.type() != ProjectExplorer::Task::DisruptingError) == true))
    {
        return;
    }

    this->compile_diagnostics.append(diagnosticFromTask(task));
}

}  // namespace qcai2
