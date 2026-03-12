#pragma once

#include "OutputCaptureSupport.h"

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
}

namespace qcai2
{

class IdeOutputCapture final : public QObject
{
    Q_OBJECT

public:
    explicit IdeOutputCapture(QObject *parent = nullptr);

    void initialize();

    QString compileOutputSnapshot(int maxLines = 200) const;
    QString applicationOutputSnapshot(int maxLines = 200) const;
    QString diagnosticsSnapshot(int maxItems = 50) const;

    void ingestExternalBuildOutput(const QString &output);

private:
    void attachProject(ProjectExplorer::Project *project);
    void attachTarget(ProjectExplorer::Target *target);
    void attachBuildConfiguration(ProjectExplorer::BuildConfiguration *buildConfiguration);
    void attachBuildStepList(ProjectExplorer::BuildStepList *stepList);
    void attachBuildStep(ProjectExplorer::BuildStep *step);
    void attachRunControl(ProjectExplorer::RunControl *runControl);
    void trackObject(QObject *object, QSet<const QObject *> &trackedSet);
    void refreshRunControls();

    void beginCompileCapture(const QString &banner);
    void appendCompileBanner(const QString &banner);
    void appendApplicationBanner(const QString &banner);
    void recordCompileTask(const ProjectExplorer::Task &task);

    QSet<const QObject *> m_projects;
    QSet<const QObject *> m_targets;
    QSet<const QObject *> m_buildConfigurations;
    QSet<const QObject *> m_buildStepLists;
    QSet<const QObject *> m_buildSteps;
    QSet<const QObject *> m_runControls;

    BoundedTextBuffer m_compileBuffer;
    BoundedTextBuffer m_applicationBuffer;
    QList<CapturedDiagnostic> m_compileDiagnostics;
    QString m_externalBuildOutput;
    QTimer m_runControlScanTimer;
    bool m_buildInProgress = false;
};

}  // namespace qcai2
