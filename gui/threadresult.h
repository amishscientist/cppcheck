/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2021 Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef THREADRESULT_H
#define THREADRESULT_H

#include "color.h"
#include "errorlogger.h"
#include "importproject.h"

#include <QMutex>
#include <QObject>
#include <QStringList>

class ErrorItem;

/// @addtogroup GUI
/// @{

/**
 * @brief Threads use this class to obtain new files to process and to publish results
 *
 */
class ThreadResult : public QObject, public ErrorLogger {
    Q_OBJECT
public:
    ThreadResult();
    virtual ~ThreadResult();

    /**
     * @brief Get next unprocessed file
     * @return File path
     */
    QString getNextFile();

    ImportProject::FileSettings getNextFileSettings();

    /**
     * @brief Set list of files to check
     * @param files List of files to check
     */
    void setFiles(const QStringList &files);

    void setProject(const ImportProject &prj);

    /**
     * @brief Clear files to check
     *
     */
    void clearFiles();

    /**
     * @brief Get the number of files to check
     *
     */
    int getFileCount() const;

    /**
     * ErrorLogger methods
     */
    void reportOut(const std::string &outmsg, Color c = Color::Reset) override;
    void reportErr(const ErrorMessage &msg) override;
    void bughuntingReport(const std::string &str) override;

public slots:

    /**
     * @brief Slot threads use to signal this class that a specific file is checked
     * @param file File that is checked
     */
    void fileChecked(const QString &file);
signals:
    /**
     * @brief Progress signal
     * @param value Current progress
     * @param description Description of the current stage
     */
    void progress(int value, const QString& description);

    /**
     * @brief Signal of a new error
     *
     * @param item Error data
     */
    void error(const ErrorItem &item);

    /**
     * @brief Signal of a new log message
     *
     * @param logline Log line
     */
    void log(const QString &logline);

    /**
     * @brief Signal of a debug error
     *
     * @param item Error data
     */
    void debugError(const ErrorItem &item);

    /** @brief bug hunting report */
    void bughuntingReportLine(QString line);

protected:

    /**
     * @brief Mutex
     *
     */
    mutable QMutex mutex;

    /**
     * @brief List of files to check
     *
     */
    QStringList mFiles;

    std::list<ImportProject::FileSettings> mFileSettings;

    /**
     * @brief Max progress
     *
     */
    quint64 mMaxProgress;

    /**
     * @brief Current progress
     *
     */
    quint64 mProgress;

    /**
     * @brief Current number of files checked
     *
     */
    unsigned long mFilesChecked;

    /**
     * @brief Total number of files
     *
     */
    unsigned long mTotalFiles;
};
/// @}
#endif // THREADRESULT_H
