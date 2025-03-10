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


#ifndef RESULTSVIEW_H
#define RESULTSVIEW_H

#include "ui_resultsview.h"

#include "report.h"
#include "showtypes.h"

class ErrorItem;
class ApplicationList;
class QModelIndex;
class QPrinter;
class QSettings;
class CheckStatistics;

/// @addtogroup GUI
/// @{

/**
 * @brief Widget to show cppcheck progressbar and result
 *
 */
class ResultsView : public QWidget {
    Q_OBJECT
public:

    explicit ResultsView(QWidget * parent = nullptr);
    void initialize(QSettings *settings, ApplicationList *list, ThreadHandler *checkThreadHandler);
    ResultsView(const ResultsView &) = delete;
    virtual ~ResultsView();
    ResultsView &operator=(const ResultsView &) = delete;

    void setAddedFunctionContracts(const QStringList &addedContracts);
    void setAddedVariableContracts(const QStringList &added);

    /**
     * @brief Clear results and statistics and reset progressinfo.
     * @param results Remove all the results from view?
     */
    void clear(bool results);

    /**
     * @brief Remove a file from the results.
     */
    void clear(const QString &filename);

    /**
     * @brief Remove a recheck file from the results.
     */
    void clearRecheckFile(const QString &filename);

    /** Clear the contracts */
    void clearContracts();

    /**
     * @brief Write statistics in file
     *
     * @param filename Filename to save statistics to
     */
    void saveStatistics(const QString &filename) const;

    /**
     * @brief Save results to a file
     *
     * @param filename Filename to save results to
     * @param type Type of the report.
     */
    void save(const QString &filename, Report::Type type) const;

    /**
     * @brief Update results from old report (tag, sinceDate)
     */
    void updateFromOldReport(const QString &filename) const;

    /**
     * @brief Update tree settings
     *
     * @param showFullPath Show full path of files in the tree
     * @param saveFullPath Save full path of files in reports
     * @param saveAllErrors Save all visible errors
     * @param showNoErrorsMessage Show "no errors"?
     * @param showErrorId Show error id?
     * @param showInconclusive Show inconclusive?
     */
    void updateSettings(bool showFullPath,
                        bool saveFullPath,
                        bool saveAllErrors,
                        bool showNoErrorsMessage,
                        bool showErrorId,
                        bool showInconclusive);

    /**
     * @brief Update Code Editor Style
     *
     * Function will read updated Code Editor styling from
     * stored program settings.
     *
     * @param settings Pointer to QSettings Object
     */
    void updateStyleSetting(QSettings *settings);

    /**
     * @brief Set the directory we are checking
     *
     * This is used to split error file path to relative if necessary
     * @param dir Directory we are checking
     */
    void setCheckDirectory(const QString &dir);

    /**
     * @brief Get the directory we are checking
     *
     * @return Directory containing source files
     */

    QString getCheckDirectory();

    /**
     * @brief Inform the view that checking has started
     *
     * @param count Count of files to be checked.
     */
    void checkingStarted(int count);

    /**
     * @brief Inform the view that checking finished.
     *
     */
    void checkingFinished();

    /**
     * @brief Do we have visible results to show?
     *
     * @return true if there is at least one warning/error to show.
     */
    bool hasVisibleResults() const;

    /**
     * @brief Do we have results from check?
     *
     * @return true if there is at least one warning/error, hidden or visible.
     */
    bool hasResults() const;

    /**
     * @brief Save View's settings
     *
     * @param settings program settings.
     */
    void saveSettings(QSettings *settings);

    /**
     * @brief Translate this view
     *
     */
    void translate();

    void disableProgressbar();

    /**
     * @brief Read errors from report XML file.
     * @param filename Report file to read.
     *
     */
    void readErrorsXml(const QString &filename);

    /**
     * @brief Return checking statistics.
     * @return Pointer to checking statistics.
     */
    CheckStatistics *getStatistics() const {
        return mStatistics;
    }

    /**
     * @brief Return Showtypes.
     * @return Pointer to Showtypes.
     */
    ShowTypes * getShowTypes() const {
        return &mUI.mTree->mShowSeverities;
    }

    /** Show/hide the contract tabs */
    void showContracts(bool visible);

signals:

    /**
     * @brief Signal to be emitted when we have results
     *
     */
    void gotResults();

    /**
     * @brief Signal that results have been hidden or shown
     *
     * @param hidden true if there are some hidden results, or false if there are not
     */
    void resultsHidden(bool hidden);

    /**
     * @brief Signal to perform recheck of selected files
     *
     * @param selectedFilesList list of selected files
     */
    void checkSelected(QStringList selectedFilesList);

    /** Suppress Ids */
    void suppressIds(QStringList ids);

    /** Edit contract for function */
    void editFunctionContract(QString function);

    /** Delete contract for function */
    void deleteFunctionContract(QString function);

    /** Edit contract for variable */
    void editVariableContract(QString var);

    /** Delete variable contract */
    void deleteVariableContract(QString var);

    /**
     * @brief Show/hide certain type of errors
     * Refreshes the tree.
     *
     * @param type Type of error to show/hide
     * @param show Should specified errors be shown (true) or hidden (false)
     */
    void showResults(ShowTypes::ShowType type, bool show);

    /**
     * @brief Show/hide cppcheck errors.
     * Refreshes the tree.
     *
     * @param show Should specified errors be shown (true) or hidden (false)
     */
    void showCppcheckResults(bool show);

    /**
     * @brief Show/hide clang-tidy/clang-analyzer errors.
     * Refreshes the tree.
     *
     * @param show Should specified errors be shown (true) or hidden (false)
     */
    void showClangResults(bool show);

    /**
     * @brief Collapse all results in the result list.
     */
    void collapseAllResults();

    /**
     * @brief Expand all results in the result list.
     */
    void expandAllResults();

    /**
     * @brief Show hidden results in the result list.
     */
    void showHiddenResults();

public slots:

    /**
     * @brief Slot for updating the checking progress
     *
     * @param value Current progress value
     * @param description Description to accompany the progress
     */
    void progress(int value, const QString& description);

    /**
     * @brief Slot for new error to be displayed
     *
     * @param item Error data
     */
    void error(const ErrorItem &item);

    /**
     * @brief Filters the results in the result list.
     */
    void filterResults(const QString& filter);

    /**
     * @brief Update detailed message when selected item is changed.
     *
     * @param index Position of new selected item.
     */
    void updateDetails(const QModelIndex &index);

    /**
     * @brief Slot opening a print dialog to print the current report
     */
    void print();

    /**
     * @brief Slot printing the current report to the printer.
     * @param printer The printer used for printing the report.
     */
    void print(QPrinter* printer);

    /**
     * @brief Slot opening a print preview dialog
     */
    void printPreview();

    /**
     * \brief Log message
     */
    void log(const QString &str);

    /**
     * \brief debug message
     */
    void debugError(const ErrorItem &item);

    /**
     * \brief bughunting report line
     */
    void bughuntingReportLine(const QString& line);

    /**
     * \brief Clear log messages
     */
    void logClear();

    /**
     * \brief Copy selected log message entry
     */
    void logCopyEntry();

    /**
     * \brief Copy all log messages
     */
    void logCopyComplete();

    /** \brief Contract was double clicked => edit it */
    void contractDoubleClicked(QListWidgetItem* item);

    /** \brief Variable was double clicked => edit it */
    void variableDoubleClicked(QListWidgetItem* item);

    void editVariablesFilter(const QString &text);

protected:
    /**
     * @brief Should we show a "No errors found dialog" every time no errors were found?
     */
    bool mShowNoErrorsMessage;

    Ui::ResultsView mUI;

    CheckStatistics *mStatistics;

    bool eventFilter(QObject *target, QEvent *event);
private slots:
    /**
     * @brief Custom context menu for Analysis Log
     * @param pos Mouse click position
     */
    void on_mListLog_customContextMenuRequested(const QPoint &pos);
private:
    QSet<QString> mFunctionContracts;
    QSet<QString> mVariableContracts;

    /** Current file shown in the code editor */
    QString mCurrentFileName;
};
/// @}
#endif // RESULTSVIEW_H
