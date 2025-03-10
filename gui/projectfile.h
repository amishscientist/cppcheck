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

#ifndef PROJECT_FILE_H
#define PROJECT_FILE_H

#include "settings.h"
#include "suppressions.h"

#include <map>

#include <QObject>
#include <QString>
#include <QStringList>

class QXmlStreamReader;
class QXmlStreamWriter;

/// @addtogroup GUI
/// @{


/**
 * @brief A class that reads and writes project files.
 * The project files contain project-specific settings for checking. For
 * example a list of include paths.
 */
class ProjectFile : public QObject {
    Q_OBJECT

public:
    explicit ProjectFile(QObject *parent = nullptr);
    explicit ProjectFile(const QString &filename, QObject *parent = nullptr);
    ~ProjectFile() {
        if (this == mActiveProject) mActiveProject = nullptr;
    }

    static ProjectFile* getActiveProject() {
        return mActiveProject;
    }
    void setActiveProject() {
        mActiveProject = this;
    }

    /**
     * @brief Read the project file.
     * @param filename Filename (can be also given to constructor).
     */
    bool read(const QString &filename = QString());

    /**
     * @brief Get project root path.
     * @return project root path.
     */
    QString getRootPath() const {
        return mRootPath;
    }

    QString getBuildDir() const {
        return mBuildDir;
    }

    QString getImportProject() const {
        return mImportProject;
    }

    bool getAnalyzeAllVsConfigs() const {
        return mAnalyzeAllVsConfigs;
    }

    bool getCheckHeaders() const {
        return mCheckHeaders;
    }

    void setCheckHeaders(bool b) {
        mCheckHeaders = b;
    }

    bool getCheckUnusedTemplates() const {
        return mCheckUnusedTemplates;
    }

    void setCheckUnusedTemplates(bool b) {
        mCheckUnusedTemplates = b;
    }

    /**
     * @brief Get list of include directories.
     * @return list of directories.
     */
    QStringList getIncludeDirs() const {
        return ProjectFile::fromNativeSeparators(mIncludeDirs);
    }

    /**
     * @brief Get list of defines.
     * @return list of defines.
     */
    QStringList getDefines() const {
        return mDefines;
    }

    /**
     * @brief Get list of undefines.
     * @return list of undefines.
     */
    QStringList getUndefines() const {
        return mUndefines;
    }

    /**
     * @brief Get list of paths to check.
     * @return list of paths.
     */
    QStringList getCheckPaths() const {
        return ProjectFile::fromNativeSeparators(mPaths);
    }

    /**
     * @brief Get list of paths to exclude from the check.
     * @return list of paths.
     */
    QStringList getExcludedPaths() const {
        return ProjectFile::fromNativeSeparators(mExcludedPaths);
    }

    /**
     * @brief Get list of paths to exclude from the check.
     * @return list of paths.
     */
    QStringList getVsConfigurations() const {
        return mVsConfigurations;
    }

    /**
     * @brief Get list libraries.
     * @return list of libraries.
     */
    QStringList getLibraries() const {
        return mLibraries;
    }

    /**
     * @brief Get platform.
     * @return Current platform. If it ends with .xml then it is a file. Otherwise it must match one of the return values from @sa cppcheck::Platform::platformString() ("win32A", "unix32", ..)
     */
    QString getPlatform() const {
        return mPlatform;
    }

    /**
     * @brief Get "raw" suppressions.
     * @return list of suppressions.
     */
    QList<Suppressions::Suppression> getSuppressions() const {
        return mSuppressions;
    }

    /**
     * @brief Get list addons.
     * @return list of addons.
     */
    QStringList getAddons() const {
        return mAddons;
    }

    /**
     * @brief Get path to addon python script
     * @param filesDir Data files folder set by --data-dir
     * @param addon addon i.e. "misra" to lookup
     */
    static QString getAddonFilePath(QString filesDir, const QString &addon);

    /**
     * @brief Get list of addons and tools.
     * @return list of addons and tools.
     */
    QStringList getAddonsAndTools() const;

    bool getClangAnalyzer() const {
        return false; //mClangAnalyzer;
    }

    void setClangAnalyzer(bool c) {
        mClangAnalyzer = c;
    }

    bool getClangTidy() const {
        return mClangTidy;
    }

    void setClangTidy(bool c) {
        mClangTidy = c;
    }

    QStringList getTags() const {
        return mTags;
    }

    int getMaxCtuDepth() const {
        return mMaxCtuDepth;
    }

    void setMaxCtuDepth(int maxCtuDepth) {
        mMaxCtuDepth = maxCtuDepth;
    }

    int getMaxTemplateRecursion() const {
        return mMaxTemplateRecursion;
    }

    void setMaxTemplateRecursion(int maxTemplateRecursion) {
        mMaxTemplateRecursion = maxTemplateRecursion;
    }

    const std::map<std::string,std::string>& getFunctionContracts() const {
        return mFunctionContracts;
    }

    const std::map<QString, Settings::VariableContracts>& getVariableContracts() const {
        return mVariableContracts;
    }

    void setVariableContracts(QString var, const QString& min, const QString& max) {
        mVariableContracts[var] = Settings::VariableContracts{min.toStdString(), max.toStdString()};
    }

    void deleteFunctionContract(const QString& function) {
        mFunctionContracts.erase(function.toStdString());
    }

    void deleteVariableContract(const QString& var) {
        mVariableContracts.erase(var);
    }

    /**
     * @brief Get filename for the project file.
     * @return file name.
     */
    QString getFilename() const {
        return mFilename;
    }

    /**
     * @brief Set project root path.
     * @param rootpath new project root path.
     */
    void setRootPath(const QString &rootpath) {
        mRootPath = rootpath;
    }

    void setBuildDir(const QString &buildDir) {
        mBuildDir = buildDir;
    }

    void setImportProject(const QString &importProject) {
        mImportProject = importProject;
    }

    void setAnalyzeAllVsConfigs(bool b) {
        mAnalyzeAllVsConfigs = b;
    }

    /**
     * @brief Set list of includes.
     * @param includes List of defines.
     */
    void setIncludes(const QStringList &includes);

    /**
     * @brief Set list of defines.
     * @param defines List of defines.
     */
    void setDefines(const QStringList &defines);

    /**
     * @brief Set list of undefines.
     * @param undefines List of undefines.
     */
    void setUndefines(const QStringList &undefines);

    /**
     * @brief Set list of paths to check.
     * @param paths List of paths.
     */
    void setCheckPaths(const QStringList &paths);

    /**
     * @brief Set list of paths to exclude from the check.
     * @param paths List of paths.
     */
    void setExcludedPaths(const QStringList &paths);

    /**
     * @brief Set list of libraries.
     * @param libraries List of libraries.
     */
    void setLibraries(const QStringList &libraries);

    /** Set contract for a function */
    void setFunctionContract(const QString& function, const QString& expects);

    /**
     * @brief Set platform.
     * @param platform platform.
     */
    void setPlatform(const QString &platform);

    /**
     * @brief Set list of suppressions.
     * @param suppressions List of suppressions.
     */
    void setSuppressions(const QList<Suppressions::Suppression> &suppressions);

    /** Add suppression */
    void addSuppression(const Suppressions::Suppression &suppression);

    /**
     * @brief Set list of addons.
     * @param addons List of addons.
     */
    void setAddons(const QStringList &addons);

    /** @brief Set list of Visual Studio configurations to be checked
     *  @param vsConfigs List of configurations
     */
    void setVSConfigurations(const QStringList &vsConfigs);

    /**
     * @brief Set tags.
     * @param tags tag list
     */
    void setTags(const QStringList &tags) {
        mTags = tags;
    }

    /** Set tags for a warning */
    void setWarningTags(std::size_t hash, const QString& tags);

    /** Get tags for a warning */
    QString getWarningTags(std::size_t hash) const;

    /**
     * @brief Write project file (to disk).
     * @param filename Filename to use.
     */
    bool write(const QString &filename = QString());

    /**
     * @brief Set filename for the project file.
     * @param filename Filename to use.
     */
    void setFilename(const QString &filename) {
        mFilename = filename;
    }

    /** Do not only check how interface is used. Also check that interface is safe. */
    class SafeChecks : public Settings::SafeChecks {
    public:
        SafeChecks() : Settings::SafeChecks() {}

        void loadFromXml(QXmlStreamReader &xmlReader);
        void saveToXml(QXmlStreamWriter &xmlWriter) const;
    };

    SafeChecks safeChecks;

    /** Check unknown function return values */
    QStringList getCheckUnknownFunctionReturn() const {
        return mCheckUnknownFunctionReturn;
    }
    void setCheckUnknownFunctionReturn(const QStringList &s) {
        mCheckUnknownFunctionReturn = s;
    }

    /** Use Clang parser */
    bool clangParser;

    /** Bug hunting */
    bool bugHunting;
protected:

    /**
     * @brief Read optional root path from XML.
     * @param reader XML stream reader.
     */
    void readRootPath(QXmlStreamReader &reader);

    void readBuildDir(QXmlStreamReader &reader);

    /**
     * @brief Read importproject from XML.
     * @param reader XML stream reader.
     */
    void readImportProject(QXmlStreamReader &reader);

    bool readBool(QXmlStreamReader &reader);

    int readInt(QXmlStreamReader &reader, int defaultValue);

    /**
     * @brief Read list of include directories from XML.
     * @param reader XML stream reader.
     */
    void readIncludeDirs(QXmlStreamReader &reader);

    /**
     * @brief Read list of defines from XML.
     * @param reader XML stream reader.
     */
    void readDefines(QXmlStreamReader &reader);

    /**
     * @brief Read list paths to check.
     * @param reader XML stream reader.
     */
    void readCheckPaths(QXmlStreamReader &reader);

    /**
     * @brief Read lists of excluded paths.
     * @param reader XML stream reader.
     */
    void readExcludes(QXmlStreamReader &reader);

    /**
     * @brief Read function contracts.
     * @param reader XML stream reader.
     */
    void readFunctionContracts(QXmlStreamReader &reader);

    /**
     * @brief Read variable constraints.
     * @param reader XML stream reader.
     */
    void readVariableContracts(QXmlStreamReader &reader);

    /**
     * @brief Read lists of Visual Studio configurations
     * @param reader XML stream reader.
     */
    void readVsConfigurations(QXmlStreamReader &reader);

    /**
     * @brief Read platform text.
     * @param reader XML stream reader.
     */
    void readPlatform(QXmlStreamReader &reader);

    /**
     * @brief Read suppressions.
     * @param reader XML stream reader.
     */
    void readSuppressions(QXmlStreamReader &reader);

    /**
     * @brief Read tag warnings, what warnings are tagged with a specific tag
     * @param reader XML stream reader.
     */
    void readTagWarnings(QXmlStreamReader &reader, const QString &tag);

    /**
     * @brief Read string list
     * @param stringlist   destination string list
     * @param reader       XML stream reader
     * @param elementname  elementname for each string
     */
    void readStringList(QStringList &stringlist, QXmlStreamReader &reader, const char elementname[]);

    /**
     * @brief Write string list
     * @param xmlWriter xml writer
     * @param stringlist string list to write
     * @param startelementname name of start element
     * @param stringelementname name of each string element
     */
    static void writeStringList(QXmlStreamWriter &xmlWriter, const QStringList &stringlist, const char startelementname[], const char stringelementname[]);

private:

    void clear();

    /**
     * @brief Convert paths
     */
    static QStringList fromNativeSeparators(const QStringList &paths);

    /**
     * @brief Filename (+path) of the project file.
     */
    QString mFilename;

    /**
     * @brief Root path (optional) for the project.
     * This is the project root path. If it is present then all relative paths in
     * the project file are relative to this path. Otherwise paths are relative
     * to project file's path.
     */
    QString mRootPath;

    /** Cppcheck build dir */
    QString mBuildDir;

    /** Visual studio project/solution , compile database */
    QString mImportProject;

    /**
     * Should all visual studio configurations be analyzed?
     * If this is false then only the Debug configuration
     * for the set platform is analyzed.
     */
    bool mAnalyzeAllVsConfigs;

    /** Check only a selected VS configuration */
    QStringList mVsConfigurations;

    /** Check code in headers */
    bool mCheckHeaders;

    /** Check code in unused templates */
    bool mCheckUnusedTemplates;

    /**
     * @brief List of include directories used to search include files.
     */
    QStringList mIncludeDirs;

    /**
     * @brief List of defines.
     */
    QStringList mDefines;

    /**
     * @brief List of undefines.
     */
    QStringList mUndefines;

    /**
     * @brief List of paths to check.
     */
    QStringList mPaths;

    /**
     * @brief Paths excluded from the check.
     */
    QStringList mExcludedPaths;

    /**
     * @brief List of libraries.
     */
    QStringList mLibraries;

    std::map<std::string, std::string> mFunctionContracts;

    std::map<QString, Settings::VariableContracts> mVariableContracts;

    /**
     * @brief Platform
     */
    QString mPlatform;

    /**
     * @brief List of suppressions.
     */
    QList<Suppressions::Suppression> mSuppressions;

    /**
     * @brief List of addons.
     */
    QStringList mAddons;

    /** @brief Execute clang analyzer? */
    bool mClangAnalyzer;

    /** @brief Execute clang-tidy? */
    bool mClangTidy;

    /**
     * @brief Tags
     */
    QStringList mTags;

    /**
     * @brief Warning tags
     */
    std::map<std::size_t, QString> mWarningTags;

    /** Max CTU depth */
    int mMaxCtuDepth;

    /** Max template instantiation recursion */
    int mMaxTemplateRecursion;

    QStringList mCheckUnknownFunctionReturn;

    static ProjectFile *mActiveProject;
};
/// @}
#endif  // PROJECT_FILE_H
