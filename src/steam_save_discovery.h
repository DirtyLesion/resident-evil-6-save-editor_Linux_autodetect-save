#pragma once

#include <QString>
#include <QStringList>

// Finds readable Resident Evil 6 PC saves in common Steam install roots.
// Results are ordered by modification time (newest first), so the Open dialog
// can preselect a likely current save while still letting the user choose.
// Parameters are injectable for deterministic tests.
QStringList FindResidentEvil6SaveCandidates(const QString& homePath,
                                           const QString& xdgDataHome = QString(),
                                           const QString& steamDir = QString());
