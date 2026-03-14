/*! Shared migration helpers for global settings and project-local state storage. */
#pragma once

#include <QJsonObject>
#include <QSettings>
#include <QString>

namespace qcai2::Migration
{

struct Revision
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    int build = 0;
    bool valid = false;

    QString versionString() const;
    QString buildSuffixString() const;
    QString revisionString() const;
};

Revision currentRevision();
QString currentVersionString();
QString currentBuildSuffix();
QString currentRevisionString();

Revision parseRevision(const QString &version, const QString &buildSuffix = {});
bool isOlder(const Revision &lhs, const Revision &rhs);

QString globalBackupDirPath();
QString globalStructuredSettingsFilePath();
QString projectBackupDirPath(const QString &storagePath);

void stampGlobalSettings(QSettings &settings);
bool migrateGlobalSettings(QSettings &settings, QString *error = nullptr);

void stampProjectState(QJsonObject &root);
bool migrateProjectState(const QString &storagePath, QString *error = nullptr);

QString projectGoalFilePath(const QString &storagePath);
QString projectActionsLogFilePath(const QString &storagePath);

}  // namespace qcai2::Migration
