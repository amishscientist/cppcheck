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
#ifndef valueflowH
#define valueflowH
//---------------------------------------------------------------------------

#include "config.h"
#include "mathlib.h"
#include "utils.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class ErrorLogger;
struct InferModel;
class Settings;
class SymbolDatabase;
class Token;
class TokenList;
class ValueType;
class Variable;

template<class T>
class ValuePtr;

namespace ValueFlow {
    struct increment {
        template<class T>
        void operator()(T& x) const {
            x++;
        }
    };
    struct decrement {
        template<class T>
        void operator()(T& x) const {
            x--;
        }
    };

    struct less {
        template<class T, class U>
        bool operator()(const T& x, const U& y) const {
            return x < y;
        }
    };

    struct adjacent {
        template<class T, class U>
        bool operator()(const T& x, const U& y) const {
            return std::abs(x - y) == 1;
        }
    };

    struct equalVisitor {
        template<class T, class U>
        void operator()(bool& result, T x, U y) const {
            result = !(x > y || x < y);
        }
    };
    class CPPCHECKLIB Value {
    public:
        typedef std::pair<const Token *, std::string> ErrorPathItem;
        typedef std::list<ErrorPathItem> ErrorPath;
        enum class Bound { Upper, Lower, Point };

        explicit Value(long long val = 0, Bound b = Bound::Point)
            : valueType(ValueType::INT),
            bound(b),
            intvalue(val),
            tokvalue(nullptr),
            floatValue(0.0),
            moveKind(MoveKind::NonMovedVariable),
            varvalue(val),
            condition(nullptr),
            varId(0U),
            safe(false),
            conditional(false),
            macro(false),
            defaultArg(false),
            indirect(0),
            path(0),
            wideintvalue(val),
            subexpressions(),
            lifetimeKind(LifetimeKind::Object),
            lifetimeScope(LifetimeScope::Local),
            valueKind(ValueKind::Possible)
        {}
        Value(const Token* c, long long val, Bound b = Bound::Point);

        static Value unknown();

        bool equalValue(const ValueFlow::Value& rhs) const {
            if (valueType != rhs.valueType)
                return false;
            switch (valueType) {
            case ValueType::INT:
            case ValueType::CONTAINER_SIZE:
            case ValueType::BUFFER_SIZE:
            case ValueType::ITERATOR_START:
            case ValueType::ITERATOR_END:
                if (intvalue != rhs.intvalue)
                    return false;
                break;
            case ValueType::TOK:
                if (tokvalue != rhs.tokvalue)
                    return false;
                break;
            case ValueType::FLOAT:
                // TODO: Write some better comparison
                if (floatValue > rhs.floatValue || floatValue < rhs.floatValue)
                    return false;
                break;
            case ValueType::MOVED:
                if (moveKind != rhs.moveKind)
                    return false;
                break;
            case ValueType::UNINIT:
                break;
            case ValueType::LIFETIME:
                if (tokvalue != rhs.tokvalue)
                    return false;
                break;
            case ValueType::SYMBOLIC:
                if (!sameToken(tokvalue, rhs.tokvalue))
                    return false;
                if (intvalue != rhs.intvalue)
                    return false;
                break;
            }
            return true;
        }

        template<class T, class F>
        static void visitValue(T& self, F f) {
            switch (self.valueType) {
            case ValueType::INT:
            case ValueType::SYMBOLIC:
            case ValueType::BUFFER_SIZE:
            case ValueType::CONTAINER_SIZE:
            case ValueType::ITERATOR_START:
            case ValueType::ITERATOR_END: {
                f(self.intvalue);
                break;
            }
            case ValueType::FLOAT: {
                f(self.floatValue);
                break;
            }
            case ValueType::UNINIT:
            case ValueType::TOK:
            case ValueType::LIFETIME:
            case ValueType::MOVED:
                break;
            }
        }

        struct compareVisitor {
            struct innerVisitor {
                template<class Compare, class T, class U>
                void operator()(bool& result, Compare compare, T x, U y) const {
                    result = compare(x, y);
                }
            };
            template<class Compare, class T>
            void operator()(bool& result, const Value& rhs, Compare compare, T x) const {
                visitValue(rhs,
                           std::bind(innerVisitor{}, std::ref(result), std::move(compare), x, std::placeholders::_1));
            }
        };

        template<class Compare>
        bool compareValue(const Value& rhs, Compare compare) const {
            assert((!this->isSymbolicValue() && !rhs.isSymbolicValue()) ||
                   (this->valueType == rhs.valueType && sameToken(this->tokvalue, rhs.tokvalue)));
            bool result = false;
            visitValue(
                *this,
                std::bind(compareVisitor{}, std::ref(result), std::ref(rhs), std::move(compare), std::placeholders::_1));
            return result;
        }

        bool operator==(const Value &rhs) const {
            if (!equalValue(rhs))
                return false;

            return varvalue == rhs.varvalue &&
                   condition == rhs.condition &&
                   varId == rhs.varId &&
                   conditional == rhs.conditional &&
                   defaultArg == rhs.defaultArg &&
                   indirect == rhs.indirect &&
                   valueKind == rhs.valueKind;
        }

        bool operator!=(const Value &rhs) const {
            return !(*this == rhs);
        }

        template<class T, REQUIRES("T must be an arithmetic type", std::is_arithmetic<T> )>
        bool equalTo(const T& x) const {
            bool result = false;
            visitValue(*this, std::bind(equalVisitor{}, std::ref(result), x, std::placeholders::_1));
            return result;
        }

        void decreaseRange() {
            if (bound == Bound::Lower)
                visitValue(*this, increment{});
            else if (bound == Bound::Upper)
                visitValue(*this, decrement{});
        }

        void invertBound() {
            if (bound == Bound::Lower)
                bound = Bound::Upper;
            else if (bound == Bound::Upper)
                bound = Bound::Lower;
        }

        void invertRange() {
            invertBound();
            decreaseRange();
        }

        void assumeCondition(const Token* tok);

        std::string infoString() const;

        enum class ValueType {
            INT,
            TOK,
            FLOAT,
            MOVED,
            UNINIT,
            CONTAINER_SIZE,
            LIFETIME,
            BUFFER_SIZE,
            ITERATOR_START,
            ITERATOR_END,
            SYMBOLIC
        } valueType;
        bool isIntValue() const {
            return valueType == ValueType::INT;
        }
        bool isTokValue() const {
            return valueType == ValueType::TOK;
        }
        bool isFloatValue() const {
            return valueType == ValueType::FLOAT;
        }
        bool isMovedValue() const {
            return valueType == ValueType::MOVED;
        }
        bool isUninitValue() const {
            return valueType == ValueType::UNINIT;
        }
        bool isContainerSizeValue() const {
            return valueType == ValueType::CONTAINER_SIZE;
        }
        bool isLifetimeValue() const {
            return valueType == ValueType::LIFETIME;
        }
        bool isBufferSizeValue() const {
            return valueType == ValueType::BUFFER_SIZE;
        }
        bool isIteratorValue() const {
            return valueType == ValueType::ITERATOR_START || valueType == ValueType::ITERATOR_END;
        }
        bool isIteratorStartValue() const {
            return valueType == ValueType::ITERATOR_START;
        }
        bool isIteratorEndValue() const {
            return valueType == ValueType::ITERATOR_END;
        }
        bool isSymbolicValue() const {
            return valueType == ValueType::SYMBOLIC;
        }

        bool isLocalLifetimeValue() const {
            return valueType == ValueType::LIFETIME && lifetimeScope == LifetimeScope::Local;
        }

        bool isArgumentLifetimeValue() const {
            return valueType == ValueType::LIFETIME && lifetimeScope == LifetimeScope::Argument;
        }

        bool isSubFunctionLifetimeValue() const {
            return valueType == ValueType::LIFETIME && lifetimeScope == LifetimeScope::SubFunction;
        }

        bool isNonValue() const {
            return isMovedValue() || isUninitValue() || isLifetimeValue();
        }

        /** The value bound  */
        Bound bound;

        /** int value (or sometimes bool value?) */
        long long intvalue;

        /** token value - the token that has the value. this is used for pointer aliases, strings, etc. */
        const Token *tokvalue;

        /** float value */
        double floatValue;

        /** kind of moved  */
        enum class MoveKind {NonMovedVariable, MovedVariable, ForwardedVariable} moveKind;

        /** For calculated values - variable value that calculated value depends on */
        long long varvalue;

        /** Condition that this value depends on */
        const Token *condition;

        ErrorPath errorPath;

        /** For calculated values - varId that calculated value depends on */
        nonneg int varId;

        /** value relies on safe checking */
        bool safe;

        /** Conditional value */
        bool conditional;

        /** Value is is from an expanded macro */
        bool macro;

        /** Is this value passed as default parameter to the function? */
        bool defaultArg;

        int indirect;

        /** Path id */
        MathLib::bigint path;

        /** int value before implicit truncation */
        long long wideintvalue;

        std::vector<std::string> subexpressions;

        enum class LifetimeKind {
            // Pointer points to a member of lifetime
            Object,
            // A member of object points to the lifetime
            SubObject,
            // Lambda has captured lifetime(similar to SubObject)
            Lambda,
            // Iterator points to the lifetime of a container(similar to Object)
            Iterator,
            // A pointer that holds the address of the lifetime
            Address
        } lifetimeKind;

        enum class LifetimeScope { Local, Argument, SubFunction, ThisPointer, ThisValue } lifetimeScope;

        static const char* toString(MoveKind moveKind);
        static const char* toString(LifetimeKind lifetimeKind);
        static const char* toString(LifetimeScope lifetimeScope);
        static const char* toString(Bound bound);

        /** How known is this value */
        enum class ValueKind {
            /** This value is possible, other unlisted values may also be possible */
            Possible,
            /** Only listed values are possible */
            Known,
            /** Inconclusive */
            Inconclusive,
            /** Listed values are impossible */
            Impossible
        } valueKind;

        void setKnown() {
            valueKind = ValueKind::Known;
        }

        bool isKnown() const {
            return valueKind == ValueKind::Known;
        }

        void setPossible() {
            valueKind = ValueKind::Possible;
        }

        bool isPossible() const {
            return valueKind == ValueKind::Possible;
        }

        bool isImpossible() const {
            return valueKind == ValueKind::Impossible;
        }

        void setImpossible() {
            valueKind = ValueKind::Impossible;
        }

        void setInconclusive(bool inconclusive = true) {
            if (inconclusive)
                valueKind = ValueKind::Inconclusive;
        }

        bool isInconclusive() const {
            return valueKind == ValueKind::Inconclusive;
        }

        void changeKnownToPossible() {
            if (isKnown())
                valueKind = ValueKind::Possible;
        }

        bool errorSeverity() const {
            return !condition && !defaultArg;
        }

        static bool sameToken(const Token* tok1, const Token* tok2);
    };

    /// Constant folding of expression. This can be used before the full ValueFlow has been executed (ValueFlow::setValues).
    const ValueFlow::Value * valueFlowConstantFoldAST(Token *expr, const Settings *settings);

    /// Perform valueflow analysis.
    void setValues(TokenList *tokenlist, SymbolDatabase* symboldatabase, ErrorLogger *errorLogger, const Settings *settings);

    std::string eitherTheConditionIsRedundant(const Token *condition);

    size_t getSizeOf(const ValueType &vt, const Settings *settings);

    const ValueFlow::Value* findValue(const std::list<ValueFlow::Value>& values,
                                      const Settings* settings,
                                      std::function<bool(const ValueFlow::Value&)> pred);

    std::vector<ValueFlow::Value> isOutOfBounds(const Value& size, const Token* indexTok, bool possible = true);
}

bool isContainerSizeChanged(const Token* tok, const Settings* settings = nullptr, int depth = 20);

struct LifetimeToken {
    const Token* token;
    bool addressOf;
    ValueFlow::Value::ErrorPath errorPath;
    bool inconclusive;

    LifetimeToken() : token(nullptr), addressOf(false), errorPath(), inconclusive(false) {}

    LifetimeToken(const Token* token, ValueFlow::Value::ErrorPath errorPath)
        : token(token), addressOf(false), errorPath(std::move(errorPath)), inconclusive(false)
    {}

    LifetimeToken(const Token* token, bool addressOf, ValueFlow::Value::ErrorPath errorPath)
        : token(token), addressOf(addressOf), errorPath(std::move(errorPath)), inconclusive(false)
    {}

    static std::vector<LifetimeToken> setAddressOf(std::vector<LifetimeToken> v, bool b) {
        for (LifetimeToken& x : v)
            x.addressOf = b;
        return v;
    }

    static std::vector<LifetimeToken> setInconclusive(std::vector<LifetimeToken> v, bool b) {
        for (LifetimeToken& x : v)
            x.inconclusive = b;
        return v;
    }
};

const Token *parseCompareInt(const Token *tok, ValueFlow::Value &true_value, ValueFlow::Value &false_value, const std::function<std::vector<MathLib::bigint>(const Token*)>& evaluate);
const Token *parseCompareInt(const Token *tok, ValueFlow::Value &true_value, ValueFlow::Value &false_value);

ValueFlow::Value inferCondition(std::string op, MathLib::bigint val, const Token* varTok);
ValueFlow::Value inferCondition(const std::string& op, const Token* varTok, MathLib::bigint val);

CPPCHECKLIB ValuePtr<InferModel> makeIntegralInferModel();

std::vector<LifetimeToken> getLifetimeTokens(const Token* tok,
                                             bool escape = false,
                                             ValueFlow::Value::ErrorPath errorPath = ValueFlow::Value::ErrorPath{});

bool hasLifetimeToken(const Token* tok, const Token* lifetime);

const Variable* getLifetimeVariable(const Token* tok, ValueFlow::Value::ErrorPath& errorPath, bool* addressOf = nullptr);

const Variable* getLifetimeVariable(const Token* tok);

bool isLifetimeBorrowed(const Token *tok, const Settings *settings);

std::string lifetimeType(const Token *tok, const ValueFlow::Value *val);

std::string lifetimeMessage(const Token *tok, const ValueFlow::Value *val, ValueFlow::Value::ErrorPath &errorPath);

CPPCHECKLIB ValueFlow::Value getLifetimeObjValue(const Token *tok, bool inconclusive = false);

CPPCHECKLIB std::vector<ValueFlow::Value> getLifetimeObjValues(const Token* tok,
                                                               bool inconclusive = false,
                                                               MathLib::bigint path = 0);

#endif // valueflowH
