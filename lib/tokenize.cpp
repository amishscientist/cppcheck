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
#include "tokenize.h"

#include "check.h"
#include "errorlogger.h"
#include "library.h"
#include "mathlib.h"
#include "platform.h"
#include "preprocessor.h"
#include "settings.h"
#include "standards.h"
#include "summaries.h"
#include "symboldatabase.h"
#include "templatesimplifier.h"
#include "timer.h"
#include "token.h"
#include "utils.h"
#include "valueflow.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <exception>
#include <memory>
#include <set>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <simplecpp.h>

//---------------------------------------------------------------------------

namespace {
    // local struct used in setVarId
    // in order to store information about the scope
    struct VarIdScopeInfo {
        VarIdScopeInfo()
            : isExecutable(false), isStructInit(false), isEnum(false), startVarid(0) {}
        VarIdScopeInfo(bool isExecutable, bool isStructInit, bool isEnum, nonneg int startVarid)
            : isExecutable(isExecutable), isStructInit(isStructInit), isEnum(isEnum), startVarid(startVarid) {}

        const bool isExecutable;
        const bool isStructInit;
        const bool isEnum;
        const nonneg int startVarid;
    };
}

/** Return whether tok is the "{" that starts an enumerator list */
static bool isEnumStart(const Token* tok)
{
    if (!tok || tok->str() != "{")
        return false;
    return (tok->strAt(-1) == "enum") || (tok->strAt(-2) == "enum");
}

template<typename T>
static void skipEnumBody(T **tok)
{
    T *defStart = *tok;
    while (Token::Match(defStart, "%name%|::|:"))
        defStart = defStart->next();
    if (defStart && defStart->str() == "{")
        *tok = defStart->link()->next();
}

const Token * Tokenizer::isFunctionHead(const Token *tok, const std::string &endsWith) const
{
    return Tokenizer::isFunctionHead(tok, endsWith, isCPP());
}

const Token * Tokenizer::isFunctionHead(const Token *tok, const std::string &endsWith, bool cpp)
{
    if (!tok)
        return nullptr;
    if (tok->str() == "(")
        tok = tok->link();
    if (Token::Match(tok, ") ;|{|[")) {
        tok = tok->next();
        while (tok && tok->str() == "[" && tok->link()) {
            if (endsWith.find(tok->str()) != std::string::npos)
                return tok;
            tok = tok->link()->next();
        }
        return (tok && endsWith.find(tok->str()) != std::string::npos) ? tok : nullptr;
    }
    if (cpp && tok->str() == ")") {
        tok = tok->next();
        while (Token::Match(tok, "const|noexcept|override|final|volatile|&|&& !!(") ||
               (Token::Match(tok, "%name% !!(") && tok->isUpperCaseName()))
            tok = tok->next();
        if (tok && tok->str() == ")")
            tok = tok->next();
        while (tok && tok->str() == "[")
            tok = tok->link()->next();
        if (Token::Match(tok, "throw|noexcept ("))
            tok = tok->linkAt(1)->next();
        if (Token::Match(tok, "%name% (") && tok->isUpperCaseName())
            tok = tok->linkAt(1)->next();
        if (tok && tok->originalName() == "->") { // trailing return type
            for (tok = tok->next(); tok && !Token::Match(tok, ";|{|override|final"); tok = tok->next())
                if (tok->link() && Token::Match(tok, "<|[|("))
                    tok = tok->link();
        }
        while (Token::Match(tok, "override|final !!(") ||
               (Token::Match(tok, "%name% !!(") && tok->isUpperCaseName()))
            tok = tok->next();
        if (Token::Match(tok, "= 0|default|delete ;"))
            tok = tok->tokAt(2);

        return (tok && endsWith.find(tok->str()) != std::string::npos) ? tok : nullptr;
    }
    return nullptr;
}

/**
 * is tok the start brace { of a class, struct, union, or enum
 */
static bool isClassStructUnionEnumStart(const Token * tok)
{
    if (!Token::Match(tok->previous(), "class|struct|union|enum|%name%|>|>> {"))
        return false;
    const Token * tok2 = tok->previous();
    while (tok2 && !Token::Match(tok2, "class|struct|union|enum|{|}|;"))
        tok2 = tok2->previous();
    return Token::Match(tok2, "class|struct|union|enum");
}

//---------------------------------------------------------------------------

Tokenizer::Tokenizer() :
    list(nullptr),
    mSettings(nullptr),
    mErrorLogger(nullptr),
    mSymbolDatabase(nullptr),
    mTemplateSimplifier(nullptr),
    mVarId(0),
    mUnnamedCount(0),
    mCodeWithTemplates(false), //is there any templates?
    mTimerResults(nullptr)
#ifdef MAXTIME
    , mMaxTime(std::time(0) + MAXTIME)
#endif
    , mPreprocessor(nullptr)
{}

Tokenizer::Tokenizer(const Settings *settings, ErrorLogger *errorLogger) :
    list(settings),
    mSettings(settings),
    mErrorLogger(errorLogger),
    mSymbolDatabase(nullptr),
    mTemplateSimplifier(nullptr),
    mVarId(0),
    mUnnamedCount(0),
    mCodeWithTemplates(false), //is there any templates?
    mTimerResults(nullptr)
#ifdef MAXTIME
    ,mMaxTime(std::time(0) + MAXTIME)
#endif
    , mPreprocessor(nullptr)
{
    // make sure settings are specified
    assert(mSettings);

    mTemplateSimplifier = new TemplateSimplifier(this);
}

Tokenizer::~Tokenizer()
{
    delete mSymbolDatabase;
    delete mTemplateSimplifier;
}


//---------------------------------------------------------------------------
// SizeOfType - gives the size of a type
//---------------------------------------------------------------------------

nonneg int Tokenizer::sizeOfType(const Token *type) const
{
    if (!type || type->str().empty())
        return 0;

    if (type->tokType() == Token::eString)
        return Token::getStrLength(type) + 1U;

    const std::map<std::string, int>::const_iterator it = mTypeSize.find(type->str());
    if (it == mTypeSize.end()) {
        const Library::PodType* podtype = mSettings->library.podtype(type->str());
        if (!podtype)
            return 0;

        return podtype->size;
    } else if (type->isLong()) {
        if (type->str() == "double")
            return mSettings->sizeof_long_double;
        else if (type->str() == "long")
            return mSettings->sizeof_long_long;
    }

    return it->second;
}
//---------------------------------------------------------------------------

// check if this statement is a duplicate definition
bool Tokenizer::duplicateTypedef(Token **tokPtr, const Token *name, const Token *typeDef) const
{
    // check for an end of definition
    const Token * tok = *tokPtr;
    if (tok && Token::Match(tok->next(), ";|,|[|=|)|>|(|{")) {
        const Token * end = tok->next();

        if (end->str() == "[") {
            if (!end->link())
                syntaxError(end); // invalid code
            end = end->link()->next();
        } else if (end->str() == ",") {
            // check for derived class
            if (Token::Match(tok->previous(), "public|private|protected"))
                return false;

            // find end of definition
            while (end && end->next() && !Token::Match(end->next(), ";|)|>")) {
                if (end->next()->str() == "(")
                    end = end->linkAt(1);

                end = (end)?end->next():nullptr;
            }
            if (end)
                end = end->next();
        } else if (end->str() == "(") {
            if (tok->previous()->str().compare(0, 8, "operator")  == 0) {
                // conversion operator
                return false;
            } else if (tok->previous()->str() == "typedef") {
                // typedef of function returning this type
                return false;
            } else if (Token::Match(tok->previous(), "public:|private:|protected:")) {
                return false;
            } else if (tok->previous()->str() == ">") {
                if (!Token::Match(tok->tokAt(-2), "%type%"))
                    return false;

                if (!Token::Match(tok->tokAt(-3), ",|<"))
                    return false;

                *tokPtr = end->link();
                return true;
            }
        }

        if (end) {
            if (Token::simpleMatch(end, ") {")) { // function parameter ?
                // look backwards
                if (Token::Match(tok->previous(), "%type%") &&
                    !Token::Match(tok->previous(), "return|new|const|struct")) {
                    // duplicate definition so skip entire function
                    *tokPtr = end->next()->link();
                    return true;
                }
            } else if (end->str() == ">") { // template parameter ?
                // look backwards
                if (Token::Match(tok->previous(), "%type%") &&
                    !Token::Match(tok->previous(), "return|new|const|volatile")) {
                    // duplicate definition so skip entire template
                    while (end && end->str() != "{")
                        end = end->next();
                    if (end) {
                        *tokPtr = end->link();
                        return true;
                    }
                }
            } else {
                // look backwards
                if (Token::Match(tok->previous(), "typedef|}|>") ||
                    (end->str() == ";" && tok->previous()->str() == ",") ||
                    (tok->previous()->str() == "*" && tok->next()->str() != "(") ||
                    (Token::Match(tok->previous(), "%type%") &&
                     (!Token::Match(tok->previous(), "return|new|const|friend|public|private|protected|throw|extern") &&
                      !Token::simpleMatch(tok->tokAt(-2), "friend class")))) {
                    // scan backwards for the end of the previous statement
                    while (tok && tok->previous() && !Token::Match(tok->previous(), ";|{")) {
                        if (tok->previous()->str() == "}") {
                            tok = tok->previous()->link();
                        } else if (tok->previous()->str() == "typedef") {
                            return true;
                        } else if (tok->previous()->str() == "enum") {
                            return true;
                        } else if (tok->previous()->str() == "struct") {
                            if (tok->strAt(-2) == "typedef" &&
                                tok->next()->str() == "{" &&
                                typeDef->strAt(3) != "{") {
                                // declaration after forward declaration
                                return true;
                            } else if (tok->next()->str() == "{") {
                                return true;
                            } else if (Token::Match(tok->next(), ")|*")) {
                                return true;
                            } else if (tok->next()->str() == name->str()) {
                                return true;
                            } else if (tok->next()->str() != ";") {
                                return true;
                            } else {
                                return false;
                            }
                        } else if (tok->previous()->str() == "union") {
                            if (tok->next()->str() != ";") {
                                return true;
                            } else {
                                return false;
                            }
                        } else if (isCPP() && tok->previous()->str() == "class") {
                            if (tok->next()->str() != ";") {
                                return true;
                            } else {
                                return false;
                            }
                        }
                        if (tok)
                            tok = tok->previous();
                    }

                    if ((*tokPtr)->strAt(1) != "(" || !Token::Match((*tokPtr)->linkAt(1), ") .|(|["))
                        return true;
                }
            }
        }
    }

    return false;
}

void Tokenizer::unsupportedTypedef(const Token *tok) const
{
    if (!mSettings->debugwarnings)
        return;

    std::ostringstream str;
    const Token *tok1 = tok;
    int level = 0;
    while (tok) {
        if (level == 0 && tok->str() == ";")
            break;
        else if (tok->str() == "{")
            ++level;
        else if (tok->str() == "}") {
            if (level == 0)
                break;
            --level;
        }

        if (tok != tok1)
            str << " ";
        str << tok->str();
        tok = tok->next();
    }
    if (tok)
        str << " ;";

    reportError(tok1, Severity::debug, "simplifyTypedef",
                "Failed to parse \'" + str.str() + "\'. The checking continues anyway.");
}

Token * Tokenizer::deleteInvalidTypedef(Token *typeDef)
{
    Token *tok = nullptr;

    // remove typedef but leave ;
    while (typeDef->next()) {
        if (typeDef->next()->str() == ";") {
            typeDef->deleteNext();
            break;
        } else if (typeDef->next()->str() == "{")
            Token::eraseTokens(typeDef, typeDef->linkAt(1));
        else if (typeDef->next()->str() == "}")
            break;
        typeDef->deleteNext();
    }

    if (typeDef != list.front()) {
        tok = typeDef->previous();
        tok->deleteNext();
    } else {
        list.front()->deleteThis();
        tok = list.front();
    }

    return tok;
}

namespace {
    struct Space {
        Space() : bodyEnd(nullptr), bodyEnd2(nullptr), isNamespace(false) {}
        std::string className;
        const Token * bodyEnd;  // for body contains typedef define
        const Token * bodyEnd2; // for body contains typedef using
        bool isNamespace;
        std::set<std::string> recordTypes;
    };
}

static Token *splitDefinitionFromTypedef(Token *tok, nonneg int *unnamedCount)
{
    std::string name;
    bool isConst = false;
    Token *tok1 = tok->next();

    // skip const if present
    if (tok1->str() == "const") {
        tok1->deleteThis();
        isConst = true;
    }

    // skip "class|struct|union|enum"
    tok1 = tok1->next();

    const bool hasName = Token::Match(tok1, "%name%");

    // skip name
    if (hasName) {
        name = tok1->str();
        tok1 = tok1->next();
    }

    // skip base classes if present
    if (tok1->str() == ":") {
        tok1 = tok1->next();
        while (tok1 && tok1->str() != "{")
            tok1 = tok1->next();
        if (!tok1)
            return nullptr;
    }

    // skip to end
    tok1 = tok1->link();

    if (!hasName) { // unnamed
        if (tok1->next()) {
            // use typedef name if available
            if (Token::Match(tok1->next(), "%type%"))
                name = tok1->next()->str();
            else // create a unique name
                name = "Unnamed" + MathLib::toString((*unnamedCount)++);
            tok->next()->insertToken(name);
        } else
            return nullptr;
    }

    tok1->insertToken(";");
    tok1 = tok1->next();

    if (tok1->next() && tok1->next()->str() == ";" && tok1->previous()->str() == "}") {
        tok->deleteThis();
        tok1->deleteThis();
        return nullptr;
    } else {
        tok1->insertToken("typedef");
        tok1 = tok1->next();
        Token * tok3 = tok1;
        if (isConst) {
            tok1->insertToken("const");
            tok1 = tok1->next();
        }
        tok1->insertToken(tok->next()->str()); // struct, union or enum
        tok1 = tok1->next();
        tok1->insertToken(name);
        tok->deleteThis();
        tok = tok3;
    }

    return tok;
}

/* This function is called when processing function related typedefs.
 * If simplifyTypedef generates an "Internal Error" message and the
 * code that generated it deals in some way with functions, then this
 * function will probably need to be extended to handle a new function
 * related pattern */
Token *Tokenizer::processFunc(Token *tok2, bool inOperator) const
{
    if (tok2->next() && tok2->next()->str() != ")" &&
        tok2->next()->str() != ",") {
        // skip over tokens for some types of canonicalization
        if (Token::Match(tok2->next(), "( * %type% ) ("))
            tok2 = tok2->linkAt(5);
        else if (Token::Match(tok2->next(), "* ( * %type% ) ("))
            tok2 = tok2->linkAt(6);
        else if (Token::Match(tok2->next(), "* ( * %type% ) ;"))
            tok2 = tok2->tokAt(5);
        else if (Token::Match(tok2->next(), "* ( %type% [") &&
                 Token::Match(tok2->linkAt(4), "] ) ;|="))
            tok2 = tok2->linkAt(4)->next();
        else if (Token::Match(tok2->next(), "* ( * %type% ("))
            tok2 = tok2->linkAt(5)->next();
        else if (Token::simpleMatch(tok2->next(), "* [") &&
                 Token::simpleMatch(tok2->linkAt(2), "] ;"))
            tok2 = tok2->next();
        else {
            if (tok2->next()->str() == "(")
                tok2 = tok2->next()->link();
            else if (!inOperator && !Token::Match(tok2->next(), "[|>|;")) {
                tok2 = tok2->next();

                while (Token::Match(tok2, "*|&") &&
                       !Token::Match(tok2->next(), ")|>"))
                    tok2 = tok2->next();

                // skip over namespace
                while (Token::Match(tok2, "%name% ::"))
                    tok2 = tok2->tokAt(2);

                if (!tok2)
                    return nullptr;

                if (tok2->str() == "(" &&
                    tok2->link()->next() &&
                    tok2->link()->next()->str() == "(") {
                    tok2 = tok2->link();

                    if (tok2->next()->str() == "(")
                        tok2 = tok2->next()->link();
                }

                // skip over typedef parameter
                if (tok2->next() && tok2->next()->str() == "(") {
                    tok2 = tok2->next()->link();
                    if (!tok2->next())
                        syntaxError(tok2);

                    if (tok2->next()->str() == "(")
                        tok2 = tok2->next()->link();
                }
            }
        }
    }
    return tok2;
}

void Tokenizer::simplifyUsingToTypedef()
{
    if (!isCPP() || mSettings->standards.cpp < Standards::CPP11)
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // using a::b;  =>   typedef  a::b  b;
        if ((Token::Match(tok, "[;{}] using %name% :: %name% ::|;") && !tok->tokAt(2)->isKeyword()) ||
            (Token::Match(tok, "[;{}] using :: %name% :: %name% ::|;") && !tok->tokAt(3)->isKeyword())) {
            Token *endtok = tok->tokAt(5);
            if (Token::Match(endtok, "%name%"))
                endtok = endtok->next();
            while (Token::Match(endtok, ":: %name%"))
                endtok = endtok->tokAt(2);
            if (endtok && endtok->str() == ";") {
                tok->next()->str("typedef");
                endtok = endtok->previous();
                endtok->insertToken(endtok->str());
            }
        }
    }
}

void Tokenizer::simplifyTypedef()
{
    std::vector<Space> spaceInfo;
    bool isNamespace = false;
    std::string className;
    std::string fullClassName;
    bool hasClass = false;
    bool goback = false;

    // add global namespace
    spaceInfo.emplace_back(Space{});

    // Convert "using a::b;" to corresponding typedef statements
    simplifyUsingToTypedef();

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (mErrorLogger && !list.getFiles().empty())
            mErrorLogger->reportProgress(list.getFiles()[0], "Tokenize (typedef)", tok->progressValue());

        if (Settings::terminated())
            return;

        if (isMaxTime())
            return;

        if (goback) {
            //jump back once, see the comment at the end of the function
            goback = false;
            tok = tok->previous();
        }

        if (tok->str() != "typedef") {
            if (Token::simpleMatch(tok, "( typedef")) {
                // Skip typedefs inside parentheses (#2453 and #4002)
                tok = tok->next();
            } else if (Token::Match(tok, "class|struct|namespace %any%") &&
                       (!tok->previous() || tok->previous()->str() != "enum")) {
                isNamespace = (tok->str() == "namespace");
                hasClass = true;
                className = tok->next()->str();
                const Token *tok1 = tok->next();
                fullClassName = className;
                while (Token::Match(tok1, "%name% :: %name%")) {
                    tok1 = tok1->tokAt(2);
                    fullClassName += " :: " + tok1->str();
                }
            } else if (hasClass && tok->str() == ";") {
                hasClass = false;
            } else if (hasClass && tok->str() == "{") {
                if (!isNamespace)
                    spaceInfo.back().recordTypes.insert(fullClassName);

                Space info;
                info.isNamespace = isNamespace;
                info.className = className;
                info.bodyEnd = tok->link();
                info.bodyEnd2 = tok->link();
                spaceInfo.push_back(info);

                hasClass = false;
            } else if (spaceInfo.size() > 1 && tok->str() == "}" && spaceInfo.back().bodyEnd == tok) {
                spaceInfo.pop_back();
            }
            continue;
        }

        // pull struct, union, enum or class definition out of typedef
        // use typedef name for unnamed struct, union, enum or class
        if (Token::Match(tok->next(), "const| struct|enum|union|class %type%| {|:")) {
            Token *tok1 = splitDefinitionFromTypedef(tok, &mUnnamedCount);
            if (!tok1)
                continue;
            tok = tok1;
        }

        /** @todo add support for union */
        if (Token::Match(tok->next(), "enum %type% %type% ;") && tok->strAt(2) == tok->strAt(3)) {
            tok->deleteNext(3);
            tok->deleteThis();
            if (tok->next())
                tok->deleteThis();
            //now the next token to process is 'tok', not 'tok->next()';
            goback = true;
            continue;
        }

        Token *typeName;
        Token *typeStart = nullptr;
        Token *typeEnd = nullptr;
        Token *argStart = nullptr;
        Token *argEnd = nullptr;
        Token *arrayStart = nullptr;
        Token *arrayEnd = nullptr;
        Token *specStart = nullptr;
        Token *specEnd = nullptr;
        Token *typeDef = tok;
        Token *argFuncRetStart = nullptr;
        Token *argFuncRetEnd = nullptr;
        Token *funcStart = nullptr;
        Token *funcEnd = nullptr;
        Token *tokOffset = tok->next();
        bool function = false;
        bool functionPtr = false;
        bool functionRetFuncPtr = false;
        bool functionPtrRetFuncPtr = false;
        bool ptrToArray = false;
        bool refToArray = false;
        bool ptrMember = false;
        bool typeOf = false;
        Token *namespaceStart = nullptr;
        Token *namespaceEnd = nullptr;

        // check for invalid input
        if (!tokOffset)
            syntaxError(tok);


        if (tokOffset->str() == "::") {
            typeStart = tokOffset;
            tokOffset = tokOffset->next();

            while (Token::Match(tokOffset, "%type% ::"))
                tokOffset = tokOffset->tokAt(2);

            typeEnd = tokOffset;

            if (Token::Match(tokOffset, "%type%"))
                tokOffset = tokOffset->next();
        } else if (Token::Match(tokOffset, "%type% ::")) {
            typeStart = tokOffset;

            do {
                tokOffset = tokOffset->tokAt(2);
            } while (Token::Match(tokOffset, "%type% ::"));

            typeEnd = tokOffset;

            if (Token::Match(tokOffset, "%type%"))
                tokOffset = tokOffset->next();
        } else if (Token::Match(tokOffset, "%type%")) {
            typeStart = tokOffset;

            while (Token::Match(tokOffset, "const|struct|enum %type%") ||
                   (tokOffset->next() && tokOffset->next()->isStandardType()))
                tokOffset = tokOffset->next();

            typeEnd = tokOffset;
            tokOffset = tokOffset->next();

            while (Token::Match(tokOffset, "%type%") &&
                   (tokOffset->isStandardType() || Token::Match(tokOffset, "unsigned|signed"))) {
                typeEnd = tokOffset;
                tokOffset = tokOffset->next();
            }

            bool atEnd = false;
            while (!atEnd) {
                if (tokOffset && tokOffset->str() == "::") {
                    typeEnd = tokOffset;
                    tokOffset = tokOffset->next();
                }

                if (Token::Match(tokOffset, "%type%") &&
                    tokOffset->next() && !Token::Match(tokOffset->next(), "[|;|,|(")) {
                    typeEnd = tokOffset;
                    tokOffset = tokOffset->next();
                } else if (Token::simpleMatch(tokOffset, "const (")) {
                    typeEnd = tokOffset;
                    tokOffset = tokOffset->next();
                    atEnd = true;
                } else
                    atEnd = true;
            }
        } else
            continue; // invalid input

        // check for invalid input
        if (!tokOffset)
            syntaxError(tok);

        // check for template
        if (!isC() && tokOffset->str() == "<") {
            typeEnd = tokOffset->findClosingBracket();

            while (typeEnd && Token::Match(typeEnd->next(), ":: %type%"))
                typeEnd = typeEnd->tokAt(2);

            if (!typeEnd) {
                // internal error
                return;
            }

            while (Token::Match(typeEnd->next(), "const|volatile"))
                typeEnd = typeEnd->next();

            tok = typeEnd;
            tokOffset = tok->next();
        }

        std::list<std::string> pointers;
        // check for pointers and references
        while (Token::Match(tokOffset, "*|&|&&|const")) {
            pointers.push_back(tokOffset->str());
            tokOffset = tokOffset->next();
        }

        // check for invalid input
        if (!tokOffset)
            syntaxError(tok);

        if (tokOffset->isName() && !tokOffset->isKeyword()) {
            // found the type name
            typeName = tokOffset;
            tokOffset = tokOffset->next();

            // check for array
            while (tokOffset && tokOffset->str() == "[") {
                if (!arrayStart)
                    arrayStart = tokOffset;
                arrayEnd = tokOffset->link();
                tokOffset = arrayEnd->next();
            }

            // check for end or another
            if (Token::Match(tokOffset, ";|,"))
                tok = tokOffset;

            // or a function typedef
            else if (tokOffset && tokOffset->str() == "(") {
                Token *tokOffset2 = nullptr;
                if (Token::Match(tokOffset, "( *|%name%")) {
                    tokOffset2 = tokOffset->next();
                    if (tokOffset2->str() == "typename")
                        tokOffset2 = tokOffset2->next();
                    while (Token::Match(tokOffset2, "%type% ::"))
                        tokOffset2 = tokOffset2->tokAt(2);
                }

                // unhandled typedef, skip it and continue
                if (typeName->str() == "void") {
                    unsupportedTypedef(typeDef);
                    tok = deleteInvalidTypedef(typeDef);
                    if (tok == list.front())
                        //now the next token to process is 'tok', not 'tok->next()';
                        goback = true;
                    continue;
                }

                // function pointer
                else if (Token::Match(tokOffset2, "* %name% ) (")) {
                    // name token wasn't a name, it was part of the type
                    typeEnd = typeEnd->next();
                    functionPtr = true;
                    funcStart = funcEnd = tokOffset2; // *
                    tokOffset = tokOffset2->tokAt(3); // (
                    typeName = tokOffset->tokAt(-2);
                    argStart = tokOffset;
                    argEnd = tokOffset->link();
                    tok = argEnd->next();
                }

                // function
                else if (isFunctionHead(tokOffset->link(), ";,")) {
                    function = true;
                    if (tokOffset->link()->next()->str() == "const") {
                        specStart = tokOffset->link()->next();
                        specEnd = specStart;
                    }
                    argStart = tokOffset;
                    argEnd = tokOffset->link();
                    tok = argEnd->next();
                    if (specStart)
                        tok = tok->next();
                }

                // syntax error
                else
                    syntaxError(tok);
            }

            // unhandled typedef, skip it and continue
            else {
                unsupportedTypedef(typeDef);
                tok = deleteInvalidTypedef(typeDef);
                if (tok == list.front())
                    //now the next token to process is 'tok', not 'tok->next()';
                    goback = true;
                continue;
            }
        }

        // typeof: typedef typeof ( ... ) type;
        else if (Token::simpleMatch(tokOffset->previous(), "typeof (") &&
                 Token::Match(tokOffset->link(), ") %type% ;")) {
            argStart = tokOffset;
            argEnd = tokOffset->link();
            typeName = tokOffset->link()->next();
            tok = typeName->next();
            typeOf = true;
        }

        // function: typedef ... ( ... type )( ... );
        //           typedef ... (( ... type )( ... ));
        //           typedef ... ( * ( ... type )( ... ));
        else if (tokOffset->str() == "(" && (
                     (tokOffset->link() && Token::Match(tokOffset->link()->previous(), "%type% ) (") &&
                      Token::Match(tokOffset->link()->next()->link(), ") const|volatile|;")) ||
                     (Token::simpleMatch(tokOffset, "( (") &&
                      tokOffset->next() && Token::Match(tokOffset->next()->link()->previous(), "%type% ) (") &&
                      Token::Match(tokOffset->next()->link()->next()->link(), ") const|volatile| ) ;|,")) ||
                     (Token::simpleMatch(tokOffset, "( * (") &&
                      tokOffset->linkAt(2) && Token::Match(tokOffset->linkAt(2)->previous(), "%type% ) (") &&
                      Token::Match(tokOffset->linkAt(2)->next()->link(), ") const|volatile| ) ;|,")))) {
            if (tokOffset->next()->str() == "(")
                tokOffset = tokOffset->next();
            else if (Token::simpleMatch(tokOffset, "( * (")) {
                pointers.emplace_back("*");
                tokOffset = tokOffset->tokAt(2);
            }

            if (tokOffset->link()->strAt(-2) == "*")
                functionPtr = true;
            else
                function = true;
            funcStart = tokOffset->next();
            tokOffset = tokOffset->link();
            funcEnd = tokOffset->tokAt(-2);
            typeName = tokOffset->previous();
            argStart = tokOffset->next();
            argEnd = tokOffset->next()->link();
            if (!argEnd)
                syntaxError(argStart);

            tok = argEnd->next();
            Token *spec = tok;
            if (Token::Match(spec, "const|volatile")) {
                specStart = spec;
                specEnd = spec;
                while (Token::Match(spec->next(), "const|volatile")) {
                    specEnd = spec->next();
                    spec = specEnd;
                }
                tok = specEnd->next();
            }
            if (!tok)
                syntaxError(specEnd);

            if (tok->str() == ")")
                tok = tok->next();
        }

        else if (Token::Match(tokOffset, "( %type% (")) {
            function = true;
            if (tokOffset->link()->next()) {
                tok = tokOffset->link()->next();
                tokOffset = tokOffset->tokAt(2);
                typeName = tokOffset->previous();
                argStart = tokOffset;
                argEnd = tokOffset->link();
            } else {
                // internal error
                continue;
            }
        }

        // pointer to function returning pointer to function
        else if (Token::Match(tokOffset, "( * ( * %type% ) (") &&
                 Token::simpleMatch(tokOffset->linkAt(6), ") ) (") &&
                 Token::Match(tokOffset->linkAt(6)->linkAt(2), ") ;|,")) {
            functionPtrRetFuncPtr = true;

            tokOffset = tokOffset->tokAt(6);
            typeName = tokOffset->tokAt(-2);
            argStart = tokOffset;
            argEnd = tokOffset->link();
            if (!argEnd)
                syntaxError(arrayStart);

            argFuncRetStart = argEnd->tokAt(2);
            argFuncRetEnd = argFuncRetStart->link();
            if (!argFuncRetEnd)
                syntaxError(argFuncRetStart);

            tok = argFuncRetEnd->next();
        }

        // function returning pointer to function
        else if (Token::Match(tokOffset, "( * %type% (") &&
                 Token::simpleMatch(tokOffset->linkAt(3), ") ) (") &&
                 Token::Match(tokOffset->linkAt(3)->linkAt(2), ") ;|,")) {
            functionRetFuncPtr = true;

            tokOffset = tokOffset->tokAt(3);
            typeName = tokOffset->previous();
            argStart = tokOffset;
            argEnd = tokOffset->link();

            argFuncRetStart = argEnd->tokAt(2);
            if (!argFuncRetStart)
                syntaxError(tokOffset);

            argFuncRetEnd = argFuncRetStart->link();
            if (!argFuncRetEnd)
                syntaxError(tokOffset);

            tok = argFuncRetEnd->next();
        } else if (Token::Match(tokOffset, "( * ( %type% ) (")) {
            functionRetFuncPtr = true;

            tokOffset = tokOffset->tokAt(5);
            typeName = tokOffset->tokAt(-2);
            argStart = tokOffset;
            argEnd = tokOffset->link();
            if (!argEnd)
                syntaxError(arrayStart);

            argFuncRetStart = argEnd->tokAt(2);
            if (!argFuncRetStart)
                syntaxError(tokOffset);

            argFuncRetEnd = argFuncRetStart->link();
            if (!argFuncRetEnd)
                syntaxError(tokOffset);

            tok = argFuncRetEnd->next();
        }

        // pointer/reference to array
        else if (Token::Match(tokOffset, "( *|& %type% ) [")) {
            ptrToArray = (tokOffset->next()->str() == "*");
            refToArray = !ptrToArray;
            tokOffset = tokOffset->tokAt(2);
            typeName = tokOffset;
            arrayStart = tokOffset->tokAt(2);
            arrayEnd = arrayStart->link();
            if (!arrayEnd)
                syntaxError(arrayStart);

            tok = arrayEnd->next();
        }

        // pointer to class member
        else if (Token::Match(tokOffset, "( %type% :: * %type% ) ;")) {
            tokOffset = tokOffset->tokAt(2);
            namespaceStart = tokOffset->previous();
            namespaceEnd = tokOffset;
            ptrMember = true;
            tokOffset = tokOffset->tokAt(2);
            typeName = tokOffset;
            tok = tokOffset->tokAt(2);
        }

        // unhandled typedef, skip it and continue
        else {
            unsupportedTypedef(typeDef);
            tok = deleteInvalidTypedef(typeDef);
            if (tok == list.front())
                //now the next token to process is 'tok', not 'tok->next()';
                goback = true;
            continue;
        }

        bool done = false;
        bool ok = true;

        TypedefInfo typedefInfo;
        typedefInfo.name = typeName->str();
        typedefInfo.filename = list.file(typeName);
        typedefInfo.lineNumber = typeName->linenr();
        typedefInfo.column = typeName->column();
        typedefInfo.used = false;
        mTypedefInfo.push_back(typedefInfo);

        while (!done) {
            std::string pattern = typeName->str();
            int scope = 0;
            bool simplifyType = false;
            bool inMemberFunc = false;
            int memberScope = 0;
            bool globalScope = false;
            int classLevel = spaceInfo.size();
            bool inTypeDef = false;
            std::string removed;
            std::string classPath;
            for (size_t i = 1; i < spaceInfo.size(); ++i) {
                if (!classPath.empty())
                    classPath += " :: ";
                classPath += spaceInfo[i].className;
            }

            for (Token *tok2 = tok; tok2; tok2 = tok2->next()) {
                if (Settings::terminated())
                    return;

                removed.clear();

                if (Token::simpleMatch(tok2, "typedef"))
                    inTypeDef = true;

                if (inTypeDef && Token::simpleMatch(tok2, ";"))
                    inTypeDef = false;

                // Check for variable declared with the same name
                if (!inTypeDef && spaceInfo.size() == 1 && Token::Match(tok2->previous(), "%name%") &&
                    !tok2->previous()->isKeyword()) {
                    Token* varDecl = tok2;
                    while (Token::Match(varDecl, "*|&|&&|const"))
                        varDecl = varDecl->next();
                    if (Token::Match(varDecl, "%name% ;|,|)|=") && varDecl->str() == typeName->str()) {
                        // Skip to the next closing brace
                        if (Token::Match(varDecl, "%name% ) {")) { // is argument variable
                            tok2 = varDecl->linkAt(2)->next();
                        } else {
                            tok2 = varDecl;
                            while (tok2 && !Token::simpleMatch(tok2, "}")) {
                                if (Token::Match(tok2, "(|{|["))
                                    tok2 = tok2->link();
                                tok2 = tok2->next();
                            }
                        }
                        if (!tok2)
                            break;
                        continue;
                    }
                }

                if (tok2->link()) { // Pre-check for performance
                    // check for end of scope
                    if (tok2->str() == "}") {
                        // check for end of member function
                        if (inMemberFunc) {
                            --memberScope;
                            if (memberScope == 0)
                                inMemberFunc = false;
                        }

                        if (classLevel > 1 && tok2 == spaceInfo[classLevel - 1].bodyEnd2) {
                            --classLevel;
                            pattern.clear();

                            for (int i = classLevel; i < spaceInfo.size(); ++i)
                                pattern += (spaceInfo[i].className + " :: ");

                            pattern += typeName->str();
                        } else {
                            if (scope == 0)
                                break;
                            --scope;
                        }
                    }

                    // check for member functions
                    else if (isCPP() && tok2->str() == "(" && isFunctionHead(tok2, "{")) {
                        const Token *func = tok2->previous();

                        /** @todo add support for multi-token operators */
                        if (func->previous()->str() == "operator")
                            func = func->previous();

                        if (!func->previous())
                            syntaxError(func);

                        // check for qualifier
                        if (Token::Match(func->tokAt(-2), "%name% ::")) {
                            int offset = -2;
                            while (Token::Match(func->tokAt(offset - 2), "%name% ::"))
                                offset -= 2;
                            // check for available and matching class name
                            if (spaceInfo.size() > 1 && classLevel < spaceInfo.size() &&
                                func->strAt(offset) == spaceInfo[classLevel].className) {
                                memberScope = 0;
                                inMemberFunc = true;
                            }
                        }
                    }

                    // check for entering a new scope
                    else if (tok2->str() == "{") {
                        // check for entering a new namespace
                        if (isCPP() && tok2->strAt(-2) == "namespace") {
                            if (classLevel < spaceInfo.size() &&
                                spaceInfo[classLevel].isNamespace &&
                                spaceInfo[classLevel].className == tok2->previous()->str()) {
                                spaceInfo[classLevel].bodyEnd2 = tok2->link();
                                ++classLevel;
                                pattern.clear();
                                for (int i = classLevel; i < spaceInfo.size(); ++i)
                                    pattern += spaceInfo[i].className + " :: ";

                                pattern += typeName->str();
                            }
                            ++scope;
                        }

                        // keep track of scopes within member function
                        if (inMemberFunc)
                            ++memberScope;

                        ++scope;
                    }
                }

                // check for operator typedef
                /** @todo add support for multi-token operators */
                else if (isCPP() &&
                         tok2->str() == "operator" &&
                         tok2->next() &&
                         tok2->next()->str() == typeName->str() &&
                         tok2->linkAt(2) &&
                         tok2->strAt(2) == "(" &&
                         Token::Match(tok2->linkAt(2), ") const| {")) {
                    // check for qualifier
                    if (tok2->previous()->str() == "::") {
                        // check for available and matching class name
                        if (spaceInfo.size() > 1 && classLevel < spaceInfo.size() &&
                            tok2->strAt(-2) == spaceInfo[classLevel].className) {
                            tok2 = tok2->next();
                            simplifyType = true;
                        }
                    }
                }

                else if (Token::Match(tok2->previous(), "class|struct %name% [:{]")) {
                    // don't replace names in struct/class definition
                }

                // check for typedef that can be substituted
                else if ((tok2->isNameOnly() || (tok2->isName() && tok2->isExpandedMacro())) &&
                         (Token::simpleMatch(tok2, pattern.c_str(), pattern.size()) ||
                          (inMemberFunc && tok2->str() == typeName->str()))) {
                    // member function class variables don't need qualification
                    if (!(inMemberFunc && tok2->str() == typeName->str()) && pattern.find("::") != std::string::npos) { // has a "something ::"
                        Token *start = tok2;
                        int count = 0;
                        int back = classLevel - 1;
                        bool good = true;
                        // check for extra qualification
                        while (back >= 1) {
                            Token *qualificationTok = start->tokAt(-2);
                            if (!Token::Match(qualificationTok, "%type% ::"))
                                break;
                            if (qualificationTok->str() == spaceInfo[back].className) {
                                start = qualificationTok;
                                back--;
                                count++;
                            } else {
                                good = false;
                                break;
                            }
                        }
                        // check global namespace
                        if (good && back == 1 && start->strAt(-1) == "::")
                            good = false;

                        if (good) {
                            // remove any extra qualification if present
                            while (count) {
                                if (!removed.empty())
                                    removed.insert(0, " ");
                                removed.insert(0, tok2->strAt(-2) + " " + tok2->strAt(-1));
                                tok2->tokAt(-3)->deleteNext(2);
                                --count;
                            }

                            // remove global namespace if present
                            if (tok2->strAt(-1) == "::") {
                                removed.insert(0, ":: ");
                                tok2->tokAt(-2)->deleteNext();
                                globalScope = true;
                            }

                            // remove qualification if present
                            for (int i = classLevel; i < spaceInfo.size(); ++i) {
                                if (!removed.empty())
                                    removed += " ";
                                removed += (tok2->str() + " " + tok2->strAt(1));
                                tok2->deleteThis();
                                tok2->deleteThis();
                            }
                            simplifyType = true;
                        }
                    } else {
                        if (tok2->strAt(-1) == "::") {
                            int relativeSpaceInfoSize = spaceInfo.size();
                            Token * tokBeforeType = tok2->previous();
                            while (relativeSpaceInfoSize > 1 &&
                                   tokBeforeType && tokBeforeType->str() == "::" &&
                                   tokBeforeType->strAt(-1) == spaceInfo[relativeSpaceInfoSize-1].className) {
                                tokBeforeType = tokBeforeType->tokAt(-2);
                                --relativeSpaceInfoSize;
                            }
                            if (tokBeforeType && tokBeforeType->str() != "::") {
                                Token::eraseTokens(tokBeforeType, tok2);
                                simplifyType = true;
                            }
                        } else if (Token::Match(tok2->previous(), "case|;|{|} %type% :")) {
                            tok2 = tok2->next();
                        } else if (duplicateTypedef(&tok2, typeName, typeDef)) {
                            // skip to end of scope if not already there
                            if (tok2->str() != "}") {
                                while (tok2->next()) {
                                    if (tok2->next()->str() == "{")
                                        tok2 = tok2->linkAt(1)->previous();
                                    else if (tok2->next()->str() == "}")
                                        break;

                                    tok2 = tok2->next();
                                }
                            }
                        } else if (Token::Match(tok2->tokAt(-2), "%type% *|&")) {
                            // Ticket #5868: Don't substitute variable names
                        } else if (tok2->previous()->str() != ".") {
                            simplifyType = true;
                        }
                    }
                }

                if (simplifyType) {
                    mTypedefInfo.back().used = true;

                    // can't simplify 'operator functionPtr ()' and 'functionPtr operator ... ()'
                    if (functionPtr && (tok2->previous()->str() == "operator" ||
                                        (tok2->next() && tok2->next()->str() == "operator"))) {
                        simplifyType = false;
                        tok2 = tok2->next();
                        continue;
                    }

                    // There are 2 categories of typedef substitutions:
                    // 1. variable declarations that preserve the variable name like
                    //    global, local, and function parameters
                    // 2. not variable declarations that have no name like derived
                    //    classes, casts, operators, and template parameters

                    // try to determine which category this substitution is
                    bool inCast = false;
                    bool inTemplate = false;
                    bool inOperator = false;
                    bool inSizeof = false;

                    const bool sameStartEnd = (typeStart == typeEnd);

                    // check for derived class: class A : some_typedef {
                    const bool isDerived = Token::Match(tok2->previous(), "public|protected|private %type% {|,");

                    // check for cast: (some_typedef) A or static_cast<some_typedef>(A)
                    // todo: check for more complicated casts like: (const some_typedef *)A
                    if ((tok2->previous()->str() == "(" && tok2->next()->str() == ")" && tok2->strAt(-2) != "sizeof") ||
                        (tok2->previous()->str() == "<" && Token::simpleMatch(tok2->next(), "> (")) ||
                        Token::Match(tok2->tokAt(-2), "( const %name% )"))
                        inCast = true;

                    // check for template parameters: t<some_typedef> t1
                    else if (Token::Match(tok2->previous(), "<|,") &&
                             Token::Match(tok2->next(), "&|*| &|*| >|,"))
                        inTemplate = true;

                    else if (Token::Match(tok2->tokAt(-2), "sizeof ( %type% )"))
                        inSizeof = true;

                    // check for operator
                    if (tok2->strAt(-1) == "operator" ||
                        Token::simpleMatch(tok2->tokAt(-2), "operator const"))
                        inOperator = true;

                    if (typeStart->str() == "typename" && tok2->strAt(-1)=="typename") {
                        // Remove one typename if it is already contained in the goal
                        typeStart = typeStart->next();
                    }

                    // skip over class or struct in derived class declaration
                    bool structRemoved = false;
                    if (isDerived && Token::Match(typeStart, "class|struct")) {
                        if (typeStart->str() == "struct")
                            structRemoved = true;
                        typeStart = typeStart->next();
                    }
                    if (Token::Match(typeStart, "struct|class") && Token::Match(tok2, "%name% ::"))
                        typeStart = typeStart->next();

                    if (sameStartEnd)
                        typeEnd = typeStart;

                    // start substituting at the typedef name by replacing it with the type
                    tok2->str(typeStart->str());

                    // restore qualification if it was removed
                    if (typeStart->str() == "struct" || structRemoved) {
                        if (structRemoved)
                            tok2 = tok2->previous();

                        if (globalScope) {
                            tok2->insertToken("::");
                            tok2 = tok2->next();
                        }

                        for (int i = classLevel; i < spaceInfo.size(); ++i) {
                            tok2->insertToken(spaceInfo[i].className);
                            tok2 = tok2->next();
                            tok2->insertToken("::");
                            tok2 = tok2->next();
                        }
                    }

                    // add some qualification back if needed
                    Token *start = tok2;
                    std::string removed1 = removed;
                    std::string::size_type idx = removed1.rfind(" ::");

                    if (idx != std::string::npos)
                        removed1.resize(idx);
                    if (removed1 == classPath && !removed1.empty()) {
                        for (std::vector<Space>::const_reverse_iterator it = spaceInfo.crbegin(); it != spaceInfo.crend(); ++it) {
                            if (it->recordTypes.find(start->str()) != it->recordTypes.end()) {
                                std::string::size_type spaceIdx = 0;
                                std::string::size_type startIdx = 0;
                                while ((spaceIdx = removed1.find(" ", startIdx)) != std::string::npos) {
                                    tok2->previous()->insertToken(removed1.substr(startIdx, spaceIdx - startIdx));
                                    startIdx = spaceIdx + 1;
                                }
                                tok2->previous()->insertToken(removed1.substr(startIdx));
                                tok2->previous()->insertToken("::");
                                break;
                            }
                            idx = removed1.rfind(" ::");
                            if (idx == std::string::npos)
                                break;

                            removed1.resize(idx);
                        }
                    }
                    // add remainder of type
                    tok2 = TokenList::copyTokens(tok2, typeStart->next(), typeEnd);

                    if (!pointers.empty()) {
                        for (const std::string &p : pointers) {
                            tok2->insertToken(p);
                            tok2 = tok2->next();
                        }
                    }

                    if (funcStart && funcEnd) {
                        tok2->insertToken("(");
                        tok2 = tok2->next();
                        Token *paren = tok2;
                        tok2 = TokenList::copyTokens(tok2, funcStart, funcEnd);

                        if (!inCast)
                            tok2 = processFunc(tok2, inOperator);

                        if (!tok2)
                            break;

                        while (Token::Match(tok2, "%name%|] ["))
                            tok2 = tok2->linkAt(1);

                        tok2->insertToken(")");
                        tok2 = tok2->next();
                        Token::createMutualLinks(tok2, paren);

                        tok2 = TokenList::copyTokens(tok2, argStart, argEnd);

                        if (specStart) {
                            Token *spec = specStart;
                            tok2->insertToken(spec->str());
                            tok2 = tok2->next();
                            while (spec != specEnd) {
                                spec = spec->next();
                                tok2->insertToken(spec->str());
                                tok2 = tok2->next();
                            }
                        }
                    }

                    else if (functionPtr || function) {
                        // don't add parentheses around function names because it
                        // confuses other simplifications
                        bool needParen = true;
                        if (!inTemplate && function && tok2->next() && tok2->next()->str() != "*")
                            needParen = false;
                        if (needParen) {
                            tok2->insertToken("(");
                            tok2 = tok2->next();
                        }
                        Token *tok3 = tok2;
                        if (namespaceStart) {
                            const Token *tok4 = namespaceStart;

                            while (tok4 != namespaceEnd) {
                                tok2->insertToken(tok4->str());
                                tok2 = tok2->next();
                                tok4 = tok4->next();
                            }
                            tok2->insertToken(namespaceEnd->str());
                            tok2 = tok2->next();
                        }
                        if (functionPtr) {
                            tok2->insertToken("*");
                            tok2 = tok2->next();
                        }

                        if (!inCast)
                            tok2 = processFunc(tok2, inOperator);

                        if (needParen) {
                            if (!tok2)
                                syntaxError(nullptr);

                            tok2->insertToken(")");
                            tok2 = tok2->next();
                            Token::createMutualLinks(tok2, tok3);
                        }
                        if (!tok2)
                            syntaxError(nullptr);

                        tok2 = TokenList::copyTokens(tok2, argStart, argEnd);
                        if (inTemplate) {
                            if (!tok2)
                                syntaxError(nullptr);

                            tok2 = tok2->next();
                        }

                        if (specStart) {
                            Token *spec = specStart;
                            tok2->insertToken(spec->str());
                            tok2 = tok2->next();
                            while (spec != specEnd) {
                                spec = spec->next();
                                tok2->insertToken(spec->str());
                                tok2 = tok2->next();
                            }
                        }
                    } else if (functionRetFuncPtr || functionPtrRetFuncPtr) {
                        tok2->insertToken("(");
                        tok2 = tok2->next();
                        Token *tok3 = tok2;
                        tok2->insertToken("*");
                        tok2 = tok2->next();

                        Token * tok4 = nullptr;
                        if (functionPtrRetFuncPtr) {
                            tok2->insertToken("(");
                            tok2 = tok2->next();
                            tok4 = tok2;
                            tok2->insertToken("*");
                            tok2 = tok2->next();
                        }

                        // skip over variable name if there
                        if (!inCast) {
                            if (!tok2 || !tok2->next())
                                syntaxError(nullptr);

                            if (tok2->next()->str() != ")")
                                tok2 = tok2->next();
                        }

                        if (tok4 && functionPtrRetFuncPtr) {
                            tok2->insertToken(")");
                            tok2 = tok2->next();
                            Token::createMutualLinks(tok2, tok4);
                        }

                        tok2 = TokenList::copyTokens(tok2, argStart, argEnd);

                        tok2->insertToken(")");
                        tok2 = tok2->next();
                        Token::createMutualLinks(tok2, tok3);

                        tok2 = TokenList::copyTokens(tok2, argFuncRetStart, argFuncRetEnd);
                    } else if (ptrToArray || refToArray) {
                        tok2->insertToken("(");
                        tok2 = tok2->next();
                        Token *tok3 = tok2;

                        if (ptrToArray)
                            tok2->insertToken("*");
                        else
                            tok2->insertToken("&");
                        tok2 = tok2->next();

                        bool hasName = false;
                        // skip over name
                        if (tok2->next() && tok2->next()->str() != ")" && tok2->next()->str() != "," &&
                            tok2->next()->str() != ">") {
                            hasName = true;
                            if (tok2->next()->str() != "(")
                                tok2 = tok2->next();

                            // check for function and skip over args
                            if (tok2 && tok2->next() && tok2->next()->str() == "(")
                                tok2 = tok2->next()->link();

                            // check for array
                            if (tok2 && tok2->next() && tok2->next()->str() == "[")
                                tok2 = tok2->next()->link();
                        }

                        tok2->insertToken(")");
                        Token::createMutualLinks(tok2->next(), tok3);

                        if (!hasName)
                            tok2 = tok2->next();
                    } else if (ptrMember) {
                        if (Token::simpleMatch(tok2, "* (")) {
                            tok2->insertToken("*");
                            tok2 = tok2->next();
                        } else {
                            // This is the case of casting operator.
                            // Name is not available, and () should not be
                            // inserted
                            const bool castOperator = inOperator && Token::Match(tok2, "%type% (");
                            Token *openParenthesis = nullptr;

                            if (!castOperator) {
                                tok2->insertToken("(");
                                tok2 = tok2->next();

                                openParenthesis = tok2;
                            }

                            const Token *tok4 = namespaceStart;

                            while (tok4 != namespaceEnd) {
                                tok2->insertToken(tok4->str());
                                tok2 = tok2->next();
                                tok4 = tok4->next();
                            }
                            tok2->insertToken(namespaceEnd->str());
                            tok2 = tok2->next();

                            tok2->insertToken("*");
                            tok2 = tok2->next();

                            if (openParenthesis) {
                                // Skip over name, if any
                                if (Token::Match(tok2->next(), "%name%"))
                                    tok2 = tok2->next();

                                tok2->insertToken(")");
                                tok2 = tok2->next();

                                Token::createMutualLinks(tok2, openParenthesis);
                            }
                        }
                    } else if (typeOf) {
                        tok2 = TokenList::copyTokens(tok2, argStart, argEnd);
                    } else if (Token::Match(tok2, "%name% [")) {
                        while (Token::Match(tok2, "%name%|] [")) {
                            tok2 = tok2->linkAt(1);
                        }
                        tok2 = tok2->previous();
                    }

                    if (arrayStart && arrayEnd) {
                        do {
                            if (!tok2->next())
                                syntaxError(tok2); // can't recover so quit

                            if (!inCast && !inSizeof && !inTemplate)
                                tok2 = tok2->next();

                            if (tok2->str() == "const")
                                tok2 = tok2->next();

                            // reference or pointer to array?
                            if (Token::Match(tok2, "&|*|&&")) {
                                tok2 = tok2->previous();
                                tok2->insertToken("(");
                                Token *tok3 = tok2->next();

                                // handle missing variable name
                                if (Token::Match(tok3, "( *|&|&& *|&|&& %name%"))
                                    tok2 = tok3->tokAt(3);
                                else if (Token::Match(tok2->tokAt(3), "[(),;]"))
                                    tok2 = tok2->tokAt(2);
                                else
                                    tok2 = tok2->tokAt(3);
                                if (!tok2)
                                    syntaxError(nullptr);

                                while (tok2->strAt(1) == "::")
                                    tok2 = tok2->tokAt(2);

                                // skip over function parameters
                                if (tok2->str() == "(")
                                    tok2 = tok2->link();

                                if (tok2->strAt(1) == "(")
                                    tok2 = tok2->linkAt(1);

                                // skip over const/noexcept
                                while (Token::Match(tok2->next(), "const|noexcept"))
                                    tok2 = tok2->next();

                                tok2->insertToken(")");
                                tok2 = tok2->next();
                                Token::createMutualLinks(tok2, tok3);
                            }

                            if (!tok2->next())
                                syntaxError(tok2); // can't recover so quit

                            // skip over array dimensions
                            while (tok2->next()->str() == "[")
                                tok2 = tok2->linkAt(1);

                            tok2 = TokenList::copyTokens(tok2, arrayStart, arrayEnd);
                            if (!tok2->next())
                                syntaxError(tok2);

                            if (tok2->str() == "=") {
                                if (!tok2->next())
                                    syntaxError(tok2);
                                if (tok2->next()->str() == "{")
                                    tok2 = tok2->next()->link()->next();
                                else if (tok2->next()->str().at(0) == '\"')
                                    tok2 = tok2->tokAt(2);
                            }
                        } while (Token::Match(tok2, ", %name% ;|=|,"));
                    }

                    simplifyType = false;
                }
                if (!tok2)
                    break;
            }

            if (!tok)
                syntaxError(nullptr);

            if (tok->str() == ";")
                done = true;
            else if (tok->str() == ",") {
                arrayStart = nullptr;
                arrayEnd = nullptr;
                tokOffset = tok->next();
                pointers.clear();

                while (Token::Match(tokOffset, "*|&")) {
                    pointers.push_back(tokOffset->str());
                    tokOffset = tokOffset->next();
                }

                if (Token::Match(tokOffset, "%type%")) {
                    typeName = tokOffset;
                    tokOffset = tokOffset->next();

                    if (tokOffset && tokOffset->str() == "[") {
                        arrayStart = tokOffset;

                        for (;;) {
                            while (tokOffset->next() && !Token::Match(tokOffset->next(), ";|,"))
                                tokOffset = tokOffset->next();

                            if (!tokOffset->next())
                                return; // invalid input
                            else if (tokOffset->next()->str() == ";")
                                break;
                            else if (tokOffset->str() == "]")
                                break;
                            else
                                tokOffset = tokOffset->next();
                        }

                        arrayEnd = tokOffset;
                        tokOffset = tokOffset->next();
                    }

                    if (Token::Match(tokOffset, ";|,"))
                        tok = tokOffset;
                    else {
                        // we encountered a typedef we don't support yet so just continue
                        done = true;
                        ok = false;
                    }
                } else {
                    // we encountered a typedef we don't support yet so just continue
                    done = true;
                    ok = false;
                }
            } else {
                // something is really wrong (internal error)
                done = true;
                ok = false;
            }
        }

        if (ok) {
            // remove typedef
            Token::eraseTokens(typeDef, tok);

            if (typeDef != list.front()) {
                tok = typeDef->previous();
                tok->deleteNext();
                //no need to remove last token in the list
                if (tok->tokAt(2))
                    tok->deleteNext();
            } else {
                list.front()->deleteThis();
                //no need to remove last token in the list
                if (list.front()->next())
                    list.front()->deleteThis();
                tok = list.front();
                //now the next token to process is 'tok', not 'tok->next()';
                goback = true;
            }
        }
    }
}

namespace {
    struct ScopeInfo3 {
        enum Type { Global, Namespace, Record, MemberFunction, Other };
        ScopeInfo3() : parent(nullptr), type(Global), bodyStart(nullptr), bodyEnd(nullptr) {}
        ScopeInfo3(ScopeInfo3 *parent_, Type type_, const std::string &name_, const Token *bodyStart_, const Token *bodyEnd_)
            : parent(parent_), type(type_), name(name_), bodyStart(bodyStart_), bodyEnd(bodyEnd_) {
            if (name.empty())
                return;
            fullName = name;
            ScopeInfo3 *scope = parent;
            while (scope && scope->parent) {
                if (scope->name.empty())
                    break;
                fullName = scope->name + " :: " + fullName;
                scope = scope->parent;
            }
        }
        ScopeInfo3 *parent;
        std::list<ScopeInfo3> children;
        Type type;
        std::string fullName;
        std::string name;
        const Token * bodyStart;
        const Token * bodyEnd;
        std::set<std::string> usingNamespaces;
        std::set<std::string> recordTypes;
        std::set<std::string> baseTypes;

        ScopeInfo3 *addChild(Type scopeType, const std::string &scopeName, const Token *bodyStartToken, const Token *bodyEndToken) {
            children.emplace_back(this, scopeType, scopeName, bodyStartToken, bodyEndToken);
            return &children.back();
        }

        bool hasChild(const std::string &childName) const {
            for (const auto & child : children) {
                if (child.name == childName)
                    return true;
            }
            return false;
        }

        const ScopeInfo3 * findInChildren(const std::string & scope) const {
            for (const auto & child : children) {
                if (child.type == Record && (child.name == scope || child.fullName == scope))
                    return &child;
                else {
                    const ScopeInfo3 * temp = child.findInChildren(scope);
                    if (temp)
                        return temp;
                }
            }
            return nullptr;
        }

        const ScopeInfo3 * findScope(const std::string & scope) const {
            const ScopeInfo3 * tempScope = this;
            while (tempScope) {
                // check children
                for (const auto & child : tempScope->children) {
                    if (&child != this && child.type == Record && (child.name == scope || child.fullName == scope))
                        return &child;
                }
                // check siblings for same name
                if (tempScope->parent) {
                    for (const auto &sibling : tempScope->parent->children) {
                        if (sibling.name == tempScope->name && &sibling != this) {
                            const ScopeInfo3 * temp = sibling.findInChildren(scope);
                            if (temp)
                                return temp;
                        }
                    }
                }
                tempScope = tempScope->parent;
            }
            return nullptr;
        }

        bool findTypeInBase(const std::string &scope) const {
            // check in base types first
            if (baseTypes.find(scope) != baseTypes.end())
                return true;
            // check in base types base types
            for (const std::string & base : baseTypes) {
                const ScopeInfo3 * baseScope = findScope(base);
                // bail on uninstantiated recursive template
                if (baseScope == this)
                    return false;
                if (baseScope && baseScope->fullName == scope)
                    return true;
                if (baseScope && baseScope->findTypeInBase(scope))
                    return true;
            }
            return false;
        }

        ScopeInfo3 * findScope(const ScopeInfo3 * scope) {
            if (scope->bodyStart == bodyStart)
                return this;
            for (auto & child : children) {
                ScopeInfo3 * temp = child.findScope(scope);
                if (temp)
                    return temp;
            }
            return nullptr;
        }
    };

    void setScopeInfo(Token *tok, ScopeInfo3 **scopeInfo, bool debug=false)
    {
        if (!tok)
            return;
        if (tok->str() == "{" && (*scopeInfo)->parent && tok == (*scopeInfo)->bodyStart)
            return;
        if (tok->str() == "}") {
            if ((*scopeInfo)->parent && tok == (*scopeInfo)->bodyEnd)
                *scopeInfo = (*scopeInfo)->parent;
            else {
                // Try to find parent scope
                ScopeInfo3 *parent = (*scopeInfo)->parent;
                while (parent && parent->bodyEnd != tok)
                    parent = parent->parent;
                if (parent) {
                    *scopeInfo = parent;
                    if (debug)
                        throw std::runtime_error("Internal error: unmatched }");
                }
            }
            return;
        }
        if (!Token::Match(tok, "namespace|class|struct|union %name% {|:|::|<")) {
            // check for using namespace
            if (Token::Match(tok, "using namespace %name% ;|::")) {
                const Token * tok1 = tok->tokAt(2);
                std::string nameSpace;
                while (tok1 && tok1->str() != ";") {
                    if (!nameSpace.empty())
                        nameSpace += " ";
                    nameSpace += tok1->str();
                    tok1 = tok1->next();
                }
                (*scopeInfo)->usingNamespaces.insert(nameSpace);
            }
            // check for member function
            else if (tok->str() == "{") {
                bool added = false;
                Token *tok1 = tok;
                while (Token::Match(tok1->previous(), "const|volatile|final|override|&|&&|noexcept"))
                    tok1 = tok1->previous();
                if (tok1->previous() && (tok1->strAt(-1) == ")" || tok->strAt(-1) == "}")) {
                    tok1 = tok1->linkAt(-1);
                    if (Token::Match(tok1->previous(), "throw|noexcept (")) {
                        tok1 = tok1->previous();
                        while (Token::Match(tok1->previous(), "const|volatile|final|override|&|&&|noexcept"))
                            tok1 = tok1->previous();
                        if (tok1->strAt(-1) != ")")
                            return;
                        tok1 = tok1->linkAt(-1);
                    } else {
                        while (Token::Match(tok1->tokAt(-2), ":|, %name%")) {
                            tok1 = tok1->tokAt(-2);
                            if (tok1->strAt(-1) != ")" && tok1->strAt(-1) != "}")
                                return;
                            tok1 = tok1->linkAt(-1);
                        }
                    }
                    if (tok1->strAt(-1) == ">")
                        tok1 = tok1->previous()->findOpeningBracket();
                    if (tok1 && (Token::Match(tok1->tokAt(-3), "%name% :: %name%") ||
                                 Token::Match(tok1->tokAt(-4), "%name% :: ~ %name%"))) {
                        tok1 = tok1->tokAt(-2);
                        if (tok1->str() == "~")
                            tok1 = tok1->previous();
                        std::string scope = tok1->strAt(-1);
                        while (Token::Match(tok1->tokAt(-2), ":: %name%")) {
                            scope = tok1->strAt(-3) + " :: " + scope;
                            tok1 = tok1->tokAt(-2);
                        }
                        *scopeInfo = (*scopeInfo)->addChild(ScopeInfo3::MemberFunction, scope, tok, tok->link());
                        added = true;
                    }
                }

                if (!added)
                    *scopeInfo = (*scopeInfo)->addChild(ScopeInfo3::Other, "", tok, tok->link());
            }
            return;
        }

        const bool record = Token::Match(tok, "class|struct|union %name%");
        tok = tok->next();
        std::string classname = tok->str();
        while (Token::Match(tok, "%name% :: %name%")) {
            tok = tok->tokAt(2);
            classname += " :: " + tok->str();
        }

        // add record type to scope info
        if (record)
            (*scopeInfo)->recordTypes.insert(classname);
        tok = tok->next();

        // skip template parameters
        if (tok && tok->str() == "<") {
            tok = tok->findClosingBracket();
            if (tok)
                tok = tok->next();
        }

        // get base class types
        std::set<std::string> baseTypes;
        if (tok && tok->str() == ":") {
            do {
                tok = tok->next();
                while (Token::Match(tok, "public|protected|private|virtual"))
                    tok = tok->next();
                std::string base;
                while (tok && !Token::Match(tok, ";|,|{")) {
                    if (!base.empty())
                        base += ' ';
                    base += tok->str();
                    tok = tok->next();
                    // skip template parameters
                    if (tok && tok->str() == "<") {
                        tok = tok->findClosingBracket();
                        if (tok)
                            tok = tok->next();
                    }
                }
                baseTypes.insert(base);
            } while (tok && !Token::Match(tok, ";|{"));
        }

        if (tok && tok->str() == "{") {
            *scopeInfo = (*scopeInfo)->addChild(record ? ScopeInfo3::Record : ScopeInfo3::Namespace, classname, tok, tok->link());
            (*scopeInfo)->baseTypes = baseTypes;
        }
    }

    Token *findSemicolon(Token *tok)
    {
        int level = 0;

        for (; tok && (level > 0 || tok->str() != ";"); tok = tok->next()) {
            if (tok->str() == "{")
                ++level;
            else if (level > 0 && tok->str() == "}")
                --level;
        }

        return tok;
    }

    bool usingMatch(
        const Token *nameToken,
        const std::string &scope,
        Token **tok,
        const std::string &scope1,
        const ScopeInfo3 *currentScope,
        const ScopeInfo3 *memberClassScope)
    {
        Token *tok1 = *tok;

        if (tok1 && tok1->str() != nameToken->str())
            return false;

        // skip this using
        if (tok1 == nameToken) {
            *tok = findSemicolon(tok1);
            return false;
        }

        // skip other using with this name
        if (tok1->strAt(-1) == "using") {
            // fixme: this is wrong
            // skip to end of scope
            if (currentScope->bodyEnd)
                *tok = currentScope->bodyEnd->previous();
            return false;
        }

        if (Token::Match(tok1->tokAt(-1), "class|struct|union|enum|namespace")) {
            // fixme
            return false;
        }

        // get qualification
        std::string qualification;
        const Token* tok2 = tok1;
        std::string::size_type index = scope.size();
        std::string::size_type new_index = std::string::npos;
        bool match = true;
        while (Token::Match(tok2->tokAt(-2), "%name% ::") && !tok2->tokAt(-2)->isKeyword()) {
            std::string last;
            if (match && !scope1.empty()) {
                new_index = scope1.rfind(' ', index - 1);
                if (new_index != std::string::npos)
                    last = scope1.substr(new_index, index - new_index);
                else if (!qualification.empty())
                    last.clear();
                else
                    last = scope1;
            } else
                match = false;
            if (match && tok2->strAt(-2) == last)
                index = new_index;
            else {
                if (!qualification.empty())
                    qualification = " :: " + qualification;
                qualification = tok2->strAt(-2) + qualification;
            }
            tok2 = tok2->tokAt(-2);
        }

        std::string fullScope1 = scope1;
        if (!scope1.empty() && !qualification.empty())
            fullScope1 += " :: ";
        fullScope1 += qualification;

        if (scope == fullScope1)
            return true;

        const ScopeInfo3 *scopeInfo = memberClassScope ? memberClassScope : currentScope;

        // check in base types
        if (scopeInfo->findTypeInBase(scope))
            return true;

        // check using namespace
        const ScopeInfo3 * tempScope = scopeInfo;
        while (tempScope) {
            //if (!tempScope->parent->usingNamespaces.empty()) {
            if (!tempScope->usingNamespaces.empty()) {
                if (qualification.empty()) {
                    if (tempScope->usingNamespaces.find(scope) != tempScope->usingNamespaces.end())
                        return true;
                } else {
                    for (auto ns : tempScope->usingNamespaces) {
                        if (scope == ns + " :: " + qualification)
                            return true;
                    }
                }
            }
            tempScope = tempScope->parent;
        }

        std::string newScope1 = scope1;

        // scopes didn't match so try higher scopes
        index = newScope1.size();
        while (!newScope1.empty()) {
            std::string::size_type separator = newScope1.rfind(" :: ", index - 1);
            if (separator != std::string::npos)
                newScope1 = newScope1.substr(0, separator);
            else
                newScope1.clear();

            std::string newFullScope1 = newScope1;
            if (!newScope1.empty() && !qualification.empty())
                newFullScope1 += " :: ";
            newFullScope1 += qualification;

            if (scope == newFullScope1)
                return true;
        }

        return false;
    }

    std::string memberFunctionScope(const Token *tok)
    {
        std::string qualification;
        const Token *qualTok = tok->strAt(-2) == "~" ? tok->tokAt(-4) : tok->tokAt(-3);
        while (Token::Match(qualTok, "%type% ::")) {
            if (!qualification.empty())
                qualification = " :: " + qualification;
            qualification = qualTok->str() + qualification;
            qualTok = qualTok->tokAt(-2);
        }
        return qualification;
    }

    const Token * memberFunctionEnd(const Token *tok)
    {
        if (tok->str() != "(")
            return nullptr;
        const Token *end = tok->link()->next();
        while (end) {
            if (end->str() == "{" && !Token::Match(end->tokAt(-2), ":|, %name%"))
                return end;
            else if (end->str() == ";")
                break;
            end = end->next();
        }
        return nullptr;
    }
} // namespace

bool Tokenizer::isMemberFunction(const Token *openParen) const
{
    return (Token::Match(openParen->tokAt(-2), ":: %name% (") ||
            Token::Match(openParen->tokAt(-3), ":: ~ %name% (")) &&
           isFunctionHead(openParen, "{|:");
}

static bool scopesMatch(const std::string &scope1, const std::string &scope2, const ScopeInfo3 *globalScope)
{
    if (scope1.empty() || scope2.empty())
        return false;

    // check if scopes match
    if (scope1 == scope2)
        return true;

    // check if scopes only differ by global qualification
    if (scope1 == (":: " + scope2)) {
        std::string::size_type end = scope2.find_first_of(' ');
        if (end == std::string::npos)
            end = scope2.size();
        if (globalScope->hasChild(scope2.substr(0, end)))
            return true;
    } else if (scope2 == (":: " + scope1)) {
        std::string::size_type end = scope1.find_first_of(' ');
        if (end == std::string::npos)
            end = scope1.size();
        if (globalScope->hasChild(scope1.substr(0, end)))
            return true;
    }

    return false;
}

bool Tokenizer::simplifyUsing()
{
    if (!isCPP() || mSettings->standards.cpp < Standards::CPP11)
        return false;

    bool substitute = false;
    ScopeInfo3 scopeInfo;
    ScopeInfo3 *currentScope = &scopeInfo;
    struct Using {
        Using(Token *start, Token *end) : startTok(start), endTok(end) {}
        Token *startTok;
        Token *endTok;
    };
    std::list<Using> usingList;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (mErrorLogger && !list.getFiles().empty())
            mErrorLogger->reportProgress(list.getFiles()[0], "Tokenize (using)", tok->progressValue());

        if (Settings::terminated())
            return substitute;

        if (Token::Match(tok, "enum class|struct")) {
            Token *bodyStart = tok;
            while (Token::Match(bodyStart, "%name%|:|::|<")) {
                if (bodyStart->str() == "<")
                    bodyStart = bodyStart->findClosingBracket();
                bodyStart = bodyStart ? bodyStart->next() : nullptr;
            }
            if (Token::simpleMatch(bodyStart, "{"))
                tok = bodyStart->link();
            continue;
        }

        if (Token::Match(tok, "{|}|namespace|class|struct|union") ||
            Token::Match(tok, "using namespace %name% ;|::")) {
            try {
                setScopeInfo(tok, &currentScope, mSettings->debugwarnings);
            } catch (const std::runtime_error &) {
                reportError(tok, Severity::debug, "simplifyUsingUnmatchedBodyEnd",
                            "simplifyUsing: unmatched body end");
            }
            continue;
        }

        // skip template declarations
        if (Token::Match(tok, "template < !!>")) {
            // add template record type to scope info
            const Token *end = tok->next()->findClosingBracket();
            if (end && Token::Match(end->next(), "class|struct|union %name%"))
                currentScope->recordTypes.insert(end->strAt(2));

            Token *declEndToken = TemplateSimplifier::findTemplateDeclarationEnd(tok);
            if (declEndToken)
                tok = declEndToken;
            continue;
        }

        // look for non-template type aliases
        if (!(tok->strAt(-1) != ">" &&
              (Token::Match(tok, "using %name% = ::| %name%") ||
               (Token::Match(tok, "using %name% [ [") &&
                Token::Match(tok->linkAt(2), "] ] = ::| %name%")))))
            continue;

        std::string name = tok->strAt(1);
        const Token *nameToken = tok->next();
        std::string scope = currentScope->fullName;
        Token *usingStart = tok;
        Token *start;
        if (tok->strAt(2) == "=")
            start = tok->tokAt(3);
        else
            start = tok->linkAt(2)->tokAt(3);
        Token *usingEnd = findSemicolon(start);
        if (!usingEnd)
            continue;

        // Move struct defined in using out of using.
        // using T = struct t { }; => struct t { }; using T = struct t;
        // fixme: this doesn't handle attributes
        if (Token::Match(start, "class|struct|union|enum %name%| {|:")) {
            Token *structEnd = start->tokAt(1);
            const bool hasName = Token::Match(structEnd, "%name%");

            // skip over name if present
            if (hasName)
                structEnd = structEnd->next();

            // skip over base class information
            if (structEnd->str() == ":") {
                structEnd = structEnd->next(); // skip over ":"
                while (structEnd && structEnd->str() != "{")
                    structEnd = structEnd->next();
                if (!structEnd)
                    continue;
            }

            // use link to go to end
            structEnd = structEnd->link();

            // add ';' after end of struct
            structEnd->insertToken(";", "");

            // add name for anonymous struct
            if (!hasName) {
                std::string newName;
                if (structEnd->strAt(2) == ";")
                    newName = name;
                else
                    newName = "Unnamed" + MathLib::toString(mUnnamedCount++);
                TokenList::copyTokens(structEnd->next(), tok, start);
                structEnd->tokAt(5)->insertToken(newName, "");
                start->insertToken(newName, "");
            } else
                TokenList::copyTokens(structEnd->next(), tok, start->next());

            // add using after end of struct
            usingStart = structEnd->tokAt(2);
            nameToken = usingStart->next();
            if (usingStart->strAt(2) == "=")
                start = usingStart->tokAt(3);
            else
                start = usingStart->linkAt(2)->tokAt(3);
            usingEnd = findSemicolon(start);

            // delete original using before struct
            tok->deleteThis();
            tok->deleteThis();
            tok->deleteThis();
            tok = usingStart;
        }

        // remove 'typename' and 'template'
        else if (start->str() == "typename") {
            start->deleteThis();
            Token *temp = start;
            while (Token::Match(temp, "%name% ::"))
                temp = temp->tokAt(2);
            if (Token::Match(temp, "template %name%"))
                temp->deleteThis();
        }

        if (usingEnd)
            tok = usingEnd;

        // Unfortunately we have to start searching from the beginning
        // of the token stream because templates are instantiated at
        // the end of the token stream and it may be used before then.
        ScopeInfo3 scopeInfo1;
        ScopeInfo3 *currentScope1 = &scopeInfo1;
        Token *startToken = list.front();
        Token *endToken = nullptr;
        bool inMemberFunc = false;
        const ScopeInfo3 * memberFuncScope = nullptr;
        const Token * memberFuncEnd = nullptr;

        // We can limit the search to the current function when the type alias
        // is defined in that function.
        if (currentScope->type == ScopeInfo3::Other ||
            currentScope->type == ScopeInfo3::MemberFunction) {
            scopeInfo1 = scopeInfo;
            currentScope1 = scopeInfo1.findScope(currentScope);
            if (!currentScope1)
                return substitute; // something bad happened
            startToken = usingEnd->next();
            endToken = currentScope->bodyEnd->next();
            if (currentScope->type == ScopeInfo3::MemberFunction) {
                const ScopeInfo3 * temp = currentScope->findScope(currentScope->fullName);
                if (temp) {
                    inMemberFunc = true;
                    memberFuncScope = temp;
                    memberFuncEnd = endToken;
                }
            }
        }

        std::string scope1 = currentScope1->fullName;
        bool skip = false; // don't erase type aliases we can't parse
        Token *enumOpenBrace = nullptr;
        for (Token* tok1 = startToken; !skip && tok1 && tok1 != endToken; tok1 = tok1->next()) {
            // skip enum body
            if (tok1 && tok1 == enumOpenBrace) {
                tok1 = tok1->link();
                enumOpenBrace = nullptr;
                continue;
            }

            if ((Token::Match(tok1, "{|}|namespace|class|struct|union") && tok1->strAt(-1) != "using") ||
                Token::Match(tok1, "using namespace %name% ;|::")) {
                try {
                    setScopeInfo(tok1, &currentScope1, mSettings->debugwarnings);
                } catch (const std::runtime_error &) {
                    reportError(tok1, Severity::debug, "simplifyUsingUnmatchedBodyEnd",
                                "simplifyUsing: unmatched body end");
                }
                scope1 = currentScope1->fullName;
                if (inMemberFunc && memberFuncEnd && tok1 == memberFuncEnd) {
                    inMemberFunc = false;
                    memberFuncScope = nullptr;
                    memberFuncEnd = nullptr;
                }
                continue;
            }

            // skip template definitions
            if (Token::Match(tok1, "template < !!>")) {
                Token *declEndToken = TemplateSimplifier::findTemplateDeclarationEnd(tok1);
                if (declEndToken)
                    tok1 = declEndToken;
                continue;
            }

            // check for enum with body
            if (tok1->str() == "enum") {
                if (Token::Match(tok1, "enum class|struct"))
                    tok1 = tok1->next();
                Token *defStart = tok1;
                while (Token::Match(defStart, "%name%|::|:"))
                    defStart = defStart->next();
                if (Token::simpleMatch(defStart, "{"))
                    enumOpenBrace = defStart;
                continue;
            }

            // check for member function and adjust scope
            if (isMemberFunction(tok1)) {
                if (!scope1.empty())
                    scope1 += " :: ";
                scope1 += memberFunctionScope(tok1);
                const ScopeInfo3 * temp = currentScope1->findScope(scope1);
                if (temp) {
                    const Token *end = memberFunctionEnd(tok1);
                    if (end) {
                        inMemberFunc = true;
                        memberFuncScope = temp;
                        memberFuncEnd = end;
                    }
                }
                continue;
            } else if (inMemberFunc && memberFuncScope) {
                if (!usingMatch(nameToken, scope, &tok1, scope1, currentScope1, memberFuncScope))
                    continue;
            } else if (!usingMatch(nameToken, scope, &tok1, scope1, currentScope1, nullptr))
                continue;

            // remove the qualification
            std::string fullScope = scope;
            std::string removed;
            while (Token::Match(tok1->tokAt(-2), "%name% ::") && !tok1->tokAt(-2)->isKeyword()) {
                removed = (tok1->strAt(-2) + " :: ") + removed;
                if (fullScope == tok1->strAt(-2)) {
                    tok1->deletePrevious();
                    tok1->deletePrevious();
                    break;
                } else {
                    const std::string::size_type idx = fullScope.rfind(" ");

                    if (idx == std::string::npos)
                        break;

                    if (tok1->strAt(-2) == fullScope.substr(idx + 1)) {
                        tok1->deletePrevious();
                        tok1->deletePrevious();
                        fullScope.resize(idx - 3);
                    } else
                        break;
                }
            }

            // remove global namespace if present
            if (tok1->strAt(-1) == "::") {
                removed.insert(0, ":: ");
                tok1->deletePrevious();
            }

            Token * arrayStart = nullptr;

            // parse the type
            Token *type = start;
            if (type->str() == "::") {
                type = type->next();
                while (Token::Match(type, "%type% ::"))
                    type = type->tokAt(2);
                if (Token::Match(type, "%type%"))
                    type = type->next();
            } else if (Token::Match(type, "%type% ::")) {
                do {
                    type = type->tokAt(2);
                } while (Token::Match(type, "%type% ::"));
                if (Token::Match(type, "%type%"))
                    type = type->next();
            } else if (Token::Match(type, "%type%")) {
                while (Token::Match(type, "const|class|struct|union|enum %type%") ||
                       (type->next() && type->next()->isStandardType()))
                    type = type->next();

                type = type->next();

                while (Token::Match(type, "%type%") &&
                       (type->isStandardType() || Token::Match(type, "unsigned|signed"))) {
                    type = type->next();
                }

                bool atEnd = false;
                while (!atEnd) {
                    if (type && type->str() == "::") {
                        type = type->next();
                    }

                    if (Token::Match(type, "%type%") &&
                        type->next() && !Token::Match(type->next(), "[|,|(")) {
                        type = type->next();
                    } else if (Token::simpleMatch(type, "const (")) {
                        type = type->next();
                        atEnd = true;
                    } else
                        atEnd = true;
                }
            } else
                syntaxError(type);

            // check for invalid input
            if (!type)
                syntaxError(tok1);

            // check for template
            if (type->str() == "<") {
                type = type->findClosingBracket();

                while (type && Token::Match(type->next(), ":: %type%"))
                    type = type->tokAt(2);

                if (!type) {
                    syntaxError(tok1);
                }

                while (Token::Match(type->next(), "const|volatile"))
                    type = type->next();

                type = type->next();
            }

            // check for pointers and references
            std::list<std::string> pointers;
            while (Token::Match(type, "*|&|&&|const")) {
                pointers.push_back(type->str());
                type = type->next();
            }

            // check for array
            if (type && type->str() == "[") {
                do {
                    if (!arrayStart)
                        arrayStart = type;

                    bool atEnd = false;
                    while (!atEnd) {
                        while (type->next() && !Token::Match(type->next(), ";|,")) {
                            type = type->next();
                        }

                        if (!type->next())
                            syntaxError(type); // invalid input
                        else if (type->next()->str() == ";")
                            atEnd = true;
                        else if (type->str() == "]")
                            atEnd = true;
                        else
                            type = type->next();
                    }

                    type = type->next();
                } while (type && type->str() == "[");
            }

            // make sure we are in a good state
            if (!tok1 || !tok1->next())
                break; // bail

            Token* after = tok1->next();
            // check if type was parsed
            if (type && type == usingEnd) {
                // check for array syntax and add type around variable
                if (arrayStart) {
                    if (Token::Match(tok1->next(), "%name%")) {
                        TokenList::copyTokens(tok1->next(), arrayStart, usingEnd->previous());
                        TokenList::copyTokens(tok1, start, arrayStart->previous());
                        tok1->deleteThis();
                        substitute = true;
                    }
                } else {
                    // add some qualification back if needed
                    std::string removed1 = removed;
                    std::string::size_type idx = removed1.rfind(" ::");
                    if (idx != std::string::npos)
                        removed1.resize(idx);
                    if (scopesMatch(removed1, scope, &scopeInfo1)) {
                        ScopeInfo3 * tempScope = currentScope;
                        while (tempScope->parent) {
                            if (tempScope->recordTypes.find(start->str()) != tempScope->recordTypes.end()) {
                                std::string::size_type spaceIdx = 0;
                                std::string::size_type startIdx = 0;
                                while ((spaceIdx = removed1.find(" ", startIdx)) != std::string::npos) {
                                    tok1->previous()->insertToken(removed1.substr(startIdx, spaceIdx - startIdx));
                                    startIdx = spaceIdx + 1;
                                }
                                tok1->previous()->insertToken(removed1.substr(startIdx));
                                tok1->previous()->insertToken("::");
                                break;
                            }
                            idx = removed1.rfind(" ::");
                            if (idx == std::string::npos)
                                break;

                            removed1.resize(idx);
                            tempScope = tempScope->parent;
                        }
                    }

                    // just replace simple type aliases
                    TokenList::copyTokens(tok1, start, usingEnd->previous());
                    tok1->deleteThis();
                    substitute = true;
                }
            } else {
                skip = true;
                if (mSettings->debugwarnings && mErrorLogger) {
                    std::string str;
                    for (Token *tok3 = usingStart; tok3 && tok3 != usingEnd; tok3 = tok3->next()) {
                        if (!str.empty())
                            str += ' ';
                        str += tok3->str();
                    }
                    str += " ;";
                    std::list<const Token *> callstack(1, usingStart);
                    mErrorLogger->reportErr(ErrorMessage(callstack, &list, Severity::debug, "simplifyUsing",
                                                         "Failed to parse \'" + str + "\'. The checking continues anyway.", Certainty::normal));
                }
            }
            tok1 = after;
        }

        if (!skip)
            usingList.emplace_back(usingStart, usingEnd);
    }

    // delete all used type alias definitions
    for (std::list<Using>::reverse_iterator it = usingList.rbegin(); it != usingList.rend(); ++it) {
        Token *usingStart = it->startTok;
        Token *usingEnd = it->endTok;
        if (usingStart->previous()) {
            if (usingEnd->next())
                Token::eraseTokens(usingStart->previous(), usingEnd->next());
            else {
                Token::eraseTokens(usingStart->previous(), usingEnd);
                usingEnd->deleteThis();
            }
        } else {
            if (usingEnd->next()) {
                Token::eraseTokens(usingStart, usingEnd->next());
                usingStart->deleteThis();
            } else {
                // this is the only code being checked so leave ';'
                Token::eraseTokens(usingStart, usingEnd);
                usingStart->deleteThis();
            }
        }
    }

    return substitute;
}
void Tokenizer::simplifyMulAndParens()
{
    if (!list.front())
        return;
    for (Token *tok = list.front()->tokAt(3); tok; tok = tok->next()) {
        if (!tok->isName())
            continue;
        //fix ticket #2784 - improved by ticket #3184
        int closedPars = 0;
        Token *tokend = tok->next();
        Token *tokbegin = tok->previous();
        while (tokend && tokend->str() == ")") {
            ++closedPars;
            tokend = tokend->next();
        }
        if (!tokend || !(tokend->isAssignmentOp()))
            continue;
        while (Token::Match(tokbegin, "&|(")) {
            if (tokbegin->str() == "&") {
                if (Token::Match(tokbegin->tokAt(-2), "[;{}&(] *")) {
                    //remove '* &'
                    tokbegin = tokbegin->tokAt(-2);
                    tokbegin->deleteNext(2);
                } else if (Token::Match(tokbegin->tokAt(-3), "[;{}&(] * (")) {
                    if (closedPars == 0)
                        break;
                    --closedPars;
                    //remove ')'
                    tok->deleteNext();
                    //remove '* ( &'
                    tokbegin = tokbegin->tokAt(-3);
                    tokbegin->deleteNext(3);
                } else
                    break;
            } else if (tokbegin->str() == "(") {
                if (closedPars == 0)
                    break;

                //find consecutive opening parentheses
                int openPars = 0;
                while (tokbegin && tokbegin->str() == "(" && openPars <= closedPars) {
                    ++openPars;
                    tokbegin = tokbegin->previous();
                }
                if (!tokbegin || openPars > closedPars)
                    break;

                if ((openPars == closedPars && Token::Match(tokbegin, "[;{}]")) ||
                    Token::Match(tokbegin->tokAt(-2), "[;{}&(] * &") ||
                    Token::Match(tokbegin->tokAt(-3), "[;{}&(] * ( &")) {
                    //remove the excessive parentheses around the variable
                    while (openPars > 0) {
                        tok->deleteNext();
                        tokbegin->deleteNext();
                        --closedPars;
                        --openPars;
                    }
                } else
                    break;
            }
        }
    }
}

bool Tokenizer::createTokens(std::istream &code,
                             const std::string& FileName)
{
    // make sure settings specified
    assert(mSettings);

    return list.createTokens(code, FileName);
}

void Tokenizer::createTokens(simplecpp::TokenList&& tokenList)
{
    // make sure settings specified
    assert(mSettings);
    list.createTokens(std::move(tokenList));
}

bool Tokenizer::simplifyTokens1(const std::string &configuration)
{
    // Fill the map mTypeSize..
    fillTypeSizes();

    mConfiguration = configuration;

    if (!simplifyTokenList1(list.getFiles().front().c_str()))
        return false;

    if (mTimerResults) {
        Timer t("Tokenizer::simplifyTokens1::createAst", mSettings->showtime, mTimerResults);
        list.createAst();
        list.validateAst();
    } else {
        list.createAst();
        list.validateAst();
    }

    if (mTimerResults) {
        Timer t("Tokenizer::simplifyTokens1::createSymbolDatabase", mSettings->showtime, mTimerResults);
        createSymbolDatabase();
    } else {
        createSymbolDatabase();
    }

    if (mTimerResults) {
        Timer t("Tokenizer::simplifyTokens1::setValueType", mSettings->showtime, mTimerResults);
        mSymbolDatabase->setValueTypeInTokenList(true);
    } else {
        mSymbolDatabase->setValueTypeInTokenList(true);
    }

    if (!mSettings->buildDir.empty())
        Summaries::create(this, configuration);

    // TODO: do not run valueflow if no checks are being performed at all - e.g. unusedFunctions only
    const char* disableValueflowEnv = std::getenv("DISABLE_VALUEFLOW");
    const bool doValueFlow = !disableValueflowEnv || (std::strcmp(disableValueflowEnv, "1") != 0);

    if (doValueFlow) {
        if (mTimerResults) {
            Timer t("Tokenizer::simplifyTokens1::ValueFlow", mSettings->showtime, mTimerResults);
            ValueFlow::setValues(&list, mSymbolDatabase, mErrorLogger, mSettings);
        } else {
            ValueFlow::setValues(&list, mSymbolDatabase, mErrorLogger, mSettings);
        }
    }

    // Warn about unhandled character literals
    if (mSettings->severity.isEnabled(Severity::portability)) {
        for (const Token *tok = tokens(); tok; tok = tok->next()) {
            if (tok->tokType() == Token::eChar && tok->values().empty()) {
                try {
                    simplecpp::characterLiteralToLL(tok->str());
                } catch (const std::exception &e) {
                    unhandledCharLiteral(tok, e.what());
                }
            }
        }
    }

    if (doValueFlow) {
        mSymbolDatabase->setArrayDimensionsUsingValueFlow();
    }

    printDebugOutput(1);

    return true;
}

bool Tokenizer::tokenize(std::istream &code,
                         const char FileName[],
                         const std::string &configuration)
{
    if (!createTokens(code, FileName))
        return false;

    return simplifyTokens1(configuration);
}
//---------------------------------------------------------------------------

void Tokenizer::findComplicatedSyntaxErrorsInTemplates()
{
    validate();
    mTemplateSimplifier->checkComplicatedSyntaxErrorsInTemplates();
}

void Tokenizer::checkForEnumsWithTypedef()
{
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "enum %name% {")) {
            tok = tok->tokAt(2);
            const Token *tok2 = Token::findsimplematch(tok, "typedef", tok->link());
            if (tok2)
                syntaxError(tok2);
            tok = tok->link();
        }
    }
}

void Tokenizer::fillTypeSizes()
{
    mTypeSize.clear();
    mTypeSize["char"] = 1;
    mTypeSize["_Bool"] = mSettings->sizeof_bool;
    mTypeSize["bool"] = mSettings->sizeof_bool;
    mTypeSize["short"] = mSettings->sizeof_short;
    mTypeSize["int"] = mSettings->sizeof_int;
    mTypeSize["long"] = mSettings->sizeof_long;
    mTypeSize["float"] = mSettings->sizeof_float;
    mTypeSize["double"] = mSettings->sizeof_double;
    mTypeSize["wchar_t"] = mSettings->sizeof_wchar_t;
    mTypeSize["size_t"] = mSettings->sizeof_size_t;
    mTypeSize["*"] = mSettings->sizeof_pointer;
}

void Tokenizer::combineOperators()
{
    const bool cpp = isCPP();

    // Combine tokens..
    for (Token *tok = list.front(); tok && tok->next(); tok = tok->next()) {
        const char c1 = tok->str()[0];

        if (tok->str().length() == 1 && tok->next()->str().length() == 1) {
            const char c2 = tok->next()->str()[0];

            // combine +-*/ and =
            if (c2 == '=' && (std::strchr("+-*/%|^=!<>", c1))) {
                // skip templates
                if (cpp && (tok->str() == ">" || Token::simpleMatch(tok->previous(), "> *"))) {
                    const Token* opening =
                        tok->str() == ">" ? tok->findOpeningBracket() : tok->previous()->findOpeningBracket();
                    if (opening && Token::Match(opening->previous(), "%name%"))
                        continue;
                }
                tok->str(tok->str() + c2);
                tok->deleteNext();
                continue;
            }
        } else if (tok->next()->str() == "=") {
            if (tok->str() == ">>") {
                tok->str(">>=");
                tok->deleteNext();
            } else if (tok->str() == "<<") {
                tok->str("<<=");
                tok->deleteNext();
            }
        } else if (cpp && (c1 == 'p' || c1 == '_') &&
                   Token::Match(tok, "private|protected|public|__published : !!:")) {
            bool simplify = false;
            int par = 0;
            for (const Token *prev = tok->previous(); prev; prev = prev->previous()) {
                if (prev->str() == ")") {
                    ++par;
                } else if (prev->str() == "(") {
                    if (par == 0U)
                        break;
                    --par;
                }
                if (par != 0U || prev->str() == "(")
                    continue;
                if (Token::Match(prev, "[;{}]")) {
                    simplify = true;
                    break;
                }
                if (prev->isName() && prev->isUpperCaseName())
                    continue;
                if (prev->isName() && endsWith(prev->str(), ':'))
                    simplify = true;
                break;
            }
            if (simplify) {
                tok->str(tok->str() + ":");
                tok->deleteNext();
            }
        } else if (tok->str() == "->") {
            // If the preceding sequence is "( & %name% )", replace it by "%name%"
            Token *t = tok->tokAt(-4);
            if (Token::Match(t, "( & %name% )") && !Token::simpleMatch(t->previous(), ">")) {
                t->deleteThis();
                t->deleteThis();
                t->deleteNext();
                tok->str(".");
            } else {
                tok->str(".");
                tok->originalName("->");
            }
        }
    }
}

void Tokenizer::combineStringAndCharLiterals()
{
    // Combine strings
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!isStringLiteral(tok->str()))
            continue;

        tok->str(simplifyString(tok->str()));

        while (Token::Match(tok->next(), "%str%") || Token::Match(tok->next(), "_T|_TEXT|TEXT ( %str% )")) {
            if (tok->next()->isName()) {
                if (!mSettings->isWindowsPlatform())
                    break;
                tok->deleteNext(2);
                tok->next()->deleteNext();
            }
            // Two strings after each other, combine them
            tok->concatStr(simplifyString(tok->next()->str()));
            tok->deleteNext();
        }
    }
}

void Tokenizer::concatenateNegativeNumberAndAnyPositive()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "?|:|,|(|[|{|return|case|sizeof|%op% +|-") || tok->tokType() == Token::eIncDecOp)
            continue;

        while (tok->str() != ">" && tok->next() && tok->next()->str() == "+")
            tok->deleteNext();

        if (Token::Match(tok->next(), "- %num%")) {
            tok->deleteNext();
            tok->next()->str("-" + tok->next()->str());
        }
    }
}

void Tokenizer::simplifyExternC()
{
    if (isC())
        return;

    // Add attributes to all tokens within `extern "C"` inlines and blocks, and remove the `extern "C"` tokens.
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::simpleMatch(tok, "extern \"C\"")) {
            Token *tok2 = tok->next();
            if (tok->strAt(2) == "{") {
                tok2 = tok2->next(); // skip {
                while ((tok2 = tok2->next()) && tok2 != tok->linkAt(2))
                    tok2->isExternC(true);
                tok->linkAt(2)->deleteThis(); // }
                tok->deleteNext(2); // "C" {
            } else {
                while ((tok2 = tok2->next()) && !Token::simpleMatch(tok2, ";"))
                    tok2->isExternC(true);
                tok->deleteNext(); // "C"
            }
            tok->deleteThis(); // extern
        }
    }
}

void Tokenizer::simplifyRoundCurlyParentheses()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (Token::Match(tok, "[;{}:] ( {") &&
               Token::simpleMatch(tok->linkAt(2), "} ) ;")) {
            if (tok->str() == ":" && !Token::Match(tok->tokAt(-2),"[;{}] %type% :"))
                break;
            Token *end = tok->linkAt(2)->tokAt(-3);
            if (Token::Match(end, "[;{}] %num%|%str% ;"))
                end->deleteNext(2);
            tok->linkAt(2)->previous()->deleteNext(3);
            tok->deleteNext(2);
        }
        if (Token::Match(tok, "( { %bool%|%char%|%num%|%str%|%name% ; } )")) {
            tok->deleteNext();
            tok->deleteThis();
            tok->deleteNext(3);
        }
    }
}

void Tokenizer::simplifySQL()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::simpleMatch(tok, "__CPPCHECK_EMBEDDED_SQL_EXEC__ SQL"))
            continue;

        const Token *end = findSQLBlockEnd(tok);
        if (end == nullptr)
            syntaxError(nullptr);

        const std::string instruction = tok->stringifyList(end);
        // delete all tokens until the embedded SQL block end
        Token::eraseTokens(tok, end);

        // insert "asm ( "instruction" ) ;"
        tok->str("asm");
        // it can happen that 'end' is NULL when wrong code is inserted
        if (!tok->next())
            tok->insertToken(";");
        tok->insertToken(")");
        tok->insertToken("\"" + instruction + "\"");
        tok->insertToken("(");
        // jump to ';' and continue
        tok = tok->tokAt(3);
    }
}

void Tokenizer::simplifyArrayAccessSyntax()
{
    // 0[a] -> a[0]
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->isNumber() && Token::Match(tok, "%num% [ %name% ]")) {
            const std::string number(tok->str());
            Token* indexTok = tok->tokAt(2);
            tok->str(indexTok->str());
            tok->varId(indexTok->varId());
            indexTok->str(number);
        }
    }
}

void Tokenizer::simplifyRedundantConsecutiveBraces()
{
    // Remove redundant consecutive braces, i.e. '.. { { .. } } ..' -> '.. { .. } ..'.
    for (Token *tok = list.front(); tok;) {
        if (Token::simpleMatch(tok, "= {")) {
            tok = tok->linkAt(1);
        } else if (Token::simpleMatch(tok, "{ {") && Token::simpleMatch(tok->next()->link(), "} }")) {
            //remove internal parentheses
            tok->next()->link()->deleteThis();
            tok->deleteNext();
        } else
            tok = tok->next();
    }
}

void Tokenizer::simplifyDoublePlusAndDoubleMinus()
{
    // Convert - - into + and + - into -
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (tok->next()) {
            if (tok->str() == "+") {
                if (tok->next()->str()[0] == '-') {
                    tok = tok->next();
                    if (tok->str().size() == 1) {
                        tok = tok->previous();
                        tok->str("-");
                        tok->deleteNext();
                    } else if (tok->isNumber()) {
                        tok->str(tok->str().substr(1));
                        tok = tok->previous();
                        tok->str("-");
                    }
                    continue;
                }
            } else if (tok->str() == "-") {
                if (tok->next()->str()[0] == '-') {
                    tok = tok->next();
                    if (tok->str().size() == 1) {
                        tok = tok->previous();
                        tok->str("+");
                        tok->deleteNext();
                    } else if (tok->isNumber()) {
                        tok->str(tok->str().substr(1));
                        tok = tok->previous();
                        tok->str("+");
                    }
                    continue;
                }
            }

            break;
        }
    }
}

/** Specify array size if it hasn't been given */

void Tokenizer::arraySize()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName() || !Token::Match(tok, "%var% [ ] ="))
            continue;
        bool addlength = false;
        if (Token::Match(tok, "%var% [ ] = { %str% } ;")) {
            Token *t = tok->tokAt(3);
            t->deleteNext();
            t->next()->deleteNext();
            addlength = true;
        }

        if (addlength || Token::Match(tok, "%var% [ ] = %str% ;")) {
            tok = tok->next();
            const int sz = Token::getStrArraySize(tok->tokAt(3));
            tok->insertToken(MathLib::toString(sz));
            tok = tok->tokAt(5);
        }

        else if (Token::Match(tok, "%var% [ ] = {")) {
            MathLib::biguint sz = 1;
            tok = tok->next();
            Token *end = tok->linkAt(3);
            for (Token *tok2 = tok->tokAt(4); tok2 && tok2 != end; tok2 = tok2->next()) {
                if (tok2->link() && Token::Match(tok2, "{|(|[|<")) {
                    if (tok2->str() == "[" && tok2->link()->strAt(1) == "=") { // designated initializer
                        if (Token::Match(tok2, "[ %num% ]"))
                            sz = std::max(sz, MathLib::toULongNumber(tok2->strAt(1)) + 1U);
                        else {
                            sz = 0;
                            break;
                        }
                    }
                    tok2 = tok2->link();
                } else if (tok2->str() == ",") {
                    if (!Token::Match(tok2->next(), "[},]"))
                        ++sz;
                    else {
                        tok2 = tok2->previous();
                        tok2->deleteNext();
                    }
                }
            }

            if (sz != 0)
                tok->insertToken(MathLib::toString(sz));

            tok = end->next() ? end->next() : end;
        }
    }
}

static Token *skipTernaryOp(Token *tok)
{
    int colonLevel = 1;
    while (nullptr != (tok = tok->next())) {
        if (tok->str() == "?") {
            ++colonLevel;
        } else if (tok->str() == ":") {
            --colonLevel;
            if (colonLevel == 0) {
                tok = tok->next();
                break;
            }
        }
        if (tok->link() && Token::Match(tok, "[(<]"))
            tok = tok->link();
        else if (Token::Match(tok->next(), "[{};)]"))
            break;
    }
    if (colonLevel > 0) // Ticket #5214: Make sure the ':' matches the proper '?'
        return nullptr;
    return tok;
}

// Skips until the colon at the end of the case label, the argument must point to the "case" token.
// In case of success returns the colon token.
// In case of failure returns the token that caused the error.
static Token *skipCaseLabel(Token *tok)
{
    assert(tok->str() == "case");
    while (nullptr != (tok = tok->next())) {
        if (Token::Match(tok, "(|["))
            tok = tok->link();
        else if (tok->str() == "?") {
            Token * tok1 = skipTernaryOp(tok);
            if (!tok1)
                return tok;
            tok = tok1;
        }
        if (Token::Match(tok, "[:{};]"))
            return tok;
    }
    return nullptr;
}

const Token * Tokenizer::startOfExecutableScope(const Token * tok)
{
    if (tok->str() != ")")
        return nullptr;

    tok = isFunctionHead(tok, ":{", true);

    if (Token::Match(tok, ": %name% [({]")) {
        while (Token::Match(tok, "[:,] %name% [({]"))
            tok = tok->linkAt(2)->next();
    }

    return (tok && tok->str() == "{") ? tok : nullptr;
}


/** simplify labels and case|default in the code: add a ";" if not already in.*/

void Tokenizer::simplifyLabelsCaseDefault()
{
    const bool cpp = isCPP();
    bool executablescope = false;
    int indentLevel = 0;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // Simplify labels in the executable scope..
        Token *start = const_cast<Token *>(startOfExecutableScope(tok));
        if (start) {
            tok = start;
            executablescope = true;
        }

        if (!executablescope)
            continue;

        if (tok->str() == "{") {
            if (tok->previous()->str() == "=")
                tok = tok->link();
            else
                ++indentLevel;
        } else if (tok->str() == "}") {
            --indentLevel;
            if (indentLevel == 0) {
                executablescope = false;
                continue;
            }
        } else if (Token::Match(tok, "(|["))
            tok = tok->link();

        if (Token::Match(tok, "[;{}:] case")) {
            tok = skipCaseLabel(tok->next());
            if (!tok)
                break;
            if (tok->str() != ":" || tok->strAt(-1) == "case" || !tok->next())
                syntaxError(tok);
            if (tok->next()->str() != ";" && tok->next()->str() != "case")
                tok->insertToken(";");
            else
                tok = tok->previous();
        } else if (Token::Match(tok, "[;{}] %name% : !!;")) {
            if (!cpp || !Token::Match(tok->next(), "class|struct|enum")) {
                tok = tok->tokAt(2);
                tok->insertToken(";");
            }
        }
    }
}


void Tokenizer::simplifyCaseRange()
{
    for (Token* tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "case %num%|%char% ... %num%|%char% :")) {
            const MathLib::bigint start = MathLib::toLongNumber(tok->strAt(1));
            MathLib::bigint end = MathLib::toLongNumber(tok->strAt(3));
            end = std::min(start + 50, end); // Simplify it 50 times at maximum
            if (start < end) {
                tok = tok->tokAt(2);
                tok->str(":");
                tok->insertToken("case");
                for (MathLib::bigint i = end-1; i > start; i--) {
                    tok->insertToken(":");
                    tok->insertToken(MathLib::toString(i));
                    tok->insertToken("case");
                }
            }
        }
    }
}

void Tokenizer::calculateScopes()
{
    for (auto *tok = list.front(); tok; tok = tok->next())
        tok->scopeInfo(nullptr);

    std::string nextScopeNameAddition;
    std::shared_ptr<ScopeInfo2> primaryScope = std::make_shared<ScopeInfo2>("", nullptr);
    list.front()->scopeInfo(primaryScope);

    for (Token* tok = list.front(); tok; tok = tok->next()) {
        if (tok == list.front() || !tok->scopeInfo()) {
            if (tok != list.front())
                tok->scopeInfo(tok->previous()->scopeInfo());

            if (Token::Match(tok, "using namespace %name% ::|<|;")) {
                std::string usingNamespaceName;
                for (const Token* namespaceNameToken = tok->tokAt(2);
                     namespaceNameToken && namespaceNameToken->str() != ";";
                     namespaceNameToken = namespaceNameToken->next()) {
                    usingNamespaceName += namespaceNameToken->str();
                    usingNamespaceName += " ";
                }
                if (usingNamespaceName.length() > 0)
                    usingNamespaceName = usingNamespaceName.substr(0, usingNamespaceName.length() - 1);
                tok->scopeInfo()->usingNamespaces.insert(usingNamespaceName);
            } else if (Token::Match(tok, "namespace|class|struct|union %name% {|::|:|<")) {
                for (Token* nameTok = tok->next(); nameTok && !Token::Match(nameTok, "{|:"); nameTok = nameTok->next()) {
                    if (Token::Match(nameTok, ";|<")) {
                        nextScopeNameAddition = "";
                        break;
                    }
                    nextScopeNameAddition.append(nameTok->str());
                    nextScopeNameAddition.append(" ");
                }
                if (nextScopeNameAddition.length() > 0)
                    nextScopeNameAddition = nextScopeNameAddition.substr(0, nextScopeNameAddition.length() - 1);
            }

            if (Token::simpleMatch(tok, "{")) {
                // This might be the opening of a member function
                Token *tok1 = tok;
                while (Token::Match(tok1->previous(), "const|volatile|final|override|&|&&|noexcept"))
                    tok1 = tok1->previous();
                if (tok1->previous() && tok1->strAt(-1) == ")") {
                    bool member = true;
                    tok1 = tok1->linkAt(-1);
                    if (Token::Match(tok1->previous(), "throw|noexcept")) {
                        tok1 = tok1->previous();
                        while (Token::Match(tok1->previous(), "const|volatile|final|override|&|&&|noexcept"))
                            tok1 = tok1->previous();
                        if (tok1->strAt(-1) != ")")
                            member = false;
                    } else if (Token::Match(tok->tokAt(-2), ":|, %name%")) {
                        tok1 = tok1->tokAt(-2);
                        if (tok1->strAt(-1) != ")")
                            member = false;
                    }
                    if (member) {
                        if (tok1->strAt(-1) == ">")
                            tok1 = tok1->previous()->findOpeningBracket();
                        if (tok1 && Token::Match(tok1->tokAt(-3), "%name% :: %name%")) {
                            tok1 = tok1->tokAt(-2);
                            std::string scope = tok1->strAt(-1);
                            while (Token::Match(tok1->tokAt(-2), ":: %name%")) {
                                scope = tok1->strAt(-3) + " :: " + scope;
                                tok1 = tok1->tokAt(-2);
                            }

                            if (!nextScopeNameAddition.empty() && !scope.empty())
                                nextScopeNameAddition += " :: ";
                            nextScopeNameAddition += scope;
                        }
                    }
                }

                // New scope is opening, record it here
                std::shared_ptr<ScopeInfo2> newScopeInfo = std::make_shared<ScopeInfo2>(tok->scopeInfo()->name, tok->link(), tok->scopeInfo()->usingNamespaces);

                if (newScopeInfo->name != "" && nextScopeNameAddition != "")
                    newScopeInfo->name.append(" :: ");
                newScopeInfo->name.append(nextScopeNameAddition);
                nextScopeNameAddition = "";

                if (tok->link())
                    tok->link()->scopeInfo(tok->scopeInfo());
                tok->scopeInfo(newScopeInfo);
            }
        }
    }
}

void Tokenizer::simplifyTemplates()
{
    if (isC())
        return;

    mTemplateSimplifier->simplifyTemplates(
#ifdef MAXTIME
        mMaxTime,
#else
        0, // ignored
#endif
        mCodeWithTemplates);
}
//---------------------------------------------------------------------------


static bool setVarIdParseDeclaration(const Token **tok, const std::map<std::string,int> &variableId, bool executableScope, bool cpp, bool c)
{
    const Token *tok2 = *tok;
    if (!tok2->isName())
        return false;

    int typeCount = 0;
    int singleNameCount = 0;
    bool hasstruct = false;   // Is there a "struct" or "class"?
    bool bracket = false;
    bool ref = false;
    while (tok2) {
        if (tok2->isName()) {
            if (cpp && Token::Match(tok2, "namespace|public|private|protected"))
                return false;
            if (cpp && Token::simpleMatch(tok2, "decltype (")) {
                typeCount = 1;
                tok2 = tok2->linkAt(1)->next();
                continue;
            }
            if (Token::Match(tok2, "struct|union|enum") || (!c && Token::Match(tok2, "class|typename"))) {
                hasstruct = true;
                typeCount = 0;
                singleNameCount = 0;
            } else if (tok2->str() == "const") {
                // just skip "const"
            } else if (!hasstruct && variableId.find(tok2->str()) != variableId.end() && tok2->previous()->str() != "::") {
                ++typeCount;
                tok2 = tok2->next();
                if (!tok2 || tok2->str() != "::")
                    break;
            } else {
                if (tok2->str() != "void" || Token::Match(tok2, "void const| *|(")) // just "void" cannot be a variable type
                    ++typeCount;
                ++singleNameCount;
            }
        } else if (!c && ((TemplateSimplifier::templateParameters(tok2) > 0) ||
                          Token::simpleMatch(tok2, "< >") /* Ticket #4764 */)) {
            const Token *start = *tok;
            if (Token::Match(start->previous(), "%or%|%oror%|&&|&|^|+|-|*|/"))
                return false;
            const Token * const closingBracket = tok2->findClosingBracket();
            if (closingBracket == nullptr) { /* Ticket #8151 */
                throw tok2;
            }
            tok2 = closingBracket;
            if (tok2->str() != ">")
                break;
            singleNameCount = 1;
            if (Token::Match(tok2, "> %name% %or%|%oror%|&&|&|^|+|-|*|/") && !Token::Match(tok2, "> const [*&]"))
                return false;
            if (Token::Match(tok2, "> %name% )")) {
                if (Token::Match(tok2->linkAt(2)->previous(), "if|for|while ("))
                    return false;
                if (!Token::Match(tok2->linkAt(2)->previous(), "%name% ("))
                    return false;
            }
        } else if (Token::Match(tok2, "&|&&")) {
            ref = !bracket;
        } else if (singleNameCount >= 1 && Token::Match(tok2, "( [*&]") && Token::Match(tok2->link()->next(), "(|[")) {
            bracket = true; // Skip: Seems to be valid pointer to array or function pointer
        } else if (singleNameCount >= 1 && Token::Match(tok2, "( * %name% [") && Token::Match(tok2->linkAt(3), "] ) [;,]")) {
            bracket = true;
        } else if (tok2->str() == "::") {
            singleNameCount = 0;
        } else if (tok2->str() != "*" && tok2->str() != "::" && tok2->str() != "...") {
            break;
        }
        tok2 = tok2->next();
    }

    if (tok2) {
        bool isLambdaArg = false;
        {
            const Token *tok3 = (*tok)->previous();
            if (tok3 && tok3->str() == ",") {
                while (tok3 && !Token::Match(tok3,";|(|[|{")) {
                    if (Token::Match(tok3, ")|]"))
                        tok3 = tok3->link();
                    tok3 = tok3->previous();
                }

                if (tok3 && executableScope && Token::Match(tok3->previous(), "%name% (")) {
                    const Token *fdecl = tok3->previous();
                    int count = 0;
                    while (Token::Match(fdecl, "%name%|*")) {
                        fdecl = fdecl->previous();
                        count++;
                    }
                    if (!Token::Match(fdecl, "[;{}] %name%") || count <= 1)
                        return false;
                }
            }

            if (cpp && tok3 && Token::simpleMatch(tok3->previous(), "] (") && Token::simpleMatch(tok3->link(), ") {"))
                isLambdaArg = true;
        }


        *tok = tok2;

        // In executable scopes, references must be assigned
        // Catching by reference is an exception
        if (executableScope && ref && !isLambdaArg) {
            if (Token::Match(tok2, "(|=|{|:"))
                ;   // reference is assigned => ok
            else if (tok2->str() != ")" || tok2->link()->strAt(-1) != "catch")
                return false;   // not catching by reference => not declaration
        }
    }

    // Check if array declaration is valid (#2638)
    // invalid declaration: AAA a[4] = 0;
    if (typeCount >= 2 && executableScope && tok2 && tok2->str() == "[") {
        const Token *tok3 = tok2->link()->next();
        while (tok3 && tok3->str() == "[") {
            tok3 = tok3->link()->next();
        }
        if (Token::Match(tok3, "= %num%"))
            return false;
    }

    return (typeCount >= 2 && tok2 && Token::Match(tok2->tokAt(-2), "!!:: %type%"));
}


void Tokenizer::setVarIdStructMembers(Token **tok1,
                                      std::map<int, std::map<std::string, int>>& structMembers,
                                      nonneg int *varId) const
{
    Token *tok = *tok1;

    if (Token::Match(tok, "%name% = { . %name% =|{")) {
        const int struct_varid = tok->varId();
        if (struct_varid == 0)
            return;

        std::map<std::string, int>& members = structMembers[struct_varid];

        tok = tok->tokAt(3);
        while (tok->str() != "}") {
            if (Token::Match(tok, "{|[|("))
                tok = tok->link();
            if (Token::Match(tok->previous(), "[,{] . %name% =|{")) {
                tok = tok->next();
                const std::map<std::string, int>::iterator it = members.find(tok->str());
                if (it == members.end()) {
                    members[tok->str()] = ++(*varId);
                    tok->varId(*varId);
                } else {
                    tok->varId(it->second);
                }
            }
            tok = tok->next();
        }

        return;
    }

    while (Token::Match(tok->next(), ")| . %name% !!(")) {
        // Don't set varid for trailing return type
        if (tok->strAt(1) == ")" && tok->linkAt(1)->previous()->isName() &&
            isFunctionHead(tok->linkAt(1), "{|;")) {
            tok = tok->tokAt(3);
            continue;
        }
        const int struct_varid = tok->varId();
        tok = tok->tokAt(2);
        if (struct_varid == 0)
            continue;

        if (tok->str() == ".")
            tok = tok->next();

        // Don't set varid for template function
        if (TemplateSimplifier::templateParameters(tok->next()) > 0)
            break;

        std::map<std::string, int>& members = structMembers[struct_varid];
        const std::map<std::string, int>::iterator it = members.find(tok->str());
        if (it == members.end()) {
            members[tok->str()] = ++(*varId);
            tok->varId(*varId);
        } else {
            tok->varId(it->second);
        }
    }
    // tok can't be null
    *tok1 = tok;
}


void Tokenizer::setVarIdClassDeclaration(const Token * const startToken,
                                         const VariableMap &variableMap,
                                         const nonneg int scopeStartVarId,
                                         std::map<int, std::map<std::string,int>>& structMembers)
{
    // end of scope
    const Token * const endToken = startToken->link();

    // determine class name
    std::string className;
    for (const Token *tok = startToken->previous(); tok; tok = tok->previous()) {
        if (!tok->isName() && tok->str() != ":")
            break;
        if (Token::Match(tok, "class|struct|enum %type% [:{]")) {
            className = tok->next()->str();
            break;
        }
    }

    // replace varids..
    int indentlevel = 0;
    bool initList = false;
    bool inEnum = false;
    const Token *initListArgLastToken = nullptr;
    for (Token *tok = startToken->next(); tok != endToken; tok = tok->next()) {
        if (!tok)
            syntaxError(nullptr);
        if (initList) {
            if (tok == initListArgLastToken)
                initListArgLastToken = nullptr;
            else if (!initListArgLastToken &&
                     Token::Match(tok->previous(), "%name%|>|>> {|(") &&
                     Token::Match(tok->link(), "}|) ,|{"))
                initListArgLastToken = tok->link();
        }
        if (tok->str() == "{") {
            inEnum = isEnumStart(tok);
            if (initList && !initListArgLastToken)
                initList = false;
            ++indentlevel;
        } else if (tok->str() == "}") {
            --indentlevel;
            inEnum = false;
        } else if (initList && indentlevel == 0 && Token::Match(tok->previous(), "[,:] %name% [({]")) {
            const std::map<std::string, int>::const_iterator it = variableMap.find(tok->str());
            if (it != variableMap.end()) {
                tok->varId(it->second);
            }
        } else if (tok->isName() && tok->varId() <= scopeStartVarId) {
            if (indentlevel > 0 || initList) {
                if (Token::Match(tok->previous(), "::|.") && tok->strAt(-2) != "this" && !Token::simpleMatch(tok->tokAt(-5), "( * this ) ."))
                    continue;
                if (!tok->next())
                    syntaxError(nullptr);
                if (tok->next()->str() == "::") {
                    if (tok->str() == className)
                        tok = tok->tokAt(2);
                    else
                        continue;
                }

                if (!inEnum) {
                    const std::map<std::string, int>::const_iterator it = variableMap.find(tok->str());
                    if (it != variableMap.end()) {
                        tok->varId(it->second);
                        setVarIdStructMembers(&tok, structMembers, variableMap.getVarId());
                    }
                }
            }
        } else if (indentlevel == 0 && tok->str() == ":" && !initListArgLastToken)
            initList = true;
    }
}



// Update the variable ids..
// Parse each function..
void Tokenizer::setVarIdClassFunction(const std::string &classname,
                                      Token * const startToken,
                                      const Token * const endToken,
                                      const std::map<std::string, int> &varlist,
                                      std::map<int, std::map<std::string, int>>& structMembers,
                                      nonneg int *varId_)
{
    for (Token *tok2 = startToken; tok2 && tok2 != endToken; tok2 = tok2->next()) {
        if (tok2->varId() != 0 || !tok2->isName())
            continue;
        if (Token::Match(tok2->tokAt(-2), ("!!" + classname + " ::").c_str()))
            continue;
        if (Token::Match(tok2->tokAt(-4), "%name% :: %name% ::")) // Currently unsupported
            continue;
        if (Token::Match(tok2->tokAt(-2), "!!this .") && !Token::simpleMatch(tok2->tokAt(-5), "( * this ) ."))
            continue;

        const std::map<std::string,int>::const_iterator it = varlist.find(tok2->str());
        if (it != varlist.end()) {
            tok2->varId(it->second);
            setVarIdStructMembers(&tok2, structMembers, varId_);
        }
    }
}



void Tokenizer::setVarId()
{
    // Clear all variable ids
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->isName())
            tok->varId(0);
    }

    setPodTypes();

    setVarIdPass1();

    setVarIdPass2();
}


// Variable declarations can't start with "return" etc.
#define NOTSTART_C "NOT", "case", "default", "goto", "not", "return", "sizeof", "typedef"
static const std::unordered_set<std::string> notstart_c = { NOTSTART_C };
static const std::unordered_set<std::string> notstart_cpp = { NOTSTART_C,
                                                              "delete", "friend", "new", "throw", "using", "virtual", "explicit", "const_cast", "dynamic_cast", "reinterpret_cast", "static_cast", "template"
};

void Tokenizer::setVarIdPass1()
{
    // Variable declarations can't start with "return" etc.
    const std::unordered_set<std::string>& notstart = (isC()) ? notstart_c : notstart_cpp;

    VariableMap variableMap;
    std::map<int, std::map<std::string, int>> structMembers;

    std::stack<VarIdScopeInfo> scopeStack;

    scopeStack.push(VarIdScopeInfo());
    std::stack<const Token *> functionDeclEndStack;
    const Token *functionDeclEndToken = nullptr;
    bool initlist = false;
    bool inlineFunction = false;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->isOp())
            continue;
        if (tok == functionDeclEndToken) {
            functionDeclEndStack.pop();
            functionDeclEndToken = functionDeclEndStack.empty() ? nullptr : functionDeclEndStack.top();
            if (tok->str() == ":")
                initlist = true;
            else if (tok->str() == ";") {
                if (!variableMap.leaveScope())
                    cppcheckError(tok);
            } else if (tok->str() == "{") {
                scopeStack.push(VarIdScopeInfo(true, scopeStack.top().isStructInit || tok->strAt(-1) == "=", /*isEnum=*/ false, *variableMap.getVarId()));

                // check if this '{' is a start of an "if" body
                const Token * ifToken = tok->previous();
                if (ifToken && ifToken->str() == ")")
                    ifToken = ifToken->link();
                else
                    ifToken = nullptr;
                if (ifToken)
                    ifToken = ifToken->previous();
                if (ifToken && ifToken->str() == "if") {
                    // open another scope to differentiate between variables declared in the "if" condition and in the "if" body
                    variableMap.enterScope();
                }
            }
        } else if (!initlist && tok->str()=="(") {
            const Token * newFunctionDeclEnd = nullptr;
            if (!scopeStack.top().isExecutable)
                newFunctionDeclEnd = isFunctionHead(tok, "{:;");
            else {
                Token const * const tokenLinkNext = tok->link()->next();
                if (tokenLinkNext && tokenLinkNext->str() == "{") // might be for- or while-loop or if-statement
                    newFunctionDeclEnd = tokenLinkNext;
            }
            if (newFunctionDeclEnd && newFunctionDeclEnd != functionDeclEndToken) {
                functionDeclEndStack.push(newFunctionDeclEnd);
                functionDeclEndToken = newFunctionDeclEnd;
                variableMap.enterScope();
            }
        } else if (Token::Match(tok, "{|}")) {
            inlineFunction = false;

            const Token * const startToken = (tok->str() == "{") ? tok : tok->link();

            // parse anonymous unions as part of the current scope
            if (!Token::Match(startToken->previous(), "union|struct|enum {") &&
                !(initlist && Token::Match(startToken->previous(), "%name%|>|>>") && Token::Match(startToken->link(), "} ,|{"))) {

                if (tok->str() == "{") {
                    bool isExecutable;
                    const Token *prev = tok->previous();
                    while (Token::Match(prev, "%name%|."))
                        prev = prev->previous();
                    const bool isLambda = prev && prev->str() == ")" && Token::simpleMatch(prev->link()->previous(), "] (");
                    if ((!isLambda && (tok->strAt(-1) == ")" || Token::Match(tok->tokAt(-2), ") %type%"))) ||
                        (initlist && tok->strAt(-1) == "}")) {
                        isExecutable = true;
                    } else {
                        isExecutable = ((scopeStack.top().isExecutable || initlist || tok->strAt(-1) == "else") &&
                                        !isClassStructUnionEnumStart(tok));
                        if (!(scopeStack.top().isStructInit || tok->strAt(-1) == "="))
                            variableMap.enterScope();
                    }
                    initlist = false;
                    scopeStack.push(VarIdScopeInfo(isExecutable, scopeStack.top().isStructInit || tok->strAt(-1) == "=", isEnumStart(tok), *variableMap.getVarId()));
                } else { /* if (tok->str() == "}") */
                    bool isNamespace = false;
                    for (const Token *tok1 = tok->link()->previous(); tok1 && tok1->isName(); tok1 = tok1->previous()) {
                        if (tok1->str() == "namespace") {
                            isNamespace = true;
                            break;
                        }
                    }
                    // Set variable ids in class declaration..
                    if (!initlist && !isC() && !scopeStack.top().isExecutable && tok->link() && !isNamespace) {
                        setVarIdClassDeclaration(tok->link(),
                                                 variableMap,
                                                 scopeStack.top().startVarid,
                                                 structMembers);
                    }

                    if (!scopeStack.top().isStructInit) {
                        variableMap.leaveScope();

                        // check if this '}' is an end of an "else" body or an "if" body without an "else" part
                        const Token * ifToken = startToken->previous();
                        if (ifToken && ifToken->str() == ")")
                            ifToken = ifToken->link()->previous();
                        else
                            ifToken = nullptr;
                        if (startToken->strAt(-1) == "else" || (ifToken && ifToken->str() == "if" && tok->strAt(1) != "else")) {
                            // leave the extra scope used to differentiate between variables declared in the "if" condition and in the "if" body
                            variableMap.leaveScope();
                        }
                    }

                    scopeStack.pop();
                    if (scopeStack.empty()) {  // should be impossible
                        scopeStack.push(VarIdScopeInfo());
                    }
                }
            }
        }

        if (!scopeStack.top().isStructInit &&
            (tok == list.front() ||
             Token::Match(tok, "[;{}]") ||
             (tok->str() == "(" && isFunctionHead(tok,"{")) ||
             (tok->str() == "(" && !scopeStack.top().isExecutable && isFunctionHead(tok,";:")) ||
             (tok->str() == "," && (!scopeStack.top().isExecutable || inlineFunction)) ||
             (tok->isName() && endsWith(tok->str(), ':')))) {

            // No variable declarations in sizeof
            if (Token::simpleMatch(tok->previous(), "sizeof (")) {
                continue;
            }

            if (Settings::terminated())
                return;

            // locate the variable name..
            const Token *tok2 = (tok->isName()) ? tok : tok->next();

            // private: protected: public: etc
            while (tok2 && endsWith(tok2->str(), ':')) {
                tok2 = tok2->next();
            }
            if (!tok2)
                break;

            // Variable declaration can't start with "return", etc
            if (notstart.find(tok2->str()) != notstart.end())
                continue;

            if (!isC() && Token::simpleMatch(tok2, "const new"))
                continue;

            bool decl;
            if (isCPP() && mSettings->standards.cpp >= Standards::CPP17 && Token::Match(tok, "[(;{}] const| auto &|&&| [")) {
                // Structured bindings
                tok2 = Token::findsimplematch(tok, "[");
                if ((Token::simpleMatch(tok->previous(), "for (") && Token::simpleMatch(tok2->link(), "] :")) ||
                    Token::simpleMatch(tok2->link(), "] =")) {
                    while (tok2 && tok2->str() != "]") {
                        if (Token::Match(tok2, "%name% [,]]"))
                            variableMap.addVariable(tok2->str());
                        tok2 = tok2->next();
                    }
                    continue;
                }
            }

            try { /* Ticket #8151 */
                decl = setVarIdParseDeclaration(&tok2, variableMap.map(), scopeStack.top().isExecutable, isCPP(), isC());
            } catch (const Token * errTok) {
                syntaxError(errTok);
            }
            if (decl) {
                if (isCPP()) {
                    if (Token *declTypeTok = Token::findsimplematch(tok, "decltype (", tok2)) {
                        for (Token *declTok = declTypeTok->linkAt(1); declTok != declTypeTok; declTok = declTok->previous()) {
                            if (declTok->isName() && !Token::Match(declTok->previous(), "::|.") && variableMap.hasVariable(declTok->str()))
                                declTok->varId(variableMap.find(declTok->str())->second);
                        }
                    }
                }

                if (tok->str() == "(" && isFunctionHead(tok,"{") && scopeStack.top().isExecutable)
                    inlineFunction = true;

                const Token* prev2 = tok2->previous();
                if (Token::Match(prev2, "%type% [;[=,)]") && tok2->previous()->str() != "const")
                    ;
                else if (Token::Match(prev2, "%type% :") && tok->strAt(-1) == "for")
                    ;
                else if (Token::Match(prev2, "%type% ( !!)") && Token::simpleMatch(tok2->link(), ") ;")) {
                    // In C++ , a variable can't be called operator+ or something like that.
                    if (isCPP() &&
                        prev2->isOperatorKeyword())
                        continue;

                    const Token *tok3 = tok2->next();
                    if (!tok3->isStandardType() && tok3->str() != "void" && !Token::Match(tok3, "struct|union|class %type%") && tok3->str() != "." && !Token::Match(tok2->link()->previous(), "[&*]")) {
                        if (!scopeStack.top().isExecutable) {
                            // Detecting initializations with () in non-executable scope is hard and often impossible to be done safely. Thus, only treat code as a variable that definitely is one.
                            decl = false;
                            bool rhs = false;
                            for (; tok3; tok3 = tok3->nextArgumentBeforeCreateLinks2()) {
                                if (tok3->str() == "=") {
                                    rhs = true;
                                    continue;
                                }

                                if (tok3->str() == ",") {
                                    rhs = false;
                                    continue;
                                }

                                if (rhs)
                                    continue;

                                if (tok3->isLiteral() ||
                                    (tok3->isName() && variableMap.hasVariable(tok3->str())) ||
                                    tok3->isOp() ||
                                    tok3->str() == "(" ||
                                    notstart.find(tok3->str()) != notstart.end()) {
                                    decl = true;
                                    break;
                                }
                            }
                        }
                    } else
                        decl = false;
                } else if (isCPP() && Token::Match(prev2, "%type% {") && Token::simpleMatch(tok2->link(), "} ;")) { // C++11 initialization style
                    if (tok2->link() != tok2->next() && // add value-initialized variable T x{};
                        (Token::Match(prev2, "do|try|else") || Token::Match(prev2->tokAt(-2), "struct|class|:")))
                        continue;
                } else
                    decl = false;

                if (decl) {
                    variableMap.addVariable(prev2->str());

                    if (Token::simpleMatch(tok->previous(), "for (") && Token::Match(prev2, "%name% [=,]")) {
                        for (const Token *tok3 = prev2->next(); tok3 && tok3->str() != ";"; tok3 = tok3->next()) {
                            if (Token::Match(tok3, "[([]"))
                                tok3 = tok3->link();
                            if (Token::Match(tok3, ", %name% [,=;]"))
                                variableMap.addVariable(tok3->next()->str());
                        }
                    }

                    // set varid for template parameters..
                    tok = tok->next();
                    while (Token::Match(tok, "%name%|::"))
                        tok = tok->next();
                    if (tok && tok->str() == "<") {
                        const Token *end = tok->findClosingBracket();
                        while (tok != end) {
                            if (tok->isName() && !(Token::simpleMatch(tok->next(), "<") &&
                                                   Token::Match(tok->tokAt(-2), "std :: %name%"))) {
                                const std::map<std::string, int>::const_iterator it = variableMap.find(tok->str());
                                if (it != variableMap.end())
                                    tok->varId(it->second);
                            }
                            tok = tok->next();
                        }
                    }

                    tok = tok2->previous();
                }
            }
        }

        if (tok->isName()) {
            // don't set variable id after a struct|enum|union
            if (Token::Match(tok->previous(), "struct|enum|union") || (isCPP() && tok->strAt(-1) == "class"))
                continue;

            if (!isC()) {
                if (tok->previous() && tok->previous()->str() == "::")
                    continue;
                if (tok->next() && tok->next()->str() == "::")
                    continue;
                if (Token::simpleMatch(tok->tokAt(-2), ":: template"))
                    continue;
            }

            // function declaration inside executable scope? Function declaration is of form: type name "(" args ")"
            if (scopeStack.top().isExecutable && Token::Match(tok, "%name% [,)]")) {
                bool par = false;
                const Token *start, *end;

                // search begin of function declaration
                for (start = tok; Token::Match(start, "%name%|*|&|,|("); start = start->previous()) {
                    if (start->str() == "(") {
                        if (par)
                            break;
                        par = true;
                    }
                    if (Token::Match(start, "[(,]")) {
                        if (!Token::Match(start, "[(,] %type% %name%|*|&"))
                            break;
                    }
                    if (start->varId() > 0)
                        break;
                }

                // search end of function declaration
                for (end = tok->next(); Token::Match(end, "%name%|*|&|,"); end = end->next()) {}

                // there are tokens which can't appear at the begin of a function declaration such as "return"
                const bool isNotstartKeyword = start->next() && notstart.find(start->next()->str()) != notstart.end();

                // now check if it is a function declaration
                if (Token::Match(start, "[;{}] %type% %name%|*") && par && Token::simpleMatch(end, ") ;") && !isNotstartKeyword)
                    // function declaration => don't set varid
                    continue;
            }

            if (!scopeStack.top().isEnum || !(Token::Match(tok->previous(), "{|,") && Token::Match(tok->next(), ",|=|}"))) {
                const std::map<std::string, int>::const_iterator it = variableMap.find(tok->str());
                if (it != variableMap.end()) {
                    tok->varId(it->second);
                    setVarIdStructMembers(&tok, structMembers, variableMap.getVarId());
                }
            }
        } else if (Token::Match(tok, "::|. %name%")) {
            // Don't set varid after a :: or . token
            tok = tok->next();
        } else if (tok->str() == ":" && Token::Match(tok->tokAt(-2), "class %type%")) {
            do {
                tok = tok->next();
            } while (tok && (tok->isName() || tok->str() == ","));
            if (!tok)
                break;
            tok = tok->previous();
        }
    }

    mVarId = *variableMap.getVarId();
}

namespace {
    struct Member {
        Member(const std::list<std::string> &s, const std::list<const Token *> &ns, Token *t) : usingnamespaces(ns), scope(s), tok(t) {}
        std::list<const Token *> usingnamespaces;
        std::list<std::string> scope;
        Token *tok;
    };
}

static std::string getScopeName(const std::list<ScopeInfo2> &scopeInfo)
{
    std::string ret;
    for (const ScopeInfo2 &si : scopeInfo)
        ret += (ret.empty() ? "" : " :: ") + (si.name);
    return ret;
}

static Token * matchMemberName(const std::list<std::string> &scope, const Token *nsToken, Token *memberToken, const std::list<ScopeInfo2> &scopeInfo)
{
    std::list<ScopeInfo2>::const_iterator scopeIt = scopeInfo.begin();

    // Current scope..
    for (std::list<std::string>::const_iterator it = scope.begin(); it != scope.end(); ++it) {
        if (scopeIt == scopeInfo.end() || scopeIt->name != *it)
            return nullptr;
        ++scopeIt;
    }

    // using namespace..
    if (nsToken) {
        while (Token::Match(nsToken, "%name% ::")) {
            if (scopeIt != scopeInfo.end() && nsToken->str() == scopeIt->name) {
                nsToken = nsToken->tokAt(2);
                ++scopeIt;
            } else {
                return nullptr;
            }
        }
        if (!Token::Match(nsToken, "%name% ;"))
            return nullptr;
        if (scopeIt == scopeInfo.end() || nsToken->str() != scopeIt->name)
            return nullptr;
        ++scopeIt;
    }

    // Parse member tokens..
    while (scopeIt != scopeInfo.end()) {
        if (!Token::Match(memberToken, "%name% ::|<"))
            return nullptr;
        if (memberToken->str() != scopeIt->name)
            return nullptr;
        if (memberToken->next()->str() == "<") {
            memberToken = memberToken->next()->findClosingBracket();
            if (!Token::simpleMatch(memberToken, "> ::"))
                return nullptr;
        }
        memberToken = memberToken->tokAt(2);
        ++scopeIt;
    }

    return Token::Match(memberToken, "~| %name%") ? memberToken : nullptr;
}

static Token * matchMemberName(const Member &member, const std::list<ScopeInfo2> &scopeInfo)
{
    if (scopeInfo.empty())
        return nullptr;

    // Does this member match without "using namespace"..
    Token *ret = matchMemberName(member.scope, nullptr, member.tok, scopeInfo);
    if (ret)
        return ret;

    // Try to match member using the "using namespace ..." namespaces..
    for (const Token *ns : member.usingnamespaces) {
        ret = matchMemberName(member.scope, ns, member.tok, scopeInfo);
        if (ret)
            return ret;
    }

    return nullptr;
}

static Token * matchMemberVarName(const Member &var, const std::list<ScopeInfo2> &scopeInfo)
{
    Token *tok = matchMemberName(var, scopeInfo);
    return Token::Match(tok, "%name% !!(") ? tok : nullptr;
}

static Token * matchMemberFunctionName(const Member &func, const std::list<ScopeInfo2> &scopeInfo)
{
    Token *tok = matchMemberName(func, scopeInfo);
    return Token::Match(tok, "~| %name% (") ? tok : nullptr;
}

void Tokenizer::setVarIdPass2()
{
    std::map<int, std::map<std::string, int>> structMembers;

    // Member functions and variables in this source
    std::list<Member> allMemberFunctions;
    std::list<Member> allMemberVars;
    if (!isC()) {
        std::map<const Token *, std::string> endOfScope;
        std::list<std::string> scope;
        std::list<const Token *> usingnamespaces;
        for (Token *tok = list.front(); tok; tok = tok->next()) {
            if (!tok->previous() || Token::Match(tok->previous(), "[;{}]")) {
                if (Token::Match(tok, "using namespace %name% ::|;")) {
                    Token *endtok = tok->tokAt(2);
                    while (Token::Match(endtok, "%name% ::"))
                        endtok = endtok->tokAt(2);
                    if (Token::Match(endtok, "%name% ;"))
                        usingnamespaces.push_back(tok->tokAt(2));
                    tok = endtok;
                    continue;
                } else if (Token::Match(tok, "namespace %name% {")) {
                    scope.push_back(tok->strAt(1));
                    endOfScope[tok->linkAt(2)] = tok->strAt(1);
                }
            }

            if (tok->str() == "}") {
                const std::map<const Token *, std::string>::iterator it = endOfScope.find(tok);
                if (it != endOfScope.end())
                    scope.remove(it->second);
            }

            Token* const tok1 = tok;
            if (Token::Match(tok->previous(), "!!:: %name% :: ~| %name%"))
                tok = tok->next();
            else if (Token::Match(tok->previous(), "!!:: %name% <") && Token::Match(tok->next()->findClosingBracket(),"> :: ~| %name%"))
                tok = tok->next()->findClosingBracket()->next();
            else
                continue;

            while (Token::Match(tok, ":: ~| %name%")) {
                tok = tok->next();
                if (tok->str() == "~")
                    tok = tok->next();
                else if (Token::Match(tok, "%name% <") && Token::Match(tok->next()->findClosingBracket(),"> :: ~| %name%"))
                    tok = tok->next()->findClosingBracket()->next();
                else if (Token::Match(tok, "%name% ::"))
                    tok = tok->next();
                else
                    break;
            }
            if (!tok->next())
                syntaxError(tok);
            if (Token::Match(tok, "%name% ("))
                allMemberFunctions.emplace_back(scope, usingnamespaces, tok1);
            else
                allMemberVars.emplace_back(scope, usingnamespaces, tok1);
        }
    }

    std::list<ScopeInfo2> scopeInfo;

    // class members..
    std::map<std::string, std::map<std::string, int>> varsByClass;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (tok->str() == "}" && !scopeInfo.empty() && tok == scopeInfo.back().bodyEnd)
            scopeInfo.pop_back();

        if (!Token::Match(tok, "namespace|class|struct %name% {|:|::|<"))
            continue;

        const std::string &scopeName(getScopeName(scopeInfo));
        const std::string scopeName2(scopeName.empty() ? std::string() : (scopeName + " :: "));

        std::list<const Token *> classnameTokens;
        classnameTokens.push_back(tok->next());
        const Token* tokStart = tok->tokAt(2);
        while (Token::Match(tokStart, ":: %name%") || tokStart->str() == "<") {
            if (tokStart->str() == "<") {
                // skip the template part
                tokStart = tokStart->findClosingBracket()->next();
            } else {
                classnameTokens.push_back(tokStart->next());
                tokStart = tokStart->tokAt(2);
            }
        }

        std::string classname;
        for (const Token *it : classnameTokens)
            classname += (classname.empty() ? "" : " :: ") + it->str();

        std::map<std::string, int> &thisClassVars = varsByClass[scopeName2 + classname];
        while (Token::Match(tokStart, ":|::|,|%name%")) {
            if (Token::Match(tokStart, "%name% <")) {
                tokStart = tokStart->next()->findClosingBracket();
                if (tokStart)
                    tokStart = tokStart->next();
                continue;
            }
            if (Token::Match(tokStart, "%name% ,|{")) {
                std::string baseClassName = tokStart->str();
                std::string scopeName3(scopeName2);
                while (!scopeName3.empty()) {
                    const std::string name = scopeName3 + baseClassName;
                    if (varsByClass.find(name) != varsByClass.end()) {
                        baseClassName = name;
                        break;
                    }
                    // Remove last scope name
                    if (scopeName3.size() <= 8)
                        break;
                    scopeName3.erase(scopeName3.size() - 4);
                    const std::string::size_type pos = scopeName3.rfind(" :: ");
                    if (pos == std::string::npos)
                        break;
                    scopeName3.erase(pos + 4);
                }
                const std::map<std::string, int>& baseClassVars = varsByClass[baseClassName];
                thisClassVars.insert(baseClassVars.begin(), baseClassVars.end());
            }
            tokStart = tokStart->next();
        }
        if (!Token::simpleMatch(tokStart, "{"))
            continue;

        // What member variables are there in this class?
        for (const Token *it : classnameTokens)
            scopeInfo.emplace_back(it->str(), tokStart->link());

        for (Token *tok2 = tokStart->next(); tok2 && tok2 != tokStart->link(); tok2 = tok2->next()) {
            // skip parentheses..
            if (tok2->link()) {
                if (tok2->str() == "(") {
                    Token *funcstart = const_cast<Token*>(isFunctionHead(tok2, "{"));
                    if (funcstart) {
                        setVarIdClassFunction(scopeName2 + classname, funcstart, funcstart->link(), thisClassVars, structMembers, &mVarId);
                        tok2 = funcstart->link();
                        continue;
                    }
                }
                if (tok2->str() == "{") {
                    if (tok2->strAt(-1) == ")")
                        setVarIdClassFunction(scopeName2 + classname, tok2, tok2->link(), thisClassVars, structMembers, &mVarId);
                    tok2 = tok2->link();
                } else if (Token::Match(tok2, "( %name%|)") && !Token::Match(tok2->link(), "(|[")) {
                    tok2 = tok2->link();

                    // Skip initialization list
                    while (Token::Match(tok2, ") [:,] %name% ("))
                        tok2 = tok2->linkAt(3);
                }
            }

            // Found a member variable..
            else if (tok2->varId() > 0)
                thisClassVars[tok2->str()] = tok2->varId();
        }

        // Are there any member variables in this class?
        if (thisClassVars.empty())
            continue;

        // Member variables
        for (const Member &var : allMemberVars) {
            Token *tok2 = matchMemberVarName(var, scopeInfo);
            if (!tok2)
                continue;
            tok2->varId(thisClassVars[tok2->str()]);
        }

        if (isC() || tok->str() == "namespace")
            continue;

        // Set variable ids in member functions for this class..
        for (const Member &func : allMemberFunctions) {
            Token *tok2 = matchMemberFunctionName(func, scopeInfo);
            if (!tok2)
                continue;

            if (tok2->str() == "~")
                tok2 = tok2->linkAt(2);
            else
                tok2 = tok2->linkAt(1);

            // If this is a function implementation.. add it to funclist
            Token * start = const_cast<Token *>(isFunctionHead(tok2, "{"));
            if (start) {
                setVarIdClassFunction(classname, start, start->link(), thisClassVars, structMembers, &mVarId);
            }

            if (Token::Match(tok2, ") %name% ("))
                tok2 = tok2->linkAt(2);

            // constructor with initializer list
            if (!Token::Match(tok2, ") : ::| %name%"))
                continue;

            Token *tok3 = tok2;
            while (Token::Match(tok3, "[)}] [,:]")) {
                tok3 = tok3->tokAt(2);
                if (Token::Match(tok3, ":: %name%"))
                    tok3 = tok3->next();
                while (Token::Match(tok3, "%name% :: %name%"))
                    tok3 = tok3->tokAt(2);
                if (!Token::Match(tok3, "%name% (|{|<"))
                    break;

                // set varid
                const std::map<std::string, int>::const_iterator varpos = thisClassVars.find(tok3->str());
                if (varpos != thisClassVars.end())
                    tok3->varId(varpos->second);

                // goto end of var
                if (tok3->strAt(1) == "<") {
                    tok3 = tok3->next()->findClosingBracket();
                    if (tok3 && tok3->next() && tok3->next()->link())
                        tok3 = tok3->next()->link();
                } else
                    tok3 = tok3->linkAt(1);
            }
            if (Token::Match(tok3, ")|} {")) {
                setVarIdClassFunction(classname, tok2, tok3->next()->link(), thisClassVars, structMembers, &mVarId);
            }
        }
    }
}

static void linkBrackets(const Tokenizer * const tokenizer, std::stack<const Token*>& type, std::stack<Token*>& links, Token * const token, const char open, const char close)
{
    if (token->str()[0] == open) {
        links.push(token);
        type.push(token);
    } else if (token->str()[0] == close) {
        if (links.empty()) {
            // Error, { and } don't match.
            tokenizer->unmatchedToken(token);
        }
        if (type.top()->str()[0] != open) {
            tokenizer->unmatchedToken(type.top());
        }
        type.pop();

        Token::createMutualLinks(links.top(), token);
        links.pop();
    }
}

void Tokenizer::createLinks()
{
    std::stack<const Token*> type;
    std::stack<Token*> links1;
    std::stack<Token*> links2;
    std::stack<Token*> links3;
    for (Token *token = list.front(); token; token = token->next()) {
        if (token->link()) {
            token->link(nullptr);
        }

        linkBrackets(this, type, links1, token, '{', '}');

        linkBrackets(this, type, links2, token, '(', ')');

        linkBrackets(this, type, links3, token, '[', ']');
    }

    if (!links1.empty()) {
        // Error, { and } don't match.
        unmatchedToken(links1.top());
    }

    if (!links2.empty()) {
        // Error, ( and ) don't match.
        unmatchedToken(links2.top());
    }

    if (!links3.empty()) {
        // Error, [ and ] don't match.
        unmatchedToken(links3.top());
    }
}

void Tokenizer::createLinks2()
{
    if (isC())
        return;

    bool isStruct = false;

    std::stack<Token*> type;
    std::stack<Token*> templateTokens;
    for (Token *token = list.front(); token; token = token->next()) {
        if (Token::Match(token, "%name%|> %name% [:<]"))
            isStruct = true;
        else if (Token::Match(token, "[;{}]"))
            isStruct = false;

        if (token->link()) {
            if (Token::Match(token, "{|[|("))
                type.push(token);
            else if (!type.empty() && Token::Match(token, "}|]|)")) {
                while (type.top()->str() == "<") {
                    if (!templateTokens.empty() && templateTokens.top()->next() == type.top())
                        templateTokens.pop();
                    type.pop();
                }
                type.pop();
            }
        } else if (templateTokens.empty() && !isStruct && Token::Match(token, "%oror%|&&|;")) {
            if (Token::Match(token, "&& [,>]"))
                continue;
            // If there is some such code:  A<B||C>..
            // Then this is probably a template instantiation if either "B" or "C" has comparisons
            if (token->tokType() == Token::eLogicalOp && !type.empty() && type.top()->str() == "<") {
                const Token *prev = token->previous();
                bool foundComparison = false;
                while (Token::Match(prev, "%name%|%num%|%str%|%cop%|)|]") && prev != type.top()) {
                    if (prev->str() == ")" || prev->str() == "]")
                        prev = prev->link();
                    else if (prev->tokType() == Token::eLogicalOp)
                        break;
                    else if (prev->isComparisonOp())
                        foundComparison = true;
                    prev = prev->previous();
                }
                if (prev == type.top() && foundComparison)
                    continue;
                const Token *next = token->next();
                foundComparison = false;
                while (Token::Match(next, "%name%|%num%|%str%|%cop%|(|[") && next->str() != ">") {
                    if (next->str() == "(" || next->str() == "[")
                        next = next->link();
                    else if (next->tokType() == Token::eLogicalOp)
                        break;
                    else if (next->isComparisonOp())
                        foundComparison = true;
                    next = next->next();
                }
                if (next && next->str() == ">" && foundComparison)
                    continue;
            }

            while (!type.empty() && type.top()->str() == "<") {
                const Token* end = type.top()->findClosingBracket();
                if (Token::Match(end, "> %comp%|;|.|=|{|::"))
                    break;
                // Variable declaration
                if (Token::Match(end, "> %var% ;") && (type.top()->tokAt(-2) == nullptr || Token::Match(type.top()->tokAt(-2), ";|}|{")))
                    break;
                type.pop();
            }
        } else if (token->str() == "<" &&
                   ((token->previous() && (token->previous()->isTemplate() ||
                                           (token->previous()->isName() && !token->previous()->varId()))) ||
                    Token::Match(token->next(), ">|>>"))) {
            type.push(token);
            if (token->previous()->str() == "template")
                templateTokens.push(token);
        } else if (token->str() == ">" || token->str() == ">>") {
            if (type.empty() || type.top()->str() != "<") // < and > don't match.
                continue;
            Token * const top1 = type.top();
            type.pop();
            Token * const top2 = type.empty() ? nullptr : type.top();
            type.push(top1);
            if (!top2 || top2->str() != "<") {
                if (token->str() == ">>")
                    continue;
                if (!Token::Match(token->next(), "%name%|%cop%|%assign%|::|,|(|)|{|}|;|[|:|.|=|...") &&
                    !Token::Match(token->next(), "&& %name% ="))
                    continue;
            }
            // if > is followed by [ .. "new a<b>[" is expected
            // unless this is from varidiac expansion
            if (token->strAt(1) == "[" && !Token::simpleMatch(token->tokAt(-1), "... >") &&
                !Token::Match(token->tokAt(1), "[ ]")) {
                Token *prev = type.top()->previous();
                while (prev && Token::Match(prev->previous(), ":: %name%"))
                    prev = prev->tokAt(-2);
                if (prev && prev->str() != "new")
                    prev = prev->previous();
                if (!prev || prev->str() != "new")
                    continue;
            }

            if (token->str() == ">>" && top1 && top2) {
                type.pop();
                type.pop();
                // Split the angle brackets
                token->str(">");
                Token::createMutualLinks(top1, token->insertTokenBefore(">"));
                Token::createMutualLinks(top2, token);
                if (templateTokens.size() == 2 && (top1 == templateTokens.top() || top2 == templateTokens.top())) {
                    templateTokens.pop();
                    templateTokens.pop();
                }
            } else {
                type.pop();
                if (Token::Match(token, "> %name%") && !token->next()->isKeyword() &&
                    Token::Match(top1->tokAt(-2), "%op% %name% <") &&
                    (templateTokens.empty() || top1 != templateTokens.top()))
                    continue;
                Token::createMutualLinks(top1, token);
                if (!templateTokens.empty() && top1 == templateTokens.top())
                    templateTokens.pop();
            }
        }
    }
}

void Tokenizer::sizeofAddParentheses()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "sizeof !!("))
            continue;
        if (tok->next()->isLiteral() || Token::Match(tok->next(), "%name%|*|~|!|&")) {
            Token *endToken = tok->next();
            while (Token::simpleMatch(endToken, "* *"))
                endToken = endToken->next();
            while (Token::Match(endToken->next(), "%name%|%num%|%str%|[|(|.|::|++|--|!|~") || (Token::Match(endToken, "%type% * %op%|?|:|const|;|,"))) {
                if (Token::Match(endToken->next(), "(|["))
                    endToken = endToken->linkAt(1);
                else
                    endToken = endToken->next();
            }

            // Add ( after sizeof and ) behind endToken
            tok->insertToken("(");
            endToken->insertToken(")");
            Token::createMutualLinks(tok->next(), endToken->next());
        }
    }
}

bool Tokenizer::simplifySizeof()
{
    // Locate variable declarations and calculate the size
    std::map<int, int> sizeOfVar;
    std::map<int, const Token *> declTokOfVar;
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->varId() != 0 && sizeOfVar.find(tok->varId()) == sizeOfVar.end()) {
            const int varId = tok->varId();
            if (Token::Match(tok->tokAt(-3), "[;{}(,] %type% * %name% [;,)]") ||
                Token::Match(tok->tokAt(-4), "[;{}(,] const %type% * %name% [;),]") ||
                Token::Match(tok->tokAt(-2), "[;{}(,] %type% %name% [;),]") ||
                Token::Match(tok->tokAt(-3), "[;{}(,] const %type% %name% [;),]")) {
                const int size = sizeOfType(tok->previous());
                if (size == 0) {
                    continue;
                }

                sizeOfVar[varId] = size;
                declTokOfVar[varId] = tok;
            }

            else if (Token::Match(tok->previous(), "%type% %name% [ %num% ] [[;=]") ||
                     Token::Match(tok->tokAt(-2), "%type% * %name% [ %num% ] [[;=]")) {
                int size = sizeOfType(tok->previous());
                if (size == 0)
                    continue;

                const Token* tok2 = tok->next();
                do {
                    const MathLib::bigint num = MathLib::toLongNumber(tok2->strAt(1));
                    if (num<0)
                        break;
                    size *= num;
                    tok2 = tok2->tokAt(3);
                } while (Token::Match(tok2, "[ %num% ]"));
                if (Token::Match(tok2, "[;=]")) {
                    sizeOfVar[varId] = size;
                    declTokOfVar[varId] = tok;
                }
                if (!tok2) {
                    syntaxError(tok);
                }
                tok = tok2;
            }

            else if (Token::Match(tok->previous(), "%type% %name% [ %num% ] [,)]") ||
                     Token::Match(tok->tokAt(-2), "%type% * %name% [ %num% ] [,)]")) {
                Token tempTok;
                tempTok.str("*");
                sizeOfVar[varId] = sizeOfType(&tempTok);
                declTokOfVar[varId] = tok;
            }
        }
    }

    bool ret = false;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != "sizeof")
            continue;

        if (Token::simpleMatch(tok->next(), "...")) {
            //tok->deleteNext(3);
            tok->deleteNext();
        }

        // sizeof('x')
        if (Token::Match(tok->next(), "( %char% )")) {
            tok->deleteNext();
            tok->deleteThis();
            tok->deleteNext();
            std::ostringstream sz;
            sz << ((isC()) ? mSettings->sizeof_int : 1);
            tok->str(sz.str());
            ret = true;
            continue;
        }

        // sizeof ("text")
        if (Token::Match(tok->next(), "( %str% )")) {
            tok->deleteNext();
            tok->deleteThis();
            tok->deleteNext();
            std::ostringstream ostr;
            ostr << (Token::getStrLength(tok) + 1);
            tok->str(ostr.str());
            ret = true;
            continue;
        }

        // sizeof(type *) => sizeof(*)
        if (Token::Match(tok->next(), "( %type% * )")) {
            tok->next()->deleteNext();
        }

        if (Token::simpleMatch(tok->next(), "( * )")) {
            tok->str(MathLib::toString(sizeOfType(tok->tokAt(2))));
            tok->deleteNext(3);
            ret = true;
        }

        // sizeof( a )
        else if (Token::Match(tok->next(), "( %var% )")) {
            const std::map<int, int>::const_iterator sizeOfVarPos = sizeOfVar.find(tok->tokAt(2)->varId());
            if (sizeOfVarPos != sizeOfVar.end()) {
                tok->deleteNext();
                tok->deleteThis();
                tok->deleteNext();
                tok->str(MathLib::toString(sizeOfVarPos->second));
                ret = true;
            } else {
                // don't try to replace size of variable if variable has
                // similar name with type (#329)
            }
        }

        else if (Token::Match(tok->next(), "( %type% )")) {
            const int size = sizeOfType(tok->tokAt(2));
            if (size > 0) {
                tok->str(MathLib::toString(size));
                tok->deleteNext(3);
                ret = true;
            }
        }

        else if (Token::simpleMatch(tok->next(), "( *") || Token::Match(tok->next(), "( %name% [")) {
            int derefs = 0;

            const Token* nametok = tok->tokAt(2);
            if (nametok->str() == "*") {
                do {
                    nametok = nametok->next();
                    derefs++;
                } while (nametok && nametok->str() == "*");

                if (!Token::Match(nametok, "%name% )"))
                    continue;
            } else {
                const Token* tok2 = nametok->next();
                do {
                    tok2 = tok2->link()->next();
                    derefs++;
                } while (tok2 && tok2->str() == "[");

                if (!tok2 || tok2->str() != ")")
                    continue;
            }

            // Some default value
            MathLib::biguint size = 0;

            const int varid = nametok->varId();
            if (derefs != 0 && varid != 0 && declTokOfVar.find(varid) != declTokOfVar.end()) {
                // Try to locate variable declaration..
                const Token *decltok = declTokOfVar[varid];
                if (Token::Match(decltok->previous(), "%type%|* %name% [")) {
                    size = sizeOfType(decltok->previous());
                } else if (Token::Match(decltok->tokAt(-2), "%type% * %name%")) {
                    size = sizeOfType(decltok->tokAt(-2));
                }
                // Multi-dimensional array..
                if (Token::Match(decltok, "%name% [") && Token::simpleMatch(decltok->linkAt(1), "] [")) {
                    const Token *tok2 = decltok;
                    for (int i = 0; i < derefs; i++)
                        tok2 = tok2->linkAt(1); // Skip all dimensions that are dereferenced before the sizeof call
                    while (Token::Match(tok2, "] [ %num% ]")) {
                        size *= MathLib::toULongNumber(tok2->strAt(2));
                        tok2 = tok2->linkAt(1);
                    }
                    if (Token::simpleMatch(tok2, "] ["))
                        continue;
                }
            } else if (nametok->strAt(1) == "[" && nametok->isStandardType()) {
                size = sizeOfType(nametok);
                if (size == 0)
                    continue;
                const Token *tok2 = nametok->next();
                while (Token::Match(tok2, "[ %num% ]")) {
                    size *= MathLib::toULongNumber(tok2->strAt(1));
                    tok2 = tok2->link()->next();
                }
                if (!tok2 || tok2->str() != ")")
                    continue;
            }

            if (size > 0) {
                tok->str(MathLib::toString(size));
                Token::eraseTokens(tok, tok->next()->link()->next());
                ret = true;
            }
        }
    }
    return ret;
}

bool Tokenizer::simplifyTokenList1(const char FileName[])
{
    if (Settings::terminated())
        return false;

    // if MACRO
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "if|for|while|BOOST_FOREACH %name% (")) {
            if (Token::simpleMatch(tok, "for each")) {
                // 'for each ( )' -> 'asm ( )'
                tok->str("asm");
                tok->deleteNext();
            } else if (tok->strAt(1) == "constexpr") {
                tok->deleteNext();
                tok->isConstexpr(true);
            } else {
                syntaxError(tok);
            }
        }
    }

    // Is there C++ code in C file?
    validateC();

    // remove MACRO in variable declaration: MACRO int x;
    removeMacroInVarDecl();

    // Combine strings and character literals, e.g. L"string", L'c', "string1" "string2"
    combineStringAndCharLiterals();

    // replace inline SQL with "asm()" (Oracle PRO*C). Ticket: #1959
    simplifySQL();

    createLinks();

    removePragma();

    // Simplify the C alternative tokens (and, or, etc.)
    simplifyCAlternativeTokens();

    simplifyFunctionTryCatch();

    simplifyHeadersAndUnusedTemplates();

    // Remove __asm..
    simplifyAsm();

    // foo < bar < >> => foo < bar < > >
    if (isCPP() || mSettings->daca)
        splitTemplateRightAngleBrackets(!isCPP());

    // Remove extra "template" tokens that are not used by cppcheck
    removeExtraTemplateKeywords();

    removeAlignas();

    simplifySpaceshipOperator();

    // Bail out if code is garbage
    if (mTimerResults) {
        Timer t("Tokenizer::tokenize::findGarbageCode", mSettings->showtime, mTimerResults);
        findGarbageCode();
    } else {
        findGarbageCode();
    }

    checkConfiguration();

    // if (x) MACRO() ..
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::simpleMatch(tok, "if (")) {
            tok = tok->next()->link();
            if (Token::Match(tok, ") %name% (") &&
                tok->next()->isUpperCaseName() &&
                Token::Match(tok->linkAt(2), ") {|else")) {
                syntaxError(tok->next());
            }
        }
    }

    if (Settings::terminated())
        return false;

    // convert C++17 style nested namespaces to old style namespaces
    simplifyNestedNamespace();

    // convert c++20 coroutines
    simplifyCoroutines();

    // simplify namespace aliases
    simplifyNamespaceAliases();

    // Remove [[attribute]] and alignas(?)
    simplifyCPPAttribute();

    // remove __attribute__((?))
    simplifyAttribute();

    // simplify cppcheck attributes __cppcheck_?__(?)
    simplifyCppcheckAttribute();

    // Combine tokens..
    combineOperators();

    // replace 'sin(0)' to '0' and other similar math expressions
    simplifyMathExpressions();

    // combine "- %num%"
    concatenateNegativeNumberAndAnyPositive();

    // remove extern "C" and extern "C" {}
    if (isCPP())
        simplifyExternC();

    // simplify weird but legal code: "[;{}] ( { code; } ) ;"->"[;{}] code;"
    simplifyRoundCurlyParentheses();

    // check for simple syntax errors..
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::simpleMatch(tok, "> struct {") &&
            Token::simpleMatch(tok->linkAt(2), "} ;")) {
            syntaxError(tok);
        }
    }

    if (!simplifyAddBraces())
        return false;

    sizeofAddParentheses();

    // Simplify: 0[foo] -> *(foo)
    for (Token* tok = list.front(); tok; tok = tok->next()) {
        if (Token::simpleMatch(tok, "0 [") && tok->linkAt(1)) {
            tok->str("*");
            tok->next()->str("(");
            tok->linkAt(1)->str(")");
        }
    }

    if (Settings::terminated())
        return false;

    // Remove __declspec()
    simplifyDeclspec();
    validate();

    // Remove "inline", "register", and "restrict"
    simplifyKeyword();

    // simplify simple calculations inside <..>
    if (isCPP()) {
        Token *lt = nullptr;
        for (Token *tok = list.front(); tok; tok = tok->next()) {
            if (Token::Match(tok, "[;{}]"))
                lt = nullptr;
            else if (Token::Match(tok, "%type% <"))
                lt = tok->next();
            else if (lt && Token::Match(tok, ">|>> %name%|::|(")) {
                const Token * const end = tok;
                for (tok = lt; tok != end; tok = tok->next()) {
                    if (tok->isNumber())
                        TemplateSimplifier::simplifyNumericCalculations(tok);
                }
                lt = tok->next();
            }
        }
    }

    // Convert K&R function declarations to modern C
    simplifyVarDecl(true);
    simplifyFunctionParameters();

    // simplify case ranges (gcc extension)
    simplifyCaseRange();

    // simplify labels and 'case|default'-like syntaxes
    simplifyLabelsCaseDefault();

    // simplify '[;{}] * & ( %any% ) =' to '%any% ='
    simplifyMulAndParens();

    if (!isC() && !mSettings->library.markupFile(FileName)) {
        findComplicatedSyntaxErrorsInTemplates();
    }

    if (Settings::terminated())
        return false;

    // remove calling conventions __cdecl, __stdcall..
    simplifyCallingConvention();

    addSemicolonAfterUnknownMacro();

    // remove some unhandled macros in global scope
    removeMacrosInGlobalScope();

    // remove undefined macro in class definition:
    // class DLLEXPORT Fred { };
    // class Fred FINAL : Base { };
    removeMacroInClassDef();

    // That call here fixes #7190
    validate();

    // remove unnecessary member qualification..
    removeUnnecessaryQualification();

    // convert Microsoft memory functions
    simplifyMicrosoftMemoryFunctions();

    // convert Microsoft string functions
    simplifyMicrosoftStringFunctions();

    if (Settings::terminated())
        return false;

    // Remove Qt signals and slots
    simplifyQtSignalsSlots();

    // remove Borland stuff..
    simplifyBorland();

    // syntax error: enum with typedef in it
    checkForEnumsWithTypedef();

    // Add parentheses to ternary operator where necessary
    prepareTernaryOpForAST();

    // Change initialisation of variable to assignment
    simplifyInitVar();

    // Split up variable declarations.
    simplifyVarDecl(false);

    reportUnknownMacros();

    // typedef..
    if (mTimerResults) {
        Timer t("Tokenizer::tokenize::simplifyTypedef", mSettings->showtime, mTimerResults);
        simplifyTypedef();
    } else {
        simplifyTypedef();
    }

    // using A = B;
    while (simplifyUsing())
        ;

    // Add parentheses to ternary operator where necessary
    // TODO: this is only necessary if one typedef simplification had a comma and was used within ?:
    // If typedef handling is refactored and moved to symboldatabase someday we can remove this
    prepareTernaryOpForAST();

    for (Token* tok = list.front(); tok;) {
        if (Token::Match(tok, "union|struct|class union|struct|class"))
            tok->deleteNext();
        else
            tok = tok->next();
    }

    // class x y {
    if (isCPP() && mSettings->severity.isEnabled(Severity::information)) {
        for (const Token *tok = list.front(); tok; tok = tok->next()) {
            if (Token::Match(tok, "class %type% %type% [:{]")) {
                unhandled_macro_class_x_y(tok);
            }
        }
    }

    // catch bad typedef canonicalization
    //
    // to reproduce bad typedef, download upx-ucl from:
    // http://packages.debian.org/sid/upx-ucl
    // analyse the file src/stub/src/i386-linux.elf.interp-main.c
    validate();

    // The simplify enum have inner loops
    if (Settings::terminated())
        return false;

    // Put ^{} statements in asm()
    simplifyAsm2();

    // @..
    simplifyAt();

    // When the assembly code has been cleaned up, no @ is allowed
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "(") {
            const Token *tok1 = tok;
            tok = tok->link();
            if (!tok)
                syntaxError(tok1);
        } else if (tok->str() == "@") {
            syntaxError(tok);
        }
    }

    // Order keywords "static" and "const"
    simplifyStaticConst();

    // convert platform dependent types to standard types
    // 32 bits: size_t -> unsigned long
    // 64 bits: size_t -> unsigned long long
    list.simplifyPlatformTypes();

    // collapse compound standard types into a single token
    // unsigned long long int => long (with _isUnsigned=true,_isLong=true)
    list.simplifyStdType();

    if (Settings::terminated())
        return false;

    // simplify bit fields..
    simplifyBitfields();

    if (Settings::terminated())
        return false;

    // struct simplification "struct S {} s; => struct S { } ; S s ;
    simplifyStructDecl();

    if (Settings::terminated())
        return false;

    // x = ({ 123; });  =>   { x = 123; }
    simplifyAssignmentBlock();

    if (Settings::terminated())
        return false;

    simplifyVariableMultipleAssign();

    // Collapse operator name tokens into single token
    // operator = => operator=
    simplifyOperatorName();

    // Remove redundant parentheses
    simplifyRedundantParentheses();

    if (isCPP())
        simplifyTypeIntrinsics();

    if (!isC()) {
        // Handle templates..
        if (mTimerResults) {
            Timer t("Tokenizer::tokenize::simplifyTemplates", mSettings->showtime, mTimerResults);
            simplifyTemplates();
        } else {
            simplifyTemplates();
        }

        // The simplifyTemplates have inner loops
        if (Settings::terminated())
            return false;

        validate(); // #6847 - invalid code
    }

    // Simplify pointer to standard types (C only)
    simplifyPointerToStandardType();

    // simplify function pointers
    simplifyFunctionPointers();

    // Change initialisation of variable to assignment
    simplifyInitVar();

    // Split up variable declarations.
    simplifyVarDecl(false);

    elseif();

    validate(); // #6772 "segmentation fault (invalid code) in Tokenizer::setVarId"

    if (mTimerResults) {
        Timer t("Tokenizer::tokenize::setVarId", mSettings->showtime, mTimerResults);
        setVarId();
    } else {
        setVarId();
    }

    // Link < with >
    createLinks2();

    // Mark C++ casts
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "const_cast|dynamic_cast|reinterpret_cast|static_cast <") && Token::simpleMatch(tok->linkAt(1), "> (")) {
            tok = tok->linkAt(1)->next();
            tok->isCast(true);
        }
    }

    // specify array size
    arraySize();

    // The simplify enum might have inner loops
    if (Settings::terminated())
        return false;

    // Add std:: in front of std classes, when using namespace std; was given
    simplifyNamespaceStd();

    // Change initialisation of variable to assignment
    simplifyInitVar();

    simplifyDoublePlusAndDoubleMinus();

    simplifyArrayAccessSyntax();

    Token::assignProgressValues(list.front());

    removeRedundantSemicolons();

    simplifyRedundantConsecutiveBraces();

    simplifyEmptyNamespaces();

    simplifyIfSwitchForInit();

    simplifyOverloadedOperators();

    validate();

    list.front()->assignIndexes();

    return true;
}

bool Tokenizer::simplifyTokenList2()
{
    // clear the _functionList so it can't contain dead pointers
    deleteSymbolDatabase();

    // Clear AST,ValueFlow. These will be created again at the end of this function.
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        tok->clearAst();
        tok->clearValueFlow();
    }

    // Convert e.g. atol("0") into 0
    simplifyMathFunctions();

    // f(x=g())   =>   x=g(); f(x)
    simplifyAssignmentInFunctionCall();

    // ";a+=b;" => ";a=a+b;"
    simplifyCompoundAssignment();

    simplifyCharAt();

    // simplify references
    simplifyReference();

    simplifyStd();

    if (Settings::terminated())
        return false;

    simplifySizeof();

    simplifyUndefinedSizeArray();

    simplifyCasts();

    // Simplify simple calculations before replace constants, this allows the replacement of constants that are calculated
    // e.g. const static int value = sizeof(X)/sizeof(Y);
    simplifyCalculations();

    if (Settings::terminated())
        return false;

    // Replace "*(ptr + num)" => "ptr[num]"
    simplifyOffsetPointerDereference();

    // Replace "&str[num]" => "(str + num)"
    simplifyOffsetPointerReference();

    removeRedundantAssignment();

    simplifyRealloc();

    // Change initialisation of variable to assignment
    simplifyInitVar();

    // Simplify variable declarations
    simplifyVarDecl(false);

    simplifyErrNoInWhile();
    simplifyIfAndWhileAssign();
    simplifyRedundantParentheses();
    simplifyNestedStrcat();
    simplifyFuncInWhile();

    simplifyIfAndWhileAssign();

    // replace strlen(str)
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "strlen ( %str% )")) {
            tok->str(MathLib::toString(Token::getStrLength(tok->tokAt(2))));
            tok->deleteNext(3);
        }
    }

    bool modified = true;
    while (modified) {
        if (Settings::terminated())
            return false;

        modified = false;
        modified |= simplifyConditions();
        modified |= simplifyFunctionReturn();
        modified |= simplifyKnownVariables();
        modified |= simplifyStrlen();

        modified |= removeRedundantConditions();
        modified |= simplifyRedundantParentheses();
        modified |= simplifyConstTernaryOp();
        modified |= simplifyCalculations();
        validate();
    }

    // simplify redundant loops
    simplifyWhile0();
    removeRedundantFor();

    // Remove redundant parentheses in return..
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (Token::simpleMatch(tok, "return (")) {
            Token *tok2 = tok->next()->link();
            if (Token::simpleMatch(tok2, ") ;")) {
                tok->deleteNext();
                tok2->deleteThis();
            } else {
                break;
            }
        }
    }

    simplifyReturnStrncat();

    removeRedundantAssignment();

    simplifyComma();

    removeRedundantSemicolons();

    simplifyFlowControl();

    simplifyRedundantConsecutiveBraces();

    simplifyEmptyNamespaces();

    simplifyMathFunctions();

    validate();

    Token::assignProgressValues(list.front());

    list.front()->assignIndexes();

    list.createAst();
    // needed for #7208 (garbage code) and #7724 (ast max depth limit)
    list.validateAst();

    // Create symbol database and then remove const keywords
    createSymbolDatabase();
    mSymbolDatabase->setValueTypeInTokenList(true);

    ValueFlow::setValues(&list, mSymbolDatabase, mErrorLogger, mSettings);

    if (Settings::terminated())
        return false;

    printDebugOutput(2);

    return true;
}
//---------------------------------------------------------------------------

void Tokenizer::printDebugOutput(int simplification) const
{
    const bool debug = (simplification != 1U && mSettings->debugSimplified) ||
                       (simplification != 2U && mSettings->debugnormal);

    if (debug && list.front()) {
        list.front()->printOut(nullptr, list.getFiles());

        if (mSettings->xml)
            std::cout << "<debug>" << std::endl;

        if (mSymbolDatabase) {
            if (mSettings->xml)
                mSymbolDatabase->printXml(std::cout);
            else if (mSettings->verbose) {
                mSymbolDatabase->printOut("Symbol database");
            }
        }

        if (mSettings->verbose)
            list.front()->printAst(mSettings->verbose, mSettings->xml, list.getFiles(), std::cout);

        list.front()->printValueFlow(mSettings->xml, std::cout);

        if (mSettings->xml)
            std::cout << "</debug>" << std::endl;
    }

    if (mSymbolDatabase && simplification == 2U && mSettings->debugwarnings) {
        printUnknownTypes();

        // the typeStartToken() should come before typeEndToken()
        for (const Variable *var : mSymbolDatabase->variableList()) {
            if (!var)
                continue;

            const Token * typetok = var->typeStartToken();
            while (typetok && typetok != var->typeEndToken())
                typetok = typetok->next();

            if (typetok != var->typeEndToken()) {
                reportError(var->typeStartToken(),
                            Severity::debug,
                            "debug",
                            "Variable::typeStartToken() of variable '" + var->name() + "' is not located before Variable::typeEndToken(). The location of the typeStartToken() is '" + var->typeStartToken()->str() + "' at line " + MathLib::toString(var->typeStartToken()->linenr()));
            }
        }
    }
}

void Tokenizer::dump(std::ostream &out) const
{
    // Create a xml data dump.
    // The idea is not that this will be readable for humans. It's a
    // data dump that 3rd party tools could load and get useful info from.

    // tokens..
    out << "  <tokenlist>" << std::endl;
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        out << "    <token id=\"" << tok << "\" file=\"" << ErrorLogger::toxml(list.file(tok)) << "\" linenr=\"" << tok->linenr() << "\" column=\"" << tok->column() << "\"";
        out << " str=\"" << ErrorLogger::toxml(tok->str()) << '\"';
        out << " scope=\"" << tok->scope() << '\"';
        if (tok->isName()) {
            out << " type=\"name\"";
            if (tok->isUnsigned())
                out << " isUnsigned=\"true\"";
            else if (tok->isSigned())
                out << " isSigned=\"true\"";
        } else if (tok->isNumber()) {
            out << " type=\"number\"";
            if (MathLib::isInt(tok->str()))
                out << " isInt=\"true\"";
            if (MathLib::isFloat(tok->str()))
                out << " isFloat=\"true\"";
        } else if (tok->tokType() == Token::eString)
            out << " type=\"string\" strlen=\"" << Token::getStrLength(tok) << '\"';
        else if (tok->tokType() == Token::eChar)
            out << " type=\"char\"";
        else if (tok->isBoolean())
            out << " type=\"boolean\"";
        else if (tok->isOp()) {
            out << " type=\"op\"";
            if (tok->isArithmeticalOp())
                out << " isArithmeticalOp=\"true\"";
            else if (tok->isAssignmentOp())
                out << " isAssignmentOp=\"true\"";
            else if (tok->isComparisonOp())
                out << " isComparisonOp=\"true\"";
            else if (tok->tokType() == Token::eLogicalOp)
                out << " isLogicalOp=\"true\"";
        }
        if (tok->isExpandedMacro())
            out << " isExpandedMacro=\"true\"";
        if (tok->isSplittedVarDeclComma())
            out << " isSplittedVarDeclComma=\"true\"";
        if (tok->isSplittedVarDeclEq())
            out << " isSplittedVarDeclEq=\"true\"";
        if (tok->isImplicitInt())
            out << " isImplicitInt=\"true\"";
        if (tok->link())
            out << " link=\"" << tok->link() << '\"';
        if (tok->varId() > 0)
            out << " varId=\"" << MathLib::toString(tok->varId()) << '\"';
        if (tok->variable())
            out << " variable=\"" << tok->variable() << '\"';
        if (tok->function())
            out << " function=\"" << tok->function() << '\"';
        if (!tok->values().empty())
            out << " values=\"" << &tok->values() << '\"';
        if (tok->type())
            out << " type-scope=\"" << tok->type()->classScope << '\"';
        if (tok->astParent())
            out << " astParent=\"" << tok->astParent() << '\"';
        if (tok->astOperand1())
            out << " astOperand1=\"" << tok->astOperand1() << '\"';
        if (tok->astOperand2())
            out << " astOperand2=\"" << tok->astOperand2() << '\"';
        if (!tok->originalName().empty())
            out << " originalName=\"" << tok->originalName() << '\"';
        if (tok->valueType()) {
            const std::string vt = tok->valueType()->dump();
            if (!vt.empty())
                out << ' ' << vt;
        }
        if (!tok->varId() && tok->scope()->isExecutable() && Token::Match(tok, "%name% (")) {
            if (mSettings->library.isnoreturn(tok))
                out << " noreturn=\"true\"";
        }

        out << "/>" << std::endl;
    }
    out << "  </tokenlist>" << std::endl;

    mSymbolDatabase->printXml(out);
    if (list.front())
        list.front()->printValueFlow(true, out);

    if (!mTypedefInfo.empty()) {
        out << "  <typedef-info>" << std::endl;
        for (const TypedefInfo &typedefInfo: mTypedefInfo) {
            out << "    <info"
                << " name=\"" << typedefInfo.name << "\""
                << " file=\"" << typedefInfo.filename << "\""
                << " line=\"" << typedefInfo.lineNumber << "\""
                << " column=\"" << typedefInfo.column << "\""
                << " used=\"" << (typedefInfo.used?1:0) << "\""
                << "/>" << std::endl;
        }
        out << "  </typedef-info>" << std::endl;
    }
}

void Tokenizer::simplifyHeadersAndUnusedTemplates()
{
    if (mSettings->checkHeaders && mSettings->checkUnusedTemplates)
        // Full analysis. All information in the headers are kept.
        return;

    const bool checkHeaders = mSettings->checkHeaders;
    const bool removeUnusedIncludedFunctions = !mSettings->checkHeaders;
    const bool removeUnusedIncludedClasses   = !mSettings->checkHeaders;
    const bool removeUnusedIncludedTemplates = !mSettings->checkUnusedTemplates || !mSettings->checkHeaders;
    const bool removeUnusedTemplates = !mSettings->checkUnusedTemplates;

    // checkHeaders:
    //
    // If it is true then keep all code in the headers. It's possible
    // to remove unused types/variables if false positives / false
    // negatives can be avoided.
    //
    // If it is false, then we want to remove selected stuff from the
    // headers but not *everything*. The intention here is to not damage
    // the analysis of the source file. You should get all warnings in
    // the source file. You should not get false positives.

    // functions and types to keep
    std::set<std::string> keep;
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (isCPP() && Token::simpleMatch(tok, "template <")) {
            const Token *closingBracket = tok->next()->findClosingBracket();
            if (Token::Match(closingBracket, "> class|struct %name% {"))
                tok = closingBracket->linkAt(3);
        }

        if (!tok->isName() || tok->isKeyword())
            continue;

        if (!checkHeaders && tok->fileIndex() != 0)
            continue;

        if (Token::Match(tok, "%name% (") && !Token::simpleMatch(tok->linkAt(1), ") {")) {
            keep.insert(tok->str());
            continue;
        }

        if (Token::Match(tok, "%name% %name%|::|*|&|<")) {
            keep.insert(tok->str());
        }
    }

    const std::set<std::string> functionStart{"static", "const", "unsigned", "signed", "void", "bool", "char", "short", "int", "long", "float", "*"};

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        const bool isIncluded = (tok->fileIndex() != 0);

        // Remove executable code
        if (isIncluded && !mSettings->checkHeaders && tok->str() == "{") {
            // TODO: We probably need to keep the executable code if this function is called from the source file.
            const Token *prev = tok->previous();
            while (prev && prev->isName())
                prev = prev->previous();
            if (Token::simpleMatch(prev, ")")) {
                // Replace all tokens from { to } with a ";".
                Token::eraseTokens(tok,tok->link()->next());
                tok->str(";");
                tok->link(nullptr);
            }
        }

        if (!tok->previous() || Token::Match(tok->previous(), "[;{}]")) {
            // Remove unused function declarations
            if (isIncluded && removeUnusedIncludedFunctions) {
                while (true) {
                    Token *start = tok;
                    while (start && functionStart.find(start->str()) != functionStart.end())
                        start = start->next();
                    if (Token::Match(start, "%name% (") && Token::Match(start->linkAt(1), ") const| ;") && keep.find(start->str()) == keep.end()) {
                        Token::eraseTokens(tok, start->linkAt(1)->tokAt(2));
                        tok->deleteThis();
                    } else
                        break;
                }
            }

            if (isIncluded && removeUnusedIncludedClasses) {
                if (Token::Match(tok, "class|struct %name% [:{]") && keep.find(tok->strAt(1)) == keep.end()) {
                    // Remove this class/struct
                    const Token *endToken = tok->tokAt(2);
                    if (endToken->str() == ":") {
                        endToken = endToken->next();
                        while (Token::Match(endToken, "%name%|,"))
                            endToken = endToken->next();
                    }
                    if (endToken && endToken->str() == "{" && Token::simpleMatch(endToken->link(), "} ;")) {
                        Token::eraseTokens(tok, endToken->link()->next());
                        tok->deleteThis();
                    }
                }
            }

            if (removeUnusedTemplates || (isIncluded && removeUnusedIncludedTemplates)) {
                if (Token::Match(tok, "template < %name%")) {
                    const Token *closingBracket = tok->next()->findClosingBracket();
                    if (Token::Match(closingBracket, "> class|struct %name% [;:{]") && keep.find(closingBracket->strAt(2)) == keep.end()) {
                        const Token *endToken = closingBracket->tokAt(3);
                        if (endToken->str() == ":") {
                            endToken = endToken->next();
                            while (Token::Match(endToken, "%name%|,"))
                                endToken = endToken->next();
                        }
                        if (endToken && endToken->str() == "{")
                            endToken = endToken->link()->next();
                        if (endToken && endToken->str() == ";") {
                            Token::eraseTokens(tok, endToken);
                            tok->deleteThis();
                        }
                    } else if (Token::Match(closingBracket, "> %type% %name% (") && Token::simpleMatch(closingBracket->linkAt(3), ") {") && keep.find(closingBracket->strAt(2)) == keep.end()) {
                        const Token *endToken = closingBracket->linkAt(3)->linkAt(1)->next();
                        Token::eraseTokens(tok, endToken);
                        tok->deleteThis();
                    }
                }
            }
        }
    }
}

void Tokenizer::removeExtraTemplateKeywords()
{
    if (isCPP()) {
        for (Token *tok = list.front(); tok; tok = tok->next()) {
            if (Token::Match(tok, "%name%|>|) .|:: template %name%")) {
                tok->next()->deleteNext();
                Token* templateName = tok->tokAt(2);
                while (Token::Match(templateName, "%name%|::")) {
                    templateName->isTemplate(true);
                    templateName = templateName->next();
                }
                if (Token::Match(templateName->previous(), "operator %op%|(")) {
                    templateName->isTemplate(true);
                    if (templateName->str() == "(" && templateName->link())
                        templateName->link()->isTemplate(true);
                }
            }
        }
    }
}

static std::string getExpression(const Token *tok)
{
    std::string line;
    for (const Token *prev = tok->previous(); prev && !Token::Match(prev, "[;{}]"); prev = prev->previous())
        line = prev->str() + " " + line;
    line += "!!!" + tok->str() + "!!!";
    for (const Token *next = tok->next(); next && !Token::Match(next, "[;{}]"); next = next->next())
        line = line + " " + next->str();
    return line;
}

void Tokenizer::splitTemplateRightAngleBrackets(bool check)
{
    std::set<std::string> vars;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "[;{}] %type% %type% [;,=]") && tok->next()->isStandardType())
            vars.insert(tok->strAt(2));

        // Ticket #6181: normalize C++11 template parameter list closing syntax
        if (tok->previous() && tok->str() == "<" && TemplateSimplifier::templateParameters(tok) && vars.find(tok->previous()->str()) == vars.end()) {
            Token *endTok = tok->findClosingBracket();
            if (check) {
                if (Token::Match(endTok, ">>|>>="))
                    reportError(tok, Severity::debug, "dacaWrongSplitTemplateRightAngleBrackets", "bad closing bracket for !!!<!!!: " + getExpression(tok), false);
                continue;
            }
            if (endTok && endTok->str() == ">>") {
                endTok->str(">");
                endTok->insertToken(">");
            } else if (endTok && endTok->str() == ">>=") {
                endTok->str(">");
                endTok->insertToken("=");
                endTok->insertToken(">");
            }
        } else if (Token::Match(tok, "class|struct|union|=|:|public|protected|private %name% <") && vars.find(tok->next()->str()) == vars.end()) {
            Token *endTok = tok->tokAt(2)->findClosingBracket();
            if (check) {
                if (Token::simpleMatch(endTok, ">>"))
                    reportError(tok, Severity::debug, "dacaWrongSplitTemplateRightAngleBrackets", "bad closing bracket for !!!<!!!: " + getExpression(tok), false);
                continue;
            }
            if (Token::Match(endTok, ">> ;|{|%type%")) {
                endTok->str(">");
                endTok->insertToken(">");
            }
        }
    }
}

void Tokenizer::removeMacrosInGlobalScope()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "(") {
            tok = tok->link();
            if (Token::Match(tok, ") %type% {") &&
                !Token::Match(tok->next(), "const|namespace|class|struct|union|noexcept|override|final|volatile"))
                tok->deleteNext();
        }

        if (Token::Match(tok, "%type%") && tok->isUpperCaseName() &&
            (!tok->previous() || Token::Match(tok->previous(), "[;{}]") || (tok->previous()->isName() && endsWith(tok->previous()->str(), ':')))) {
            const Token *tok2 = tok->next();
            if (tok2 && tok2->str() == "(")
                tok2 = tok2->link()->next();

            // Several unknown macros...
            while (Token::Match(tok2, "%type% (") && tok2->isUpperCaseName())
                tok2 = tok2->linkAt(1)->next();

            if (Token::Match(tok, "%name% (") && Token::Match(tok2, "%name% *|&|::|<| %name%") && !Token::Match(tok2, "namespace|class|struct|union|private:|protected:|public:"))
                unknownMacroError(tok);

            if (Token::Match(tok, "%type% (") && Token::Match(tok2, "%type% (") && !Token::Match(tok2, "noexcept|throw") && isFunctionHead(tok2->next(), ":;{"))
                unknownMacroError(tok);

            // remove unknown macros before namespace|class|struct|union
            if (Token::Match(tok2, "namespace|class|struct|union")) {
                // is there a "{" for?
                const Token *tok3 = tok2;
                while (tok3 && !Token::Match(tok3,"[;{}()]"))
                    tok3 = tok3->next();
                if (tok3 && tok3->str() == "{") {
                    Token::eraseTokens(tok, tok2);
                    tok->deleteThis();
                }
                continue;
            }

            // replace unknown macros before foo(
            /*
                        if (Token::Match(tok2, "%type% (") && isFunctionHead(tok2->next(), "{")) {
                            std::string typeName;
                            for (const Token* tok3 = tok; tok3 != tok2; tok3 = tok3->next())
                                typeName += tok3->str();
                            Token::eraseTokens(tok, tok2);
                            tok->str(typeName);
                        }
             */
            // remove unknown macros before foo::foo(
            if (Token::Match(tok2, "%type% :: %type%")) {
                const Token *tok3 = tok2;
                while (Token::Match(tok3, "%type% :: %type% ::"))
                    tok3 = tok3->tokAt(2);
                if (Token::Match(tok3, "%type% :: %type% (") && tok3->str() == tok3->strAt(2)) {
                    Token::eraseTokens(tok, tok2);
                    tok->deleteThis();
                }
                continue;
            }
        }

        // Skip executable scopes
        if (tok->str() == "{") {
            const Token *prev = tok->previous();
            while (prev && prev->isName())
                prev = prev->previous();
            if (prev && prev->str() == ")")
                tok = tok->link();
        }
    }
}

//---------------------------------------------------------------------------

void Tokenizer::removePragma()
{
    if (isC() && mSettings->standards.c == Standards::C89)
        return;
    if (isCPP() && mSettings->standards.cpp == Standards::CPP03)
        return;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (Token::simpleMatch(tok, "_Pragma (")) {
            Token::eraseTokens(tok, tok->linkAt(1)->next());
            tok->deleteThis();
        }
    }
}

//---------------------------------------------------------------------------

void Tokenizer::removeMacroInClassDef()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "class|struct %name% %name% {|:"))
            continue;

        const bool nextIsUppercase = tok->next()->isUpperCaseName();
        const bool afterNextIsUppercase = tok->tokAt(2)->isUpperCaseName();
        if (nextIsUppercase && !afterNextIsUppercase)
            tok->deleteNext();
        else if (!nextIsUppercase && afterNextIsUppercase)
            tok->next()->deleteNext();
    }
}

//---------------------------------------------------------------------------

void Tokenizer::removeMacroInVarDecl()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "[;{}] %name% (") && tok->next()->isUpperCaseName()) {
            // goto ')' parentheses
            const Token *tok2 = tok;
            int parlevel = 0;
            while (tok2) {
                if (tok2->str() == "(")
                    ++parlevel;
                else if (tok2->str() == ")") {
                    if (--parlevel <= 0)
                        break;
                }
                tok2 = tok2->next();
            }
            tok2 = tok2 ? tok2->next() : nullptr;

            // check if this is a variable declaration..
            const Token *tok3 = tok2;
            while (tok3 && tok3->isUpperCaseName())
                tok3 = tok3->next();
            if (tok3 && (tok3->isStandardType() || Token::Match(tok3,"const|static|struct|union|class")))
                Token::eraseTokens(tok,tok2);
        }
    }
}
//---------------------------------------------------------------------------

void Tokenizer::addSemicolonAfterUnknownMacro()
{
    if (!isCPP())
        return;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != ")")
            continue;
        const Token *macro = tok->link() ? tok->link()->previous() : nullptr;
        if (!macro || !macro->isName())
            continue;
        if (Token::simpleMatch(tok, ") try") && !Token::Match(macro, "if|for|while"))
            tok->insertToken(";");
        else if (Token::simpleMatch(tok, ") using"))
            tok->insertToken(";");
    }
}
//---------------------------------------------------------------------------

void Tokenizer::removeRedundantAssignment()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "{")
            tok = tok->link();

        const Token * const start = const_cast<Token *>(startOfExecutableScope(tok));
        if (start) {
            tok = start->previous();
            // parse in this function..
            std::set<int> localvars;
            const Token * const end = tok->next()->link();
            for (Token * tok2 = tok->next(); tok2 && tok2 != end; tok2 = tok2->next()) {
                // skip local class or struct
                if (Token::Match(tok2, "class|struct %type% {|:")) {
                    // skip to '{'
                    tok2 = tok2->tokAt(2);
                    while (tok2 && tok2->str() != "{")
                        tok2 = tok2->next();

                    if (tok2)
                        tok2 = tok2->link(); // skip local class or struct
                    else
                        return;
                } else if (Token::Match(tok2, "[;{}] %type% * %name% ;") && tok2->next()->str() != "return") {
                    tok2 = tok2->tokAt(3);
                    localvars.insert(tok2->varId());
                } else if (Token::Match(tok2, "[;{}] %type% %name% ;") && tok2->next()->isStandardType()) {
                    tok2 = tok2->tokAt(2);
                    localvars.insert(tok2->varId());
                } else if (tok2->varId() &&
                           !Token::Match(tok2->previous(), "[;{}] %name% = %char%|%num%|%name% ;")) {
                    localvars.erase(tok2->varId());
                }
            }
            localvars.erase(0);
            if (!localvars.empty()) {
                for (Token *tok2 = tok->next(); tok2 && tok2 != end;) {
                    if (Token::Match(tok2, "[;{}] %type% %name% ;") && localvars.find(tok2->tokAt(2)->varId()) != localvars.end()) {
                        tok2->deleteNext(3);
                    } else if ((Token::Match(tok2, "[;{}] %type% * %name% ;") &&
                                localvars.find(tok2->tokAt(3)->varId()) != localvars.end()) ||
                               (Token::Match(tok2, "[;{}] %name% = %any% ;") &&
                                localvars.find(tok2->next()->varId()) != localvars.end())) {
                        tok2->deleteNext(4);
                    } else
                        tok2 = tok2->next();
                }
            }
        }
    }
}

void Tokenizer::simplifyRealloc()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "(|[") ||
            (tok->str() == "{" && tok->previous() && tok->previous()->str() == "="))
            tok = tok->link();
        else if (Token::Match(tok, "[;{}] %name% = realloc (")) {
            tok = tok->tokAt(3);
            if (Token::simpleMatch(tok->next(), "( 0 ,")) {
                //no "x = realloc(0,);"
                if (!Token::simpleMatch(tok->next()->link(), ") ;") || tok->next()->link()->previous() == tok->tokAt(3))
                    continue;

                // delete "0 ,"
                tok->next()->deleteNext(2);

                // Change function name "realloc" to "malloc"
                tok->str("malloc");
                tok = tok->next()->link();
            } else {
                Token *tok2 = tok->next()->link()->tokAt(-2);
                //no "x = realloc(,0);"
                if (!Token::simpleMatch(tok2, ", 0 ) ;") || tok2 == tok->tokAt(2))
                    continue;

                //remove ", 0"
                tok2 = tok2->previous();
                tok2->deleteNext(2);
                //change "realloc" to "free"
                tok->str("free");
                //insert "0" after "var ="
                tok = tok->previous();
                tok->insertToken("0");
                //move "var = 0" between "free(...)" and ";"
                tok2 = tok2->next();
                Token::move(tok->previous(), tok->next(), tok2);
                //add missing ";" after "free(...)"
                tok2->insertToken(";");
                //goto before last ";" and continue
                tok = tok->next();
            }
        }
    }
}

void Tokenizer::simplifyEmptyNamespaces()
{
    if (isC())
        return;

    bool goback = false;
    for (Token *tok = list.front(); tok; tok = tok ? tok->next() : nullptr) {
        if (goback) {
            tok = tok->previous();
            goback = false;
        }
        if (Token::Match(tok, "(|[|{")) {
            tok = tok->link();
            continue;
        }
        if (!Token::Match(tok, "namespace %name%| {"))
            continue;
        bool isAnonymousNS = tok->strAt(1) == "{";
        if (tok->strAt(3 - isAnonymousNS) == "}") {
            tok->deleteNext(3 - isAnonymousNS); // remove '%name%| { }'
            if (!tok->previous()) {
                // remove 'namespace' or replace it with ';' if isolated
                tok->deleteThis();
                goback = true;
            } else {                    // '%any% namespace %any%'
                tok = tok->previous();  // goto previous token
                tok->deleteNext();      // remove next token: 'namespace'
                if (tok->str() == "{") {
                    // Go back in case we were within a namespace that's empty now
                    tok = tok->tokAt(-2) ? tok->tokAt(-2) : tok->previous();
                    goback = true;
                }
            }
        } else {
            tok = tok->tokAt(2 - isAnonymousNS);
        }
    }
}

void Tokenizer::simplifyFlowControl()
{
    for (Token *begin = list.front(); begin; begin = begin->next()) {

        if (Token::Match(begin, "(|[") ||
            (begin->str() == "{" && begin->previous() && begin->strAt(-1) == "="))
            begin = begin->link();

        //function scope
        if (!Token::simpleMatch(begin, ") {") && !Token::Match(begin, ") %name% {"))
            continue;

        Token* end = begin->linkAt(1+(begin->next()->str() == "{" ? 0 : 1));
        int indentLevel = 0;
        bool stilldead = false;

        for (Token *tok = begin; tok && tok != end; tok = tok->next()) {
            if (Token::Match(tok, "(|[")) {
                tok = tok->link();
                continue;
            }

            if (tok->str() == "{") {
                if (tok->previous() && tok->previous()->str() == "=") {
                    tok = tok->link();
                    continue;
                }
                ++indentLevel;
            } else if (tok->str() == "}") {
                if (indentLevel == 0)
                    break;
                --indentLevel;
                if (stilldead) {
                    eraseDeadCode(tok, nullptr);
                    if (indentLevel == 1 || tok->next()->str() != "}" || !Token::Match(tok->next()->link()->previous(), ";|{|}|do {"))
                        stilldead = false;
                    continue;
                }
            }

            if (indentLevel == 0)
                continue;

            if (Token::Match(tok,"continue|break ;")) {
                tok = tok->next();
                eraseDeadCode(tok, nullptr);

            } else if (Token::Match(tok,"return|goto") ||
                       (Token::Match(tok->previous(), "[;{}] %name% (") &&
                        mSettings->library.isnoreturn(tok)) ||
                       (isCPP() && tok->str() == "throw")) {
                if (tok->next()->str() == "}")
                    syntaxError(tok->next()); // invalid code like in #6731
                //TODO: ensure that we exclude user-defined 'exit|abort|throw', except for 'noreturn'
                //catch the first ';'
                for (Token *tok2 = tok->next(); tok2; tok2 = tok2->next()) {
                    if (Token::Match(tok2, "(|[")) {
                        tok2 = tok2->link();
                    } else if (tok2->str() == ";") {
                        tok = tok2;
                        eraseDeadCode(tok, nullptr);
                        break;
                    } else if (Token::Match(tok2, "[{}]"))
                        break;
                }
                //if everything is removed, then remove also the code after an inferior scope
                //only if the actual scope is not special
                if (indentLevel > 1 && tok->next()->str() == "}" && Token::Match(tok->next()->link()->previous(), ";|{|}|do {"))
                    stilldead = true;
            }
        }
        begin = end;
    }
}


bool Tokenizer::removeRedundantConditions()
{
    // Return value for function. Set to true if there are any simplifications
    bool ret = false;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "if ( %bool% ) {"))
            continue;

        // Find matching else
        Token *elseTag = tok->linkAt(4)->next();

        const bool boolValue = (tok->strAt(2) == "true");

        // Handle if with else
        if (Token::simpleMatch(elseTag, "else {")) {
            // Handle else
            if (!boolValue) {
                // Convert "if( false ) {aaa;} else {bbb;}" => "{bbb;}"

                //remove '(false)'
                tok->deleteNext(3);
                //delete dead code inside scope
                eraseDeadCode(tok, elseTag);
                //remove 'else'
                elseTag->deleteThis();
                //remove 'if'
                tok->deleteThis();
            } else {
                // Convert "if( true ) {aaa;} else {bbb;}" => "{aaa;}"
                const Token *end = elseTag->next()->link()->next();

                // Remove "else { bbb; }"
                elseTag = elseTag->previous();
                eraseDeadCode(elseTag, end);

                // Remove "if( true )"
                tok->deleteNext(3);
                tok->deleteThis();
            }

            ret = true;
        }

        // Handle if without else
        else {
            if (!boolValue) {
                //remove '(false)'
                tok->deleteNext(3);
                //delete dead code inside scope
                eraseDeadCode(tok, elseTag);
                //remove 'if'
                tok->deleteThis();
            } else {
                // convert "if( true ) {aaa;}" => "{aaa;}"
                tok->deleteNext(3);
                tok->deleteThis();
            }

            ret = true;
        }
    }

    return ret;
}

void Tokenizer::removeRedundantFor()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "[;{}] for ( %name% = %num% ; %name% < %num% ; ++| %name% ++| ) {") ||
            Token::Match(tok, "[;{}] for ( %type% %name% = %num% ; %name% < %num% ; ++| %name% ++| ) {")) {
            // Same variable name..
            const Token* varTok = tok->tokAt(3);
            const bool type = varTok->next()->isName();
            if (type)
                varTok = varTok->next();
            const std::string varname(varTok->str());
            const int varid(varTok->varId());
            if (varname != varTok->strAt(4))
                continue;
            const Token *vartok2 = tok->linkAt(2)->previous();
            if (vartok2->str() == "++")
                vartok2 = vartok2->previous();
            else if (vartok2->strAt(-1) != "++")
                continue;
            if (varname != vartok2->str())
                continue;

            // Check that the difference of the numeric values is 1
            const MathLib::bigint num1(MathLib::toLongNumber(varTok->strAt(2)));
            const MathLib::bigint num2(MathLib::toLongNumber(varTok->strAt(6)));
            if (num1 + 1 != num2)
                continue;

            // check how loop variable is used in loop..
            bool read = false;
            bool write = false;
            const Token* end = tok->linkAt(2)->next()->link();
            for (const Token *tok2 = tok->linkAt(2); tok2 != end; tok2 = tok2->next()) {
                if (tok2->str() == varname) {
                    if (tok2->previous()->isArithmeticalOp() &&
                        tok2->next() &&
                        (tok2->next()->isArithmeticalOp() || tok2->next()->str() == ";")) {
                        read = true;
                    } else {
                        read = write = true;
                        break;
                    }
                }
            }

            // Simplify loop if loop variable isn't written
            if (!write) {
                Token* bodyBegin = tok->linkAt(2)->next();
                // remove "for ("
                tok->deleteNext(2);

                // If loop variable is read then keep assignment before
                // loop body..
                if (type) {
                    tok->insertToken("{");
                    Token::createMutualLinks(tok->next(), bodyBegin->link());
                    bodyBegin->deleteThis();
                    tok = tok->tokAt(6);
                } else if (read) {
                    // goto ";"
                    tok = tok->tokAt(4);
                } else {
                    // remove "x = 0 ;"
                    tok->deleteNext(4);
                }

                // remove "x < 1 ; x ++ )"
                tok->deleteNext(7);

                if (!type) {
                    // Add assignment after the loop body so the loop variable
                    // get the correct end value
                    Token *tok2 = tok->next()->link();
                    tok2->insertToken(";");
                    tok2->insertToken(MathLib::toString(num2));
                    tok2->insertToken("=");
                    tok2->insertToken(varname);
                    tok2->next()->varId(varid);
                }
            }
        }
    }
}


void Tokenizer::removeRedundantSemicolons()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->link() && tok->str() == "(") {
            tok = tok->link();
            continue;
        }
        for (;;) {
            if (Token::simpleMatch(tok, "; ;")) {
                tok->deleteNext();
            } else if (Token::simpleMatch(tok, "; { ; }")) {
                tok->deleteNext(3);
            } else {
                break;
            }
        }
    }
}


bool Tokenizer::simplifyAddBraces()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        Token const * tokRet=simplifyAddBracesToCommand(tok);
        if (!tokRet)
            return false;
    }
    return true;
}

Token *Tokenizer::simplifyAddBracesToCommand(Token *tok)
{
    Token * tokEnd=tok;
    if (Token::Match(tok,"for|switch|BOOST_FOREACH")) {
        tokEnd=simplifyAddBracesPair(tok,true);
    } else if (tok->str()=="while") {
        Token *tokPossibleDo=tok->previous();
        if (Token::simpleMatch(tok->previous(), "{"))
            tokPossibleDo = nullptr;
        else if (Token::simpleMatch(tokPossibleDo,"}"))
            tokPossibleDo = tokPossibleDo->link();
        if (!tokPossibleDo || tokPossibleDo->strAt(-1) != "do")
            tokEnd=simplifyAddBracesPair(tok,true);
    } else if (tok->str()=="do") {
        tokEnd=simplifyAddBracesPair(tok,false);
        if (tokEnd!=tok) {
            // walk on to next token, i.e. "while"
            // such that simplifyAddBracesPair does not close other braces
            // before the "while"
            if (tokEnd) {
                tokEnd=tokEnd->next();
                if (!tokEnd || tokEnd->str()!="while") // no while
                    syntaxError(tok);
            }
        }
    } else if (tok->str()=="if" && !Token::simpleMatch(tok->tokAt(-2), "operator \"\"")) {
        tokEnd=simplifyAddBracesPair(tok,true);
        if (!tokEnd)
            return nullptr;
        if (tokEnd->strAt(1) == "else") {
            Token * tokEndNextNext= tokEnd->tokAt(2);
            if (!tokEndNextNext || tokEndNextNext->str() == "}")
                syntaxError(tokEndNextNext);
            if (tokEndNextNext->str() == "if")
                // do not change "else if ..." to "else { if ... }"
                tokEnd=simplifyAddBracesToCommand(tokEndNextNext);
            else
                tokEnd=simplifyAddBracesPair(tokEnd->next(),false);
        }
    }

    return tokEnd;
}

Token *Tokenizer::simplifyAddBracesPair(Token *tok, bool commandWithCondition)
{
    Token * tokCondition=tok->next();
    if (!tokCondition) // Missing condition
        return tok;

    Token *tokAfterCondition=tokCondition;
    if (commandWithCondition) {
        if (tokCondition->str()=="(")
            tokAfterCondition=tokCondition->link();
        else
            syntaxError(tok); // Bad condition

        if (!tokAfterCondition || tokAfterCondition->strAt(1) == "]")
            syntaxError(tok); // Bad condition

        tokAfterCondition=tokAfterCondition->next();
        if (!tokAfterCondition || Token::Match(tokAfterCondition, ")|}|,")) {
            // No tokens left where to add braces around
            return tok;
        }
    }
    // Skip labels
    Token * tokStatement = tokAfterCondition;
    while (true) {
        if (Token::Match(tokStatement, "%name% :"))
            tokStatement = tokStatement->tokAt(2);
        else if (tokStatement->str() == "case") {
            tokStatement = skipCaseLabel(tokStatement);
            if (!tokStatement)
                return tok;
            if (tokStatement->str() != ":")
                syntaxError(tokStatement);
            tokStatement = tokStatement->next();
        } else
            break;
        if (!tokStatement)
            return tok;
    }
    Token * tokBracesEnd=nullptr;
    if (tokStatement->str() == "{") {
        // already surrounded by braces
        if (tokStatement != tokAfterCondition) {
            // Move the opening brace before labels
            Token::move(tokStatement, tokStatement, tokAfterCondition->previous());
        }
        tokBracesEnd = tokStatement->link();
    } else if (Token::simpleMatch(tokStatement, "try {") &&
               Token::simpleMatch(tokStatement->linkAt(1), "} catch (")) {
        tokAfterCondition->previous()->insertToken("{");
        Token * tokOpenBrace = tokAfterCondition->previous();
        Token * tokEnd = tokStatement->linkAt(1)->linkAt(2)->linkAt(1);
        if (!tokEnd) {
            syntaxError(tokStatement);
        }
        tokEnd->insertToken("}");
        Token * tokCloseBrace = tokEnd->next();

        Token::createMutualLinks(tokOpenBrace, tokCloseBrace);
        tokBracesEnd = tokCloseBrace;
    } else {
        Token * tokEnd = simplifyAddBracesToCommand(tokStatement);
        if (!tokEnd) // Ticket #4887
            return tok;
        if (tokEnd->str()!="}") {
            // Token does not end with brace
            // Look for ; to add own closing brace after it
            while (tokEnd && !Token::Match(tokEnd, ";|)|}")) {
                if (tokEnd->tokType()==Token::eBracket || tokEnd->str() == "(") {
                    tokEnd = tokEnd->link();
                    if (!tokEnd) {
                        // Inner bracket does not close
                        return tok;
                    }
                }
                tokEnd=tokEnd->next();
            }
            if (!tokEnd || tokEnd->str() != ";") {
                // No trailing ;
                return tok;
            }
        }

        tokAfterCondition->previous()->insertToken("{");
        Token * tokOpenBrace=tokAfterCondition->previous();

        tokEnd->insertToken("}");
        Token * tokCloseBrace=tokEnd->next();

        Token::createMutualLinks(tokOpenBrace,tokCloseBrace);
        tokBracesEnd=tokCloseBrace;
    }

    return tokBracesEnd;
}

void Tokenizer::simplifyCompoundAssignment()
{
    // Simplify compound assignments:
    // "a+=b" => "a = a + b"
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "[;{}] (| *| (| %name%"))
            continue;
        if (tok->next()->str() == "return")
            continue;
        // backup current token..
        Token * const tok1 = tok;

        if (tok->next()->str() == "*")
            tok = tok->next();

        if (tok->next() && tok->next()->str() == "(") {
            tok = tok->next()->link()->next();
        } else {
            // variable..
            tok = tok->tokAt(2);
            while (Token::Match(tok, ". %name%") ||
                   Token::Match(tok, "[|(")) {
                if (tok->str() == ".")
                    tok = tok->tokAt(2);
                else {
                    // goto "]" or ")"
                    tok = tok->link();

                    // goto next token..
                    tok = tok ? tok->next() : nullptr;
                }
            }
        }
        if (!tok)
            break;

        // Is current token at a compound assignment: +=|-=|.. ?
        const std::string &str = tok->str();
        std::string op;  // operator used in assignment
        if (tok->isAssignmentOp() && str.size() == 2)
            op = str.substr(0, 1);
        else if (tok->isAssignmentOp() && str.size() == 3)
            op = str.substr(0, 2);
        else {
            tok = tok1;
            continue;
        }

        // Remove the whole statement if it says: "+=0;", "-=0;", "*=1;" or "/=1;"
        if (Token::Match(tok, "+=|-= 0 ;") ||
            Token::simpleMatch(tok, "|= 0 ;") ||
            Token::Match(tok, "*=|/= 1 ;")) {
            tok = tok1;
            while (tok->next()->str() != ";")
                tok->deleteNext();
        } else {
            // Enclose the rhs in parentheses..
            if (!Token::Match(tok->tokAt(2), "[;)]")) {
                // Only enclose rhs in parentheses if there is some operator
                bool someOperator = false;
                for (Token *tok2 = tok->next(); tok2; tok2 = tok2->next()) {
                    if (tok2->link() && Token::Match(tok2, "{|[|("))
                        tok2 = tok2->link();

                    if (Token::Match(tok2->next(), "[;)]")) {
                        if (someOperator) {
                            tok->insertToken("(");
                            tok2->insertToken(")");
                            Token::createMutualLinks(tok->next(), tok2->next());
                        }
                        break;
                    }

                    someOperator |= (tok2->isOp() || tok2->str() == "?");
                }
            }

            // simplify the compound assignment..
            tok->str("=");
            tok->insertToken(op);

            std::stack<Token *> tokend;
            for (Token *tok2 = tok->previous(); tok2 && tok2 != tok1; tok2 = tok2->previous()) {
                // Don't duplicate ++ and --. Put preincrement in lhs. Put
                // postincrement in rhs.
                if (tok2->tokType() == Token::eIncDecOp) {
                    // pre increment/decrement => don't copy
                    if (tok2->next()->isName()) {
                        continue;
                    }

                    // post increment/decrement => move from lhs to rhs
                    tok->insertToken(tok2->str());
                    tok2->deleteThis();
                    continue;
                }

                // Copy token from lhs to rhs
                tok->insertToken(tok2->str());
                tok->next()->varId(tok2->varId());
                if (Token::Match(tok->next(), "]|)|}"))
                    tokend.push(tok->next());
                else if (Token::Match(tok->next(), "(|[|{")) {
                    Token::createMutualLinks(tok->next(), tokend.top());
                    tokend.pop();
                }
            }
        }
    }
}

bool Tokenizer::simplifyConditions()
{
    bool ret = false;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "! %bool%|%num%")) {
            tok->deleteThis();
            if (Token::Match(tok, "0|false"))
                tok->str("true");
            else
                tok->str("false");

            ret = true;
        }

        if (Token::simpleMatch(tok, "&& true &&")) {
            tok->deleteNext(2);
            ret = true;
        }

        else if (Token::simpleMatch(tok, "|| false ||")) {
            tok->deleteNext(2);
            ret = true;
        }

        else if (Token::Match(tok, "(|&& true && true &&|)")) {
            tok->deleteNext(2);
            ret = true;
        }

        else if (Token::Match(tok, "%oror%|( false %oror% false %oror%|)")) {
            tok->deleteNext(2);
            ret = true;
        }

        else if (Token::simpleMatch(tok, "( true ||") ||
                 Token::simpleMatch(tok, "( false &&")) {
            Token::eraseTokens(tok->next(), tok->link());
            ret = true;
        }

        else if (Token::simpleMatch(tok, "|| true )") ||
                 Token::simpleMatch(tok, "&& false )")) {
            tok = tok->next();
            Token::eraseTokens(tok->next()->link(), tok);
            ret = true;
        }

        else if (Token::simpleMatch(tok, "&& false &&") ||
                 Token::simpleMatch(tok, "|| true ||")) {
            //goto '('
            Token *tok2 = tok;
            while (tok2 && tok2->previous()) {
                if (tok2->previous()->str() == ")")
                    tok2 = tok2->previous()->link();
                else {
                    tok2 = tok2->previous();
                    if (tok2->str() == "(")
                        break;
                }
            }
            if (!tok2)
                continue;
            //move tok to 'true|false' position
            tok = tok->next();
            //remove everything before 'true|false'
            Token::eraseTokens(tok2, tok);
            //remove everything after 'true|false'
            Token::eraseTokens(tok, tok2->link());
            ret = true;
        }

        // Change numeric constant in condition to "true" or "false"
        if (Token::Match(tok, "if|while ( %num% )|%oror%|&&")) {
            tok->tokAt(2)->str((tok->strAt(2) != "0") ? "true" : "false");
            ret = true;
        }
        if (Token::Match(tok, "&&|%oror% %num% )|%oror%|&&")) {
            tok->next()->str((tok->next()->str() != "0") ? "true" : "false");
            ret = true;
        }

        // Reduce "(%num% == %num%)" => "(true)"/"(false)"
        if (Token::Match(tok, "&&|%oror%|(") &&
            (Token::Match(tok->next(), "%num% %any% %num%") ||
             Token::Match(tok->next(), "%bool% %any% %bool%")) &&
            Token::Match(tok->tokAt(4), "&&|%oror%|)|?")) {
            std::string cmp = tok->strAt(2);
            bool result = false;
            if (tok->next()->isNumber()) {
                // Compare numbers

                if (cmp == "==" || cmp == "!=") {
                    const std::string& op1(tok->next()->str());
                    const std::string& op2(tok->strAt(3));

                    bool eq = false;
                    if (MathLib::isInt(op1) && MathLib::isInt(op2))
                        eq = (MathLib::toLongNumber(op1) == MathLib::toLongNumber(op2));
                    else {
                        eq = (op1 == op2);

                        // It is inconclusive whether two unequal float representations are numerically equal
                        if (!eq && MathLib::isFloat(op1))
                            cmp.clear();
                    }

                    if (cmp == "==")
                        result = eq;
                    else
                        result = !eq;
                } else {
                    const double op1 = MathLib::toDoubleNumber(tok->next()->str());
                    const double op2 = MathLib::toDoubleNumber(tok->strAt(3));
                    if (cmp == ">=")
                        result = (op1 >= op2);
                    else if (cmp == ">")
                        result = (op1 > op2);
                    else if (cmp == "<=")
                        result = (op1 <= op2);
                    else if (cmp == "<")
                        result = (op1 < op2);
                    else
                        cmp.clear();
                }
            } else {
                // Compare boolean
                const bool op1 = (tok->next()->str() == std::string("true"));
                const bool op2 = (tok->strAt(3) == std::string("true"));

                if (cmp == "==")
                    result = (op1 == op2);
                else if (cmp == "!=")
                    result = (op1 != op2);
                else if (cmp == ">=")
                    result = (op1 >= op2);
                else if (cmp == ">")
                    result = (op1 > op2);
                else if (cmp == "<=")
                    result = (op1 <= op2);
                else if (cmp == "<")
                    result = (op1 < op2);
                else
                    cmp.clear();
            }

            if (!cmp.empty()) {
                tok = tok->next();
                tok->deleteNext(2);

                tok->str(result ? "true" : "false");
                ret = true;
            }
        }
    }

    return ret;
}

bool Tokenizer::simplifyConstTernaryOp()
{
    bool ret = false;
    const Token *templateParameterEnd = nullptr; // The end of the current template parameter list, if any
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "<" && TemplateSimplifier::templateParameters(tok))
            templateParameterEnd = tok->findClosingBracket();
        if (tok == templateParameterEnd)
            templateParameterEnd = nullptr; // End of the current template parameter list
        if (tok->str() != "?")
            continue;

        if (!Token::Match(tok->tokAt(-2), "<|=|,|(|[|{|}|;|case|return %bool%|%num%") &&
            !Token::Match(tok->tokAt(-4), "<|=|,|(|[|{|}|;|case|return ( %bool%|%num% )"))
            continue;

        const int offset = (tok->previous()->str() == ")") ? 2 : 1;

        if (tok->strAt(-2*offset) == "<") {
            if (isC() || !TemplateSimplifier::templateParameters(tok->tokAt(-2*offset)))
                continue; // '<' is less than; the condition is not a constant
        }

        // Find the token ":" then go to the next token
        Token *colon = skipTernaryOp(tok);
        if (!colon || colon->previous()->str() != ":" || !colon->next())
            continue;

        //handle the GNU extension: "x ? : y" <-> "x ? x : y"
        if (colon->previous() == tok->next())
            tok->insertToken(tok->strAt(-offset));

        // go back before the condition, if possible
        tok = tok->tokAt(-2);
        if (offset == 2) {
            // go further back before the "("
            tok = tok->tokAt(-2);
            //simplify the parentheses
            tok->deleteNext();
            tok->next()->deleteNext();
        }

        if (Token::Match(tok->next(), "false|0")) {
            // Use code after colon, remove code before it.
            Token::eraseTokens(tok, colon);

            tok = tok->next();
            ret = true;
        }

        // The condition is true. Delete the operator after the ":"..
        else {
            // delete the condition token and the "?"
            tok->deleteNext(2);

            int ternaryOplevel = 0;
            for (const Token *endTok = colon; endTok; endTok = endTok->next()) {
                if (Token::Match(endTok, "(|[|{"))
                    endTok = endTok->link();
                else if (endTok->str() == "<" && (endTok->strAt(1) == ">" || TemplateSimplifier::templateParameters(endTok)))
                    endTok = endTok->findClosingBracket();
                else if (endTok->str() == "?")
                    ++ternaryOplevel;
                else if (Token::Match(endTok, ")|}|]|;|,|:|>")) {
                    if (endTok->str() == ":" && ternaryOplevel)
                        --ternaryOplevel;
                    else if (endTok->str() == ">" && !templateParameterEnd)
                        ;
                    else {
                        Token::eraseTokens(colon->tokAt(-2), endTok);
                        ret = true;
                        break;
                    }
                }
            }
        }
    }
    return ret;
}

void Tokenizer::simplifyUndefinedSizeArray()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%type%")) {
            Token *tok2 = tok->next();
            while (tok2 && tok2->str() == "*")
                tok2 = tok2->next();
            if (!Token::Match(tok2, "%name% [ ] ;|["))
                continue;

            tok = tok2->previous();
            Token *end = tok2->next();
            int count = 0;
            do {
                end = end->tokAt(2);
                ++count;
            } while (Token::Match(end, "[ ] [;=[]"));
            if (Token::Match(end, "[;=]")) {
                do {
                    tok2->deleteNext(2);
                    tok->insertToken("*");
                } while (--count);
                tok = end;
            } else
                tok = tok->tokAt(3);
        }
    }
}

void Tokenizer::simplifyCasts()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // Don't remove cast in such cases:
        // *((char *)a + 1) = 0;
        // Remove cast when casting a function pointer:
        // (*(void (*)(char *))fp)(x);
        if (!tok->isName() &&
            Token::simpleMatch(tok->next(), "* (") &&
            !Token::Match(tok->linkAt(2), ") %name%|&")) {
            tok = tok->linkAt(2);
            continue;
        }
        // #3935 : don't remove cast in such cases:
        // ((char *)a)[1] = 0;
        if (tok->str() == "(" && Token::simpleMatch(tok->link(), ") [")) {
            tok = tok->link();
            continue;
        }
        // #4164 : ((unsigned char)1) => (1)
        if (Token::Match(tok->next(), "( %type% ) %num%") && tok->next()->link()->previous()->isStandardType()) {
            const MathLib::bigint value = MathLib::toLongNumber(tok->next()->link()->next()->str());
            int bits = mSettings->char_bit * mTypeSize[tok->next()->link()->previous()->str()];
            if (!tok->tokAt(2)->isUnsigned() && bits > 0)
                bits--;
            if (bits < 31 && value >= 0 && value < (1LL << bits)) {
                tok->linkAt(1)->next()->isCast(true);
                Token::eraseTokens(tok, tok->next()->link()->next());
            }
            continue;
        }

        while ((Token::Match(tok->next(), "( %type% *| *| *|&| ) *|&| %name%") && (tok->str() != ")" || tok->tokAt(2)->isStandardType())) ||
               Token::Match(tok->next(), "( const| %type% * *| *|&| ) *|&| %name%") ||
               Token::Match(tok->next(), "( const| %type% %type% *| *| *|&| ) *|&| %name%") ||
               (!tok->isName() && (Token::Match(tok->next(), "( %type% * *| *|&| ) (") ||
                                   Token::Match(tok->next(), "( const| %type% %type% * *| *|&| ) (")))) {
            if (tok->isName() && tok->str() != "return")
                break;

            if (isCPP() && tok->strAt(-1) == "operator")
                break;

            // Remove cast..
            Token::eraseTokens(tok, tok->next()->link()->next());

            // Set isCasted flag.
            Token *tok2 = tok->next();
            if (!Token::Match(tok2, "%name% [|."))
                tok2->isCast(true);
            else {
                // TODO: handle more complex expressions
                tok2->next()->isCast(true);
            }

            // Remove '* &'
            if (Token::simpleMatch(tok, "* &")) {
                tok->deleteNext();
                tok->deleteThis();
            }

            if (tok->str() == ")" && tok->link()->previous()) {
                // If there was another cast before this, go back
                // there to check it also. e.g. "(int)(char)x"
                tok = tok->link()->previous();
            }
        }

        // Replace pointer casts of 0.. "(char *)0" => "0"
        while (Token::Match(tok->next(), "( %type% %type%| * *| ) 0")) {
            tok->linkAt(1)->next()->isCast(true);
            Token::eraseTokens(tok, tok->next()->link()->next());
            if (tok->str() == ")" && tok->link()->previous()) {
                // If there was another cast before this, go back
                // there to check it also. e.g. "(char*)(char*)0"
                tok = tok->link()->previous();
            }
        }

        if (Token::Match(tok->next(), "dynamic_cast|reinterpret_cast|const_cast|static_cast <")) {
            Token *tok2 = tok->linkAt(2);
            if (!Token::simpleMatch(tok2, "> ("))
                break;

            tok2->tokAt(2)->isCast(true);
            Token::eraseTokens(tok, tok2->next());
        }
    }
}


void Tokenizer::simplifyFunctionParameters()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->link() && Token::Match(tok, "{|[|(")) {
            tok = tok->link();
        }

        // Find the function e.g. foo( x ) or foo( x, y )
        else if (Token::Match(tok, "%name% ( %name% [,)]") &&
                 !(tok->strAt(-1) == ":" || tok->strAt(-1) == "," || tok->strAt(-1) == "::")) {
            // We have found old style function, now we need to change it

            // First step: Get list of argument names in parentheses
            std::map<std::string, Token *> argumentNames;
            bool bailOut = false;
            Token * tokparam = nullptr;

            //take count of the function name..
            const std::string& funcName(tok->str());

            //floating token used to check for parameters
            Token *tok1 = tok;

            while (nullptr != (tok1 = tok1->tokAt(2))) {
                if (!Token::Match(tok1, "%name% [,)]")) {
                    bailOut = true;
                    break;
                }

                //same parameters: take note of the parameter
                if (argumentNames.find(tok1->str()) != argumentNames.end())
                    tokparam = tok1;
                else if (tok1->str() != funcName)
                    argumentNames[tok1->str()] = tok1;
                else {
                    if (tok1->next()->str() == ")") {
                        if (tok1->previous()->str() == ",") {
                            tok1 = tok1->tokAt(-2);
                            tok1->deleteNext(2);
                        } else {
                            tok1 = tok1->previous();
                            tok1->deleteNext();
                            bailOut = true;
                            break;
                        }
                    } else {
                        tok1 = tok1->tokAt(-2);
                        tok1->next()->deleteNext(2);
                    }
                }

                if (tok1->next()->str() == ")") {
                    tok1 = tok1->tokAt(2);
                    //expect at least a type name after round brace..
                    if (!tok1 || !tok1->isName())
                        bailOut = true;
                    break;
                }
            }

            //goto '('
            tok = tok->next();

            if (bailOut) {
                tok = tok->link();
                continue;
            }

            tok1 = tok->link()->next();

            // there should be the sequence '; {' after the round parentheses
            for (const Token* tok2 = tok1; tok2; tok2 = tok2->next()) {
                if (Token::simpleMatch(tok2, "; {"))
                    break;
                else if (tok2->str() == "{") {
                    bailOut = true;
                    break;
                }
            }

            if (bailOut) {
                tok = tok->link();
                continue;
            }

            // Last step: check out if the declarations between ')' and '{' match the parameters list
            std::map<std::string, Token *> argumentNames2;

            while (tok1 && tok1->str() != "{") {
                if (Token::Match(tok1, "(|)")) {
                    bailOut = true;
                    break;
                }
                if (tok1->str() == ";") {
                    if (tokparam) {
                        syntaxError(tokparam);
                    }
                    Token *tok2 = tok1->previous();
                    while (tok2->str() == "]")
                        tok2 = tok2->link()->previous();

                    //it should be a name..
                    if (!tok2->isName()) {
                        bailOut = true;
                        break;
                    }

                    if (argumentNames2.find(tok2->str()) != argumentNames2.end()) {
                        //same parameter names...
                        syntaxError(tok1);
                    } else
                        argumentNames2[tok2->str()] = tok2;

                    if (argumentNames.find(tok2->str()) == argumentNames.end()) {
                        //non-matching parameter... bailout
                        bailOut = true;
                        break;
                    }
                }
                tok1 = tok1->next();
            }

            if (bailOut || !tok1) {
                tok = tok->link();
                continue;
            }

            //the two containers may not hold the same size...
            //in that case, the missing parameters are defined as 'int'
            if (argumentNames.size() != argumentNames2.size()) {
                //move back 'tok1' to the last ';'
                tok1 = tok1->previous();
                for (std::pair<const std::string, Token *>& argumentName : argumentNames) {
                    if (argumentNames2.find(argumentName.first) == argumentNames2.end()) {
                        //add the missing parameter argument declaration
                        tok1->insertToken(";");
                        tok1->insertToken(argumentName.first);
                        //register the change inside argumentNames2
                        argumentNames2[argumentName.first] = tok1->next();
                        tok1->insertToken("int");
                    }
                }
            }

            while (tok->str() != ")") {
                //initialize start and end tokens to be moved
                Token *declStart = argumentNames2[tok->next()->str()];
                Token *declEnd = declStart;
                while (declStart->previous()->str() != ";" && declStart->previous()->str() != ")")
                    declStart = declStart->previous();
                while (declEnd->next()->str() != ";" && declEnd->next()->str() != "{")
                    declEnd = declEnd->next();

                //remove ';' after declaration
                declEnd->deleteNext();

                //replace the parameter name in the parentheses with all the declaration
                Token::replace(tok->next(), declStart, declEnd);

                //since there are changes to tokens, put tok where tok1 is
                tok = declEnd->next();

                //fix up line number
                if (tok->str() == ",")
                    tok->linenr(tok->previous()->linenr());
            }
            //goto forward and continue
            tok = tok->next()->link();
        }
    }
}

void Tokenizer::simplifyPointerToStandardType()
{
    if (!isC())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "& %name% [ 0 ] !!["))
            continue;

        if (!Token::Match(tok->previous(), "[,(=]"))
            continue;

        // Remove '[ 0 ]' suffix
        Token::eraseTokens(tok->next(), tok->tokAt(5));
        // Remove '&' prefix
        tok = tok->previous();
        if (!tok)
            break;
        tok->deleteNext();
    }
}

void Tokenizer::simplifyFunctionPointers()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // #2873 - do not simplify function pointer usage here:
        // (void)(xy(*p)(0));
        if (Token::simpleMatch(tok, ") (")) {
            tok = tok->next()->link();
            continue;
        }

        // check for function pointer cast
        if (Token::Match(tok, "( %type% %type%| *| *| ( * ) (") ||
            Token::Match(tok, "static_cast < %type% %type%| *| *| ( * ) (")) {
            Token *tok1 = tok;

            if (isCPP() && tok1->str() == "static_cast")
                tok1 = tok1->next();

            tok1 = tok1->next();

            if (Token::Match(tok1->next(), "%type%"))
                tok1 = tok1->next();

            while (tok1->next()->str() == "*")
                tok1 = tok1->next();

            // check that the cast ends
            if (!Token::Match(tok1->linkAt(4), ") )|>"))
                continue;

            // ok simplify this function pointer cast to an ordinary pointer cast
            tok1->deleteNext();
            tok1->next()->deleteNext();
            Token::eraseTokens(tok1->next(), tok1->linkAt(2)->next());
            continue;
        }

        // check for start of statement
        else if (tok->previous() && !Token::Match(tok->previous(), "{|}|;|,|(|public:|protected:|private:"))
            continue;

        if (Token::Match(tok, "delete|else|return|throw|typedef"))
            continue;

        while (Token::Match(tok, "%type%|:: %type%|::"))
            tok = tok->next();

        Token *tok2 = (tok && tok->isName()) ? tok->next() : nullptr;
        while (Token::Match(tok2, "*|&"))
            tok2 = tok2->next();
        if (!tok2 || tok2->str() != "(")
            continue;
        while (Token::Match(tok2, "(|:: %type%"))
            tok2 = tok2->tokAt(2);
        if (!Token::Match(tok2, "(|:: * *| %name%"))
            continue;
        tok2 = tok2->tokAt(2);
        if (tok2->str() == "*")
            tok2 = tok2->next();
        while (Token::Match(tok2, "%type%|:: %type%|::"))
            tok2 = tok2->next();

        if (!Token::Match(tok2, "%name% ) (") &&
            !Token::Match(tok2, "%name% [ ] ) (") &&
            !(Token::Match(tok2, "%name% (") && Token::simpleMatch(tok2->linkAt(1), ") ) (")))
            continue;

        while (tok && tok->str() != "(")
            tok = tok->next();

        // check that the declaration ends
        if (!tok || !tok->link() || !tok->link()->next()) {
            syntaxError(nullptr);
        }
        Token *endTok = tok->link()->next()->link();
        if (Token::simpleMatch(endTok, ") throw ("))
            endTok = endTok->linkAt(2);
        if (!Token::Match(endTok, ") const|volatile| const|volatile| ;|,|)|=|[|{"))
            continue;

        while (Token::Match(endTok->next(), "const|volatile"))
            endTok->deleteNext();

        // ok simplify this function pointer to an ordinary pointer
        Token::eraseTokens(tok->link(), endTok->next());
        if (Token::simpleMatch(tok->link()->previous(), ") )")) {
            // Function returning function pointer
            // void (*dostuff(void))(void) {}
            tok->link()->deleteThis();
            tok->deleteThis();
        } else {
            // Function pointer variable
            // void (*p)(void) {}
            tok->link()->insertToken("(");
            Token *par1 = tok->link()->next();
            par1->insertToken(")");
            par1->link(par1->next());
            par1->next()->link(par1);
            while (Token::Match(tok, "( %type% ::"))
                tok->deleteNext(2);
        }
    }
}


bool Tokenizer::simplifyFunctionReturn()
{
    std::map<std::string, const Token*> functions;

    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (tok->str() == "{")
            tok = tok->link();

        else if (Token::Match(tok, "%name% ( ) { return %bool%|%char%|%num%|%str% ; }") && tok->strAt(-1) != "::") {
            const Token* const any = tok->tokAt(5);
            functions[tok->str()] = any;
            tok = any;
        }
    }

    if (functions.empty())
        return false;

    bool ret = false;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "(|[|=|return|%op% %name% ( ) ;|]|)|%cop%")) {
            tok = tok->next();
            auto it = functions.find(tok->str());
            if (it != functions.cend()) {
                tok->str(it->second->str());
                tok->deleteNext(2);
                ret = true;
            }
        }
    }

    return ret;
}

void Tokenizer::simplifyVarDecl(const bool only_k_r_fpar)
{
    simplifyVarDecl(list.front(), nullptr, only_k_r_fpar);
}

void Tokenizer::simplifyVarDecl(Token * tokBegin, const Token * const tokEnd, const bool only_k_r_fpar)
{
    const bool isCPP11  = mSettings->standards.cpp >= Standards::CPP11;

    // Split up variable declarations..
    // "int a=4;" => "int a; a=4;"
    bool finishedwithkr = true;
    bool scopeDecl = false;
    for (Token *tok = tokBegin; tok != tokEnd; tok = tok->next()) {
        if (Token::Match(tok, "{|;"))
            scopeDecl = false;
        if (isCPP()) {
            if (Token::Match(tok, "class|struct|namespace|union"))
                scopeDecl = true;
            if (Token::Match(tok, "decltype|noexcept (")) {
                tok = tok->next()->link();
                // skip decltype(...){...}
                if (tok && Token::simpleMatch(tok->previous(), ") {"))
                    tok = tok->link();
            } else if (Token::simpleMatch(tok, "= {") ||
                       (!scopeDecl && Token::Match(tok, "%name%|> {") &&
                        !Token::Match(tok, "else|try|do|const|constexpr|override|volatile|noexcept"))) {
                if (!tok->next()->link())
                    syntaxError(tokBegin);
                // Check for lambdas before skipping
                for (Token* tok2 = tok->next(); tok2 != tok->next()->link(); tok2 = tok2->next()) {
                    Token* lambdaEnd = findLambdaEndScope(tok2);
                    if (!lambdaEnd)
                        continue;
                    simplifyVarDecl(lambdaEnd->link()->next(), lambdaEnd, only_k_r_fpar);
                }
                tok = tok->next()->link();
            }

        } else if (Token::simpleMatch(tok, "= {")) {
            tok = tok->next()->link();
        }
        if (!tok) {
            syntaxError(tokBegin);
        }
        if (only_k_r_fpar && finishedwithkr) {
            if (Token::Match(tok, "(|[|{")) {
                tok = tok->link();
                if (tok->next() && Token::Match(tok, ") !!{"))
                    tok = tok->next();
                else
                    continue;
            } else
                continue;
        } else if (tok->str() == "(") {
            if (isCPP()) {
                for (Token * tok2 = tok; tok2 && tok2 != tok->link(); tok2 = tok2->next()) {
                    if (Token::Match(tok2, "[(,] [")) {
                        // lambda function at tok2->next()
                        // find start of lambda body
                        Token * lambdaBody = tok2;
                        while (lambdaBody && lambdaBody != tok2->link() && lambdaBody->str() != "{")
                            lambdaBody = lambdaBody->next();
                        if (lambdaBody && lambdaBody != tok2->link() && lambdaBody->link())
                            simplifyVarDecl(lambdaBody, lambdaBody->link()->next(), only_k_r_fpar);
                    }
                }
            }
            tok = tok->link();
        }

        if (!tok)
            syntaxError(nullptr); // #7043 invalid code
        if (tok->previous() && !Token::Match(tok->previous(), "{|}|;|)|public:|protected:|private:"))
            continue;
        if (Token::simpleMatch(tok, "template <"))
            continue;

        Token *type0 = tok;
        if (!Token::Match(type0, "::|extern| %type%"))
            continue;
        if (Token::Match(type0, "else|return|public:|protected:|private:"))
            continue;
        if (isCPP11 && type0->str() == "using")
            continue;
        if (isCPP() && type0->str() == "namespace")
            continue;

        bool isconst = false;
        bool isstatic = false;
        Token *tok2 = type0;
        int typelen = 1;

        if (Token::Match(tok2, "::|extern")) {
            tok2 = tok2->next();
            typelen++;
        }

        //check if variable is declared 'const' or 'static' or both
        while (tok2) {
            if (!Token::Match(tok2, "const|static|constexpr") && Token::Match(tok2, "%type% const|static")) {
                tok2 = tok2->next();
                ++typelen;
            }

            if (Token::Match(tok2, "const|constexpr"))
                isconst = true;

            else if (Token::Match(tok2, "static|constexpr"))
                isstatic = true;

            else if (Token::Match(tok2, "%type% :: %type%")) {
                tok2 = tok2->next();
                ++typelen;
            }

            else
                break;

            if (tok2->strAt(1) == "*")
                break;

            if (Token::Match(tok2->next(), "& %name% ,"))
                break;

            tok2 = tok2->next();
            ++typelen;
        }

        // strange looking variable declaration => don't split up.
        if (Token::Match(tok2, "%type% *|&| %name% , %type% *|&| %name%"))
            continue;

        if (Token::Match(tok2, "struct|union|class %type%")) {
            tok2 = tok2->next();
            ++typelen;
        }

        // check for qualification..
        if (Token::Match(tok2,  ":: %type%")) {
            ++typelen;
            tok2 = tok2->next();
        }

        //skip combinations of templates and namespaces
        while (!isC() && (Token::Match(tok2, "%type% <") || Token::Match(tok2, "%type% ::"))) {
            if (tok2->next()->str() == "<" && !TemplateSimplifier::templateParameters(tok2->next())) {
                tok2 = nullptr;
                break;
            }
            typelen += 2;
            tok2 = tok2->tokAt(2);
            if (tok2 && tok2->previous()->str() == "::")
                continue;
            int indentlevel = 0;
            int parens = 0;

            for (Token *tok3 = tok2; tok3; tok3 = tok3->next()) {
                ++typelen;

                if (!parens && tok3->str() == "<") {
                    ++indentlevel;
                } else if (!parens && tok3->str() == ">") {
                    if (indentlevel == 0) {
                        tok2 = tok3->next();
                        break;
                    }
                    --indentlevel;
                } else if (!parens && tok3->str() == ">>") {
                    if (indentlevel <= 1) {
                        tok2 = tok3->next();
                        break;
                    }
                    indentlevel -= 2;
                } else if (tok3->str() == "(") {
                    ++parens;
                } else if (tok3->str() == ")") {
                    if (!parens) {
                        tok2 = nullptr;
                        break;
                    }
                    --parens;
                } else if (tok3->str() == ";") {
                    break;
                }
            }

            if (Token::Match(tok2,  ":: %type%")) {
                ++typelen;
                tok2 = tok2->next();
            }
        }

        //pattern: "%type% *| ... *| const| %name% ,|="
        if (Token::Match(tok2, "%type%") ||
            (tok2 && tok2->previous() && tok2->previous()->str() == ">")) {
            Token *varName = tok2;
            if (!tok2->previous() || tok2->previous()->str() != ">")
                varName = varName->next();
            else
                --typelen;
            //skip all the pointer part
            bool isPointerOrRef = false;
            while (Token::simpleMatch(varName, "*") || Token::Match(varName, "& %name% ,")) {
                isPointerOrRef = true;
                varName = varName->next();
            }

            while (Token::Match(varName, "%type% %type%")) {
                if (varName->str() != "const") {
                    ++typelen;
                }
                varName = varName->next();
            }
            // Function pointer
            if (Token::simpleMatch(varName, "( *") && Token::Match(varName->link()->previous(), "%name% ) ( ) =")) {
                Token *endDecl = varName->link()->tokAt(2);
                varName = varName->link()->previous();
                endDecl->insertToken(";");
                endDecl = endDecl->next();
                endDecl->insertToken(varName->str());
                continue;
            }
            //non-VLA case
            else if (Token::Match(varName, "%name% ,|=")) {
                if (varName->str() != "operator") {
                    tok2 = varName->next(); // The ',' or '=' token

                    if (tok2->str() == "=" && (isstatic || (isconst && !isPointerOrRef))) {
                        //do not split const non-pointer variables..
                        while (tok2 && tok2->str() != "," && tok2->str() != ";") {
                            if (Token::Match(tok2, "{|(|["))
                                tok2 = tok2->link();
                            const Token *tok3 = tok2;
                            if (!isC() && tok2->str() == "<" && TemplateSimplifier::templateParameters(tok2) > 0) {
                                tok2 = tok2->findClosingBracket();
                            }
                            if (!tok2)
                                syntaxError(tok3); // #6881 invalid code
                            tok2 = tok2->next();
                        }
                        if (tok2 && tok2->str() == ";")
                            tok2 = nullptr;
                    }
                } else
                    tok2 = nullptr;
            }

            //VLA case
            else if (Token::Match(varName, "%name% [")) {
                tok2 = varName->next();

                while (Token::Match(tok2->link(), "] ,|=|["))
                    tok2 = tok2->link()->next();
                if (!Token::Match(tok2, "=|,"))
                    tok2 = nullptr;
                if (tok2 && tok2->str() == "=") {
                    while (tok2 && tok2->str() != "," && tok2->str() != ";") {
                        if (Token::Match(tok2, "{|(|["))
                            tok2 = tok2->link();
                        tok2 = tok2->next();
                    }
                    if (tok2 && tok2->str() == ";")
                        tok2 = nullptr;
                }
            }

            // brace initialization
            else if (Token::Match(varName, "%name% {")) {
                tok2 = varName->next();
                tok2 = tok2->link();
                if (tok2)
                    tok2 = tok2->next();
                if (tok2 && tok2->str() != ",")
                    tok2 = nullptr;
            }

            // parenthesis, functions can't be declared like:
            // int f1(a,b), f2(c,d);
            // so if there is a comma assume this is a variable declaration
            else if (Token::Match(varName, "%name% (") && Token::simpleMatch(varName->linkAt(1), ") ,")) {
                tok2 = varName->linkAt(1)->next();
            }

            else
                tok2 = nullptr;
        } else {
            tok2 = nullptr;
        }

        if (!tok2) {
            if (only_k_r_fpar)
                finishedwithkr = false;
            continue;
        }

        if (tok2->str() == ",") {
            tok2->str(";");
            tok2->isSplittedVarDeclComma(true);
            //TODO: should we have to add also template '<>' links?
            TokenList::insertTokens(tok2, type0, typelen);
        }

        else {
            Token *eq = tok2;

            while (tok2) {
                if (Token::Match(tok2, "{|(|["))
                    tok2 = tok2->link();

                else if (!isC() && tok2->str() == "<" && tok2->previous()->isName() && !tok2->previous()->varId())
                    tok2 = tok2->findClosingBracket();

                else if (std::strchr(";,", tok2->str()[0])) {
                    // "type var ="   =>   "type var; var ="
                    const Token *varTok = type0->tokAt(typelen);
                    while (Token::Match(varTok, "%name%|*|& %name%|*|&"))
                        varTok = varTok->next();
                    if (!varTok)
                        syntaxError(tok2); // invalid code
                    TokenList::insertTokens(eq, varTok, 2);
                    eq->str(";");
                    eq->isSplittedVarDeclEq(true);

                    // "= x, "   =>   "= x; type "
                    if (tok2->str() == ",") {
                        tok2->str(";");
                        tok2->isSplittedVarDeclComma(true);
                        TokenList::insertTokens(tok2, type0, typelen);
                    }
                    break;
                }
                if (tok2)
                    tok2 = tok2->next();
            }
        }
        finishedwithkr = (only_k_r_fpar && tok2 && tok2->strAt(1) == "{");
    }
}

void Tokenizer::simplifyStaticConst()
{
    // This function will simplify the token list so that the qualifiers "extern", "static"
    // and "const" appear in the same order as in the array below.
    const std::string qualifiers[] = {"extern", "static", "const"};

    // Move 'const' before all other qualifiers and types and then
    // move 'static' before all other qualifiers and types, ...
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        bool continue2 = false;
        for (int i = 0; i < sizeof(qualifiers)/sizeof(qualifiers[0]); i++) {

            // Keep searching for a qualifier
            if (!tok->next() || tok->next()->str() != qualifiers[i])
                continue;

            // Look backwards to find the beginning of the declaration
            Token* leftTok = tok;
            bool behindOther = false;
            for (; leftTok; leftTok = leftTok->previous()) {
                for (int j = 0; j <= i; j++) {
                    if (leftTok->str() == qualifiers[j]) {
                        behindOther = true;
                        break;
                    }
                }
                if (behindOther)
                    break;
                if (!Token::Match(leftTok, "%type%|struct|::") ||
                    (isCPP() && Token::Match(leftTok, "private:|protected:|public:|operator|template"))) {
                    break;
                }
            }

            // The token preceding the declaration should indicate the start of a declaration
            if (leftTok == tok)
                continue;

            if (leftTok && !behindOther && !Token::Match(leftTok, ";|{|}|(|,|private:|protected:|public:")) {
                continue2 = true;
                break;
            }

            // Move the qualifier to the left-most position in the declaration
            tok->deleteNext();
            if (!leftTok) {
                list.front()->insertToken(qualifiers[i], emptyString, false);
                list.front()->swapWithNext();
                tok = list.front();
            } else if (leftTok->next()) {
                leftTok->next()->insertToken(qualifiers[i], emptyString, true);
                tok = leftTok->next();
            } else {
                leftTok->insertToken(qualifiers[i]);
                tok = leftTok;
            }
        }
        if (continue2)
            continue;
    }
}

void Tokenizer::simplifyIfAndWhileAssign()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok->next(), "if|while ("))
            continue;

        const Token* tokAt3 = tok->tokAt(3);
        if (!Token::Match(tokAt3, "!| (| %name% =") &&
            !Token::Match(tokAt3, "!| (| %name% . %name% =") &&
            !Token::Match(tokAt3, "0 == (| %name% =") &&
            !Token::Match(tokAt3, "0 == (| %name% . %name% ="))
            continue;

        // simplifying a "while(cond) { }" condition ?
        const bool iswhile(tok->next()->str() == "while");

        // simplifying a "do { } while(cond);" condition ?
        const bool isDoWhile = iswhile && Token::simpleMatch(tok, "}") && Token::simpleMatch(tok->link()->previous(), "do");
        Token* openBraceTok = tok->link();

        // delete the "if|while"
        tok->deleteNext();

        // Remember if there is a "!" or not. And delete it if there are.
        const bool isNot(Token::Match(tok->tokAt(2), "!|0"));
        if (isNot)
            tok->next()->deleteNext((tok->strAt(2) == "0") ? 2 : 1);

        // Delete parentheses.. and remember how many there are with
        // their links.
        std::stack<Token *> braces;
        while (tok->next()->str() == "(") {
            braces.push(tok->next()->link());
            tok->deleteNext();
        }

        // Skip the "%name% = ..."
        Token *tok2;
        for (tok2 = tok->next(); tok2; tok2 = tok2->next()) {
            if (tok2->str() == "(")
                tok2 = tok2->link();
            else if (tok2->str() == ")")
                break;
        }

        // Insert "; if|while ( .."
        tok2 = tok2->previous();
        if (tok->strAt(2) == ".") {
            tok2->insertToken(tok->strAt(3));
            tok2->next()->varId(tok->tokAt(3)->varId());
            tok2->insertToken(".");
        }
        tok2->insertToken(tok->next()->str());
        tok2->next()->varId(tok->next()->varId());

        while (!braces.empty()) {
            tok2->insertToken("(");
            Token::createMutualLinks(tok2->next(), braces.top());
            braces.pop();
        }

        if (isNot)
            tok2->next()->insertToken("!");
        tok2->insertToken(iswhile ? "while" : "if");
        if (isDoWhile) {
            tok2->insertToken("}");
            Token::createMutualLinks(openBraceTok, tok2->next());
        }

        tok2->insertToken(";");

        // delete the extra "}"
        if (isDoWhile)
            tok->deleteThis();

        // If it's a while loop, insert the assignment in the loop
        if (iswhile && !isDoWhile) {
            int indentlevel = 0;
            Token *tok3 = tok2;

            for (; tok3; tok3 = tok3->next()) {
                if (tok3->str() == "{")
                    ++indentlevel;
                else if (tok3->str() == "}") {
                    if (indentlevel <= 1)
                        break;
                    --indentlevel;
                }
            }

            if (tok3 && indentlevel == 1) {
                tok3 = tok3->previous();
                std::stack<Token *> braces2;

                for (tok2 = tok2->next(); tok2 && tok2 != tok; tok2 = tok2->previous()) {
                    tok3->insertToken(tok2->str());
                    Token *newTok = tok3->next();

                    newTok->varId(tok2->varId());
                    newTok->fileIndex(tok2->fileIndex());
                    newTok->linenr(tok2->linenr());

                    // link() new tokens manually
                    if (tok2->link()) {
                        if (Token::Match(newTok, "}|)|]|>")) {
                            braces2.push(newTok);
                        } else {
                            Token::createMutualLinks(newTok, braces2.top());
                            braces2.pop();
                        }
                    }
                }
            }
        }
    }
}

void Tokenizer::simplifyVariableMultipleAssign()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%name% = %name% = %num%|%name% ;")) {
            // skip intermediate assignments
            Token *tok2 = tok->previous();
            while (tok2 &&
                   tok2->str() == "=" &&
                   Token::Match(tok2->previous(), "%name%")) {
                tok2 = tok2->tokAt(-2);
            }

            if (!tok2 || tok2->str() != ";") {
                continue;
            }

            Token *stopAt = tok->tokAt(2);
            const Token *valueTok = stopAt->tokAt(2);
            const std::string& value(valueTok->str());
            tok2 = tok2->next();

            while (tok2 != stopAt) {
                tok2->next()->insertToken(";");
                tok2->next()->insertToken(value);
                tok2 = tok2->tokAt(4);
            }
        }
    }
}

// Binary operators simplification map
static const std::unordered_map<std::string, std::string> cAlternativeTokens = {
    std::make_pair("and", "&&")
    , std::make_pair("and_eq", "&=")
    , std::make_pair("bitand", "&")
    , std::make_pair("bitor", "|")
    , std::make_pair("not_eq", "!=")
    , std::make_pair("or", "||")
    , std::make_pair("or_eq", "|=")
    , std::make_pair("xor", "^")
    , std::make_pair("xor_eq", "^=")
};

// Simplify the C alternative tokens:
//  and      =>     &&
//  and_eq   =>     &=
//  bitand   =>     &
//  bitor    =>     |
//  compl    =>     ~
//  not      =>     !
//  not_eq   =>     !=
//  or       =>     ||
//  or_eq    =>     |=
//  xor      =>     ^
//  xor_eq   =>     ^=
bool Tokenizer::simplifyCAlternativeTokens()
{
    /* executable scope level */
    int executableScopeLevel = 0;

    std::vector<Token *> alt;
    bool replaceAll = false;  // replace all or none

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == ")") {
            if (const Token *end = isFunctionHead(tok, "{")) {
                ++executableScopeLevel;
                tok = const_cast<Token *>(end);
                continue;
            }
        }

        if (tok->str() == "{") {
            if (executableScopeLevel > 0)
                ++executableScopeLevel;
            continue;
        }

        if (tok->str() == "}") {
            if (executableScopeLevel > 0)
                --executableScopeLevel;
            continue;
        }

        if (!tok->isName())
            continue;

        const std::unordered_map<std::string, std::string>::const_iterator cOpIt = cAlternativeTokens.find(tok->str());
        if (cOpIt != cAlternativeTokens.end()) {
            alt.push_back(tok);

            // Is this a variable declaration..
            if (isC() && Token::Match(tok->previous(), "%type%|* %name% [;,=]"))
                return false;

            if (!Token::Match(tok->previous(), "%name%|%num%|%char%|)|]|> %name% %name%|%num%|%char%|%op%|("))
                continue;
            if (Token::Match(tok->next(), "%assign%|%or%|%oror%|&&|*|/|%|^") && !Token::Match(tok->previous(), "%num%|%char%|) %name% *"))
                continue;
            if (executableScopeLevel == 0 && Token::Match(tok, "%name% (")) {
                const Token *start = tok;
                while (Token::Match(start, "%name%|*"))
                    start = start->previous();
                if (!start || Token::Match(start, "[;}]"))
                    continue;
            }
            replaceAll = true;
        } else if (Token::Match(tok, "not|compl")) {
            alt.push_back(tok);

            if (Token::Match(tok->previous(), "%assign%") || Token::Match(tok->next(), "%num%")) {
                replaceAll = true;
                continue;
            }

            // Don't simplify 'not p;' (in case 'not' is a type)
            if (!Token::Match(tok->next(), "%name%|(") ||
                Token::Match(tok->previous(), "[;{}]") ||
                (executableScopeLevel == 0U && tok->strAt(-1) == "("))
                continue;

            replaceAll = true;
        }
    }

    if (!replaceAll)
        return false;

    for (Token *tok: alt) {
        const std::unordered_map<std::string, std::string>::const_iterator cOpIt = cAlternativeTokens.find(tok->str());
        if (cOpIt != cAlternativeTokens.end())
            tok->str(cOpIt->second);
        else if (tok->str() == "not")
            tok->str("!");
        else
            tok->str("~");
    }

    return !alt.empty();
}

// int i(0); => int i; i = 0;
// int i(0), j; => int i; i = 0; int j;
void Tokenizer::simplifyInitVar()
{
    if (isC())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName() || (tok->previous() && !Token::Match(tok->previous(), "[;{}]")))
            continue;

        if (tok->str() == "return")
            continue;

        if (Token::Match(tok, "class|struct|union| %type% *| %name% ( &| %any% ) ;")) {
            tok = initVar(tok);
        } else if (Token::Match(tok, "%type% *| %name% ( %type% (")) {
            const Token* tok2 = tok->tokAt(2);
            if (!tok2->link())
                tok2 = tok2->next();
            if (!tok2->link() || (tok2->link()->strAt(1) == ";" && !Token::simpleMatch(tok2->linkAt(2), ") (")))
                tok = initVar(tok);
        } else if (Token::Match(tok, "class|struct|union| %type% *| %name% ( &| %any% ) ,")) {
            Token *tok1 = tok->tokAt(5);
            while (tok1->str() != ",")
                tok1 = tok1->next();
            tok1->str(";");

            const int numTokens = (Token::Match(tok, "class|struct|union")) ? 2U : 1U;
            TokenList::insertTokens(tok1, tok, numTokens);
            tok = initVar(tok);
        }
    }
}

Token * Tokenizer::initVar(Token * tok)
{
    // call constructor of class => no simplification
    if (Token::Match(tok, "class|struct|union")) {
        if (tok->strAt(2) != "*")
            return tok;

        tok = tok->next();
    } else if (!tok->isStandardType() && tok->str() != "auto" && tok->next()->str() != "*")
        return tok;

    // goto variable name..
    tok = tok->next();
    if (tok->str() == "*")
        tok = tok->next();

    // sizeof is not a variable name..
    if (tok->str() == "sizeof")
        return tok;

    // check initializer..
    if (tok->tokAt(2)->isStandardType() || tok->strAt(2) == "void")
        return tok;
    else if (!tok->tokAt(2)->isNumber() && !Token::Match(tok->tokAt(2), "%type% (") && tok->strAt(2) != "&" && tok->tokAt(2)->varId() == 0)
        return tok;

    // insert '; var ='
    tok->insertToken(";");
    tok->next()->insertToken(tok->str());
    tok->tokAt(2)->varId(tok->varId());
    tok = tok->tokAt(2);
    tok->insertToken("=");

    // goto '('..
    tok = tok->tokAt(2);

    // delete ')'
    tok->link()->deleteThis();

    // delete this
    tok->deleteThis();

    return tok;
}


bool Tokenizer::simplifyKnownVariables()
{
    // return value for function. Set to true if any simplifications are made
    bool ret = false;

    // constants..
    {
        std::unordered_map<int, std::string> constantValues;
        std::map<int, Token*> constantVars;
        std::unordered_map<int, std::list<Token*>> constantValueUsages;
        for (Token *tok = list.front(); tok; tok = tok->next()) {
            // Reference to variable
            if (Token::Match(tok, "%type%|* & %name% = %name% ;")) {
                Token *start = tok->previous();
                while (Token::Match(start,"%type%|*|&"))
                    start = start->previous();
                if (!Token::Match(start,"[;{}]"))
                    continue;
                const Token *reftok = tok->tokAt(2);
                const Token *vartok = reftok->tokAt(2);
                int level = 0;
                for (Token *tok2 = tok->tokAt(6); tok2; tok2 = tok2->next()) {
                    if (tok2->str() == "{") {
                        ++level;
                    } else if (tok2->str() == "}") {
                        if (level <= 0)
                            break;
                        --level;
                    } else if (tok2->varId() == reftok->varId()) {
                        tok2->str(vartok->str());
                        tok2->varId(vartok->varId());
                    }
                }
                Token::eraseTokens(start, tok->tokAt(6));
                tok = start;
            }

            if (tok->isName() && (Token::Match(tok, "static| const| static| %type% const| %name% = %any% ;") ||
                                  Token::Match(tok, "static| const| static| %type% const| %name% ( %any% ) ;"))) {
                bool isconst = false;
                for (const Token *tok2 = tok; (tok2->str() != "=") && (tok2->str() != "("); tok2 = tok2->next()) {
                    if (tok2->str() == "const") {
                        isconst = true;
                        break;
                    }
                }
                if (!isconst)
                    continue;

                Token *tok1 = tok;

                // start of statement
                if (tok != list.front() && !Token::Match(tok->previous(),";|{|}|private:|protected:|public:"))
                    continue;
                // skip "const" and "static"
                while (Token::Match(tok, "const|static"))
                    tok = tok->next();
                // pod type
                if (!tok->isStandardType())
                    continue;

                Token * const vartok = (tok->next() && tok->next()->str() == "const") ? tok->tokAt(2) : tok->next();
                const Token * const valuetok = vartok->tokAt(2);
                if (Token::Match(valuetok, "%bool%|%char%|%num%|%str% )| ;")) {
                    // record a constant value for this variable
                    constantValues[vartok->varId()] = valuetok->str();
                    constantVars[vartok->varId()] = tok1;
                }
            } else if (tok->varId()) {
                // find the entry for the known variable, if any.  Exclude the location where the variable is assigned with next == "="
                if (constantValues.find(tok->varId()) != constantValues.end() && tok->next()->str() != "=") {
                    constantValueUsages[tok->varId()].push_back(tok);
                }
            }
        }

        for (auto constantVar = constantVars.rbegin(); constantVar != constantVars.rend(); constantVar++) {
            bool referenceFound = false;
            std::list<Token*> usageList = constantValueUsages[constantVar->first];
            for (Token* usage : usageList) {
                // check if any usages of each known variable are a reference
                if (Token::Match(usage->tokAt(-2), "(|[|,|{|return|%op% & %varid%", constantVar->first)) {
                    referenceFound = true;
                    break;
                }
            }

            if (!referenceFound) {
                // replace all usages of non-referenced known variables with their value
                for (Token* usage : usageList) {
                    usage->str(constantValues[constantVar->first]);
                }

                Token* startTok = constantVar->second;
                // remove variable assignment statement
                while (startTok->next()->str() != ";")
                    startTok->deleteNext();
                startTok->deleteNext();

                // #8579 if we can we want another token to delete startTok. if we can't it doesn't matter
                if (startTok->previous()) {
                    startTok->previous()->deleteNext();
                } else if (startTok->next()) {
                    startTok->next()->deletePrevious();
                } else {
                    startTok->deleteThis();
                }
                startTok = nullptr;

                constantVar->second = nullptr;
                ret = true;
            }
        }
    }

    // variable id for local, float/double, array variables
    std::set<int> localvars;
    std::set<int> floatvars;
    std::set<int> arrays;

    // auto variables..
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // Search for a block of code
        Token * const start = const_cast<Token *>(startOfExecutableScope(tok));
        if (!start)
            continue;

        for (const Token *tok2 = start->previous(); tok2 && !Token::Match(tok2, "[;{}]"); tok2 = tok2->previous()) {
            if (tok2->varId() != 0)
                localvars.insert(tok2->varId());
        }

        tok = start;
        // parse the block of code..
        int indentlevel = 0;
        Token *tok2 = tok;
        for (; tok2; tok2 = tok2->next()) {
            if (Token::Match(tok2, "[;{}] %type% %name%|*")) {
                bool isfloat = false;
                bool ispointer = false;
                const Token *vartok = tok2->next();
                while (Token::Match(vartok, "%name%|* %name%|*")) {
                    if (Token::Match(vartok, "float|double"))
                        isfloat = true;
                    if (vartok->str() == "*")
                        ispointer = true;
                    vartok = vartok->next();
                }
                if (Token::Match(vartok, "%var% ;|["))
                    localvars.insert(vartok->varId());
                if (isfloat && !ispointer && Token::Match(vartok, "%var% ;"))
                    floatvars.insert(vartok->varId());
                if (Token::Match(vartok, "%var% ["))
                    arrays.insert(vartok->varId());
            }

            if (tok2->str() == "{")
                ++indentlevel;

            else if (tok2->str() == "}") {
                --indentlevel;
                if (indentlevel <= 0)
                    break;
            }

            else if (Token::simpleMatch(tok2, "for ("))
                tok2 = tok2->next()->link();

            else if (tok2->previous()->str() != "*" && !Token::Match(tok2->tokAt(-2), "* --|++") &&
                     (Token::Match(tok2, "%name% = %bool%|%char%|%num%|%str%|%name% ;") ||
                      Token::Match(tok2, "%name% [ %num%| ] = %str% ;") ||
                      Token::Match(tok2, "%name% = & %name% ;") ||
                      (Token::Match(tok2, "%name% = & %name% [ 0 ] ;") && arrays.find(tok2->tokAt(3)->varId()) != arrays.end()))) {
                const int varid = tok2->varId();
                if (varid == 0)
                    continue;

                if (Token::Match(tok2->previous(), "[;{}]") && localvars.find(varid) == localvars.end())
                    continue;

                // initialization of static variable => the value is not *known*
                {
                    bool isstatic = false;
                    const Token *decl = tok2->previous();
                    while (decl && (decl->isName() || decl->str() == "*")) {
                        if (decl->str() == "static") {
                            isstatic = true;
                            break;
                        }
                        decl = decl->previous();
                    }
                    if (isstatic)
                        continue;
                }

                // skip loop variable
                if (Token::Match(tok2->tokAt(-2), "(|:: %type%")) {
                    const Token *tok3 = tok2->previous();
                    do {
                        tok3 = tok3->tokAt(-2);
                    } while (Token::Match(tok3->previous(), ":: %type%"));
                    if (Token::Match(tok3->tokAt(-2), "for ( %type%"))
                        continue;
                }

                // struct name..
                if (Token::Match(tok2, "%varid% = &| %varid%", tok2->varId()))
                    continue;

                const std::string structname = Token::Match(tok2->tokAt(-3), "[;{}] %name% .") ?
                                               std::string(tok2->strAt(-2) + " .") :
                                               std::string();

                const Token * const valueToken = tok2->tokAt(2);

                std::string value;
                nonneg int valueVarId = 0;

                Token *tok3 = nullptr;
                bool valueIsPointer = false;

                // there could be a hang here if tok2 is moved back by the function calls below for some reason
                if (Settings::terminated())
                    return false;

                if (!simplifyKnownVariablesGetData(varid, &tok2, &tok3, value, valueVarId, valueIsPointer, floatvars.find(tok2->varId()) != floatvars.end()))
                    continue;

                if (valueVarId > 0 && arrays.find(valueVarId) != arrays.end())
                    continue;

                ret |= simplifyKnownVariablesSimplify(&tok2, tok3, varid, structname, value, valueVarId, valueIsPointer, valueToken, indentlevel);
            }

            else if (Token::Match(tok2, "strcpy|sprintf ( %name% , %str% ) ;")) {
                const int varid(tok2->tokAt(2)->varId());
                if (varid == 0)
                    continue;

                const Token * const valueToken = tok2->tokAt(4);
                std::string value(valueToken->str());
                if (tok2->str() == "sprintf") {
                    std::string::size_type n = 0;
                    while ((n = value.find("%%", n)) != std::string::npos) {
                        // Replace "%%" with "%" - erase the first '%' and continue past the second '%'
                        value.erase(n, 1);
                        ++n;
                    }
                }
                const int valueVarId(0);
                const bool valueIsPointer(false);
                Token *tok3 = tok2->tokAt(6);
                ret |= simplifyKnownVariablesSimplify(&tok2, tok3, varid, emptyString, value, valueVarId, valueIsPointer, valueToken, indentlevel);

                // there could be a hang here if tok2 was moved back by the function call above for some reason
                if (Settings::terminated())
                    return false;
            }
        }

        if (tok2)
            tok = tok2->previous();
    }

    return ret;
}

bool Tokenizer::simplifyKnownVariablesGetData(nonneg int varid, Token **_tok2, Token **_tok3, std::string &value, nonneg int &valueVarId, bool &valueIsPointer, bool floatvar)
{
    Token *tok2 = *_tok2;
    Token *tok3 = nullptr;

    if (Token::simpleMatch(tok2->tokAt(-2), "for (")) {
        // only specific for loops is handled
        if (!Token::Match(tok2, "%varid% = %num% ; %varid% <|<= %num% ; ++| %varid% ++| ) {", varid))
            return false;

        // is there a "break" in the for loop?
        bool hasbreak = false;
        const Token* end4 = tok2->linkAt(-1)->linkAt(1);
        for (const Token *tok4 = tok2->previous()->link(); tok4 != end4; tok4 = tok4->next()) {
            if (tok4->str() == "break") {
                hasbreak = true;
                break;
            }
        }
        if (hasbreak)
            return false;

        // no break => the value of the counter value is known after the for loop..
        const Token* compareTok = tok2->tokAt(5);
        if (compareTok->str() == "<") {
            value = compareTok->next()->str();
            valueVarId = compareTok->next()->varId();
        } else
            value = MathLib::toString(MathLib::toLongNumber(compareTok->next()->str()) + 1);

        // Skip for-body..
        tok3 = tok2->previous()->link()->next()->link()->next();
    } else {
        value = tok2->strAt(2);
        valueVarId = tok2->tokAt(2)->varId();
        if (tok2->strAt(1) == "[") {
            value = tok2->next()->link()->strAt(2);
            valueVarId = 0;
        } else if (value == "&") {
            value = tok2->strAt(3);
            valueVarId = tok2->tokAt(3)->varId();

            // *ptr = &var; *ptr = 5;
            // equals
            // var = 5; not *var = 5;
            if (tok2->strAt(4) == ";")
                valueIsPointer = true;
        }

        // Add a '.0' to a decimal value and therefore convert it to an floating point number.
        else if (MathLib::isDec(tok2->strAt(2)) && floatvar) {
            value += ".0";
        }

        // float variable: convert true/false to 1.0 / 0.0
        else if (tok2->tokAt(2)->isBoolean() && floatvar) {
            value = (value == "true") ? "1.0" : "0.0";
        }

        if (Token::simpleMatch(tok2->next(), "= &"))
            tok2 = tok2->tokAt(3);

        tok3 = tok2->next();
    }
    *_tok2 = tok2;
    *_tok3 = tok3;
    return true;
}

bool Tokenizer::simplifyKnownVariablesSimplify(Token **tok2, Token *tok3, nonneg int varid, const std::string &structname, std::string &value, nonneg int valueVarId, bool valueIsPointer, const Token * const valueToken, int indentlevel) const
{
    const bool pointeralias(valueToken->isName() || Token::Match(valueToken, "& %name% ["));
    const bool varIsGlobal = (indentlevel == 0);
    const bool printDebug = mSettings->debugwarnings;

    if (mErrorLogger && !list.getFiles().empty())
        mErrorLogger->reportProgress(list.getFiles()[0], "Tokenize (simplifyKnownVariables)", tok3->progressValue());

    if (isMaxTime())
        return false;

    bool ret = false;

    Token* bailOutFromLoop = nullptr;
    int indentlevel3 = indentlevel;
    bool ret3 = false;
    for (; tok3; tok3 = tok3->next()) {
        if (tok3->str() == "{") {
            ++indentlevel3;
        } else if (tok3->str() == "}") {
            --indentlevel3;
            if (indentlevel3 < indentlevel) {
                if (Token::Match((*tok2)->tokAt(-7), "%type% * %name% ; %name% = & %name% ;") &&
                    (*tok2)->strAt(-5) == (*tok2)->strAt(-3)) {
                    (*tok2) = (*tok2)->tokAt(-4);
                    Token::eraseTokens((*tok2), (*tok2)->tokAt(6));
                }
                break;
            }
        }

        // Stop if there is a pointer alias and a shadow variable is
        // declared in an inner scope (#3058)
        if (valueIsPointer && tok3->varId() > 0 &&
            tok3->previous() && (tok3->previous()->isName() || tok3->previous()->str() == "*") &&
            valueToken->str() == "&" &&
            valueToken->next() &&
            valueToken->next()->isName() &&
            tok3->str() == valueToken->next()->str() &&
            tok3->varId() > valueToken->next()->varId()) {
            // more checking if this is a variable declaration
            bool decl = true;
            for (const Token *tok4 = tok3->previous(); tok4; tok4 = tok4->previous()) {
                if (Token::Match(tok4, "[;{}]"))
                    break;

                else if (tok4->isName()) {
                    if (tok4->varId() > 0) {
                        decl = false;
                        break;
                    }
                }

                else if (!Token::Match(tok4, "[&*]")) {
                    decl = false;
                    break;
                }
            }
            if (decl)
                break;
        }

        // Stop if label is found
        if (Token::Match(tok3, "; %type% : ;"))
            break;

        // Stop if break/continue is found ..
        if (Token::Match(tok3, "break|continue"))
            break;
        if ((indentlevel3 > 1 || !Token::simpleMatch(Token::findsimplematch(tok3,";"), "; }")) && tok3->str() == "return")
            ret3 = true;
        if (ret3 && tok3->str() == ";")
            break;

        if (pointeralias && Token::Match(tok3, ("!!= " + value).c_str()))
            break;

        // Stop if a loop is found
        if (pointeralias && Token::Match(tok3, "do|for|while"))
            break;

        // Stop if unknown function call is seen and the variable is global: it might be
        // changed by the function call
        if (varIsGlobal && tok3->str() == ")" && tok3->link() &&
            Token::Match(tok3->link()->tokAt(-2), "[;{}] %name% (") &&
            !Token::Match(tok3->link()->previous(), "if|for|while|switch|BOOST_FOREACH"))
            break;

        // Stop if something like 'while (--var)' is found
        if (Token::Match(tok3, "for|while|do")) {
            const Token *endpar = tok3->next()->link();
            if (Token::simpleMatch(endpar, ") {"))
                endpar = endpar->next()->link();
            bool bailout = false;
            for (const Token *tok4 = tok3; tok4 && tok4 != endpar; tok4 = tok4->next()) {
                if (Token::Match(tok4, "++|-- %varid%", varid) ||
                    Token::Match(tok4, "%varid% ++|--|=", varid)) {
                    bailout = true;
                    break;
                }
            }
            if (bailout)
                break;
        }

        if (bailOutFromLoop) {
            // This could be a loop, skip it, but only if it doesn't contain
            // the variable we are checking for. If it contains the variable
            // we will bail out.
            if (tok3->varId() == varid) {
                // Continue
                //tok2 = bailOutFromLoop;
                break;
            } else if (tok3 == bailOutFromLoop) {
                // We have skipped the loop
                bailOutFromLoop = nullptr;
                continue;
            }

            continue;
        } else if (tok3->str() == "{" && tok3->previous()->str() == ")") {
            // There is a possible loop after the assignment. Try to skip it.
            if (tok3->previous()->link() &&
                tok3->previous()->link()->strAt(-1) != "if")
                bailOutFromLoop = tok3->link();
            continue;
        }

        // Variable used in realloc (see Ticket #1649)
        if (Token::Match(tok3, "%name% = realloc ( %name% ,") &&
            tok3->varId() == varid &&
            tok3->tokAt(4)->varId() == varid) {
            tok3->tokAt(4)->str(value);
            ret = true;
        }

        // condition "(|&&|%OROR% %varid% )|&&|%OROR%|;
        if (!Token::Match(tok3->previous(), "( %name% )") &&
            Token::Match(tok3->previous(), "&&|(|%oror% %varid% &&|%oror%|)|;", varid)) {
            tok3->str(value);
            tok3->varId(valueVarId);
            ret = true;
        }

        // parameter in function call..
        if (tok3->varId() == varid && Token::Match(tok3->previous(), "[(,] %name% [,)]")) {
            // If the parameter is passed by value then simplify it
            if (isFunctionParameterPassedByValue(tok3)) {
                tok3->str(value);
                tok3->varId(valueVarId);
                ret = true;
            }
        }

        // Variable is used somehow in a non-defined pattern => bail out
        if (tok3->varId() == varid) {
            // This is a really generic bailout so let's try to avoid this.
            // There might be lots of false negatives.
            if (printDebug) {
                // FIXME: Fix all the debug warnings for values and then
                // remove this bailout
                if (pointeralias)
                    break;

                // suppress debug-warning when calling member function
                if (Token::Match(tok3->next(), ". %name% ("))
                    break;

                // suppress debug-warning when assignment
                if (tok3->strAt(1) == "=")
                    break;

                // taking address of variable..
                if (Token::Match(tok3->tokAt(-2), "return|= & %name% ;"))
                    break;

                // parameter in function call..
                if (Token::Match(tok3->tokAt(-2), "%name% ( %name% ,|)") ||
                    Token::Match(tok3->previous(), ", %name% ,|)"))
                    break;

                // conditional increment
                if (Token::Match(tok3->tokAt(-3), ") { ++|--") ||
                    Token::Match(tok3->tokAt(-2), ") { %name% ++|--"))
                    break;

                reportError(tok3, Severity::debug, "debug",
                            "simplifyKnownVariables: bailing out (variable="+tok3->str()+", value="+value+")");
            }

            break;
        }

        // Using the variable in condition..
        if (Token::Match(tok3->previous(), ("if ( " + structname + " %varid% %cop%|)").c_str(), varid) ||
            Token::Match(tok3, ("( " + structname + " %varid% %comp%").c_str(), varid) ||
            Token::Match(tok3, ("%comp%|!|= " + structname + " %varid% %cop%|)|;").c_str(), varid) ||
            Token::Match(tok3->previous(), "strlen|free ( %varid% )", varid)) {
            if (value[0] == '\"' && tok3->previous()->str() != "strlen") {
                // bail out if value is a string unless if it's just given
                // as parameter to strlen
                break;
            }
            if (!structname.empty()) {
                tok3->deleteNext(2);
            }
            if (Token::Match(valueToken, "& %name% ;")) {
                tok3->insertToken("&");
                tok3 = tok3->next();
            }
            tok3 = tok3->next();
            tok3->str(value);
            tok3->varId(valueVarId);
            ret = true;
        }

        // pointer alias used in condition..
        if (Token::Match(valueToken,"& %name% ;") && Token::Match(tok3, ("( * " + structname + " %varid% %cop%").c_str(), varid)) {
            tok3->deleteNext();
            if (!structname.empty())
                tok3->deleteNext(2);
            tok3 = tok3->next();
            tok3->str(value);
            tok3->varId(valueVarId);
            ret = true;
        }

        // Delete pointer alias
        if (isCPP() && pointeralias && (tok3->str() == "delete") && tok3->next() &&
            (Token::Match(tok3->next(), "%varid% ;", varid) ||
             Token::Match(tok3->next(), "[ ] %varid%", varid))) {
            tok3 = (tok3->next()->str() == "[") ? tok3->tokAt(3) : tok3->next();
            tok3->str(value);
            tok3->varId(valueVarId);
            ret = true;
        }

        // Variable is used in function call..
        if (Token::Match(tok3, ("%name% ( " + structname + " %varid% ,").c_str(), varid)) {
            static const char * const functionName[] = {
                // always simplify
                "strcmp", "strdup",
                // don't simplify buffer value
                "memcmp","memcpy","memmove","memset","strcpy","strncmp","strncpy"
            };
            for (int i = 0; i < (sizeof(functionName) / sizeof(*functionName)); ++i) {
                if (valueVarId == 0U && i >= 2)
                    break;
                if (tok3->str() == functionName[i]) {
                    Token *par1 = tok3->tokAt(2);
                    if (!structname.empty()) {
                        par1->deleteNext();
                        par1->deleteThis();
                    }
                    par1->str(value);
                    par1->varId(valueVarId);
                    break;
                }
            }
        }

        // Variable is used as 2nd parameter in function call..
        if (Token::Match(tok3, ("%name% ( %any% , " + structname + " %varid% ,|)").c_str(), varid)) {
            static const char * const functionName[] = {
                // always simplify
                "strcmp","strcpy","strncmp","strncpy",
                // don't simplify buffer value
                "memcmp","memcpy","memmove"
            };
            for (int i = 0; i < (sizeof(functionName) / sizeof(*functionName)); ++i) {
                if (valueVarId == 0U && i >= 4)
                    break;
                if (tok3->str() == functionName[i]) {
                    Token *par = tok3->tokAt(4);
                    if (!structname.empty()) {
                        par->deleteNext();
                        par->deleteThis();
                    }
                    par->str(value);
                    par->varId(valueVarId);
                    break;
                }
            }
        }

        // array usage
        if (value[0] != '\"' && Token::Match(tok3, ("[(,] " + structname + " %varid% [|%cop%").c_str(), varid)) {
            if (!structname.empty()) {
                tok3->deleteNext(2);
            }
            tok3 = tok3->next();
            tok3->str(value);
            tok3->varId(valueVarId);
            ret = true;
        }

        // The >> operator is sometimes used to assign a variable in C++
        if (isCPP() && Token::Match(tok3, (">> " + structname + " %varid%").c_str(), varid)) {
            // bailout for such code:   ; std :: cin >> i ;
            const Token *prev = tok3->previous();
            while (prev && prev->str() != "return" && Token::Match(prev, "%name%|::|*"))
                prev = prev->previous();
            if (Token::Match(prev, ";|{|}|>>"))
                break;
        }

        // Variable is used in calculation..
        if (((tok3->previous()->varId() > 0) && Token::Match(tok3, ("& " + structname + " %varid%").c_str(), varid)) ||
            (Token::Match(tok3, ("[=+-*/%^|[] " + structname + " %varid% [=?+-*/%^|;])]").c_str(), varid) && !Token::Match(tok3, ("= " + structname + " %name% =").c_str())) ||
            Token::Match(tok3, ("[(=+-*/%^|[] " + structname + " %varid% <<|>>").c_str(), varid) ||
            Token::Match(tok3, ("<<|>> " + structname + " %varid% %cop%|;|]|)").c_str(), varid) ||
            Token::Match(tok3->previous(), ("[=+-*/%^|[] ( " + structname + " %varid% !!=").c_str(), varid)) {
            if (value[0] == '\"')
                break;
            if (!structname.empty()) {
                tok3->deleteNext(2);
                ret = true;
            }
            tok3 = tok3->next();
            if (tok3->str() != value)
                ret = true;
            tok3->str(value);
            tok3->varId(valueVarId);
            if (tok3->previous()->str() == "*" && (valueIsPointer || Token::Match(valueToken, "& %name% ;"))) {
                tok3 = tok3->previous();
                tok3->deleteThis();
                ret = true;
            } else if (Token::Match(valueToken, "& %name% ;"))
                tok3->insertToken("&", emptyString, true);
        }

        if (Token::simpleMatch(tok3, "= {")) {
            const Token* const end4 = tok3->linkAt(1);
            for (const Token *tok4 = tok3; tok4 != end4; tok4 = tok4->next()) {
                if (Token::Match(tok4, "{|, %varid% ,|}", varid)) {
                    tok4->next()->str(value);
                    tok4->next()->varId(valueVarId);
                    ret = true;
                }
            }
        }

        // Using the variable in for-condition..
        if (Token::simpleMatch(tok3, "for (")) {
            for (Token *tok4 = tok3->tokAt(2); tok4; tok4 = tok4->next()) {
                if (Token::Match(tok4, "(|)"))
                    break;

                // Replace variable used in condition..
                if (Token::Match(tok4, "; %name% <|<=|!= %name% ; ++| %name% ++| )")) {
                    const Token *inctok = tok4->tokAt(5);
                    if (inctok->str() == "++")
                        inctok = inctok->next();
                    if (inctok->varId() == varid)
                        break;

                    if (tok4->next()->varId() == varid) {
                        tok4->next()->str(value);
                        tok4->next()->varId(valueVarId);
                        ret = true;
                    }
                    if (tok4->tokAt(3)->varId() == varid) {
                        tok4->tokAt(3)->str(value);
                        tok4->tokAt(3)->varId(valueVarId);
                        ret = true;
                    }
                }
            }
        }

        if (indentlevel == indentlevel3 && Token::Match(tok3->next(), "%varid% ++|--", varid) && MathLib::isInt(value)) {
            const std::string op(tok3->strAt(2));
            if (Token::Match(tok3, "[{};] %any% %any% ;")) {
                tok3->deleteNext(3);
            } else {
                tok3 = tok3->next();
                tok3->str(value);
                tok3->varId(valueVarId);
                tok3->deleteNext();
            }
            value = MathLib::incdec(value, op);
            if (!Token::simpleMatch((*tok2)->tokAt(-2), "for (")) {
                (*tok2)->tokAt(2)->str(value);
                (*tok2)->tokAt(2)->varId(valueVarId);
            }
            ret = true;
        }

        if (indentlevel == indentlevel3 && Token::Match(tok3->next(), "++|-- %varid%", varid) && MathLib::isInt(value) &&
            !Token::Match(tok3->tokAt(3), "[.[]")) {
            value = MathLib::incdec(value, tok3->next()->str());
            (*tok2)->tokAt(2)->str(value);
            (*tok2)->tokAt(2)->varId(valueVarId);
            if (Token::Match(tok3, "[;{}] %any% %any% ;")) {
                tok3->deleteNext(3);
            } else {
                tok3->deleteNext();
                tok3->next()->str(value);
                tok3->next()->varId(valueVarId);
            }
            tok3 = tok3->next();
            ret = true;
        }

        // return variable..
        if (Token::Match(tok3, "return %varid% %any%", varid) &&
            valueToken->str() != "&" &&
            (tok3->tokAt(2)->isExtendedOp() || tok3->strAt(2) == ";") &&
            value[0] != '\"') {
            tok3->next()->str(value);
            tok3->next()->varId(valueVarId);
        }

        else if (pointeralias && Token::Match(tok3, "return * %varid% ;", varid) && value[0] != '\"') {
            tok3->deleteNext();
            tok3->next()->str(value);
            tok3->next()->varId(valueVarId);
        }
    }
    return ret;
}


void Tokenizer::elseif()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::simpleMatch(tok, "else if"))
            continue;

        for (Token *tok2 = tok; tok2; tok2 = tok2->next()) {
            if (Token::Match(tok2, "(|{|["))
                tok2 = tok2->link();

            if (Token::Match(tok2, "}|;")) {
                if (tok2->next() && tok2->next()->str() != "else") {
                    tok->insertToken("{");
                    tok2->insertToken("}");
                    Token::createMutualLinks(tok->next(), tok2->next());
                    break;
                }
            }
        }
    }
}


void Tokenizer::simplifyIfSwitchForInit()
{
    if (!isCPP() || mSettings->standards.cpp < Standards::CPP17)
        return;

    const bool forInit = (mSettings->standards.cpp >= Standards::CPP20);

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "if|switch|for ("))
            continue;

        Token *semicolon = tok->tokAt(2);
        while (!Token::Match(semicolon, "[;)]")) {
            if (Token::Match(semicolon, "(|{|[") && semicolon->link())
                semicolon = semicolon->link();
            semicolon = semicolon->next();
        }
        if (semicolon->str() != ";")
            continue;

        if (tok->str() ==  "for") {
            if (!forInit)
                continue;

            // Is it a for range..
            const Token *tok2 = semicolon->next();
            bool rangeFor = false;
            while (!Token::Match(tok2, "[;)]")) {
                if (tok2->str() == "(")
                    tok2 = tok2->link();
                else if (!rangeFor && tok2->str() == "?")
                    break;
                else if (tok2->str() == ":")
                    rangeFor = true;
                tok2 = tok2->next();
            }
            if (!rangeFor || tok2->str() != ")")
                continue;
        }

        Token *endpar = tok->linkAt(1);
        if (!Token::simpleMatch(endpar, ") {"))
            continue;

        Token *endscope = endpar->linkAt(1);
        if (Token::simpleMatch(endscope, "} else {"))
            endscope = endscope->linkAt(2);

        // Simplify, the initialization expression is broken out..
        semicolon->insertToken(tok->str());
        semicolon->next()->insertToken("(");
        Token::createMutualLinks(semicolon->next()->next(), endpar);
        tok->deleteNext();
        tok->str("{");
        endscope->insertToken("}");
        Token::createMutualLinks(tok, endscope->next());
    }
}


bool Tokenizer::simplifyRedundantParentheses()
{
    bool ret = false;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != "(")
            continue;

        if (isCPP() && Token::simpleMatch(tok->previous(), "} (") &&
            Token::Match(tok->previous()->link()->previous(), "%name%|> {"))
            continue;

        if (Token::simpleMatch(tok, "( {"))
            continue;

        if (Token::Match(tok->link(), ") %num%")) {
            tok = tok->link();
            continue;
        }

        // Do not simplify if there is comma inside parentheses..
        if (Token::Match(tok->previous(), "%op% (") || Token::Match(tok->link(), ") %op%")) {
            bool innerComma = false;
            for (const Token *inner = tok->link()->previous(); inner != tok; inner = inner->previous()) {
                if (inner->str() == ")")
                    inner = inner->link();
                if (inner->str() == ",") {
                    innerComma = true;
                    break;
                }
            }
            if (innerComma)
                continue;
        }

        // !!operator = ( x ) ;
        if (tok->strAt(-2) != "operator" &&
            tok->previous() && tok->previous()->str() == "=" &&
            tok->next() && tok->next()->str() != "{" &&
            Token::simpleMatch(tok->link(), ") ;")) {
            tok->link()->deleteThis();
            tok->deleteThis();
            continue;
        }

        while (Token::simpleMatch(tok, "( (") &&
               tok->link() && tok->link()->previous() == tok->next()->link()) {
            // We have "(( *something* ))", remove the inner
            // parentheses
            tok->deleteNext();
            tok->link()->tokAt(-2)->deleteNext();
            ret = true;
        }

        if (isCPP() && Token::Match(tok->tokAt(-2), "[;{}=(] new (") && Token::Match(tok->link(), ") [;,{}[]")) {
            // Remove the parentheses in "new (type)" constructs
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        if (Token::Match(tok->previous(), "! ( %name% )")) {
            // Remove the parentheses
            tok->deleteThis();
            tok->deleteNext();
            ret = true;
        }

        if (Token::Match(tok->previous(), "[(,;{}] ( %name% ) .")) {
            // Remove the parentheses
            tok->deleteThis();
            tok->deleteNext();
            ret = true;
        }

        if (Token::Match(tok->previous(), "[(,;{}] ( %name% (") &&
            tok->link()->previous() == tok->linkAt(2)) {
            // We have "( func ( *something* ))", remove the outer
            // parentheses
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        if (Token::Match(tok->previous(), "[,;{}] ( delete [| ]| %name% ) ;")) {
            // We have "( delete [| ]| var )", remove the outer
            // parentheses
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        if (!Token::simpleMatch(tok->tokAt(-2), "operator delete") &&
            Token::Match(tok->previous(), "delete|; (") &&
            (tok->previous()->str() != "delete" || tok->next()->varId() > 0) &&
            Token::Match(tok->link(), ") ;|,")) {
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        if (Token::Match(tok->previous(), "[(!*;{}] ( %name% )") &&
            (tok->next()->varId() != 0 || Token::Match(tok->tokAt(3), "[+-/=]")) && !tok->next()->isStandardType()) {
            // We have "( var )", remove the parentheses
            tok->deleteThis();
            tok->deleteNext();
            ret = true;
        }

        while (Token::Match(tok->previous(), "[;{}[(,!*] ( %name% .")) {
            Token *tok2 = tok->tokAt(2);
            while (Token::Match(tok2, ". %name%")) {
                tok2 = tok2->tokAt(2);
            }
            if (tok2 != tok->link())
                break;
            // We have "( var . var . ... . var )", remove the parentheses
            tok = tok->previous();
            tok->deleteNext();
            tok2->deleteThis();
            ret = true;
        }

        if (Token::simpleMatch(tok->previous(), "? (") && Token::simpleMatch(tok->link(), ") :")) {
            const Token *tok2 = tok->next();
            while (tok2 && (Token::Match(tok2,"%bool%|%num%|%name%") || tok2->isArithmeticalOp()))
                tok2 = tok2->next();
            if (tok2 && tok2->str() == ")") {
                tok->link()->deleteThis();
                tok->deleteThis();
                ret = true;
                continue;
            }
        }

        while (Token::Match(tok->previous(), "[{([,] ( !!{") &&
               Token::Match(tok->link(), ") [;,])]") &&
               !Token::simpleMatch(tok->tokAt(-2), "operator ,") && // Ticket #5709
               !Token::findsimplematch(tok, ",", tok->link())) {
            // We have "( ... )", remove the parentheses
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        if (Token::simpleMatch(tok->previous(), ", (") &&
            Token::simpleMatch(tok->link(), ") =")) {
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        // Simplify "!!operator !!%name%|)|]|>|>> ( %num%|%bool% ) %op%|;|,|)"
        if (Token::Match(tok, "( %bool%|%num% ) %cop%|;|,|)") &&
            tok->strAt(-2) != "operator" &&
            tok->previous() &&
            !Token::Match(tok->previous(), "%name%|)|]") &&
            (!(isCPP() && Token::Match(tok->previous(),">|>>")))) {
            tok->link()->deleteThis();
            tok->deleteThis();
            ret = true;
        }

        if (Token::Match(tok->previous(), "*|& ( %name% )")) {
            // We may have a variable declaration looking like "type_name *(var_name)"
            Token *tok2 = tok->tokAt(-2);
            while (Token::Match(tok2, "%type%|static|const|extern") && tok2->str() != "operator") {
                tok2 = tok2->previous();
            }
            if (tok2 && !Token::Match(tok2, "[;,{]")) {
                // Not a variable declaration
            } else {
                tok->deleteThis();
                tok->deleteNext();
            }
        }
    }
    return ret;
}

void Tokenizer::simplifyTypeIntrinsics()
{
    static const std::unordered_map<std::string, std::string> intrinsics = {
        { "__has_nothrow_assign", "has_nothrow_assign" },
        { "__has_nothrow_constructor", "has_nothrow_constructor" },
        { "__has_nothrow_copy", "has_nothrow_copy" },
        { "__has_trivial_assign", "has_trivial_assign" },
        { "__has_trivial_constructor", "has_trivial_constructor" },
        { "__has_trivial_copy", "has_trivial_copy" },
        { "__has_trivial_destructor", "has_trivial_destructor" },
        { "__has_virtual_destructor", "has_virtual_destructor" },
        { "__is_abstract", "is_abstract" },
        { "__is_aggregate", "is_aggregate" },
        { "__is_assignable", "is_assignable" },
        { "__is_base_of", "is_base_of" },
        { "__is_class", "is_class" },
        { "__is_constructible", "is_constructible" },
        { "__is_convertible_to", "is_convertible_to" },
        { "__is_destructible", "is_destructible" },
        { "__is_empty", "is_empty" },
        { "__is_enum", "is_enum" },
        { "__is_final", "is_final" },
        { "__is_nothrow_assignable", "is_nothrow_assignable" },
        { "__is_nothrow_constructible", "is_nothrow_constructible" },
        { "__is_nothrow_destructible", "is_nothrow_destructible" },
        { "__is_pod", "is_pod" },
        { "__is_polymorphic", "is_polymorphic" },
        { "__is_trivially_assignable", "is_trivially_assignable" },
        { "__is_trivially_constructible", "is_trivially_constructible" },
        { "__is_union", "is_union" },
    };
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "%name% ("))
            continue;
        auto p = intrinsics.find(tok->str());
        if (p == intrinsics.end())
            continue;
        Token * end = tok->next()->link();
        Token * prev = tok->previous();
        tok->str(p->second);
        prev->insertToken("::");
        prev->insertToken("std");
        tok->next()->str("<");
        end->str(">");
        end->insertToken("}");
        end->insertToken("{");
        Token::createMutualLinks(end->tokAt(1), end->tokAt(2));
    }
}

void Tokenizer::simplifyCharAt()
{
    // Replace "string"[0] with 's'
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%str% [ %num% ]")) {
            const MathLib::bigint index = MathLib::toLongNumber(tok->strAt(2));
            // Check within range
            if (index >= 0 && index <= Token::getStrLength(tok)) {
                tok->str("'" + Token::getCharAt(tok, index) + "'");
                tok->deleteNext(3);
            }
        }
    }
}

void Tokenizer::simplifyReference()
{
    if (isC())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // starting executable scope..
        Token *start = const_cast<Token *>(startOfExecutableScope(tok));
        if (start) {
            tok = start;
            // replace references in this scope..
            Token * const end = tok->link();
            for (Token *tok2 = tok; tok2 && tok2 != end; tok2 = tok2->next()) {
                // found a reference..
                if (Token::Match(tok2, "[;{}] %type% & %name% (|= %name% )| ;")) {
                    const int refId = tok2->tokAt(3)->varId();
                    if (!refId)
                        continue;

                    // replace reference in the code..
                    for (Token *tok3 = tok2->tokAt(7); tok3 && tok3 != end; tok3 = tok3->next()) {
                        if (tok3->varId() == refId) {
                            tok3->str(tok2->strAt(5));
                            tok3->varId(tok2->tokAt(5)->varId());
                        }
                    }

                    tok2->deleteNext(6+(tok2->strAt(6)==")" ? 1 : 0));
                }
            }
            tok = end;
        }
    }
}

bool Tokenizer::simplifyCalculations()
{
    return mTemplateSimplifier->simplifyCalculations(nullptr, nullptr, false);
}

void Tokenizer::simplifyOffsetPointerDereference()
{
    // Replace "*(str + num)" => "str[num]" and
    // Replace "*(str - num)" => "str[-num]"
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName() && !tok->isLiteral()
            && !Token::Match(tok, "]|)|++|--")
            && Token::Match(tok->next(), "* ( %name% +|- %num%|%name% )")) {

            // remove '* ('
            tok->deleteNext(2);

            // '+'->'['
            tok = tok->tokAt(2);
            Token* const openBraceTok = tok;
            const bool isNegativeIndex = (tok->str() == "-");
            tok->str("[");

            // Insert a "-" in front of the number or variable
            if (isNegativeIndex) {
                if (tok->next()->isName()) {
                    tok->insertToken("-");
                    tok = tok->next();
                } else
                    tok->next()->str(std::string("-") + tok->next()->str());
            }

            tok = tok->tokAt(2);
            tok->str("]");
            Token::createMutualLinks(openBraceTok, tok);
        }
    }
}

void Tokenizer::simplifyOffsetPointerReference()
{
    std::set<int> pod;
    for (const Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->isStandardType()) {
            tok = tok->next();
            while (tok && (tok->str() == "*" || tok->isName())) {
                if (tok->varId() > 0) {
                    pod.insert(tok->varId());
                    break;
                }
                tok = tok->next();
            }
            if (!tok)
                break;
        }
    }

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "%num%|%name%|]|)") &&
            (Token::Match(tok->next(), "& %name% [ %num%|%name% ] !!["))) {
            tok = tok->next();

            if (tok->next()->varId()) {
                if (pod.find(tok->next()->varId()) == pod.end()) {
                    tok = tok->tokAt(5);
                    if (!tok)
                        syntaxError(tok);
                    continue;
                }
            }

            // '&' => '('
            tok->str("(");

            tok = tok->next();
            // '[' => '+'
            tok->deleteNext();
            tok->insertToken("+");

            tok = tok->tokAt(3);
            //remove ']'
            tok->str(")");
            Token::createMutualLinks(tok->tokAt(-4), tok);
        }
    }
}

void Tokenizer::simplifyNestedStrcat()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "[;{}] strcat ( strcat (")) {
            continue;
        }

        // find inner strcat call
        Token *tok2 = tok->tokAt(3);
        while (Token::simpleMatch(tok2, "strcat ( strcat"))
            tok2 = tok2->tokAt(2);

        if (tok2->strAt(3) != ",")
            continue;

        // If we have this code:
        //   strcat(strcat(dst, foo), bar);
        // We move this part of code before all strcat() calls: strcat(dst, foo)
        // And place "dst" token where the code was.
        Token *prevTok = tok2->previous();

        // Move tokens to new place
        Token::move(tok2, tok2->next()->link(), tok);
        tok = tok2->next()->link();

        // Insert the "dst" token
        prevTok->insertToken(tok2->strAt(2));
        prevTok->next()->varId(tok2->tokAt(2)->varId());

        // Insert semicolon after the moved strcat()
        tok->insertToken(";");
    }
}

static const std::set<std::string> stdFunctionsPresentInC = {
    "strcat",
    "strcpy",
    "strncat",
    "strncpy",
    "free",
    "malloc",
    "strdup"
};

void Tokenizer::simplifyStd()
{
    if (isC())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != "std")
            continue;

        if (Token::Match(tok->previous(), "[(,{};] std :: %name% (") &&
            stdFunctionsPresentInC.find(tok->strAt(2)) != stdFunctionsPresentInC.end()) {
            tok->deleteNext();
            tok->deleteThis();
        }
    }
}

//---------------------------------------------------------------------------
// Helper functions for handling the tokens list
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

bool Tokenizer::isScopeNoReturn(const Token *endScopeToken, bool *unknown) const
{
    std::string unknownFunc;
    const bool ret = mSettings->library.isScopeNoReturn(endScopeToken,&unknownFunc);
    if (!unknownFunc.empty() && mSettings->summaryReturn.find(unknownFunc) != mSettings->summaryReturn.end()) {
        return false;
    }
    if (unknown)
        *unknown = !unknownFunc.empty();
    if (!unknownFunc.empty() && mSettings->checkLibrary && mSettings->severity.isEnabled(Severity::information)) {
        // Is function global?
        bool globalFunction = true;
        if (Token::simpleMatch(endScopeToken->tokAt(-2), ") ; }")) {
            const Token * const ftok = endScopeToken->linkAt(-2)->previous();
            if (ftok &&
                ftok->isName() &&
                ftok->function() &&
                ftok->function()->nestedIn &&
                ftok->function()->nestedIn->type != Scope::eGlobal) {
                globalFunction = false;
            }
        }

        // don't warn for nonglobal functions (class methods, functions hidden in namespaces) since they can't be configured yet
        // FIXME: when methods and namespaces can be configured properly, remove the "globalFunction" check
        if (globalFunction) {
            reportError(endScopeToken->previous(),
                        Severity::information,
                        "checkLibraryNoReturn",
                        "--check-library: Function " + unknownFunc + "() should have <noreturn> configuration");
        }
    }
    return ret;
}

//---------------------------------------------------------------------------

bool Tokenizer::isFunctionParameterPassedByValue(const Token *fpar) const
{
    // TODO: If symbol database is available, use it.
    const Token *ftok;

    // Look at function call, what parameter number is it?
    int parameter = 1;
    for (ftok = fpar->previous(); ftok; ftok = ftok->previous()) {
        if (ftok->str() == "(")
            break;
        else if (ftok->str() == ")")
            ftok = ftok->link();
        else if (ftok->str() == ",")
            ++parameter;
        else if (Token::Match(ftok, "[;{}]"))
            break;
    }

    // Is this a function call?
    if (ftok && Token::Match(ftok->tokAt(-2), "[;{}=] %name% (")) {
        const std::string& functionName(ftok->previous()->str());

        if (functionName == "return")
            return true;

        // Locate function declaration..
        for (const Token *tok = tokens(); tok; tok = tok->next()) {
            if (tok->str() == "{")
                tok = tok->link();
            else if (Token::Match(tok, "%type% (") && tok->str() == functionName) {
                // Goto parameter
                tok = tok->tokAt(2);
                int par = 1;
                while (tok && par < parameter) {
                    if (tok->str() == ")")
                        break;
                    if (tok->str() == ",")
                        ++par;
                    tok = tok->next();
                }
                if (!tok)
                    return false;

                // If parameter was found, determine if it's passed by value
                if (par == parameter) {
                    bool knowntype = false;
                    while (tok && tok->isName()) {
                        knowntype |= tok->isStandardType();
                        knowntype |= (tok->str() == "struct");
                        tok = tok->next();
                    }
                    if (!tok || !knowntype)
                        return false;
                    if (tok->str() != "," && tok->str() != ")")
                        return false;
                    return true;
                }
            }
        }
    }
    return false;
}

//---------------------------------------------------------------------------

void Tokenizer::eraseDeadCode(Token *begin, const Token *end)
{
    if (!begin)
        return;
    const bool isgoto = Token::Match(begin->tokAt(-2), "goto %name% ;");
    int indentlevel = 1;
    int indentcase = 0;
    int indentswitch = 0;
    int indentlabel = 0;
    int roundbraces = 0;
    int indentcheck = 0;
    std::vector<int> switchindents;
    bool checklabel = false;
    Token *tok = begin;
    Token *tokcheck = nullptr;
    while (tok->next() && tok->next() != end) {
        if (tok->next()->str() == "(") {
            ++roundbraces;
            tok->deleteNext();
            continue;
        } else if (tok->next()->str() == ")") {
            if (!roundbraces)
                break;  //too many ending round parentheses
            --roundbraces;
            tok->deleteNext();
            continue;
        }

        if (roundbraces) {
            tok->deleteNext();
            continue;
        }

        if (Token::Match(tok, "[{};] switch (")) {
            if (!checklabel) {
                if (!indentlabel) {
                    //remove 'switch ( ... )'
                    Token::eraseTokens(tok, tok->linkAt(2)->next());
                } else {
                    tok = tok->linkAt(2);
                }
                if (tok->next()->str() == "{") {
                    ++indentswitch;
                    indentcase = indentlevel + 1;
                    switchindents.push_back(indentcase);
                }
            } else {
                tok = tok->linkAt(2);
                if (Token::simpleMatch(tok, ") {")) {
                    ++indentswitch;
                    indentcase = indentlevel + 1;
                    switchindents.push_back(indentcase);
                }
            }
        } else if (tok->next()->str() == "{") {
            ++indentlevel;
            if (!checklabel) {
                checklabel = true;
                tokcheck = tok;
                indentcheck = indentlevel;
                indentlabel = 0;
            }
            tok = tok->next();
        } else if (tok->next()->str() == "}") {
            --indentlevel;
            if (!indentlevel)
                break;

            if (!checklabel) {
                tok->deleteNext();
            } else {
                if (indentswitch && indentlevel == indentcase)
                    --indentlevel;
                if (indentlevel < indentcheck) {
                    const Token *end2 = tok->next();
                    tok = end2->link()->previous();  //return to initial '{'
                    if (indentswitch && Token::simpleMatch(tok, ") {") && Token::Match(tok->link()->tokAt(-2), "[{};] switch ("))
                        tok = tok->link()->tokAt(-2);       //remove also 'switch ( ... )'
                    Token::eraseTokens(tok, end2->next());
                    checklabel = false;
                    tokcheck = nullptr;
                    indentcheck = 0;
                } else {
                    tok = tok->next();
                }
            }
            if (indentswitch && indentlevel <= indentcase) {
                --indentswitch;
                switchindents.pop_back();
                if (!indentswitch)
                    indentcase = 0;
                else
                    indentcase = switchindents[indentswitch-1];
            }
        } else if (Token::Match(tok, "[{};:] case")) {
            const Token *tok2 = Token::findsimplematch(tok->next(), ": ;", end);
            if (!tok2) {
                tok->deleteNext();
                continue;
            }
            if (indentlevel == 1)
                break;      //it seems like the function was called inside a case-default block.
            if (indentlevel == indentcase)
                ++indentlevel;
            tok2 = tok2->next();
            if (!checklabel || !indentswitch) {
                Token::eraseTokens(tok, tok2->next());
            } else {
                tok = const_cast<Token *>(tok2);
            }
        } else if (Token::Match(tok, "[{};] default : ;")) {
            if (indentlevel == 1)
                break;      //it seems like the function was called inside a case-default block.
            if (indentlevel == indentcase)
                ++indentlevel;
            if (!checklabel || !indentswitch) {
                tok->deleteNext(3);
            } else {
                tok = tok->tokAt(3);
            }
        } else if (Token::Match(tok, "[{};] %name% : ;") && tok->next()->str() != "default") {
            if (checklabel) {
                indentlabel = indentlevel;
                tok = tokcheck->next();
                checklabel = false;
                indentlevel = indentcheck;
            } else {
                if (indentswitch) {
                    //Before stopping the function, since the 'switch()'
                    //instruction is removed, there's no sense to keep the
                    //case instructions. Remove them, if there are any.
                    Token *tok2 = tok->tokAt(3);
                    int indentlevel2 = indentlevel;
                    while (tok2->next() && tok2->next() != end) {
                        if (Token::Match(tok2->next(), "{|[|(")) {
                            tok2 = tok2->next()->link();
                        } else if (Token::Match(tok2, "[{};:] case")) {
                            const Token *tok3 = Token::findsimplematch(tok2->next(), ": ;", end);
                            if (!tok3) {
                                tok2 = tok2->next();
                                continue;
                            }
                            Token::eraseTokens(tok2, tok3->next());
                        } else if (Token::Match(tok2, "[{};] default : ;")) {
                            tok2->deleteNext(3);
                        } else if (tok2->next()->str() == "}") {
                            --indentlevel2;
                            if (indentlevel2 <= indentcase)
                                break;
                            tok2 = tok2->next();
                        } else {
                            tok2 = tok2->next();
                        }
                    }
                }
                break;  //stop removing tokens, we arrived to the label.
            }
        } else if (isgoto && Token::Match(tok, "[{};] do|while|for|BOOST_FOREACH")) {
            //it's possible that code inside loop is not dead,
            //because of the possible presence of the label pointed by 'goto'
            const Token *start = tok->tokAt(2);
            if (start && start->str() == "(")
                start = start->link()->next();
            if (start && start->str() == "{") {
                std::string labelpattern = "[{};] " + begin->previous()->str() + " : ;";
                bool simplify = true;
                for (Token *tok2 = start->next(); tok2 != start->link(); tok2 = tok2->next()) {
                    if (Token::Match(tok2, labelpattern.c_str())) {
                        simplify = false;
                        break;
                    }
                }
                //bailout for now
                if (!simplify)
                    break;
            }
            tok->deleteNext();
        } else {
            // no need to keep the other strings, remove them.
            if (tok->strAt(1) == "while") {
                if (tok->str() == "}" && tok->link()->strAt(-1) == "do")
                    tok->link()->previous()->deleteThis();
            }
            tok->deleteNext();
        }
    }
}

//---------------------------------------------------------------------------

void Tokenizer::syntaxError(const Token *tok, const std::string &code) const
{
    printDebugOutput(0);
    throw InternalError(tok, code.empty() ? "syntax error" : "syntax error: " + code, InternalError::SYNTAX);
}

void Tokenizer::unmatchedToken(const Token *tok) const
{
    printDebugOutput(0);
    throw InternalError(tok,
                        "Unmatched '" + tok->str() + "'. Configuration: '" + mConfiguration + "'.",
                        InternalError::SYNTAX);
}

void Tokenizer::syntaxErrorC(const Token *tok, const std::string &what) const
{
    printDebugOutput(0);
    throw InternalError(tok, "Code '"+what+"' is invalid C code. Use --std or --language to configure the language.", InternalError::SYNTAX);
}

void Tokenizer::unknownMacroError(const Token *tok1) const
{
    printDebugOutput(0);
    throw InternalError(tok1, "There is an unknown macro here somewhere. Configuration is required. If " + tok1->str() + " is a macro then please configure it.", InternalError::UNKNOWN_MACRO);
}

void Tokenizer::unhandled_macro_class_x_y(const Token *tok) const
{
    reportError(tok,
                Severity::information,
                "class_X_Y",
                "The code '" +
                tok->str() + " " +
                tok->strAt(1) + " " +
                tok->strAt(2) + " " +
                tok->strAt(3) + "' is not handled. You can use -I or --include to add handling of this code.");
}

void Tokenizer::macroWithSemicolonError(const Token *tok, const std::string &macroName) const
{
    reportError(tok,
                Severity::information,
                "macroWithSemicolon",
                "Ensure that '" + macroName + "' is defined either using -I, --include or -D.");
}

void Tokenizer::cppcheckError(const Token *tok) const
{
    printDebugOutput(0);
    throw InternalError(tok, "Analysis failed. If the code is valid then please report this failure.", InternalError::INTERNAL);
}

void Tokenizer::unhandledCharLiteral(const Token *tok, const std::string& msg) const
{
    std::string s = tok ? (" " + tok->str()) : "";
    for (int i = 0; i < s.size(); ++i) {
        if ((unsigned char)s[i] >= 0x80)
            s.clear();
    }

    reportError(tok,
                Severity::portability,
                "nonStandardCharLiteral",
                "Non-standard character literal" + s + ". " + msg);
}

/**
 * Helper function to check whether number is equal to integer constant X
 * or floating point pattern X.0
 * @param s the string to check
 * @param intConstant the integer constant to check against
 * @param floatConstant the string with stringified float constant to check against
 * @return true in case s is equal to X or X.0 and false otherwise.
 */
static bool isNumberOneOf(const std::string &s, const MathLib::bigint& intConstant, const char* floatConstant)
{
    if (MathLib::isInt(s)) {
        if (MathLib::toLongNumber(s) == intConstant)
            return true;
    } else if (MathLib::isFloat(s)) {
        if (MathLib::toString(MathLib::toDoubleNumber(s)) == floatConstant)
            return true;
    }
    return false;
}

// ------------------------------------------------------------------------
// Helper function to check whether number is zero (0 or 0.0 or 0E+0) or not?
// @param s the string to check
// @return true in case s is zero and false otherwise.
// ------------------------------------------------------------------------
bool Tokenizer::isZeroNumber(const std::string &s)
{
    return isNumberOneOf(s, 0L, "0.0");
}

// ------------------------------------------------------------------------
// Helper function to check whether number is one (1 or 0.1E+1 or 1E+0) or not?
// @param s the string to check
// @return true in case s is one and false otherwise.
// ------------------------------------------------------------------------
bool Tokenizer::isOneNumber(const std::string &s)
{
    if (!MathLib::isPositive(s))
        return false;
    return isNumberOneOf(s, 1L, "1.0");
}

// ------------------------------------------------------------------------
// Helper function to check whether number is two (2 or 0.2E+1 or 2E+0) or not?
// @param s the string to check
// @return true in case s is two and false otherwise.
// ------------------------------------------------------------------------
bool Tokenizer::isTwoNumber(const std::string &s)
{
    if (!MathLib::isPositive(s))
        return false;
    return isNumberOneOf(s, 2L, "2.0");
}

// ------------------------------------------------------
// Simplify math functions.
// It simplifies the following functions: atol(), fmin(),
// fminl(), fminf(), fmax(), fmaxl(), fmaxf(), pow(),
// powf(), powl(), cbrt(), cbrtl(), cbtrf(), sqrt(),
// sqrtf(), sqrtl(), exp(), expf(), expl(), exp2(),
// exp2f(), exp2l(), log2(), log2f(), log2l(), log1p(),
// log1pf(), log1pl(), log10(), log10l(), log10f(),
// log(), logf(), logl(), logb(), logbf(), logbl(), acosh()
// acoshf(), acoshl(), acos(), acosf(), acosl(), cosh()
// coshf(), coshf(), cos(), cosf(), cosl(), erfc(),
// erfcf(), erfcl(), ilogb(), ilogbf(), ilogbf(), erf(),
// erfl(), erff(), asin(), asinf(), asinf(), asinh(),
// asinhf(), asinhl(), tan(), tanf(), tanl(), tanh(),
// tanhf(), tanhl(), atan(), atanf(), atanl(), atanh(),
// atanhf(), atanhl(), expm1(), expm1l(), expm1f(), sin(),
// sinf(), sinl(), sinh(), sinhf(), sinhl()
// in the tokenlist.
//
// Reference:
// - http://www.cplusplus.com/reference/cmath/
// ------------------------------------------------------
void Tokenizer::simplifyMathFunctions()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->isName() && !tok->varId() && tok->strAt(1) == "(") { // precondition for function
            bool simplifcationMade = false;
            if (Token::Match(tok, "atol ( %str% )")) { //@todo Add support for atoll()
                if (Token::simpleMatch(tok->tokAt(-2), "std ::")) {
                    tok = tok->tokAt(-2);// set token index two steps back
                    tok->deleteNext(2);  // delete "std ::"
                }
                const std::string& strNumber = tok->tokAt(2)->strValue(); // get number
                const bool isNotAnInteger = (!MathLib::isInt(strNumber));// check: is not an integer
                if (strNumber.empty() || isNotAnInteger) {
                    // Ignore strings which we can't convert
                    continue;
                }
                // Convert string into a number and insert into token list
                tok->str(MathLib::toString(MathLib::toLongNumber(strNumber)));
                // remove ( %num% )
                tok->deleteNext(3);
                simplifcationMade = true;
            } else if (Token::Match(tok, "sqrt|sqrtf|sqrtl|cbrt|cbrtf|cbrtl ( %num% )")) {
                // Simplify: sqrt(0) = 0 and cbrt(0) == 0
                //           sqrt(1) = 1 and cbrt(1) == 1
                // get number string
                const std::string& parameter(tok->strAt(2));
                // is parameter 0 ?
                if (isZeroNumber(parameter)) {
                    tok->deleteNext(3);  // delete tokens
                    tok->str("0"); // insert result into token list
                    simplifcationMade = true;
                } else if (isOneNumber(parameter)) {
                    tok->deleteNext(3);  // delete tokens
                    tok->str("1"); // insert result into token list
                    simplifcationMade = true;
                }
            } else if (Token::Match(tok, "exp|expf|expl|exp2|exp2f|exp2l|cos|cosf|cosl|cosh|coshf|coshl|erfc|erfcf|erfcl ( %num% )")) {
                // Simplify: exp[f|l](0)  = 1 and exp2[f|l](0) = 1
                //           cosh[f|l](0) = 1 and cos[f|l](0)  = 1
                //           erfc[f|l](0) = 1
                // get number string
                const std::string& parameter(tok->strAt(2));
                // is parameter 0 ?
                if (isZeroNumber(parameter)) {
                    tok->deleteNext(3);  // delete tokens
                    tok->str("1"); // insert result into token list
                    simplifcationMade = true;
                }
            } else if (Token::Match(tok, "log1p|log1pf|log1pl|sin|sinf|sinl|sinh|sinhf|sinhl|erf|erff|erfl|asin|asinf|asinl|asinh|asinhf|asinhl|tan|tanf|tanl|tanh|tanhf|tanhl|atan|atanf|atanl|atanh|atanhf|atanhl|expm1|expm1f|expm1l ( %num% )")) {
                // Simplify: log1p[f|l](0) = 0 and sin[f|l](0)  = 0
                //           sinh[f|l](0)  = 0 and erf[f|l](0)  = 0
                //           asin[f|l](0)  = 0 and sinh[f|l](0) = 0
                //           tan[f|l](0)   = 0 and tanh[f|l](0) = 0
                //           atan[f|l](0)  = 0 and atanh[f|l](0)= 0
                //           expm1[f|l](0) = 0
                // get number string
                const std::string& parameter(tok->strAt(2));
                // is parameter 0 ?
                if (isZeroNumber(parameter)) {
                    tok->deleteNext(3);  // delete tokens
                    tok->str("0"); // insert result into token list
                    simplifcationMade = true;
                }
            } else if (Token::Match(tok, "log2|log2f|log2l|log|logf|logl|log10|log10f|log10l|logb|logbf|logbl|acosh|acoshf|acoshl|acos|acosf|acosl|ilogb|ilogbf|ilogbl ( %num% )")) {
                // Simplify: log2[f|l](1)  = 0 , log10[f|l](1)  = 0
                //           log[f|l](1)   = 0 , logb10[f|l](1) = 0
                //           acosh[f|l](1) = 0 , acos[f|l](1)   = 0
                //           ilogb[f|l](1) = 0
                // get number string
                const std::string& parameter(tok->strAt(2));
                // is parameter 1 ?
                if (isOneNumber(parameter)) {
                    tok->deleteNext(3);  // delete tokens
                    tok->str("0"); // insert result into token list
                    simplifcationMade = true;
                }
            } else if (Token::Match(tok, "fmin|fminl|fminf ( %num% , %num% )")) {
                // @todo if one of the parameters is NaN the other is returned
                // e.g. printf ("fmin (NaN, -1.0) = %f\n", fmin(NaN,-1.0));
                // e.g. printf ("fmin (-1.0, NaN) = %f\n", fmin(-1.0,NaN));
                const std::string& strLeftNumber(tok->strAt(2));
                const std::string& strRightNumber(tok->strAt(4));
                const bool isLessEqual =  MathLib::isLessEqual(strLeftNumber, strRightNumber);
                // case: left <= right ==> insert left
                if (isLessEqual) {
                    tok->str(strLeftNumber); // insert e.g. -1.0
                    tok->deleteNext(5);      // delete e.g. fmin ( -1.0, 1.0 )
                    simplifcationMade = true;
                } else { // case left > right ==> insert right
                    tok->str(strRightNumber); // insert e.g. 0.0
                    tok->deleteNext(5);       // delete e.g. fmin ( 1.0, 0.0 )
                    simplifcationMade = true;
                }
            } else if (Token::Match(tok, "fmax|fmaxl|fmaxf ( %num% , %num% )")) {
                // @todo if one of the parameters is NaN the other is returned
                // e.g. printf ("fmax (NaN, -1.0) = %f\n", fmax(NaN,-1.0));
                // e.g. printf ("fmax (-1.0, NaN) = %f\n", fmax(-1.0,NaN));
                const std::string& strLeftNumber(tok->strAt(2));
                const std::string& strRightNumber(tok->strAt(4));
                const bool isLessEqual =  MathLib::isLessEqual(strLeftNumber, strRightNumber);
                // case: left <= right ==> insert right
                if (isLessEqual) {
                    tok->str(strRightNumber);// insert e.g. 1.0
                    tok->deleteNext(5);      // delete e.g. fmax ( -1.0, 1.0 )
                    simplifcationMade = true;
                } else { // case left > right ==> insert left
                    tok->str(strLeftNumber);  // insert e.g. 1.0
                    tok->deleteNext(5);       // delete e.g. fmax ( 1.0, 0.0 )
                    simplifcationMade = true;
                }
            } else if (Token::Match(tok, "pow|powf|powl (")) {
                if (Token::Match(tok->tokAt(2), "%num% , %num% )")) {
                    // In case of pow ( 0 , anyNumber > 0): It can be simplified to 0
                    // In case of pow ( 0 , 0 ): It simplified to 1
                    // In case of pow ( 1 , anyNumber ): It simplified to 1
                    const std::string& leftNumber(tok->strAt(2)); // get the left parameter
                    const std::string& rightNumber(tok->strAt(4)); // get the right parameter
                    const bool isLeftNumberZero = isZeroNumber(leftNumber);
                    const bool isLeftNumberOne = isOneNumber(leftNumber);
                    const bool isRightNumberZero = isZeroNumber(rightNumber);
                    if (isLeftNumberZero && !isRightNumberZero && MathLib::isPositive(rightNumber)) { // case: 0^(y) = 0 and y > 0
                        tok->deleteNext(5); // delete tokens
                        tok->str("0");  // insert simplified result
                        simplifcationMade = true;
                    } else if (isLeftNumberZero && isRightNumberZero) { // case: 0^0 = 1
                        tok->deleteNext(5); // delete tokens
                        tok->str("1");  // insert simplified result
                        simplifcationMade = true;
                    } else if (isLeftNumberOne) { // case 1^(y) = 1
                        tok->deleteNext(5); // delete tokens
                        tok->str("1");  // insert simplified result
                        simplifcationMade = true;
                    }
                }
                if (Token::Match(tok->tokAt(2), "%any% , %num% )")) {
                    // In case of pow( x , 1 ): It can be simplified to x.
                    const std::string& leftParameter(tok->strAt(2)); // get the left parameter
                    const std::string& rightNumber(tok->strAt(4)); // get right number
                    if (isOneNumber(rightNumber)) { // case: x^(1) = x
                        tok->str(leftParameter);  // insert simplified result
                        tok->deleteNext(5); // delete tokens
                        simplifcationMade = true;
                    } else if (isZeroNumber(rightNumber)) { // case: x^(0) = 1
                        tok->deleteNext(5); // delete tokens
                        tok->str("1");  // insert simplified result
                        simplifcationMade = true;
                    }
                }
            }
            // Jump back to begin of statement if a simplification was performed
            if (simplifcationMade) {
                while (tok->previous() && tok->str() != ";") {
                    tok = tok->previous();
                }
            }
        }
    }
}

void Tokenizer::simplifyComma()
{
    bool inReturn = false;

    for (Token *tok = list.front(); tok; tok = tok->next()) {

        // skip enums
        if (Token::Match(tok, "enum class|struct| %name%| :|{")) {
            skipEnumBody(&tok);
        }
        if (!tok)
            syntaxError(nullptr); // invalid code like in #4195

        if (Token::Match(tok, "(|[") || Token::Match(tok->previous(), "%name%|= {")) {
            tok = tok->link();
            continue;
        }

        if (Token::simpleMatch(tok, "= (") && Token::simpleMatch(tok->linkAt(1), ") {")) {
            tok = tok->linkAt(1)->linkAt(1);
            continue;
        }

        // Skip unhandled template specifiers..
        if (tok->link() && tok->str() == "<")
            tok = tok->link();

        if (tok->str() == "return" && Token::Match(tok->previous(), "[;{}]"))
            inReturn = true;

        if (inReturn && Token::Match(tok, "[;{}?:]"))
            inReturn = false;

        if (!tok->next() || tok->str() != ",")
            continue;

        // We must not accept just any keyword, e.g. accepting int
        // would cause function parameters to corrupt.
        if (isCPP() && tok->strAt(1) == "delete") {
            // Handle "delete a, delete b;"
            tok->str(";");
        }

        if (isCPP() && Token::Match(tok->tokAt(-2), "delete %name% , %name% ;") &&
            tok->next()->varId() != 0) {
            // Handle "delete a, b;" - convert to delete a; b;
            tok->str(";");
        } else if (!inReturn && tok->tokAt(-2)) {
            bool replace = false;
            for (Token *tok2 = tok->previous(); tok2; tok2 = tok2->previous()) {
                if (tok2->str() == "=") {
                    // Handle "a = 0, b = 0;"
                    replace = true;
                } else if (isCPP() && (Token::Match(tok2, "delete %name%") ||
                                       Token::Match(tok2, "delete [ ] %name%"))) {
                    // Handle "delete a, a = 0;"
                    replace = true;
                } else if (Token::Match(tok2, "[?:;,{}()]")) {
                    if (replace && Token::Match(tok2, "[;{}]"))
                        tok->str(";");
                    break;
                }
            }
        }

        // find token where return ends and also count commas
        if (inReturn) {
            Token *startFrom = nullptr;    // "[;{}]" token before "return"
            Token *endAt = nullptr;        // first ";" token after "[;{}] return"

            // find "; return" pattern before comma
            for (Token *tok2 = tok->previous(); tok2; tok2 = tok2->previous()) {
                if (tok2->str() == "return") {
                    startFrom = tok2->previous();
                    break;
                }
            }
            if (!startFrom)
                // to be very sure...
                return;
            int commaCounter = 0;
            for (Token *tok2 = startFrom->next(); tok2; tok2 = tok2->next()) {
                if (tok2->str() == ";") {
                    endAt = tok2;
                    break;

                } else if (Token::Match(tok2, "(|[") ||
                           (tok2->str() == "{" && tok2->previous() && tok2->previous()->str() == "=")) {
                    tok2 = tok2->link();

                } else if (tok2->str() == ",") {
                    ++commaCounter;
                }
            }

            if (!endAt)
                //probably a syntax error
                return;

            if (commaCounter) {
                // change tokens:
                // "; return a ( ) , b ( ) , c ;"
                // to
                // "; a ( ) ; b ( ) ; return c ;"

                // remove "return"
                startFrom->deleteNext();
                for (Token *tok2 = startFrom->next(); tok2 != endAt; tok2 = tok2->next()) {
                    if (Token::Match(tok2, "(|[") ||
                        (tok2->str() == "{" && tok2->previous() && tok2->previous()->str() == "=")) {
                        tok2 = tok2->link();

                    } else if (tok2->str() == ",") {
                        tok2->str(";");
                        --commaCounter;
                        if (commaCounter == 0) {
                            tok2->insertToken("return");
                        }
                    }
                }
                tok = endAt;
            }
        }
    }
}

void Tokenizer::checkConfiguration() const
{
    if (!mSettings->checkConfiguration)
        return;
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "%name% ("))
            continue;
        if (tok->isControlFlowKeyword())
            continue;
        for (const Token *tok2 = tok->tokAt(2); tok2 && tok2->str() != ")"; tok2 = tok2->next()) {
            if (tok2->str() == ";") {
                macroWithSemicolonError(tok, tok->str());
                break;
            }
            if (Token::Match(tok2, "(|{"))
                tok2 = tok2->link();
        }
    }
}

void Tokenizer::validateC() const
{
    if (isCPP())
        return;
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        // That might trigger false positives, but it's much faster to have this truncated pattern
        if (Token::Match(tok, "const_cast|dynamic_cast|reinterpret_cast|static_cast <"))
            syntaxErrorC(tok, "C++ cast <...");
        // Template function..
        if (Token::Match(tok, "%name% < %name% > (")) {
            const Token *tok2 = tok->tokAt(5);
            while (tok2 && !Token::Match(tok2, "[()]"))
                tok2 = tok2->next();
            if (Token::simpleMatch(tok2, ") {"))
                syntaxErrorC(tok, tok->str() + '<' + tok->strAt(2) + ">() {}");
        }
        if (tok->previous() && !Token::Match(tok->previous(), "[;{}]"))
            continue;
        if (Token::Match(tok, "using namespace %name% ;"))
            syntaxErrorC(tok, "using namespace " + tok->strAt(2));
        if (Token::Match(tok, "template < class|typename %name% [,>]"))
            syntaxErrorC(tok, "template<...");
        if (Token::Match(tok, "%name% :: %name%"))
            syntaxErrorC(tok, tok->str() + tok->strAt(1) + tok->strAt(2));
        if (Token::Match(tok, "class|namespace %name% [:{]"))
            syntaxErrorC(tok, tok->str() + tok->strAt(1) + tok->strAt(2));
    }
}

void Tokenizer::validate() const
{
    std::stack<const Token *> linkTokens;
    const Token *lastTok = nullptr;
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        lastTok = tok;
        if (Token::Match(tok, "[{([]") || (tok->str() == "<" && tok->link())) {
            if (tok->link() == nullptr)
                cppcheckError(tok);

            linkTokens.push(tok);
        }

        else if (Token::Match(tok, "[})]]") || (Token::Match(tok, ">|>>") && tok->link())) {
            if (tok->link() == nullptr)
                cppcheckError(tok);

            if (linkTokens.empty() == true)
                cppcheckError(tok);

            if (tok->link() != linkTokens.top())
                cppcheckError(tok);

            if (tok != tok->link()->link())
                cppcheckError(tok);

            linkTokens.pop();
        }

        else if (tok->link() != nullptr)
            cppcheckError(tok);
    }

    if (!linkTokens.empty())
        cppcheckError(linkTokens.top());

    // Validate that the Tokenizer::list.back() is updated correctly during simplifications
    if (lastTok != list.back())
        cppcheckError(lastTok);
}

static const Token *findUnmatchedTernaryOp(const Token * const begin, const Token * const end, int depth = 0)
{
    std::stack<const Token *> ternaryOp;
    for (const Token *tok = begin; tok != end && tok->str() != ";"; tok = tok->next()) {
        if (tok->str() == "?")
            ternaryOp.push(tok);
        else if (!ternaryOp.empty() && tok->str() == ":")
            ternaryOp.pop();
        else if (depth < 100 && Token::Match(tok,"(|[")) {
            const Token *inner = findUnmatchedTernaryOp(tok->next(), tok->link(), depth+1);
            if (inner)
                return inner;
            tok = tok->link();
        }
    }
    return ternaryOp.empty() ? nullptr : ternaryOp.top();
}

static bool isCPPAttribute(const Token * tok)
{
    return Token::simpleMatch(tok, "[ [") && tok->link() && tok->link()->previous() == tok->linkAt(1);
}

static bool isAlignAttribute(const Token * tok)
{
    return Token::simpleMatch(tok, "alignas (") && tok->next()->link();
}

static const Token* skipCPPOrAlignAttribute(const Token * tok)
{
    if (isCPPAttribute(tok)) {
        return tok->link();
    } else if (isAlignAttribute(tok)) {
        return tok->next()->link();
    }
    return tok;
}

static bool isNonMacro(const Token* tok)
{
    if (tok->isKeyword())
        return true;
    if (cAlternativeTokens.count(tok->str()) > 0)
        return true;
    if (tok->str().compare(0, 2, "__") == 0) // attribute/annotation
        return true;
    return false;
}

void Tokenizer::reportUnknownMacros() const
{
    // Report unknown macros used in expressions "%name% %num%"
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%name% %num%")) {
            // A keyword is not an unknown macro
            if (tok->isKeyword())
                continue;

            if (Token::Match(tok->previous(), "%op%|("))
                unknownMacroError(tok);
        }
    }

    // Report unknown macros that contain several statements "MACRO(a;b;c)"
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "%name% ("))
            continue;
        if (!tok->isUpperCaseName())
            continue;
        const Token *endTok = tok->linkAt(1);
        for (const Token *inner = tok->tokAt(2); inner != endTok; inner = inner->next()) {
            if (Token::Match(inner, "[[({]"))
                inner = inner->link();
            else if (inner->str() == ";")
                unknownMacroError(inner);
        }
    }

    // Report unknown macros that contain struct initialization "MACRO(a, .b=3)"
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "%name% ("))
            continue;
        const Token *endTok = tok->linkAt(1);
        for (const Token *inner = tok->tokAt(2); inner != endTok; inner = inner->next()) {
            if (Token::Match(inner, "[[({]"))
                inner = inner->link();
            else if (Token::Match(inner->previous(), "[,(] . %name% =|{"))
                unknownMacroError(tok);
        }
    }

    // Report unknown macros in non-executable scopes..
    std::set<std::string> possible;
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        // Skip executable scopes..
        if (tok->str() == "{") {
            const Token *prev = tok->previous();
            while (prev && prev->isName())
                prev = prev->previous();
            if (prev && prev->str() == ")")
                tok = tok->link();
            else
                possible.clear();
        } else if (tok->str() == "}")
            possible.clear();

        if (Token::Match(tok, "%name% (") && tok->isUpperCaseName() && Token::simpleMatch(tok->linkAt(1), ") (") && Token::simpleMatch(tok->linkAt(1)->linkAt(1), ") {")) {
            // A keyword is not an unknown macro
            if (tok->isKeyword())
                continue;

            const Token *bodyStart = tok->linkAt(1)->linkAt(1)->tokAt(2);
            const Token *bodyEnd = tok->link();
            for (const Token *tok2 = bodyStart; tok2 && tok2 != bodyEnd; tok2 = tok2->next()) {
                if (Token::Match(tok2, "if|switch|for|while|return"))
                    unknownMacroError(tok);
            }
        } else if (Token::Match(tok, "%name% (") && tok->isUpperCaseName() && Token::Match(tok->linkAt(1), ") %name% (") && Token::Match(tok->linkAt(1)->linkAt(2), ") [;{]")) {
            if (possible.count(tok->str()) == 0)
                possible.insert(tok->str());
            else
                unknownMacroError(tok);
        }
    }

    // String concatenation with unknown macros
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%str% %name% (") && Token::Match(tok->linkAt(2), ") %str%")) {
            if (tok->next()->isKeyword())
                continue;
            unknownMacroError(tok->next());
        }
        if (Token::Match(tok, "[(,] %name% (") && Token::Match(tok->linkAt(2), ") %name% %name%|,|)")) {
            if (tok->next()->isKeyword() || tok->linkAt(2)->next()->isKeyword())
                continue;
            if (cAlternativeTokens.count(tok->linkAt(2)->next()->str()) > 0)
                continue;
            if (tok->next()->str().compare(0, 2, "__") == 0) // attribute/annotation
                continue;
            unknownMacroError(tok->next());
        }
    }

    // Report unknown macros without commas or operators inbetween statements: MACRO1() MACRO2()
    for (const Token* tok = tokens(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "%name% ("))
            continue;
        if (isNonMacro(tok))
            continue;

        const Token* endTok = tok->linkAt(1);
        if (!Token::Match(endTok, ") %name% (|."))
            continue;

        const Token* tok2 = endTok->next();
        if (isNonMacro(tok2))
            continue;

        if (tok2->next()->str() == "(") {
            if (Token::Match(tok->previous(), "%name%|::|>"))
                continue;
        }

        unknownMacroError(tok);
    }
}

void Tokenizer::findGarbageCode() const
{
    const bool isCPP11 = isCPP() && mSettings->standards.cpp >= Standards::CPP11;

    static const std::unordered_set<std::string> nonConsecutiveKeywords{ "break",
                                                                         "continue",
                                                                         "for",
                                                                         "goto",
                                                                         "if",
                                                                         "return",
                                                                         "switch",
                                                                         "throw",
                                                                         "typedef",
                                                                         "while" };

    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        // initialization: = {
        if (Token::simpleMatch(tok, "= {") && Token::simpleMatch(tok->linkAt(1), "} ("))
            syntaxError(tok->linkAt(1));

        // Inside [] there can't be ; or various keywords
        else if (tok->str() == "[") {
            for (const Token* inner = tok->next(); inner != tok->link(); inner = inner->next()) {
                if (Token::Match(inner, "(|[|{"))
                    inner = inner->link();
                else if (Token::Match(inner, ";|goto|return|typedef"))
                    syntaxError(inner);
            }
        }

        // array assignment
        else if (Token::Match(tok, "%assign% [") && Token::simpleMatch(tok->linkAt(1), "] ;"))
            syntaxError(tok, tok->str() + "[...];");

        // UNKNOWN_MACRO(return)
        if (tok->isKeyword() && Token::Match(tok, "throw|return )") && Token::Match(tok->linkAt(1)->previous(), "%name% ("))
            unknownMacroError(tok->linkAt(1)->previous());

        // UNKNOWN_MACRO(return)
        else if (Token::Match(tok, "%name% throw|return") && std::isupper(tok->str()[0]))
            unknownMacroError(tok);

        // Assign/increment/decrement literal
        else if (Token::Match(tok, "!!) %num%|%str%|%char% %assign%|++|--"))
            syntaxError(tok, tok->next()->str() + " " + tok->strAt(2));

        if (tok->isControlFlowKeyword() && Token::Match(tok, "if|while|for|switch")) { // if|while|for|switch (EXPR) { ... }
            if (tok->previous() && !Token::Match(tok->previous(), "%name%|:|;|{|}|)")) {
                if (Token::Match(tok->previous(), "[,(]")) {
                    const Token *prev = tok->previous();
                    while (prev && prev->str() != "(") {
                        if (prev->str() == ")")
                            prev = prev->link();
                        prev = prev->previous();
                    }
                    if (prev && Token::Match(prev->previous(), "%name% ("))
                        unknownMacroError(prev->previous());
                }
                if (!Token::simpleMatch(tok->tokAt(-2), "operator \"\" if"))
                    syntaxError(tok);
            }
            if (!Token::Match(tok->next(), "( !!)"))
                syntaxError(tok);
            if (tok->str() != "for") {
                if (isGarbageExpr(tok->next(), tok->linkAt(1), mSettings->standards.cpp>=Standards::cppstd_t::CPP17))
                    syntaxError(tok);
            }
        }

        // keyword keyword
        if (tok->isKeyword() && nonConsecutiveKeywords.count(tok->str()) != 0) {
            if (Token::Match(tok, "%name% %name%") && nonConsecutiveKeywords.count(tok->next()->str()) == 1)
                syntaxError(tok);
            const Token* prev = tok;
            while (prev && prev->isName())
                prev = prev->previous();
            if (Token::Match(prev, "%op%|%num%|%str%|%char%")) {
                if (!Token::simpleMatch(tok->tokAt(-2), "operator \"\" if") &&
                    !Token::simpleMatch(tok->tokAt(-2), "extern \"C\""))
                    syntaxError(tok, prev == tok->previous() ? (prev->str() + " " + tok->str()) : (prev->str() + " .. " + tok->str()));
            }
        }
    }

    // invalid struct declaration
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (Token::Match(tok, "struct|class|enum %name%| {") && (!tok->previous() || Token::Match(tok->previous(), "[;{}]"))) {
            const Token *tok2 = tok->linkAt(tok->next()->isName() ? 2 : 1);
            if (Token::Match(tok2, "} %op%")) {
                tok2 = tok2->next();
                if (!Token::Match(tok2, "*|&|&&"))
                    syntaxError(tok2, "Unexpected token '" + tok2->str() + "'");
                while (Token::Match(tok2, "*|&|&&"))
                    tok2 = tok2->next();
                if (!Token::Match(tok2, "%name%"))
                    syntaxError(tok2, "Unexpected token '" + tok2->str() + "'");
            }
        }
    }

    // Keywords in global scope
    static const std::unordered_set<std::string> nonGlobalKeywords{"break",
                                                                   "continue",
                                                                   "for",
                                                                   "goto",
                                                                   "if",
                                                                   "return",
                                                                   "switch",
                                                                   "while",
                                                                   "try",
                                                                   "catch"};
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (tok->str() == "{")
            tok = tok->link();
        else if (tok->isKeyword() && nonGlobalKeywords.count(tok->str()) && !Token::Match(tok->tokAt(-2), "operator %str%"))
            syntaxError(tok, "keyword '" + tok->str() + "' is not allowed in global scope");
    }

    // case keyword must be inside switch
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (Token::simpleMatch(tok, "switch (")) {
            if (Token::simpleMatch(tok->linkAt(1), ") {")) {
                tok = tok->linkAt(1)->linkAt(1);
                continue;
            }
            const Token *switchToken = tok;
            tok = tok->linkAt(1);
            if (!tok)
                syntaxError(switchToken);
            // Look for the end of the switch statement, i.e. the first semi-colon or '}'
            for (; tok; tok = tok->next()) {
                if (tok->str() == "{") {
                    tok = tok->link();
                }
                if (Token::Match(tok, ";|}")) {
                    // We're at the end of the switch block
                    if (tok->str() == "}" && tok->strAt(-1) == ":") // Invalid case
                        syntaxError(switchToken);
                    break;
                }
            }
            if (!tok)
                break;
        } else if (tok->str() == "(") {
            tok = tok->link();
        } else if (tok->str() == "case") {
            syntaxError(tok);
        }
    }

    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (!Token::simpleMatch(tok, "for (")) // find for loops
            continue;
        // count number of semicolons
        int semicolons = 0;
        const Token* const startTok = tok;
        tok = tok->next()->link()->previous(); // find ")" of the for-loop
        // walk backwards until we find the beginning (startTok) of the for() again
        for (; tok != startTok; tok = tok->previous()) {
            if (tok->str() == ";") { // do the counting
                semicolons++;
            } else if (tok->str() == ")") { // skip pairs of ( )
                tok = tok->link();
            }
        }
        // if we have an invalid number of semicolons inside for( ), assume syntax error
        if (semicolons > 2)
            syntaxError(tok);
        if (semicolons == 1 && !(isCPP() && mSettings->standards.cpp >= Standards::CPP20))
            syntaxError(tok);
    }

    // Operators without operands..
    const Token *templateEndToken = nullptr;
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (!templateEndToken) {
            if (tok->str() == "<" && isCPP())
                templateEndToken = tok->findClosingBracket();
        } else {
            if (templateEndToken == tok)
                templateEndToken = nullptr;
            if (Token::Match(tok, "> %cop%"))
                continue;
        }
        // skip C++ attributes [[...]]
        if (isCPP11 && (isCPPAttribute(tok) || isAlignAttribute(tok))) {
            tok = skipCPPOrAlignAttribute(tok);
            continue;
        }
        {
            bool match1 = Token::Match(tok, "%or%|%oror%|==|!=|+|-|/|!|>=|<=|~|^|++|--|::|sizeof");
            bool match2 = Token::Match(tok->next(), "{|if|else|while|do|for|return|switch|break");
            if (isCPP()) {
                match1 = match1 || Token::Match(tok, "::|throw|decltype|typeof");
                match2 = match2 || Token::Match(tok->next(), "try|catch|namespace");
            }
            if (match1 && match2)
                syntaxError(tok);
        }
        if (Token::Match(tok, "%or%|%oror%|~|^|!|%comp%|+|-|/|%")) {
            std::string code;
            if (Token::Match(tok->next(), ")|]|}"))
                code = tok->str() + tok->next()->str();
            if (Token::simpleMatch(tok->next(), "( )"))
                code = tok->str() + "()";
            if (!code.empty()) {
                if (isC() || (tok->str() != ">" && !Token::simpleMatch(tok->previous(), "operator")))
                    syntaxError(tok, code);
            }
        }
        if (Token::Match(tok, "%num%|%bool%|%char%|%str% %num%|%bool%|%char%|%str%") && !Token::Match(tok, "%str% %str%"))
            syntaxError(tok);
        if (Token::Match(tok, "%assign% typename|class %assign%"))
            syntaxError(tok);
        if (Token::Match(tok, "%cop%|=|,|[ %or%|%oror%|/|%"))
            syntaxError(tok);
        if (Token::Match(tok, ";|(|[ %comp%"))
            syntaxError(tok);
        if (Token::Match(tok, "%cop%|= ]") && !(isCPP() && Token::Match(tok->previous(), "[|,|%num% &|=|> ]")))
            syntaxError(tok);
        if (Token::Match(tok, "[+-] [;,)]}]") && !(isCPP() && Token::Match(tok->previous(), "operator [+-] ;")))
            syntaxError(tok);
        if (Token::simpleMatch(tok, ",") &&
            !Token::Match(tok->tokAt(-2), "[ = , &|%name%")) {
            if (Token::Match(tok->previous(), "(|[|{|<|%assign%|%or%|%oror%|==|!=|+|-|/|!|>=|<=|~|^|::|sizeof"))
                syntaxError(tok);
            if (isCPP() && Token::Match(tok->previous(), "throw|decltype|typeof"))
                syntaxError(tok);
            if (Token::Match(tok->next(), ")|]|>|%assign%|%or%|%oror%|==|!=|/|>=|<=|&&"))
                syntaxError(tok);
        }
        if (Token::simpleMatch(tok, ".") &&
            !Token::simpleMatch(tok->previous(), ".") &&
            !Token::simpleMatch(tok->next(), ".") &&
            !Token::Match(tok->previous(), "{|, . %name% =|.|[|{") &&
            !Token::Match(tok->previous(), ", . %name%")) {
            if (!Token::Match(tok->previous(), "%name%|)|]|>|}"))
                syntaxError(tok, tok->strAt(-1) + " " + tok->str() + " " + tok->strAt(1));
            if (!Token::Match(tok->next(), "%name%|*|~"))
                syntaxError(tok, tok->strAt(-1) + " " + tok->str() + " " + tok->strAt(1));
        }
        if (Token::Match(tok, "[!|+-/%^~] )|]"))
            syntaxError(tok);
        if (Token::Match(tok, "==|!=|<=|>= %comp%") && tok->strAt(-1) != "operator")
            syntaxError(tok, tok->str() + " " + tok->strAt(1));
    }

    // ternary operator without :
    if (const Token *ternaryOp = findUnmatchedTernaryOp(tokens(), nullptr))
        syntaxError(ternaryOp);

    // Code must not start with an arithmetical operand
    if (Token::Match(list.front(), "%cop%"))
        syntaxError(list.front());

    // Code must end with } ; ) NAME
    if (!Token::Match(list.back(), "%name%|;|}|)"))
        syntaxError(list.back());
    if (list.back()->str() == ")" && !Token::Match(list.back()->link()->previous(), "%name%|> ("))
        syntaxError(list.back());
    for (const Token *end = list.back(); end && end->isName(); end = end->previous()) {
        if (Token::Match(end, "void|char|short|int|long|float|double|const|volatile|static|inline|struct|class|enum|union|template|sizeof|case|break|continue|typedef"))
            syntaxError(list.back());
    }
    if ((list.back()->str()==")" || list.back()->str()=="}") && list.back()->previous() && list.back()->previous()->isControlFlowKeyword())
        syntaxError(list.back()->previous());

    // Garbage templates..
    if (isCPP()) {
        for (const Token *tok = tokens(); tok; tok = tok->next()) {
            if (!Token::simpleMatch(tok, "template <"))
                continue;
            if (tok->previous() && !Token::Match(tok->previous(), ":|;|{|}|)|>|\"C++\"")) {
                if (tok->previous()->isUpperCaseName())
                    unknownMacroError(tok->previous());
                else
                    syntaxError(tok);
            }
            const Token * const tok1 = tok;
            tok = tok->next()->findClosingBracket();
            if (!tok)
                syntaxError(tok1);
            if (!Token::Match(tok, ">|>> ::|...| %name%") &&
                !Token::Match(tok, ">|>> [ [ %name%") &&
                !Token::Match(tok, "> >|*"))
                syntaxError(tok->next() ? tok->next() : tok1);
        }
    }

    // Objective C/C++
    for (const Token *tok = tokens(); tok; tok = tok->next()) {
        if (Token::Match(tok, "[;{}] [ %name% %name% ] ;"))
            syntaxError(tok->next());
    }
}


bool Tokenizer::isGarbageExpr(const Token *start, const Token *end, bool allowSemicolon)
{
    for (const Token *tok = start; tok != end; tok = tok->next()) {
        if (tok->isControlFlowKeyword())
            return true;
        if (!allowSemicolon && tok->str() == ";")
            return true;
        if (tok->str() == "{")
            tok = tok->link();
    }
    return false;
}

std::string Tokenizer::simplifyString(const std::string &source)
{
    std::string str = source;

    for (std::string::size_type i = 0; i + 1U < str.size(); ++i) {
        if (str[i] != '\\')
            continue;

        int c = 'a';   // char
        int sz = 0;    // size of stringdata
        if (str[i+1] == 'x') {
            sz = 2;
            while (sz < 4 && std::isxdigit((unsigned char)str[i+sz]))
                sz++;
            if (sz > 2) {
                std::istringstream istr(str.substr(i+2, sz-2));
                istr >> std::hex >> c;
            }
        } else if (MathLib::isOctalDigit(str[i+1])) {
            sz = 2;
            while (sz < 4 && MathLib::isOctalDigit(str[i+sz]))
                sz++;
            std::istringstream istr(str.substr(i+1, sz-1));
            istr >> std::oct >> c;
            str = str.substr(0,i) + (char)c + str.substr(i+sz);
            continue;
        }

        if (sz <= 2)
            i++;
        else if (i+sz < str.size())
            str.replace(i, sz, std::string(1U, (char)c));
        else
            str.replace(i, str.size() - i - 1U, "a");
    }

    return str;
}

void Tokenizer::simplifyWhile0()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // while (0)
        const bool while0(Token::Match(tok->previous(), "[{};] while ( 0|false )"));

        // for (0) - not banal, ticket #3140
        const bool for0((Token::Match(tok->previous(), "[{};] for ( %name% = %num% ; %name% < %num% ;") &&
                         tok->strAt(2) == tok->strAt(6) && tok->strAt(4) == tok->strAt(8)) ||
                        (Token::Match(tok->previous(), "[{};] for ( %type% %name% = %num% ; %name% < %num% ;") &&
                         tok->strAt(3) == tok->strAt(7) && tok->strAt(5) == tok->strAt(9)));

        if (!while0 && !for0)
            continue;

        if (while0 && tok->previous()->str() == "}") {
            // find "do"
            Token *tok2 = tok->previous()->link();
            tok2 = tok2->previous();
            if (tok2 && tok2->str() == "do") {
                const bool flowmatch = Token::findmatch(tok2, "continue|break", tok) != nullptr;
                // delete "do ({)"
                tok2->deleteThis();
                if (!flowmatch)
                    tok2->deleteThis();

                // delete "(}) while ( 0 ) (;)"
                tok = tok->previous();
                tok->deleteNext(4);  // while ( 0 )
                if (tok->next() && tok->next()->str() == ";")
                    tok->deleteNext(); // ;
                if (!flowmatch)
                    tok->deleteThis(); // }

                continue;
            }
        }

        // remove "while (0) { .. }"
        if (Token::simpleMatch(tok->next()->link(), ") {")) {
            Token *end = tok->next()->link(), *old_prev = tok->previous();
            end = end->next()->link();
            if (Token::Match(tok, "for ( %name% ="))
                old_prev = end->link();
            eraseDeadCode(old_prev, end->next());
            if (old_prev && old_prev->next())
                tok = old_prev->next();
            else
                break;
        }
    }
}

void Tokenizer::simplifyFunctionTryCatch()
{
    if (!isCPP())
        return;

    for (Token * tok = list.front(); tok; tok = tok->next()) {
        if (!Token::simpleMatch(tok, "try {"))
            continue;
        if (!isFunctionHead(tok->previous(), "try"))
            continue;

        // find the end of the last catch block
        Token * const tryEndToken = tok->linkAt(1);
        Token * endToken = tryEndToken;
        while (Token::simpleMatch(endToken, "} catch (")) {
            endToken = endToken->linkAt(2)->next();
            if (!endToken)
                break;
            if (endToken->str() != "{") {
                endToken = nullptr;
                break;
            }
            endToken = endToken->link();
        }
        if (!endToken || endToken == tryEndToken)
            continue;

        tok->previous()->insertToken("{");
        endToken->insertToken("}");
        Token::createMutualLinks(tok->previous(), endToken->next());
    }
}

void Tokenizer::simplifyErrNoInWhile()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != "errno")
            continue;

        Token *endpar = nullptr;
        if (Token::Match(tok->previous(), "&& errno == EINTR ) { ;| }"))
            endpar = tok->tokAt(3);
        else if (Token::Match(tok->tokAt(-2), "&& ( errno == EINTR ) ) { ;| }"))
            endpar = tok->tokAt(4);
        else
            continue;

        if (Token::simpleMatch(endpar->link()->previous(), "while (")) {
            Token *tok1 = tok->previous();
            if (tok1->str() == "(")
                tok1 = tok1->previous();

            // erase "&& errno == EINTR"
            tok1 = tok1->previous();
            Token::eraseTokens(tok1, endpar);

            // tok is invalid.. move to endpar
            tok = endpar;
        }
    }
}


void Tokenizer::simplifyFuncInWhile()
{
    int count = 0;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!Token::Match(tok, "while ( %name% ( %name% ) ) {"))
            continue;

        Token *func = tok->tokAt(2);
        const Token * const var = tok->tokAt(4);
        Token * const end = tok->next()->link()->next()->link();

        const int varid = ++mVarId; // Create new variable
        const std::string varname("cppcheck:r" + MathLib::toString(++count));
        tok->str("int");
        tok->next()->insertToken(varname);
        tok->tokAt(2)->varId(varid);
        tok->insertToken("while");
        tok->insertToken(";");
        tok->insertToken(")");
        tok->insertToken(var->str());
        tok->next()->varId(var->varId());
        tok->insertToken("(");
        tok->insertToken(func->str());
        tok->insertToken("=");
        tok->insertToken(varname);
        tok->next()->varId(varid);
        Token::createMutualLinks(tok->tokAt(4), tok->tokAt(6));
        end->previous()->insertToken(varname);
        end->previous()->varId(varid);
        end->previous()->insertToken("=");
        Token::move(func, func->tokAt(3), end->previous());
        end->previous()->insertToken(";");

        tok = end;
    }
}

void Tokenizer::simplifyStructDecl()
{
    const bool cpp = isCPP();

    // A counter that is used when giving unique names for anonymous structs.
    int count = 0;

    // Skip simplification of unions in class definition
    std::stack<bool> skip; // true = in function, false = not in function
    skip.push(false);

    // Add names for anonymous structs
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName())
            continue;
        // check for anonymous struct/union
        if (Token::Match(tok, "struct|union {")) {
            if (Token::Match(tok->next()->link(), "} const| *|&| const| %type% ,|;|[|(|{|=")) {
                tok->insertToken("Anonymous" + MathLib::toString(count++));
            }
        }
        // check for derived anonymous class/struct
        else if (cpp && Token::Match(tok, "class|struct :")) {
            const Token *tok1 = Token::findsimplematch(tok, "{");
            if (tok1 && Token::Match(tok1->link(), "} const| *|&| const| %type% ,|;|[|(|{")) {
                tok->insertToken("Anonymous" + MathLib::toString(count++));
            }
        }
        // check for anonymous enum
        else if ((Token::simpleMatch(tok, "enum {") &&
                  !Token::Match(tok->tokAt(-3), "using %name% =") &&
                  Token::Match(tok->next()->link(), "} (| %type%| )| ,|;|[|(|{")) ||
                 (Token::Match(tok, "enum : %type% {") && Token::Match(tok->linkAt(3), "} (| %type%| )| ,|;|[|(|{"))) {
            Token *start = tok->strAt(1) == ":" ? tok->linkAt(3) : tok->linkAt(1);
            if (start && Token::Match(start->next(), "( %type% )")) {
                start->next()->link()->deleteThis();
                start->next()->deleteThis();
            }
            tok->insertToken("Anonymous" + MathLib::toString(count++));
        }
    }

    for (Token *tok = list.front(); tok; tok = tok->next()) {

        // check for start of scope and determine if it is in a function
        if (tok->str() == "{")
            skip.push(Token::Match(tok->previous(), "const|)"));

        // end of scope
        else if (tok->str() == "}" && !skip.empty())
            skip.pop();

        // check for named struct/union
        else if (Token::Match(tok, "class|struct|union|enum %type% :|{")) {
            Token *start = tok;
            while (Token::Match(start->previous(), "%type%"))
                start = start->previous();
            const Token * const type = tok->next();
            Token *next = tok->tokAt(2);

            while (next && next->str() != "{")
                next = next->next();
            if (!next)
                continue;
            skip.push(false);
            tok = next->link();
            if (!tok)
                break; // see #4869 segmentation fault in Tokenizer::simplifyStructDecl (invalid code)
            Token *restart = next;

            // check for named type
            if (Token::Match(tok->next(), "const| *|&| const| (| %type% )| ,|;|[|=|(|{")) {
                tok->insertToken(";");
                tok = tok->next();
                while (!Token::Match(start, "struct|class|union|enum")) {
                    tok->insertToken(start->str());
                    tok = tok->next();
                    start->deleteThis();
                }
                if (!tok)
                    break; // see #4869 segmentation fault in Tokenizer::simplifyStructDecl (invalid code)
                tok->insertToken(type->str());
                if (start->str() != "class") {
                    tok->insertToken(start->str());
                    tok = tok->next();
                }

                tok = tok->tokAt(2);

                if (Token::Match(tok, "( %type% )")) {
                    tok->link()->deleteThis();
                    tok->deleteThis();
                }

                // check for initialization
                if (tok && (tok->next()->str() == "(" || tok->next()->str() == "{")) {
                    tok->insertToken("=");
                    tok = tok->next();

                    if (start->str() == "enum") {
                        if (tok->next()->str() == "{") {
                            tok->next()->str("(");
                            tok->linkAt(1)->str(")");
                        }
                    }
                }
            }

            tok = restart;
        }

        // check for anonymous struct/union
        else if (Token::Match(tok, "struct|union {")) {
            const bool inFunction = skip.top();
            skip.push(false);
            Token *tok1 = tok;

            Token *restart = tok->next();
            tok = tok->next()->link();

            // unnamed anonymous struct/union so possibly remove it
            if (tok && tok->next() && tok->next()->str() == ";") {
                if (inFunction && tok1->str() == "union") {
                    // Try to create references in the union..
                    Token *tok2 = tok1->tokAt(2);
                    while (tok2) {
                        if (Token::Match(tok2, "%type% %name% ;"))
                            tok2 = tok2->tokAt(3);
                        else
                            break;
                    }
                    if (!Token::simpleMatch(tok2, "} ;"))
                        continue;
                    Token *vartok = nullptr;
                    tok2 = tok1->tokAt(2);
                    while (Token::Match(tok2, "%type% %name% ;")) {
                        if (!vartok) {
                            vartok = tok2->next();
                            tok2 = tok2->tokAt(3);
                        } else {
                            tok2->insertToken("&");
                            tok2 = tok2->tokAt(2);
                            tok2->insertToken(vartok->str());
                            tok2->next()->varId(vartok->varId());
                            tok2->insertToken("=");
                            tok2 = tok2->tokAt(4);
                        }
                    }
                }

                // don't remove unnamed anonymous unions from a class, struct or union
                if (!(!inFunction && tok1->str() == "union") && !Token::Match(tok1->tokAt(-3), "using %name% =")) {
                    skip.pop();
                    tok1->deleteThis();
                    if (tok1->next() == tok) {
                        tok1->deleteThis();
                        tok = tok1;
                    } else
                        tok1->deleteThis();
                    restart = tok1->previous();
                    tok->deleteThis();
                    if (tok->next())
                        tok->deleteThis();
                }
            }

            if (!restart) {
                simplifyStructDecl();
                return;
            } else if (!restart->next())
                return;

            tok = restart;
        }
    }
}

void Tokenizer::simplifyCallingConvention()
{
    const bool windows = mSettings->isWindowsPlatform();

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (Token::Match(tok, "__cdecl|__stdcall|__fastcall|__thiscall|__clrcall|__syscall|__pascal|__fortran|__far|__near") || (windows && Token::Match(tok, "WINAPI|APIENTRY|CALLBACK"))) {
            tok->deleteThis();
        }
    }
}

void Tokenizer::simplifyDeclspec()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        while (Token::Match(tok, "__declspec|_declspec (") && tok->next()->link() && tok->next()->link()->next()) {
            if (Token::Match(tok->tokAt(2), "noreturn|nothrow")) {
                Token *tok1 = tok->next()->link()->next();
                while (tok1 && !Token::Match(tok1, "%name%")) {
                    tok1 = tok1->next();
                }
                if (tok1) {
                    if (tok->strAt(2) == "noreturn")
                        tok1->isAttributeNoreturn(true);
                    else
                        tok1->isAttributeNothrow(true);
                }
            } else if (tok->strAt(2) == "property")
                tok->next()->link()->insertToken("__property");

            Token::eraseTokens(tok, tok->next()->link()->next());
            tok->deleteThis();
        }
    }
}

void Tokenizer::simplifyAttribute()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%type% (") && !mSettings->library.isNotLibraryFunction(tok)) {
            if (mSettings->library.isFunctionConst(tok->str(), true))
                tok->isAttributePure(true);
            if (mSettings->library.isFunctionConst(tok->str(), false))
                tok->isAttributeConst(true);
        }
        while (Token::Match(tok, "__attribute__|__attribute (")) {
            Token *after = tok;
            while (Token::Match(after, "__attribute__|__attribute ("))
                after = after->linkAt(1)->next();
            if (!after)
                syntaxError(tok);

            Token *functok = nullptr;
            if (Token::Match(after, "%name%|*")) {
                Token *ftok = after;
                while (Token::Match(ftok, "%name%|::|<|* !!(")) {
                    if (ftok->str() == "<") {
                        ftok = ftok->findClosingBracket();
                        if (!ftok)
                            break;
                    }
                    ftok = ftok->next();
                }
                if (Token::Match(ftok, "%name% ("))
                    functok = ftok;
            } else if (Token::Match(after, "[;{=:]")) {
                Token *prev = tok->previous();
                while (Token::Match(prev, "%name%"))
                    prev = prev->previous();
                if (Token::simpleMatch(prev, ")") && Token::Match(prev->link()->previous(), "%name% ("))
                    functok = prev->link()->previous();
                else if ((!prev || Token::Match(prev, "[;{}*]")) && Token::Match(tok->previous(), "%name%"))
                    functok = tok->previous();
            }

            for (Token *attr = tok->tokAt(2); attr->str() != ")"; attr = attr->next()) {
                if (Token::Match(attr, "%name% ("))
                    attr = attr->linkAt(1);

                if (Token::Match(attr, "[(,] constructor|__constructor__ [,()]")) {
                    if (!functok)
                        syntaxError(tok);
                    functok->isAttributeConstructor(true);
                }

                else if (Token::Match(attr, "[(,] destructor|__destructor__ [,()]")) {
                    if (!functok)
                        syntaxError(tok);
                    functok->isAttributeDestructor(true);
                }

                else if (Token::Match(attr, "[(,] unused|__unused__|used|__used__ [,)]")) {
                    Token *vartok = nullptr;

                    // check if after variable name
                    if (Token::Match(after, ";|=")) {
                        if (Token::Match(tok->previous(), "%type%"))
                            vartok = tok->previous();
                    }

                    // check if before variable name
                    else if (Token::Match(after, "%type%"))
                        vartok = after;

                    if (vartok) {
                        const std::string &attribute(attr->next()->str());
                        if (attribute.find("unused") != std::string::npos)
                            vartok->isAttributeUnused(true);
                        else
                            vartok->isAttributeUsed(true);
                    }
                }

                else if (Token::Match(attr, "[(,] pure|__pure__|const|__const__|noreturn|__noreturn__|nothrow|__nothrow__|warn_unused_result [,)]")) {
                    if (!functok)
                        syntaxError(tok);

                    const std::string &attribute(attr->next()->str());
                    if (attribute.find("pure") != std::string::npos)
                        functok->isAttributePure(true);
                    else if (attribute.find("const") != std::string::npos)
                        functok->isAttributeConst(true);
                    else if (attribute.find("noreturn") != std::string::npos)
                        functok->isAttributeNoreturn(true);
                    else if (attribute.find("nothrow") != std::string::npos)
                        functok->isAttributeNothrow(true);
                    else if (attribute.find("warn_unused_result") != std::string::npos)
                        functok->isAttributeNodiscard(true);
                }

                else if (Token::Match(attr, "[(,] packed [,)]") && Token::simpleMatch(tok->previous(), "}"))
                    tok->previous()->isAttributePacked(true);
            }

            Token::eraseTokens(tok, tok->linkAt(1)->next());
            tok->deleteThis();
        }
    }
}

void Tokenizer::simplifyCppcheckAttribute()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != "(")
            continue;
        if (!tok->previous())
            continue;
        const std::string &attr = tok->previous()->str();
        if (attr.compare(0, 11, "__cppcheck_") != 0) // TODO: starts_with("__cppcheck_")
            continue;
        if (attr.compare(attr.size()-2, 2, "__") != 0) // TODO: ends_with("__")
            continue;

        Token *vartok = tok->link();
        while (Token::Match(vartok->next(), "%name%|*|&|::")) {
            vartok = vartok->next();
            if (Token::Match(vartok, "%name% (") && vartok->str().compare(0,11,"__cppcheck_") == 0)
                vartok = vartok->linkAt(1);
        }

        if (vartok->isName()) {
            if (Token::Match(tok->previous(), "__cppcheck_low__ ( %num% )"))
                vartok->setCppcheckAttribute(TokenImpl::CppcheckAttributes::Type::LOW, MathLib::toLongNumber(tok->next()->str()));
            else if (Token::Match(tok->previous(), "__cppcheck_high__ ( %num% )"))
                vartok->setCppcheckAttribute(TokenImpl::CppcheckAttributes::Type::HIGH, MathLib::toLongNumber(tok->next()->str()));
        }

        // Delete cppcheck attribute..
        if (tok->tokAt(-2)) {
            tok = tok->tokAt(-2);
            Token::eraseTokens(tok, tok->linkAt(2)->next());
        } else {
            tok = tok->previous();
            Token::eraseTokens(tok, tok->linkAt(1)->next());
            tok->str(";");
        }
    }
}

void Tokenizer::simplifyCPPAttribute()
{
    if (mSettings->standards.cpp < Standards::CPP11 || isC())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!isCPPAttribute(tok) && !isAlignAttribute(tok)) {
            continue;
        }
        if (isCPPAttribute(tok)) {
            if (Token::findsimplematch(tok->tokAt(2), "noreturn", tok->link())) {
                const Token * head = skipCPPOrAlignAttribute(tok);
                while (isCPPAttribute(head) || isAlignAttribute(head))
                    head = skipCPPOrAlignAttribute(head);
                head = head->next();
                while (Token::Match(head, "%name%|::|*|&|<|>|,")) // skip return type
                    head = head->next();
                if (head && head->str() == "(" && isFunctionHead(head, "{|;")) {
                    head->previous()->isAttributeNoreturn(true);
                }
            } else if (Token::findsimplematch(tok->tokAt(2), "nodiscard", tok->link())) {
                const Token * head = skipCPPOrAlignAttribute(tok);
                while (isCPPAttribute(head) || isAlignAttribute(head))
                    head = skipCPPOrAlignAttribute(head);
                head = head->next();
                while (Token::Match(head, "%name%|::|*|&|<|>|,"))
                    head = head->next();
                if (head && head->str() == "(" && isFunctionHead(head, "{|;")) {
                    head->previous()->isAttributeNodiscard(true);
                }
            } else if (Token::findsimplematch(tok->tokAt(2), "maybe_unused", tok->link())) {
                const Token* head = skipCPPOrAlignAttribute(tok);
                while (isCPPAttribute(head) || isAlignAttribute(head))
                    head = skipCPPOrAlignAttribute(head);
                head->next()->isAttributeMaybeUnused(true);
            } else if (Token::Match(tok->previous(), ") [ [ expects|ensures|assert default|audit|axiom| : %name% <|<=|>|>= %num% ] ]")) {
                const Token *vartok = tok->tokAt(4);
                if (vartok->str() == ":")
                    vartok = vartok->next();
                Token *argtok = tok->tokAt(-2);
                while (argtok && argtok->str() != "(") {
                    if (argtok->str() == vartok->str())
                        break;
                    if (argtok->str() == ")")
                        argtok = argtok->link();
                    argtok = argtok->previous();
                }
                if (argtok && argtok->str() == vartok->str()) {
                    if (vartok->next()->str() == ">=")
                        argtok->setCppcheckAttribute(TokenImpl::CppcheckAttributes::Type::LOW, MathLib::toLongNumber(vartok->strAt(2)));
                    else if (vartok->next()->str() == ">")
                        argtok->setCppcheckAttribute(TokenImpl::CppcheckAttributes::Type::LOW, MathLib::toLongNumber(vartok->strAt(2))+1);
                    else if (vartok->next()->str() == "<=")
                        argtok->setCppcheckAttribute(TokenImpl::CppcheckAttributes::Type::HIGH, MathLib::toLongNumber(vartok->strAt(2)));
                    else if (vartok->next()->str() == "<")
                        argtok->setCppcheckAttribute(TokenImpl::CppcheckAttributes::Type::HIGH, MathLib::toLongNumber(vartok->strAt(2))-1);
                }
            }
        } else {
            if (Token::simpleMatch(tok, "alignas (")) {
                // alignment requirements could be checked here
            }
        }
        Token::eraseTokens(tok, skipCPPOrAlignAttribute(tok)->next());
        // fix iterator after removing
        if (tok->previous()) {
            tok = tok->previous();
            tok->next()->deleteThis();
        } else {
            tok->deleteThis();
            tok = list.front();
        }
    }
}

void Tokenizer::removeAlignas()
{
    if (!isCPP() || mSettings->standards.cpp < Standards::CPP11)
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "[;{}] alignas (") && Token::Match(tok->linkAt(2), ") %name%"))
            Token::eraseTokens(tok, tok->linkAt(2)->next());
    }
}

void Tokenizer::simplifySpaceshipOperator()
{
    if (isCPP() && mSettings->standards.cpp >= Standards::CPP20) {
        for (Token *tok = list.front(); tok && tok->next(); tok = tok->next()) {
            if (Token::simpleMatch(tok, "<= >")) {
                tok->str("<=>");
                tok->deleteNext();
            }
        }
    }
}

static const std::unordered_set<std::string> keywords = {
    "inline"
    , "_inline"
    , "__inline"
    , "__forceinline"
    , "register"
    , "__restrict"
    , "__restrict__"
    , "__thread"
};
// Remove "inline", "register", "restrict", "override", "static" and "constexpr"
// "restrict" keyword
//   - New to 1999 ANSI/ISO C standard
//   - Not in C++ standard yet
void Tokenizer::simplifyKeyword()
{
    // FIXME: There is a risk that "keywords" are removed by mistake. This
    // code should be fixed so it doesn't remove variables etc. Nonstandard
    // keywords should be defined with a library instead. For instance the
    // linux kernel code at least uses "_inline" as struct member name at some
    // places.

    const bool c99 = isC() && mSettings->standards.c >= Standards::C99;
    const bool cpp11 = isCPP() && mSettings->standards.cpp >= Standards::CPP11;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (keywords.find(tok->str()) != keywords.end()) {
            // Don't remove struct members
            if (!Token::simpleMatch(tok->previous(), ".")) {
                if (tok->str().find("inline") != std::string::npos && Token::Match(tok->next(), "%name%"))
                    tok->next()->isInline(true);
                tok->deleteThis(); // Simplify..
            }
        }

        if (isC() || mSettings->standards.cpp == Standards::CPP03) {
            if (tok->str() == "auto")
                tok->deleteThis();
        }

        // simplify static keyword:
        // void foo( int [ static 5 ] ); ==> void foo( int [ 5 ] );
        if (Token::Match(tok, "[ static %num%"))
            tok->deleteNext();

        if (c99) {
            while (tok->str() == "restrict")
                tok->deleteThis();

            if (mSettings->standards.c >= Standards::C11) {
                while (tok->str() == "_Atomic")
                    tok->deleteThis();
            }
        }

        else if (cpp11) {
            // final:
            // 1) struct name final { };   <- struct is final
            if (Token::Match(tok->previous(), "struct|class|union %type% final [:{]")) {
                tok->deleteNext();
            }

            // noexcept -> noexcept(true)
            // 2) void f() noexcept; -> void f() noexcept(true);
            else if (Token::Match(tok, ") noexcept :|{|;|const|override|final")) {
                // Insertion is done in inverse order
                // The brackets are linked together accordingly afterwards
                Token * tokNoExcept = tok->next();
                tokNoExcept->insertToken(")");
                Token * braceEnd = tokNoExcept->next();
                tokNoExcept->insertToken("true");
                tokNoExcept->insertToken("(");
                Token * braceStart = tokNoExcept->next();
                tok = tok->tokAt(3);
                Token::createMutualLinks(braceStart, braceEnd);
            }

            // 3) thread_local -> static
            //    on single thread thread_local has the effect of static
            else if (tok->str() == "thread_local") {
                tok->originalName(tok->str());
                tok->str("static");
            }
        }
    }
}

void Tokenizer::simplifyAssignmentInFunctionCall()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "(")
            tok = tok->link();

        // Find 'foo(var='. Exclude 'assert(var=' to allow tests to check that assert(...) does not contain side-effects
        else if (Token::Match(tok, "[;{}] %name% ( %name% =") &&
                 Token::simpleMatch(tok->linkAt(2), ") ;") &&
                 !Token::Match(tok->next(), "assert|while")) {
            const std::string& funcname(tok->next()->str());
            Token* const vartok = tok->tokAt(3);

            // Goto ',' or ')'..
            for (Token *tok2 = vartok->tokAt(2); tok2; tok2 = tok2->next()) {
                if (tok2->link() && Token::Match(tok2, "(|[|{"))
                    tok2 = tok2->link();
                else if (tok2->str() == ";")
                    break;
                else if (Token::Match(tok2, ")|,")) {
                    tok2 = tok2->previous();

                    tok2->insertToken(vartok->str());
                    tok2->next()->varId(vartok->varId());

                    tok2->insertToken("(");
                    Token::createMutualLinks(tok2->next(), tok->linkAt(2));

                    tok2->insertToken(funcname);
                    tok2->insertToken(";");

                    Token::eraseTokens(tok, vartok);
                    break;
                }
            }
        }
    }
}

void Tokenizer::simplifyAssignmentBlock()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "[;{}] %name% = ( {")) {
            const std::string &varname = tok->next()->str();

            // goto the "} )"
            int indentlevel = 0;
            Token *tok2 = tok;
            while (nullptr != (tok2 = tok2->next())) {
                if (Token::Match(tok2, "(|{"))
                    ++indentlevel;
                else if (Token::Match(tok2, ")|}")) {
                    if (indentlevel <= 2)
                        break;
                    --indentlevel;
                } else if (indentlevel == 2 && tok2->str() == varname && Token::Match(tok2->previous(), "%type%|*"))
                    // declaring variable in inner scope with same name as lhs variable
                    break;
            }
            if (indentlevel == 2 && Token::simpleMatch(tok2, "} )")) {
                tok2 = tok2->tokAt(-3);
                if (Token::Match(tok2, "[;{}] %num%|%name% ;")) {
                    tok2->insertToken("=");
                    tok2->insertToken(tok->next()->str());
                    tok2->next()->varId(tok->next()->varId());
                    tok->deleteNext(3);
                    tok2->tokAt(5)->deleteNext();
                }
            }
        }
    }
}

// Remove __asm..
void Tokenizer::simplifyAsm()
{
    std::string instruction;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "__asm|_asm|asm {") &&
            tok->next()->link()->next()) {
            instruction = tok->tokAt(2)->stringifyList(tok->next()->link());
            Token::eraseTokens(tok, tok->next()->link()->next());
        }

        else if (Token::Match(tok, "asm|__asm|__asm__ volatile|__volatile|__volatile__| (")) {
            // Goto "("
            Token *partok = tok->next();
            if (partok->str() != "(")
                partok = partok->next();
            instruction = partok->next()->stringifyList(partok->link());
            Token::eraseTokens(tok, partok->link()->next());
        }

        else if (Token::Match(tok, "_asm|__asm")) {
            Token *endasm = tok->next();
            const Token *firstSemiColon = nullptr;
            int comment = 0;
            while (Token::Match(endasm, "%num%|%name%|,|:|;") || (endasm && endasm->linenr() == comment)) {
                if (Token::Match(endasm, "_asm|__asm|__endasm"))
                    break;
                if (endasm->str() == ";") {
                    comment = endasm->linenr();
                    if (!firstSemiColon)
                        firstSemiColon = endasm;
                }
                endasm = endasm->next();
            }
            if (Token::simpleMatch(endasm, "__endasm")) {
                instruction = tok->next()->stringifyList(endasm);
                Token::eraseTokens(tok, endasm->next());
                if (!Token::simpleMatch(tok->next(), ";"))
                    tok->insertToken(";");
            } else if (firstSemiColon) {
                instruction = tok->next()->stringifyList(firstSemiColon);
                Token::eraseTokens(tok, firstSemiColon);
            } else if (!endasm) {
                instruction = tok->next()->stringifyList(endasm);
                Token::eraseTokens(tok, endasm);
                tok->insertToken(";");
            } else
                continue;
        }

        else
            continue;

        // insert "asm ( "instruction" )"
        tok->str("asm");
        if (tok->strAt(1) != ";" && tok->strAt(1) != "{")
            tok->insertToken(";");
        tok->insertToken(")");
        tok->insertToken("\"" + instruction + "\"");
        tok->insertToken("(");

        tok = tok->next();
        Token::createMutualLinks(tok, tok->tokAt(2));

        //move the new tokens in the same line as ";" if available
        tok = tok->tokAt(2);
        if (tok->next() && tok->next()->str() == ";" &&
            tok->next()->linenr() != tok->linenr()) {
            const int endposition = tok->next()->linenr();
            tok = tok->tokAt(-3);
            for (int i = 0; i < 4; ++i) {
                tok = tok->next();
                tok->linenr(endposition);
            }
        }
    }
}

void Tokenizer::simplifyAsm2()
{
    // Block declarations: ^{}
    // A C extension used to create lambda like closures.

    // Put ^{} statements in asm()
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() != "^")
            continue;

        if (Token::simpleMatch(tok, "^ {") || (Token::simpleMatch(tok->linkAt(1), ") {") && tok->strAt(-1) != "operator")) {
            Token * start = tok;
            while (start && !Token::Match(start, "[,(;{}=]")) {
                if (start->link() && Token::Match(start, ")|]|>"))
                    start = start->link();
                start = start->previous();
            }

            const Token *last = tok->next()->link();
            if (Token::simpleMatch(last, ") {"))
                last = last->linkAt(1);
            last = last->next();
            while (last && !Token::Match(last, "%cop%|,|;|{|}|)")) {
                if (Token::Match(last, "(|["))
                    last = last->link();
                last = last->next();
            }

            if (start && last) {
                std::string asmcode;
                while (start->next() != last) {
                    asmcode += start->next()->str();
                    start->deleteNext();
                }
                if (last->str() == "}")
                    start->insertToken(";");
                start->insertToken(")");
                start->insertToken("\"" + asmcode + "\"");
                start->insertToken("(");
                start->insertToken("asm");
                start->tokAt(2)->link(start->tokAt(4));
                start->tokAt(4)->link(start->tokAt(2));
                tok = start->tokAt(4);
            }
        }
    }
}

void Tokenizer::simplifyAt()
{
    std::set<std::string> var;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%name%|] @ %num%|%name%|(")) {
            const Token *end = tok->tokAt(2);
            if (end->isNumber())
                end = end->next();
            else if (end->str() == "(") {
                int par = 0;
                while ((end = end->next()) != nullptr) {
                    if (end->str() == "(")
                        par++;
                    else if (end->str() == ")") {
                        if (--par < 0)
                            break;
                    }
                }
                end = end ? end->next() : nullptr;
            } else if (var.find(end->str()) != var.end())
                end = end->next();
            else
                continue;

            if (Token::Match(end, ": %num% ;"))
                end = end->tokAt(2);

            if (end && end->str() == ";") {
                if (tok->isName())
                    var.insert(tok->str());
                tok->isAtAddress(true);
                Token::eraseTokens(tok, end);
            }
        }

        // keywords in compiler from cosmic software for STM8
        // TODO: Should use platform configuration.
        if (Token::Match(tok, "@ builtin|eeprom|far|inline|interrupt|near|noprd|nostack|nosvf|packed|stack|svlreg|tiny|vector")) {
            tok->str(tok->next()->str() + "@");
            tok->deleteNext();
        }
    }
}

// Simplify bitfields
void Tokenizer::simplifyBitfields()
{
    bool goback = false;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (goback) {
            goback = false;
            tok = tok->previous();
        }
        Token *last = nullptr;

        if (Token::simpleMatch(tok, "for ("))
            tok = tok->linkAt(1);

        if (!Token::Match(tok, ";|{|}|public:|protected:|private:"))
            continue;

        bool isEnum = false;
        if (tok->str() == "}") {
            const Token *type = tok->link()->previous();
            while (type && type->isName()) {
                if (type->str() == "enum") {
                    isEnum = true;
                    break;
                }
                type = type->previous();
            }
        }

        if (Token::Match(tok->next(), "const| %type% %name% :") &&
            !Token::Match(tok->next(), "case|public|protected|private|class|struct") &&
            !Token::simpleMatch(tok->tokAt(2), "default :")) {
            Token *tok1 = (tok->next()->str() == "const") ? tok->tokAt(3) : tok->tokAt(2);
            if (Token::Match(tok1, "%name% : %num% ;"))
                tok1->setBits(MathLib::toLongNumber(tok1->strAt(2)));
            if (tok1 && tok1->tokAt(2) &&
                (Token::Match(tok1->tokAt(2), "%bool%|%num%") ||
                 !Token::Match(tok1->tokAt(2), "public|protected|private| %type% ::|<|,|{|;"))) {
                while (tok1->next() && !Token::Match(tok1->next(), "[;,)]{}]")) {
                    if (Token::Match(tok1->next(), "[([]"))
                        Token::eraseTokens(tok1, tok1->next()->link());
                    tok1->deleteNext();
                }

                last = tok1->next();
            }
        } else if (isEnum && Token::Match(tok, "} %name%| : %num% ;")) {
            if (tok->next()->str() == ":") {
                tok->deleteNext(2);
                tok->insertToken("Anonymous");
            } else {
                tok->next()->deleteNext(2);
            }
        } else if (Token::Match(tok->next(), "const| %type% : %num%|%bool% ;") &&
                   tok->next()->str() != "default") {
            const int offset = (tok->next()->str() == "const") ? 1 : 0;
            if (!Token::Match(tok->tokAt(3 + offset), "[{};()]")) {
                tok->deleteNext(4 + offset);
                goback = true;
            }
        }

        if (last && last->str() == ",") {
            Token * tok1 = last;
            tok1->str(";");

            const Token *const tok2 = tok->next();
            tok1->insertToken(tok2->str());
            tok1 = tok1->next();
            tok1->isSigned(tok2->isSigned());
            tok1->isUnsigned(tok2->isUnsigned());
            tok1->isLong(tok2->isLong());
        }
    }
}


// Types and objects in std namespace that are neither functions nor templates
static const std::set<std::string> stdTypes = {
    "string", "wstring", "u16string", "u32string",
    "iostream", "ostream", "ofstream", "ostringstream",
    "istream", "ifstream", "istringstream", "fstream", "stringstream",
    "wstringstream", "wistringstream", "wostringstream", "wstringbuf",
    "stringbuf", "streambuf", "ios", "filebuf", "ios_base",
    "exception", "bad_exception", "bad_alloc",
    "logic_error", "domain_error", "invalid_argument_", "length_error",
    "out_of_range", "runtime_error", "range_error", "overflow_error", "underflow_error",
    "locale",
    "cout", "cerr", "clog", "cin",
    "wcerr", "wcin", "wclog", "wcout",
    "endl", "ends", "flush",
    "boolalpha", "noboolalpha", "showbase", "noshowbase",
    "showpoint", "noshowpoint", "showpos", "noshowpos",
    "skipws", "noskipws", "unitbuf", "nounitbuf", "uppercase", "nouppercase",
    "dec", "hex", "oct",
    "fixed", "scientific",
    "internal", "left", "right",
    "fpos", "streamoff", "streampos", "streamsize"
};

static const std::set<std::string> stdTemplates = {
    "array", "basic_string", "bitset", "deque", "list", "map", "multimap",
    "priority_queue", "queue", "set", "multiset", "stack", "vector", "pair",
    "iterator", "iterator_traits",
    "unordered_map", "unordered_multimap", "unordered_set", "unordered_multiset",
    "tuple", "function"
};
static const std::set<std::string> stdFunctions = {
    "getline",
    "for_each", "find", "find_if", "find_end", "find_first_of",
    "adjacent_find", "count", "count_if", "mismatch", "equal", "search", "search_n",
    "copy", "copy_backward", "swap", "swap_ranges", "iter_swap", "transform", "replace",
    "replace_if", "replace_copy", "replace_copy_if", "fill", "fill_n", "generate", "generate_n", "remove",
    "remove_if", "remove_copy", "remove_copy_if",
    "unique", "unique_copy", "reverse", "reverse_copy",
    "rotate", "rotate_copy", "random_shuffle", "partition", "stable_partition",
    "sort", "stable_sort", "partial_sort", "partial_sort_copy", "nth_element",
    "lower_bound", "upper_bound", "equal_range", "binary_search", "merge", "inplace_merge", "includes",
    "set_union", "set_intersection", "set_difference",
    "set_symmetric_difference", "push_heap", "pop_heap", "make_heap", "sort_heap",
    "min", "max", "min_element", "max_element", "lexicographical_compare", "next_permutation", "prev_permutation",
    "advance", "back_inserter", "distance", "front_inserter", "inserter",
    "make_pair", "make_shared", "make_tuple"
};


// Add std:: in front of std classes, when using namespace std; was given
void Tokenizer::simplifyNamespaceStd()
{
    if (!isCPP())
        return;

    const bool isCPP11  = mSettings->standards.cpp == Standards::CPP11;

    std::set<std::string> userFunctions;

    for (const Token* tok = Token::findsimplematch(list.front(), "using namespace std ;"); tok; tok = tok->next()) {
        bool insert = false;
        if (Token::Match(tok, "enum class|struct| %name%| :|{")) { // Don't replace within enum definitions
            skipEnumBody(&tok);
        }
        if (!Token::Match(tok->previous(), ".|::")) {
            if (Token::Match(tok, "%name% (")) {
                if (isFunctionHead(tok->next(), "{"))
                    userFunctions.insert(tok->str());
                else if (isFunctionHead(tok->next(), ";")) {
                    const Token *start = tok;
                    while (Token::Match(start->previous(), "%type%|*|&"))
                        start = start->previous();
                    if (start != tok && start->isName() && (!start->previous() || Token::Match(start->previous(), "[;{}]")))
                        userFunctions.insert(tok->str());
                }
                if (userFunctions.find(tok->str()) == userFunctions.end() && stdFunctions.find(tok->str()) != stdFunctions.end())
                    insert = true;
            } else if (Token::Match(tok, "%name% <") && stdTemplates.find(tok->str()) != stdTemplates.end())
                insert = true;
            else if (tok->isName() && !tok->varId() && !Token::Match(tok->next(), "(|<") && stdTypes.find(tok->str()) != stdTypes.end())
                insert = true;
        }

        if (insert) {
            tok->previous()->insertToken("std");
            tok->previous()->linenr(tok->linenr()); // For stylistic reasons we put the std:: in the same line as the following token
            tok->previous()->fileIndex(tok->fileIndex());
            tok->previous()->insertToken("::");
        } else if (isCPP11 && Token::Match(tok, "!!:: tr1 ::"))
            tok->next()->str("std");
    }

    for (Token* tok = list.front(); tok; tok = tok->next()) {
        if (isCPP11 && Token::simpleMatch(tok, "std :: tr1 ::"))
            Token::eraseTokens(tok, tok->tokAt(3));

        else if (Token::simpleMatch(tok, "using namespace std ;")) {
            Token::eraseTokens(tok, tok->tokAt(4));
            tok->deleteThis();
        }
    }
}


void Tokenizer::simplifyMicrosoftMemoryFunctions()
{
    // skip if not Windows
    if (!mSettings->isWindowsPlatform())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->strAt(1) != "(")
            continue;

        if (Token::Match(tok, "CopyMemory|RtlCopyMemory|RtlCopyBytes")) {
            tok->str("memcpy");
        } else if (Token::Match(tok, "MoveMemory|RtlMoveMemory")) {
            tok->str("memmove");
        } else if (Token::Match(tok, "FillMemory|RtlFillMemory|RtlFillBytes")) {
            // FillMemory(dst, len, val) -> memset(dst, val, len)
            tok->str("memset");

            Token *tok1 = tok->tokAt(2);
            if (tok1)
                tok1 = tok1->nextArgument(); // Second argument
            if (tok1) {
                Token *tok2 = tok1->nextArgument(); // Third argument

                if (tok2)
                    Token::move(tok1->previous(), tok2->tokAt(-2), tok->next()->link()->previous()); // Swap third with second argument
            }
        } else if (Token::Match(tok, "ZeroMemory|RtlZeroMemory|RtlZeroBytes|RtlSecureZeroMemory")) {
            // ZeroMemory(dst, len) -> memset(dst, 0, len)
            tok->str("memset");

            Token *tok1 = tok->tokAt(2);
            if (tok1)
                tok1 = tok1->nextArgument(); // Second argument

            if (tok1) {
                tok1 = tok1->previous();
                tok1->insertToken("0");
                tok1 = tok1->next();
                tok1->insertToken(",");
            }
        } else if (Token::simpleMatch(tok, "RtlCompareMemory")) {
            // RtlCompareMemory(src1, src2, len) -> memcmp(src1, src2, len)
            tok->str("memcmp");
            // For the record, when memcmp returns 0, both strings are equal.
            // When RtlCompareMemory returns len, both strings are equal.
            // It might be needed to improve this replacement by something
            // like ((len - memcmp(src1, src2, len)) % (len + 1)) to
            // respect execution path (if required)
        }
    }
}

namespace {
    struct triplet {
        triplet(const char* m, const char* u) :  mbcs(m), unicode(u) {}
        std::string mbcs, unicode;
    };

    const std::map<std::string, triplet> apis = {
        std::make_pair("_topen", triplet("open", "_wopen")),
        std::make_pair("_tsopen_s", triplet("_sopen_s", "_wsopen_s")),
        std::make_pair("_tfopen", triplet("fopen", "_wfopen")),
        std::make_pair("_tfopen_s", triplet("fopen_s", "_wfopen_s")),
        std::make_pair("_tfreopen", triplet("freopen", "_wfreopen")),
        std::make_pair("_tfreopen_s", triplet("freopen_s", "_wfreopen_s")),
        std::make_pair("_tcscat", triplet("strcat", "wcscat")),
        std::make_pair("_tcschr", triplet("strchr", "wcschr")),
        std::make_pair("_tcscmp", triplet("strcmp", "wcscmp")),
        std::make_pair("_tcsdup", triplet("strdup", "wcsdup")),
        std::make_pair("_tcscpy", triplet("strcpy", "wcscpy")),
        std::make_pair("_tcslen", triplet("strlen", "wcslen")),
        std::make_pair("_tcsncat", triplet("strncat", "wcsncat")),
        std::make_pair("_tcsncpy", triplet("strncpy", "wcsncpy")),
        std::make_pair("_tcsnlen", triplet("strnlen", "wcsnlen")),
        std::make_pair("_tcsrchr", triplet("strrchr", "wcsrchr")),
        std::make_pair("_tcsstr", triplet("strstr", "wcsstr")),
        std::make_pair("_tcstok", triplet("strtok", "wcstok")),
        std::make_pair("_ftprintf", triplet("fprintf", "fwprintf")),
        std::make_pair("_tprintf", triplet("printf", "wprintf")),
        std::make_pair("_stprintf", triplet("sprintf", "swprintf")),
        std::make_pair("_sntprintf", triplet("_snprintf", "_snwprintf")),
        std::make_pair("_ftscanf", triplet("fscanf", "fwscanf")),
        std::make_pair("_tscanf", triplet("scanf", "wscanf")),
        std::make_pair("_stscanf", triplet("sscanf", "swscanf")),
        std::make_pair("_ftprintf_s", triplet("fprintf_s", "fwprintf_s")),
        std::make_pair("_tprintf_s", triplet("printf_s", "wprintf_s")),
        std::make_pair("_stprintf_s", triplet("sprintf_s", "swprintf_s")),
        std::make_pair("_sntprintf_s", triplet("_snprintf_s", "_snwprintf_s")),
        std::make_pair("_ftscanf_s", triplet("fscanf_s", "fwscanf_s")),
        std::make_pair("_tscanf_s", triplet("scanf_s", "wscanf_s")),
        std::make_pair("_stscanf_s", triplet("sscanf_s", "swscanf_s"))
    };
}

void Tokenizer::simplifyMicrosoftStringFunctions()
{
    // skip if not Windows
    if (!mSettings->isWindowsPlatform())
        return;

    const bool ansi = mSettings->platformType == Settings::Win32A;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->strAt(1) != "(")
            continue;

        const std::map<std::string, triplet>::const_iterator match = apis.find(tok->str());
        if (match!=apis.end()) {
            tok->str(ansi ? match->second.mbcs : match->second.unicode);
            tok->originalName(match->first);
        } else if (Token::Match(tok, "_T|_TEXT|TEXT ( %char%|%str% )")) {
            tok->deleteNext();
            tok->deleteThis();
            tok->deleteNext();
            if (!ansi) {
                tok->isLong(true);
                if (tok->str()[0] != 'L')
                    tok->str("L" + tok->str());
            }
            while (Token::Match(tok->next(), "_T|_TEXT|TEXT ( %char%|%str% )")) {
                tok->next()->deleteNext();
                tok->next()->deleteThis();
                tok->next()->deleteNext();
                tok->concatStr(tok->next()->str());
                tok->deleteNext();
            }
        }
    }
}

// Remove Borland code
void Tokenizer::simplifyBorland()
{
    // skip if not Windows
    if (!mSettings->isWindowsPlatform())
        return;
    if (isC())
        return;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "( __closure * %name% )")) {
            tok->deleteNext();
        }
    }

    // I think that these classes are always declared at the outer scope
    // I save some time by ignoring inner classes.
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "{" && !Token::Match(tok->tokAt(-2), "namespace %type%")) {
            tok = tok->link();
            if (!tok)
                break;
        } else if (Token::Match(tok, "class %name% :|{")) {
            while (tok && tok->str() != "{" && tok->str() != ";")
                tok = tok->next();
            if (!tok)
                break;
            if (tok->str() == ";")
                continue;

            const Token* end = tok->link()->next();
            for (Token *tok2 = tok->next(); tok2 != end; tok2 = tok2->next()) {
                if (tok2->str() == "__property" &&
                    Token::Match(tok2->previous(), ";|{|}|protected:|public:|__published:")) {
                    while (tok2->next() && !Token::Match(tok2->next(), "{|;"))
                        tok2->deleteNext();
                    tok2->deleteThis();
                    if (tok2->str() == "{") {
                        Token::eraseTokens(tok2, tok2->link());
                        tok2->deleteNext();
                        tok2->deleteThis();

                        // insert "; __property ;"
                        tok2->previous()->insertToken(";");
                        tok2->previous()->insertToken("__property");
                        tok2->previous()->insertToken(";");
                    }
                }
            }
        }
    }
}

// Remove Qt signals and slots
void Tokenizer::simplifyQtSignalsSlots()
{
    if (isC())
        return;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        // check for emit which can be outside of class
        if (Token::Match(tok, "emit|Q_EMIT %name% (") &&
            Token::simpleMatch(tok->linkAt(2), ") ;")) {
            tok->deleteThis();
        } else if (!Token::Match(tok, "class %name% :|::|{"))
            continue;

        if (tok->previous() && tok->previous()->str() == "enum") {
            tok = tok->tokAt(2);
            continue;
        }

        // count { and } for tok2
        int indentlevel = 0;
        for (Token *tok2 = tok; tok2; tok2 = tok2->next()) {
            if (tok2->str() == "{") {
                ++indentlevel;
                if (indentlevel == 1)
                    tok = tok2;
                else
                    tok2 = tok2->link();
            } else if (tok2->str() == "}") {
                if (indentlevel<2)
                    break;
                else
                    --indentlevel;
            } else if (tok2->str() == ";" && indentlevel == 0)
                break;

            if (tok2->strAt(1) == "Q_OBJECT")
                tok2->deleteNext();

            if (Token::Match(tok2->next(), "public|protected|private slots|Q_SLOTS :")) {
                tok2 = tok2->next();
                tok2->str(tok2->str() + ":");
                tok2->deleteNext(2);
                tok2 = tok2->previous();
            } else if (Token::Match(tok2->next(), "signals|Q_SIGNALS :")) {
                tok2 = tok2->next();
                tok2->str("protected:");
                tok2->deleteNext();
            } else if (Token::Match(tok2->next(), "emit|Q_EMIT %name% (") &&
                       Token::simpleMatch(tok2->linkAt(3), ") ;")) {
                tok2->deleteNext();
            }
        }
    }
}

void Tokenizer::createSymbolDatabase()
{
    if (!mSymbolDatabase)
        mSymbolDatabase = new SymbolDatabase(this, mSettings, mErrorLogger);
    mSymbolDatabase->validate();
}

void Tokenizer::deleteSymbolDatabase()
{
    delete mSymbolDatabase;
    mSymbolDatabase = nullptr;
}

bool Tokenizer::operatorEnd(const Token * tok) const
{
    if (tok && tok->str() == ")") {
        if (isFunctionHead(tok, "{|;|?|:|["))
            return true;

        tok = tok->next();
        while (tok && !Token::Match(tok, "[=;{),]")) {
            if (Token::Match(tok, "const|volatile|override")) {
                tok = tok->next();
            } else if (tok->str() == "noexcept") {
                tok = tok->next();
                if (tok && tok->str() == "(") {
                    tok = tok->link()->next();
                }
            } else if (tok->str() == "throw" && tok->next() && tok->next()->str() == "(") {
                tok = tok->next()->link()->next();
            }
            // unknown macros ") MACRO {" and ") MACRO(...) {"
            else if (tok->isUpperCaseName()) {
                tok = tok->next();
                if (tok && tok->str() == "(") {
                    tok = tok->link()->next();
                }
            } else if (Token::Match(tok, "%op% !!(") ||
                       (Token::Match(tok, "%op% (") && !isFunctionHead(tok->next(), "{")))
                break;
            else
                return false;
        }

        return true;
    }

    return false;
}

void Tokenizer::simplifyOperatorName()
{
    if (isC())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "using|:: operator %op%|%name% ;")) {
            tok->next()->str("operator" + tok->strAt(2));
            tok->next()->deleteNext();
            continue;
        }

        if (tok->str() != "operator")
            continue;
        // operator op
        if (Token::Match(tok, "operator %op% (") && !operatorEnd(tok->linkAt(2))) {
            tok->str(tok->str() + tok->next()->str());
            tok->deleteNext();
            continue;
        }
        std::string op;
        Token *par = tok->next();
        bool done = false;
        while (!done && par) {
            done = true;
            if (par->isName()) {
                op += par->str();
                par = par->next();
                // merge namespaces eg. 'operator std :: string () const {'
                if (Token::Match(par, ":: %name%|%op%|.")) {
                    op += par->str();
                    par = par->next();
                }
                done = false;
            } else if (Token::Match(par, ".|%op%|,")) {
                // check for operator in template
                if (par->str() == "," && !op.empty())
                    break;
                if (!(Token::Match(par, "<|>") && !op.empty())) {
                    op += par->str();
                    par = par->next();
                    done = false;
                }
            } else if (Token::simpleMatch(par, "[ ]")) {
                op += "[]";
                par = par->tokAt(2);
                done = false;
            } else if (Token::Match(par, "( *| )")) {
                // break out and simplify..
                if (operatorEnd(par->next()))
                    break;

                while (par->str() != ")") {
                    op += par->str();
                    par = par->next();
                }
                op += ")";
                par = par->next();
                if (Token::simpleMatch(par, "...")) {
                    op.clear();
                    par = nullptr;
                    break;
                }
                done = false;
            } else if (Token::Match(par, "\"\" %name% (|;|<")) {
                op += "\"\"";
                op += par->strAt(1);
                par = par->tokAt(2);
                done = true;
            } else if (par->str() == "::") {
                op += par->str();
                par = par->next();
                done = false;
            } else if (par->str() == ";" || par->str() == ")") {
                done = true;
            } else if (par->str() != "(") {
                syntaxError(par, "operator");
            }
        }

        if (par && !op.empty()) {
            tok->str("operator" + op);
            Token::eraseTokens(tok, par);
        }

        if (!op.empty())
            tok->isOperatorKeyword(true);
    }

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "%op% %str% %name%")) {
            std::string name = tok->strAt(2);
            Token * const str = tok->next();
            str->deleteNext();
            tok->insertToken("operator\"\"" + name);
            tok = tok->next();
            tok->isOperatorKeyword(true);
            tok->insertToken("(");
            str->insertToken(")");
            Token::createMutualLinks(tok->next(), str->next());
            str->insertToken(MathLib::toString(Token::getStrLength(str)));
            str->insertToken(",");
        }
    }

    if (mSettings->debugwarnings) {
        const Token *tok = list.front();

        while ((tok = Token::findsimplematch(tok, "operator")) != nullptr) {
            reportError(tok, Severity::debug, "debug",
                        "simplifyOperatorName: found unsimplified operator name");
            tok = tok->next();
        }
    }
}

void Tokenizer::simplifyOverloadedOperators()
{
    if (isC())
        return;
    std::set<std::string> classNames;
    std::set<nonneg int> classVars;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName())
            continue;

        if (Token::simpleMatch(tok, "this ) (") && Token::simpleMatch(tok->tokAt(-2), "( *")) {
            tok = tok->next();
            tok->insertToken("operator()");
            tok->insertToken(".");
            continue;
        }

        // Get classes that have operator() member
        if (Token::Match(tok, "class|struct %name% [:{]")) {
            int indent = 0;
            for (const Token *tok2 = tok->next(); tok2; tok2 = tok2->next()) {
                if (tok2->str() == "}")
                    break;
                else if (indent == 0 && tok2->str() == ";")
                    break;
                else if (tok2->str() == "{") {
                    if (indent == 0)
                        ++indent;
                    else
                        tok2 = tok2->link();
                } else if (indent == 1 && Token::simpleMatch(tok2, "operator() (") && isFunctionHead(tok2->next(), ";{")) {
                    classNames.insert(tok->strAt(1));
                    break;
                }
            }
        }

        // Get variables that have operator() member
        if (Token::Match(tok, "%type% &| %var%") && classNames.find(tok->str()) != classNames.end()) {
            tok = tok->next();
            while (!tok->isName())
                tok = tok->next();
            classVars.insert(tok->varId());
        }

        // Simplify operator() calls
        if (Token::Match(tok, "%var% (") && classVars.find(tok->varId()) != classVars.end()) {
            // constructor init list..
            if (Token::Match(tok->previous(), "[:,]")) {
                const Token *start = tok->previous();
                while (Token::simpleMatch(start, ",")) {
                    if (Token::simpleMatch(start->previous(), ")"))
                        start = start->linkAt(-1);
                    else
                        break;
                    if (Token::Match(start->previous(), "%name%"))
                        start = start->tokAt(-2);
                    else
                        break;
                }
                const Token *after = tok->linkAt(1);
                while (Token::Match(after, ")|} , %name% (|{"))
                    after = after->linkAt(3);

                // Do not simplify initlist
                if (Token::simpleMatch(start, ":") && Token::simpleMatch(after, ") {"))
                    continue;
            }

            tok->insertToken("operator()");
            tok->insertToken(".");
        }
    }
}

// remove unnecessary member qualification..
void Tokenizer::removeUnnecessaryQualification()
{
    if (isC())
        return;

    std::vector<Space> classInfo;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "class|struct|namespace %type% :|{") &&
            (!tok->previous() || tok->previous()->str() != "enum")) {
            Space info;
            info.isNamespace = tok->str() == "namespace";
            tok = tok->next();
            info.className = tok->str();
            tok = tok->next();
            while (tok && tok->str() != "{")
                tok = tok->next();
            if (!tok)
                return;
            info.bodyEnd = tok->link();
            classInfo.push_back(info);
        } else if (!classInfo.empty()) {
            if (tok == classInfo.back().bodyEnd)
                classInfo.pop_back();
            else if (tok->str() == classInfo.back().className &&
                     !classInfo.back().isNamespace && tok->previous()->str() != ":" &&
                     (Token::Match(tok, "%type% :: ~| %type% (") ||
                      Token::Match(tok, "%type% :: operator"))) {
                const Token *tok1 = tok->tokAt(3);
                if (tok->strAt(2) == "operator") {
                    // check for operator ()
                    if (tok1->str() == "(")
                        tok1 = tok1->next();

                    while (tok1 && tok1->str() != "(") {
                        if (tok1->str() == ";")
                            break;
                        tok1 = tok1->next();
                    }
                    if (!tok1 || tok1->str() != "(")
                        continue;
                } else if (tok->strAt(2) == "~")
                    tok1 = tok1->next();

                if (!tok1 || !Token::Match(tok1->link(), ") const| {|;|:")) {
                    continue;
                }

                const bool isConstructorOrDestructor =
                    Token::Match(tok, "%type% :: ~| %type%") && (tok->strAt(2) == tok->str() || (tok->strAt(2) == "~" && tok->strAt(3) == tok->str()));
                if (!isConstructorOrDestructor) {
                    bool isPrependedByType = Token::Match(tok->previous(), "%type%");
                    if (!isPrependedByType) {
                        const Token* tok2 = tok->tokAt(-2);
                        isPrependedByType = Token::Match(tok2, "%type% *|&");
                    }
                    if (!isPrependedByType) {
                        const Token* tok3 = tok->tokAt(-3);
                        isPrependedByType = Token::Match(tok3, "%type% * *|&");
                    }
                    if (!isPrependedByType) {
                        // It's not a constructor declaration and it's not a function declaration so
                        // this is a function call which can have all the qualifiers just fine - skip.
                        continue;
                    }
                }
            }
        }
    }
}

void Tokenizer::simplifyReturnStrncat()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::simpleMatch(tok, "return strncat (") &&
            Token::simpleMatch(tok->linkAt(2), ") ;") &&
            tok->strAt(3) != ")" && tok->strAt(3) != ",") {

            //first argument
            Token *tok2 = tok->tokAt(3);

            //check if there are at least three arguments
            for (int i = 0; i < 2; ++i) {
                tok2 = tok2->nextArgument();
                if (!tok2) {
                    tok = tok->linkAt(2)->next();
                    break;
                }
            }
            if (!tok2)
                continue;

            tok2 = tok2->nextArgument();
            //we want only three arguments
            if (tok2) {
                tok = tok->linkAt(2)->next();
                continue;
            }

            // Remove 'return'
            tok->deleteThis();

            // Add 'return arg1 ;' after 'strncat(arg1, arg2, arg3);'
            tok = tok->next();

            tok2 = tok->link()->next();
            tok2->insertToken(";");

            //the last token of the first argument before ','
            const Token * const end = tok->next()->nextArgument()->tokAt(-2);

            //all the first argument is copied
            TokenList::copyTokens(tok2, tok->next(), end);
            tok2->insertToken("return");
        }
    }
}

void Tokenizer::printUnknownTypes() const
{
    if (!mSymbolDatabase)
        return;

    std::multimap<std::string, const Token *> unknowns;

    for (int i = 1; i <= mVarId; ++i) {
        const Variable *var = mSymbolDatabase->getVariableFromVarId(i);
        if (!var)
            continue;
        // is unknown type?
        if (var->type() || var->typeStartToken()->isStandardType())
            continue;

        std::string name;
        const Token * nameTok;

        // single token type?
        if (var->typeStartToken() == var->typeEndToken()) {
            nameTok = var->typeStartToken();
            name = nameTok->str();
        }

        // complicated type
        else {
            const Token *tok = var->typeStartToken();
            int level = 0;

            nameTok =  tok;

            while (tok) {
                // skip pointer and reference part of type
                if (level == 0 && Token::Match(tok, "*|&"))
                    break;

                name += tok->str();

                if (Token::Match(tok, "struct|union|enum"))
                    name += " ";

                // pointers and references are OK in template
                else if (tok->str() == "<")
                    ++level;
                else if (tok->str() == ">")
                    --level;

                if (tok == var->typeEndToken())
                    break;

                tok = tok->next();
            }
        }

        unknowns.insert(std::pair<std::string, const Token *>(name, nameTok));
    }

    if (!unknowns.empty()) {
        std::string last;
        int count = 0;

        for (std::multimap<std::string, const Token *>::const_iterator it = unknowns.begin(); it != unknowns.end(); ++it) {
            // skip types is std namespace because they are not interesting
            if (it->first.find("std::") != 0) {
                if (it->first != last) {
                    last = it->first;
                    count = 1;
                    reportError(it->second, Severity::debug, "debug", "Unknown type \'" + it->first + "\'.");
                } else {
                    if (count < 3) // limit same type to 3
                        reportError(it->second, Severity::debug, "debug", "Unknown type \'" + it->first + "\'.");
                    count++;
                }
            }
        }
    }
}

void Tokenizer::simplifyMathExpressions()
{
    for (Token *tok = list.front(); tok; tok = tok->next()) {

        //simplify Pythagorean trigonometric identity: pow(sin(x),2)+pow(cos(x),2) = 1
        //                                             pow(cos(x),2)+pow(sin(x),2) = 1
        // @todo: sin(x) * sin(x) + cos(x) * cos(x) = 1
        //        cos(x) * cos(x) + sin(x) * sin(x) = 1
        //simplify Hyperbolic identity: pow(sinh(x),2)-pow(cosh(x),2) = -1
        //                              pow(cosh(x),2)-pow(sinh(x),2) = -1
        // @todo: sinh(x) * sinh(x) - cosh(x) * cosh(x) = -1
        //        cosh(x) * cosh(x) - sinh(x) * sinh(x) = -1
        if (Token::Match(tok, "pow|powf|powl (")) {
            if (Token::Match(tok->tokAt(2), "sin|sinf|sinl (")) {
                Token * const tok2 = tok->linkAt(3);
                if (!Token::Match(tok2, ") , %num% ) + pow|powf|powl ( cos|cosf|cosl ("))
                    continue;
                const std::string& leftExponent = tok2->strAt(2);
                if (!isTwoNumber(leftExponent))
                    continue; // left exponent is not 2
                const Token * const tok3 = tok2->tokAt(8);
                Token * const tok4 = tok3->link();
                if (!Token::Match(tok4, ") , %num% )"))
                    continue;
                const std::string& rightExponent = tok4->strAt(2);
                if (!isTwoNumber(rightExponent))
                    continue; // right exponent is not 2
                if (tok->tokAt(3)->stringifyList(tok2->next()) == tok3->stringifyList(tok4->next())) {
                    Token::eraseTokens(tok, tok4->tokAt(4));
                    tok->str("1");
                }
            } else if (Token::Match(tok->tokAt(2), "cos|cosf|cosl (")) {
                Token * const tok2 = tok->linkAt(3);
                if (!Token::Match(tok2, ") , %num% ) + pow|powf|powl ( sin|sinf|sinl ("))
                    continue;
                const std::string& leftExponent = tok2->strAt(2);
                if (!isTwoNumber(leftExponent))
                    continue; // left exponent is not 2
                const Token * const tok3 = tok2->tokAt(8);
                Token * const tok4 = tok3->link();
                if (!Token::Match(tok4, ") , %num% )"))
                    continue;
                const std::string& rightExponent = tok4->strAt(2);
                if (!isTwoNumber(rightExponent))
                    continue; // right exponent is not 2
                if (tok->tokAt(3)->stringifyList(tok2->next()) == tok3->stringifyList(tok4->next())) {
                    Token::eraseTokens(tok, tok4->tokAt(4));
                    tok->str("1");
                }
            } else if (Token::Match(tok->tokAt(2), "sinh|sinhf|sinhl (")) {
                Token * const tok2 = tok->linkAt(3);
                if (!Token::Match(tok2, ") , %num% ) - pow|powf|powl ( cosh|coshf|coshl ("))
                    continue;
                const std::string& leftExponent = tok2->strAt(2);
                if (!isTwoNumber(leftExponent))
                    continue; // left exponent is not 2
                const Token * const tok3 = tok2->tokAt(8);
                Token * const tok4 = tok3->link();
                if (!Token::Match(tok4, ") , %num% )"))
                    continue;
                const std::string& rightExponent = tok4->strAt(2);
                if (!isTwoNumber(rightExponent))
                    continue; // right exponent is not 2
                if (tok->tokAt(3)->stringifyList(tok2->next()) == tok3->stringifyList(tok4->next())) {
                    Token::eraseTokens(tok, tok4->tokAt(4));
                    tok->str("-1");
                }
            } else if (Token::Match(tok->tokAt(2), "cosh|coshf|coshl (")) {
                Token * const tok2 = tok->linkAt(3);
                if (!Token::Match(tok2, ") , %num% ) - pow|powf|powl ( sinh|sinhf|sinhl ("))
                    continue;
                const std::string& leftExponent = tok2->strAt(2);
                if (!isTwoNumber(leftExponent))
                    continue; // left exponent is not 2
                const Token * const tok3 = tok2->tokAt(8);
                Token * const tok4 = tok3->link();
                if (!Token::Match(tok4, ") , %num% )"))
                    continue;
                const std::string& rightExponent = tok4->strAt(2);
                if (!isTwoNumber(rightExponent))
                    continue; // right exponent is not 2
                if (tok->tokAt(3)->stringifyList(tok2->next()) == tok3->stringifyList(tok4->next())) {
                    Token::eraseTokens(tok, tok4->tokAt(4));
                    tok->str("-1");
                }
            }
        }
    }
}

bool Tokenizer::simplifyStrlen()
{
    // replace strlen(str)
    bool modified=false;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "strlen ( %str% )")) {
            tok->str(MathLib::toString(Token::getStrLength(tok->tokAt(2))));
            tok->deleteNext(3);
            modified=true;
        }
    }
    return modified;
}

void Tokenizer::prepareTernaryOpForAST()
{
    // http://en.cppreference.com/w/cpp/language/operator_precedence says about ternary operator:
    //       "The expression in the middle of the conditional operator (between ? and :) is parsed as if parenthesized: its precedence relative to ?: is ignored."
    // The AST parser relies on this function to add such parentheses where necessary.
    for (Token* tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "?") {
            bool parenthesesNeeded = false;
            int depth = 0;
            Token* tok2 = tok->next();
            for (; tok2; tok2 = tok2->next()) {
                if (tok2->link() && Token::Match(tok2, "[|(|<"))
                    tok2 = tok2->link();
                else if (tok2->str() == ":") {
                    if (depth == 0)
                        break;
                    depth--;
                } else if (tok2->str() == ";" || (tok2->link() && tok2->str() != "{" && tok2->str() != "}"))
                    break;
                else if (tok2->str() == ",")
                    parenthesesNeeded = true;
                else if (tok2->str() == "<")
                    parenthesesNeeded = true;
                else if (tok2->str() == "?") {
                    depth++;
                    parenthesesNeeded = true;
                }
            }
            if (parenthesesNeeded && tok2 && tok2->str() == ":") {
                tok->insertToken("(");
                tok2->insertToken(")", emptyString, true);
                Token::createMutualLinks(tok->next(), tok2->previous());
            }
        }
    }
}

void Tokenizer::reportError(const Token* tok, const Severity::SeverityType severity, const std::string& id, const std::string& msg, bool inconclusive) const
{
    const std::list<const Token*> callstack(1, tok);
    reportError(callstack, severity, id, msg, inconclusive);
}

void Tokenizer::reportError(const std::list<const Token*>& callstack, Severity::SeverityType severity, const std::string& id, const std::string& msg, bool inconclusive) const
{
    const ErrorMessage errmsg(callstack, &list, severity, id, msg, inconclusive ? Certainty::inconclusive : Certainty::normal);
    if (mErrorLogger)
        mErrorLogger->reportErr(errmsg);
    else
        Check::reportError(errmsg);
}

void Tokenizer::setPodTypes()
{
    if (!mSettings)
        return;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName())
            continue;

        // pod type
        const struct Library::PodType *podType = mSettings->library.podtype(tok->str());
        if (podType) {
            const Token *prev = tok->previous();
            while (prev && prev->isName())
                prev = prev->previous();
            if (prev && !Token::Match(prev, ";|{|}|,|("))
                continue;
            tok->isStandardType(true);
        }
    }
}

const Token *Tokenizer::findSQLBlockEnd(const Token *tokSQLStart)
{
    const Token *tokLastEnd = nullptr;
    for (const Token *tok = tokSQLStart->tokAt(2); tok != nullptr; tok = tok->next()) {
        if (tokLastEnd == nullptr && tok->str() == ";")
            tokLastEnd = tok;
        else if (tok->str() == "__CPPCHECK_EMBEDDED_SQL_EXEC__") {
            if (Token::simpleMatch(tok->tokAt(-2), "END - __CPPCHECK_EMBEDDED_SQL_EXEC__ ;"))
                return tok->next();
            return tokLastEnd;
        } else if (Token::Match(tok, "{|}|==|&&|!|^|<<|>>|++|+=|-=|/=|*=|>>=|<<=|~"))
            break; // We are obviously outside the SQL block
    }

    return tokLastEnd;
}

void Tokenizer::simplifyNestedNamespace()
{
    if (!isCPP())
        return;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (Token::Match(tok, "namespace %name% ::") && tok->strAt(-1) != "using") {
            Token * tok2 = tok->tokAt(2);

            // validate syntax
            while (Token::Match(tok2, ":: %name%"))
                tok2 = tok2->tokAt(2);

            if (!tok2 || tok2->str() != "{")
                return; // syntax error

            std::stack<Token *> links;
            tok2 = tok->tokAt(2);

            while (tok2->str() == "::") {
                links.push(tok2);
                tok2->str("{");
                tok2->insertToken("namespace");
                tok2 = tok2->tokAt(3);
            }

            tok = tok2;

            if (!links.empty() && tok2->str() == "{") {
                tok2 = tok2->link();
                while (!links.empty()) {
                    tok2->insertToken("}");
                    tok2 = tok2->next();
                    Token::createMutualLinks(links.top(), tok2);
                    links.pop();
                }
            }
        }
    }
}

void Tokenizer::simplifyCoroutines()
{
    if (!isCPP() || mSettings->standards.cpp < Standards::CPP20)
        return;
    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (!tok->isName() || !Token::Match(tok, "co_return|co_yield|co_await"))
            continue;
        Token *end = tok->next();
        while (end && end->str() != ";") {
            if (Token::Match(end, "[({[]"))
                end = end->link();
            else if (Token::Match(end, "[)]}]"))
                break;
            end = end->next();
        }
        if (Token::simpleMatch(end, ";")) {
            tok->insertToken("(");
            end->previous()->insertToken(")");
            Token::createMutualLinks(tok->next(), end->previous());
        }
    }
}

static bool sameTokens(const Token *first, const Token *last, const Token *other)
{
    while (other && first->str() == other->str()) {
        if (first == last)
            return true;
        first = first->next();
        other = other->next();
    }

    return false;
}

static bool alreadyHasNamespace(const Token *first, const Token *last, const Token *end)
{
    while (end && last->str() == end->str()) {
        if (first == last)
            return true;
        last = last->previous();
        end = end->previous();
    }

    return false;
}

static Token * deleteAlias(Token * tok)
{
    Token::eraseTokens(tok, Token::findsimplematch(tok, ";"));

    // delete first token
    tok->deleteThis();

    // delete ';' if not last token
    tok->deleteThis();

    return tok;
}

void Tokenizer::simplifyNamespaceAliases()
{
    if (!isCPP())
        return;

    int scope = 0;

    for (Token *tok = list.front(); tok; tok = tok->next()) {
        if (tok->str() == "{")
            scope++;
        else if (tok->str() == "}")
            scope--;
        else if (Token::Match(tok, "namespace %name% =")) {
            const std::string name(tok->next()->str());
            Token * tokNameStart = tok->tokAt(3);
            Token * tokNameEnd = tokNameStart;

            while (tokNameEnd && tokNameEnd->next() && tokNameEnd->next()->str() != ";")
                tokNameEnd = tokNameEnd->next();

            if (!tokNameEnd)
                return; // syntax error

            int endScope = scope;
            Token * tokLast = tokNameEnd->next();
            Token * tokNext = tokLast->next();
            Token * tok2 = tokNext;

            while (tok2 && endScope >= scope) {
                if (Token::simpleMatch(tok2, "{"))
                    endScope++;
                else if (Token::simpleMatch(tok2, "}"))
                    endScope--;
                else if (tok2->str() == name) {
                    if (Token::Match(tok2->previous(), "namespace %name% =")) {
                        // check for possible duplicate aliases
                        if (sameTokens(tokNameStart, tokNameEnd, tok2->tokAt(2))) {
                            // delete duplicate
                            tok2 = deleteAlias(tok2->previous());
                            continue;
                        } else {
                            // conflicting declaration (syntax error)
                            // cppcheck-suppress duplicateBranch - remove when TODO below is addressed
                            if (endScope == scope) {
                                // delete conflicting declaration
                                tok2 = deleteAlias(tok2->previous());
                            }

                            // new declaration
                            else {
                                // TODO: use the new alias in this scope
                                tok2 = deleteAlias(tok2->previous());
                            }
                            continue;
                        }
                    }

                    if (tok2->strAt(1) == "::" && !alreadyHasNamespace(tokNameStart, tokNameEnd, tok2)) {
                        tok2->str(tokNameStart->str());
                        Token * tok3 = tokNameStart;
                        while (tok3 != tokNameEnd) {
                            tok2->insertToken(tok3->next()->str());
                            tok2 = tok2->next();
                            tok3 = tok3->next();
                        }
                    }
                }
                tok2 = tok2->next();
            }

            if (tok->previous() && tokNext) {
                Token::eraseTokens(tok->previous(), tokNext);
                tok = tokNext->previous();
            } else if (tok->previous()) {
                Token::eraseTokens(tok->previous(), tokLast);
                tok = tokLast;
            } else if (tokNext) {
                Token::eraseTokens(tok, tokNext);
                tok->deleteThis();
            } else {
                Token::eraseTokens(tok, tokLast);
                tok->deleteThis();
            }
        }
    }
}


Tokenizer::VariableMap::VariableMap() : mVarId(0) {}

void Tokenizer::VariableMap::enterScope()
{
    mScopeInfo.push(std::list<std::pair<std::string, int>>());
}

bool Tokenizer::VariableMap::leaveScope()
{
    if (mScopeInfo.empty())
        return false;

    for (const std::pair<std::string, int> &outerVariable : mScopeInfo.top()) {
        if (outerVariable.second != 0)
            mVariableId[outerVariable.first] = outerVariable.second;
        else
            mVariableId.erase(outerVariable.first);
    }
    mScopeInfo.pop();
    return true;
}

void Tokenizer::VariableMap::addVariable(const std::string &varname)
{
    if (mScopeInfo.empty()) {
        mVariableId[varname] = ++mVarId;
        return;
    }
    std::map<std::string, int>::iterator it = mVariableId.find(varname);
    if (it == mVariableId.end()) {
        mScopeInfo.top().push_back(std::pair<std::string, int>(varname, 0));
        mVariableId[varname] = ++mVarId;
        return;
    }
    mScopeInfo.top().push_back(std::pair<std::string, int>(varname, it->second));
    it->second = ++mVarId;
}

bool Tokenizer::VariableMap::hasVariable(const std::string &varname) const
{
    return mVariableId.find(varname) != mVariableId.end();
}

bool Tokenizer::hasIfdef(const Token *start, const Token *end) const
{
    if (!mPreprocessor)
        return false;
    for (const Directive &d: mPreprocessor->getDirectives()) {
        if (d.str.compare(0,3,"#if") == 0 &&
            d.linenr >= start->linenr() &&
            d.linenr <= end->linenr() &&
            start->fileIndex() < list.getFiles().size() &&
            d.file == list.getFiles()[start->fileIndex()])
            return true;
    }
    return false;
}
