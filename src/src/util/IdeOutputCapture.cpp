#include "IdeOutputCapture.h"

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
    if (!runControl)
        return QStringLiteral("Application");

    const QString displayName = runControl->displayName().trimmed();
    return displayName.isEmpty() ? QStringLiteral("Application") : displayName;
}

CapturedDiagnostic diagnosticFromTask(const ProjectExplorer::Task &task)
{
    CapturedDiagnostic diagnostic;

    switch (task.type())
    {
        case ProjectExplorer::Task::Error:
        case ProjectExplorer::Task::DisruptingError:
            diagnostic.severity = DiagnosticSeverity::Error;
            break;
        case ProjectExplorer::Task::Warning:
            diagnostic.severity = DiagnosticSeverity::Warning;
            break;
        case ProjectExplorer::Task::Unknown:
            diagnostic.severity = DiagnosticSeverity::Unknown;
            break;
    }

    diagnostic.summary = task.summary().trimmed();
    if (diagnostic.summary.isEmpty())
        diagnostic.summary = task.description().trimmed();
    diagnostic.details = task.details().join(QStringLiteral(" | ")).trimmed();
    diagnostic.filePath = task.file().toUserOutput();
    diagnostic.line = task.line();
    diagnostic.column = task.column();
    diagnostic.origin = task.origin().trimmed();

    return diagnostic;
}

}  // namespace

IdeOutputCapture::IdeOutputCapture(QObject *parent)
    : QObject(parent), m_compileBuffer(120000), m_applicationBuffer(120000),
      m_runControlScanTimer(this)
{
    m_runControlScanTimer.setInterval(1000);
    connect(&m_runControlScanTimer, &QTimer::timeout, this, &IdeOutputCapture::refreshRunControls);
}

void IdeOutputCapture::initialize()
{
    if (auto *projectManager = ProjectExplorer::ProjectManager::instance())
    {
        for (ProjectExplorer::Project *project : ProjectExplorer::ProjectManager::projects())
            attachProject(project);

        connect(projectManager, &ProjectExplorer::ProjectManager::projectAdded, this,
                &IdeOutputCapture::attachProject);
        connect(projectManager, &ProjectExplorer::ProjectManager::buildConfigurationAdded, this,
                &IdeOutputCapture::attachBuildConfiguration);
    }

    if (auto *buildManager = ProjectExplorer::BuildManager::instance())
    {
        connect(buildManager, &ProjectExplorer::BuildManager::buildStateChanged, this,
                [this](ProjectExplorer::Project *project) {
                    if (ProjectExplorer::BuildManager::isBuilding() && !m_buildInProgress)
                    {
                        const QString projectName = project ? project->displayName()
                                                            : QStringLiteral("Project");
                        beginCompileCapture(QStringLiteral("=== Build started: %1 ===")
                                                .arg(projectName));
                    }
                });
        connect(buildManager, &ProjectExplorer::BuildManager::buildQueueFinished, this,
                [this](bool success) {
                    m_buildInProgress = false;
                    appendCompileBanner(QStringLiteral("=== Build finished: %1 ===")
                                            .arg(success ? QStringLiteral("success")
                                                         : QStringLiteral("failed")));
                });
    }

    refreshRunControls();
    m_runControlScanTimer.start();
}

QString IdeOutputCapture::compileOutputSnapshot(int maxLines) const
{
    if (m_compileBuffer.isEmpty())
        return QStringLiteral("No Compile Output captured yet.");

    return m_compileBuffer.lastLines(maxLines);
}

QString IdeOutputCapture::applicationOutputSnapshot(int maxLines) const
{
    if (m_applicationBuffer.isEmpty())
        return QStringLiteral("No Application Output captured yet.");

    return m_applicationBuffer.lastLines(maxLines);
}

QString IdeOutputCapture::diagnosticsSnapshot(int maxItems) const
{
    QList<CapturedDiagnostic> diagnostics = m_compileDiagnostics;
    if (diagnostics.isEmpty())
    {
        const QString source = !m_externalBuildOutput.isEmpty() ? m_externalBuildOutput
                                                                : m_compileBuffer.text();
        diagnostics = extractDiagnosticsFromText(source);
    }

    return formatCapturedDiagnostics(diagnostics, maxItems);
}

void IdeOutputCapture::ingestExternalBuildOutput(const QString &output)
{
    beginCompileCapture(QStringLiteral("=== External build tool output ==="));
    m_compileBuffer.appendChunk(output, !output.endsWith(QLatin1Char('\n')));
    m_externalBuildOutput = output;

    const QList<CapturedDiagnostic> diagnostics = extractDiagnosticsFromText(output);
    if (!diagnostics.isEmpty())
        m_compileDiagnostics = diagnostics;
}

void IdeOutputCapture::attachProject(ProjectExplorer::Project *project)
{
    if (!project || m_projects.contains(project))
        return;

    trackObject(project, m_projects);
    for (ProjectExplorer::Target *target : project->targets())
        attachTarget(target);

    connect(project, &ProjectExplorer::Project::addedTarget, this, &IdeOutputCapture::attachTarget);
}

void IdeOutputCapture::attachTarget(ProjectExplorer::Target *target)
{
    if (!target || m_targets.contains(target))
        return;

    trackObject(target, m_targets);
    for (ProjectExplorer::BuildConfiguration *buildConfiguration : target->buildConfigurations())
        attachBuildConfiguration(buildConfiguration);

    connect(target, &ProjectExplorer::Target::addedBuildConfiguration, this,
            &IdeOutputCapture::attachBuildConfiguration);
}

void IdeOutputCapture::attachBuildConfiguration(
    ProjectExplorer::BuildConfiguration *buildConfiguration)
{
    if (!buildConfiguration || m_buildConfigurations.contains(buildConfiguration))
        return;

    trackObject(buildConfiguration, m_buildConfigurations);
    attachBuildStepList(buildConfiguration->buildSteps());
    attachBuildStepList(buildConfiguration->cleanSteps());
}

void IdeOutputCapture::attachBuildStepList(ProjectExplorer::BuildStepList *stepList)
{
    if (!stepList || m_buildStepLists.contains(stepList))
        return;

    trackObject(stepList, m_buildStepLists);
    for (ProjectExplorer::BuildStep *step : stepList->steps())
        attachBuildStep(step);

    connect(stepList, &ProjectExplorer::BuildStepList::stepInserted, this,
            [this, stepList](int position) { attachBuildStep(stepList->at(position)); });
}

void IdeOutputCapture::attachBuildStep(ProjectExplorer::BuildStep *step)
{
    if (!step || m_buildSteps.contains(step))
        return;

    trackObject(step, m_buildSteps);
    connect(step, &ProjectExplorer::BuildStep::addOutput, this,
            [this](const QString &text, ProjectExplorer::BuildStep::OutputFormat format,
                   ProjectExplorer::BuildStep::OutputNewlineSetting newlineSetting) {
                Q_UNUSED(format)
                if (!m_buildInProgress)
                    beginCompileCapture(QStringLiteral("=== Build output captured ==="));

                m_compileBuffer.appendChunk(text,
                                            newlineSetting
                                                == ProjectExplorer::BuildStep::DoAppendNewline);
            });
    connect(step, &ProjectExplorer::BuildStep::addTask, this,
            [this](const ProjectExplorer::Task &task, int linkedOutputLines, int skipLines) {
                Q_UNUSED(linkedOutputLines)
                Q_UNUSED(skipLines)
                recordCompileTask(task);
            });
}

void IdeOutputCapture::attachRunControl(ProjectExplorer::RunControl *runControl)
{
    if (!runControl || m_runControls.contains(runControl))
        return;

    trackObject(runControl, m_runControls);
    connect(runControl, &ProjectExplorer::RunControl::started, this, [this, runControl]() {
        m_applicationBuffer.clear();
        appendApplicationBanner(QStringLiteral("=== Run started: %1 ===")
                                    .arg(displayNameForRunControl(runControl)));
    });
    connect(runControl, &ProjectExplorer::RunControl::appendMessage, this,
            [this](const QString &message, Utils::OutputFormat format) {
                Q_UNUSED(format)
                m_applicationBuffer.appendChunk(message, !message.endsWith(QLatin1Char('\n')));
            });
    connect(runControl, &ProjectExplorer::RunControl::stopped, this, [this, runControl]() {
        appendApplicationBanner(QStringLiteral("=== Run stopped: %1 ===")
                                    .arg(displayNameForRunControl(runControl)));
    });
}

void IdeOutputCapture::trackObject(QObject *object, QSet<const QObject *> &trackedSet)
{
    trackedSet.insert(object);
    auto *trackedSetPtr = &trackedSet;
    connect(object, &QObject::destroyed, this,
            [object, trackedSetPtr]() { trackedSetPtr->remove(object); });
}

void IdeOutputCapture::refreshRunControls()
{
    if (!qApp)
        return;

    const auto runControls = qApp->findChildren<ProjectExplorer::RunControl *>();
    for (ProjectExplorer::RunControl *runControl : runControls)
        attachRunControl(runControl);
}

void IdeOutputCapture::beginCompileCapture(const QString &banner)
{
    m_buildInProgress = true;
    m_compileBuffer.clear();
    m_compileDiagnostics.clear();
    m_externalBuildOutput.clear();
    appendCompileBanner(banner);
}

void IdeOutputCapture::appendCompileBanner(const QString &banner)
{
    m_compileBuffer.appendChunk(banner, true);
}

void IdeOutputCapture::appendApplicationBanner(const QString &banner)
{
    m_applicationBuffer.appendChunk(banner, true);
}

void IdeOutputCapture::recordCompileTask(const ProjectExplorer::Task &task)
{
    if (!task.isError() && !task.isWarning() && task.type() != ProjectExplorer::Task::DisruptingError)
        return;

    m_compileDiagnostics.append(diagnosticFromTask(task));
}

}  // namespace qcai2
