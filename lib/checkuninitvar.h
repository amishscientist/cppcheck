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


//---------------------------------------------------------------------------
#ifndef checkuninitvarH
#define checkuninitvarH
//---------------------------------------------------------------------------

#include "check.h"
#include "config.h"
#include "ctu.h"
#include "mathlib.h"
#include "errortypes.h"
#include "utils.h"
#include "valueflow.h"

#include <list>
#include <map>
#include <set>
#include <string>

class Scope;
class Token;
class Tokenizer;
class Variable;
class ErrorLogger;
class Settings;
class Library;

namespace tinyxml2 {
    class XMLElement;
}


struct VariableValue {
    explicit VariableValue(MathLib::bigint val = 0) : value(val), notEqual(false) {}
    MathLib::bigint value;
    bool notEqual;
};

/// @addtogroup Checks
/// @{


/** @brief Checking for uninitialized variables */

class CPPCHECKLIB CheckUninitVar : public Check {
public:
    /** @brief This constructor is used when registering the CheckUninitVar */
    CheckUninitVar() : Check(myName()) {}

    /** @brief This constructor is used when running checks. */
    CheckUninitVar(const Tokenizer *tokenizer, const Settings *settings, ErrorLogger *errorLogger)
        : Check(myName(), tokenizer, settings, errorLogger) {}

    /** @brief Run checks against the normal token list */
    void runChecks(const Tokenizer *tokenizer, const Settings *settings, ErrorLogger *errorLogger) OVERRIDE {
        CheckUninitVar checkUninitVar(tokenizer, settings, errorLogger);
        checkUninitVar.check();
        checkUninitVar.valueFlowUninit();
    }

    /** Check for uninitialized variables */
    void check();
    void checkScope(const Scope* scope, const std::set<std::string> &arrayTypeDefs);
    void checkStruct(const Token *tok, const Variable &structvar);
    enum Alloc { NO_ALLOC, NO_CTOR_CALL, CTOR_CALL, ARRAY };
    bool checkScopeForVariable(const Token *tok, const Variable& var, bool* const possibleInit, bool* const noreturn, Alloc* const alloc, const std::string &membervar, std::map<nonneg int, VariableValue> variableValue);
    const Token *checkExpr(const Token *tok, const Variable& var, const Alloc alloc, bool known, bool *bailout=nullptr);
    bool checkIfForWhileHead(const Token *startparentheses, const Variable& var, bool suppressErrors, bool isuninit, Alloc alloc, const std::string &membervar);
    bool checkLoopBody(const Token *tok, const Variable& var, const Alloc alloc, const std::string &membervar, const bool suppressErrors);
    const Token* checkLoopBodyRecursive(const Token *start, const Variable& var, const Alloc alloc, const std::string &membervar, bool &bailout) const;
    void checkRhs(const Token *tok, const Variable &var, Alloc alloc, nonneg int number_of_if, const std::string &membervar);
    static const Token *isVariableUsage(bool cpp, const Token *vartok, const Library &library, bool pointer, Alloc alloc, int indirect = 0);
    const Token *isVariableUsage(const Token *vartok, bool pointer, Alloc alloc, int indirect = 0) const;
    static int isFunctionParUsage(const Token *vartok, const Library &library, bool pointer, Alloc alloc, int indirect = 0);
    int isFunctionParUsage(const Token *vartok, bool pointer, Alloc alloc, int indirect = 0) const;
    bool isMemberVariableAssignment(const Token *tok, const std::string &membervar) const;
    bool isMemberVariableUsage(const Token *tok, bool isPointer, Alloc alloc, const std::string &membervar) const;

    /** ValueFlow-based checking for uninitialized variables */
    void valueFlowUninit();

    /* data for multifile checking */
    class MyFileInfo : public Check::FileInfo {
    public:
        /** function arguments that data are unconditionally read */
        std::list<CTU::FileInfo::UnsafeUsage> unsafeUsage;

        /** Convert MyFileInfo data into xml string */
        std::string toString() const OVERRIDE;
    };

    /** @brief Parse current TU and extract file info */
    Check::FileInfo *getFileInfo(const Tokenizer *tokenizer, const Settings *settings) const OVERRIDE;

    Check::FileInfo * loadFileInfoFromXml(const tinyxml2::XMLElement *xmlElement) const OVERRIDE;

    /** @brief Analyse all file infos for all TU */
    bool analyseWholeProgram(const CTU::FileInfo *ctu, const std::list<Check::FileInfo*> &fileInfo, const Settings& settings, ErrorLogger &errorLogger) OVERRIDE;

    void uninitvarError(const Token* tok, const ValueFlow::Value& v);
    void uninitstringError(const Token *tok, const std::string &varname, bool strncpy_);
    void uninitdataError(const Token *tok, const std::string &varname);
    void uninitvarError(const Token *tok, const std::string &varname, ErrorPath errorPath);
    void uninitvarError(const Token *tok, const std::string &varname) {
        ErrorPath errorPath;
        uninitvarError(tok, varname, errorPath);
    }
    void uninitvarError(const Token *tok, const std::string &varname, Alloc alloc) {
        if (alloc == NO_CTOR_CALL || alloc == CTOR_CALL)
            uninitdataError(tok, varname);
        else
            uninitvarError(tok, varname);
    }
    void uninitStructMemberError(const Token *tok, const std::string &membername);

private:
    Check::FileInfo *getFileInfo() const;
    bool isUnsafeFunction(const Scope *scope, int argnr, const Token **tok) const;

    void getErrorMessages(ErrorLogger *errorLogger, const Settings *settings) const OVERRIDE {
        CheckUninitVar c(nullptr, settings, errorLogger);

        ValueFlow::Value v{};

        // error
        c.uninitvarError(nullptr, v);
        c.uninitstringError(nullptr, "varname", true);
        c.uninitdataError(nullptr, "varname");
        c.uninitStructMemberError(nullptr, "a.b");
    }

    static std::string myName() {
        return "Uninitialized variables";
    }

    std::string classInfo() const OVERRIDE {
        return "Uninitialized variables\n"
               "- using uninitialized local variables\n"
               "- using allocated data before it has been initialized\n";
    }
};
/// @}
//---------------------------------------------------------------------------
#endif // checkuninitvarH
