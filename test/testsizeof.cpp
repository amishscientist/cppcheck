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

#include "checksizeof.h"
#include "config.h"
#include "errortypes.h"
#include "settings.h"
#include "testsuite.h"
#include "tokenize.h"

#include <iosfwd>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <simplecpp.h>

class TestSizeof : public TestFixture {
public:
    TestSizeof() : TestFixture("TestSizeof") {}

private:
    Settings settings;

    void run() OVERRIDE {
        settings.severity.enable(Severity::warning);
        settings.severity.enable(Severity::portability);
        settings.certainty.enable(Certainty::inconclusive);

        TEST_CASE(sizeofsizeof);
        TEST_CASE(sizeofCalculation);
        TEST_CASE(sizeofFunction);
        TEST_CASE(checkPointerSizeof);
        TEST_CASE(checkPointerSizeofStruct);
        TEST_CASE(sizeofDivisionMemset);
        TEST_CASE(sizeofForArrayParameter);
        TEST_CASE(sizeofForNumericParameter);
        TEST_CASE(suspiciousSizeofCalculation);
        TEST_CASE(sizeofVoid);
        TEST_CASE(customStrncat);
    }

#define check(code) check_(code, __FILE__, __LINE__)
    void check_(const char code[], const char* file, int line) {
        // Clear the error buffer..
        errout.str("");

        // Tokenize..
        Tokenizer tokenizer(&settings, this);
        std::istringstream istr(code);
        ASSERT_LOC(tokenizer.tokenize(istr, "test.cpp"), file, line);

        // Check...
        CheckSizeof checkSizeof(&tokenizer, &settings, this);
        checkSizeof.runChecks(&tokenizer, &settings, this);
    }

    void checkP(const char code[]) {
        // Clear the error buffer..
        errout.str("");

        // Raw tokens..
        std::vector<std::string> files(1, "test.cpp");
        std::istringstream istr(code);
        const simplecpp::TokenList tokens1(istr, files, files[0]);

        // Preprocess..
        simplecpp::TokenList tokens2(files);
        std::map<std::string, simplecpp::TokenList*> filedata;
        simplecpp::preprocess(tokens2, tokens1, files, filedata, simplecpp::DUI());

        // Tokenize..
        Tokenizer tokenizer(&settings, this);
        tokenizer.createTokens(std::move(tokens2));
        tokenizer.simplifyTokens1("");

        // Check...
        CheckSizeof checkSizeof(&tokenizer, &settings, this);
        checkSizeof.runChecks(&tokenizer, &settings, this);
    }

    void sizeofsizeof() {
        check("void foo()\n"
              "{\n"
              "    int i = sizeof sizeof char;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Calling 'sizeof' on 'sizeof'.\n", errout.str());

        check("void foo()\n"
              "{\n"
              "    int i = sizeof (sizeof long);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Calling 'sizeof' on 'sizeof'.\n", errout.str());

        check("void foo(long *p)\n"
              "{\n"
              "    int i = sizeof (sizeof (p));\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Calling 'sizeof' on 'sizeof'.\n", errout.str());
    }

    void sizeofCalculation() {
        check("int a, b; int a,sizeof(a+b)");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found calculation inside sizeof().\n", errout.str());

        check("int a, b; sizeof(a*b)");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found calculation inside sizeof().\n", errout.str());

        check("int a, b; sizeof(-a)");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found calculation inside sizeof().\n", errout.str());

        check("int a, b; sizeof(*a)");
        ASSERT_EQUALS("", errout.str());

        check("sizeof(void * const)");
        ASSERT_EQUALS("", errout.str());

        check("sizeof(int*[2])");
        ASSERT_EQUALS("", errout.str());

        check("sizeof(Fred**)");
        ASSERT_EQUALS("", errout.str());

        check("sizeof(foo++)");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found calculation inside sizeof().\n", errout.str());

        check("sizeof(--foo)");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found calculation inside sizeof().\n", errout.str());

        // #6888
        checkP("#define SIZEOF1   sizeof(i != 2)\n"
               "#define SIZEOF2   ((sizeof(i != 2)))\n"
               "#define VOIDCAST1 (void)\n"
               "#define VOIDCAST2(SZ) static_cast<void>(SZ)\n"
               "int f(int i) {\n"
               "  VOIDCAST1 SIZEOF1;\n"
               "  VOIDCAST1 SIZEOF2;\n"
               "  VOIDCAST2(SIZEOF1);\n"
               "  VOIDCAST2(SIZEOF2);\n"
               "  return i + foo(1);\n"
               "}");
        ASSERT_EQUALS("", errout.str());

        checkP("#define SIZEOF1   sizeof(i != 2)\n"
               "#define SIZEOF2   ((sizeof(i != 2)))\n"
               "int f(int i) {\n"
               "  SIZEOF1;\n"
               "  SIZEOF2;\n"
               "  return i + foo(1);\n"
               "}");
        ASSERT_EQUALS("[test.cpp:4]: (warning, inconclusive) Found calculation inside sizeof().\n"
                      "[test.cpp:5]: (warning, inconclusive) Found calculation inside sizeof().\n", errout.str());

        checkP("#define MACRO(data)  f(data, sizeof(data))\n"
               "x = MACRO((unsigned int *)data + 4);");
        ASSERT_EQUALS("[test.cpp:2]: (warning, inconclusive) Found calculation inside sizeof().\n", errout.str());
    }

    void sizeofFunction() {
        check("class Foo\n"
              "{\n"
              "    int bar() { return 1; };\n"
              "}\n"
              "Foo f;int a=sizeof(f.bar());");
        ASSERT_EQUALS("[test.cpp:5]: (warning) Found function call inside sizeof().\n", errout.str());

        check("class Foo\n"
              "{\n"
              "    int bar() { return 1; };\n"
              "    int bar() const { return 1; };\n"
              "}\n"
              "Foo f;int a=sizeof(f.bar());");
        ASSERT_EQUALS("", errout.str());

        check("class Foo\n"
              "{\n"
              "    int bar() { return 1; };\n"
              "}\n"
              "Foo * fp;int a=sizeof(fp->bar());");
        ASSERT_EQUALS("[test.cpp:5]: (warning) Found function call inside sizeof().\n", errout.str());

        check("int a=sizeof(foo());");
        ASSERT_EQUALS("", errout.str());

        check("int foo() { return 1; }; int a=sizeof(foo());");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found function call inside sizeof().\n", errout.str());

        check("int foo() { return 1; }; sizeof(decltype(foo()));");
        ASSERT_EQUALS("", errout.str());

        check("int foo(int) { return 1; }; int a=sizeof(foo(0))");
        ASSERT_EQUALS("[test.cpp:1]: (warning) Found function call inside sizeof().\n", errout.str());

        check("char * buf; int a=sizeof(*buf);");
        ASSERT_EQUALS("", errout.str());

        check("int a=sizeof(foo())");
        ASSERT_EQUALS("", errout.str());

        check("int foo(int) { return 1; }; char buf[1024]; int a=sizeof(buf), foo(0)");
        ASSERT_EQUALS("", errout.str());

        check("template<class T>\n"
              "struct A\n"
              "{\n"
              "    static B f(const B &);\n"
              "    static A f(const A &);\n"
              "    static A &g();\n"
              "    static T &h();\n"
              "\n"
              "    enum {\n"
              "        X = sizeof(f(g() >> h())) == sizeof(A),\n"
              "        Y = sizeof(f(g() << h())) == sizeof(A),\n"
              "        Z = X & Y\n"
              "    };\n"
              "};");
        ASSERT_EQUALS("", errout.str());
    }

    void sizeofForArrayParameter() {
        check("void f() {\n"
              "    int a[10];\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    unsigned int a = 2;\n"
              "    unsigned int b = 2;\n"
              "    int c[(a+b)];\n"
              "    std::cout << sizeof(c) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    unsigned int a = { 2 };\n"
              "    unsigned int b[] = { 0 };\n"
              "    int c[a[b[0]]];\n"
              "    std::cout << sizeof(c) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("", errout.str());


        check("void f() {\n"
              "    unsigned int a[] = { 1 };\n"
              "    unsigned int b = 2;\n"
              "    int c[(a[0]+b)];\n"
              "    std::cout << sizeof(c) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int a[] = { 1, 2, 3 };\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int a[3] = { 1, 2, 3 };\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f( int a[]) {\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Using 'sizeof' on array given as "
                      "function argument returns size of a pointer.\n", errout.str());

        check("void f( int a[]) {\n"
              "    std::cout << sizeof a / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Using 'sizeof' on array given as "
                      "function argument returns size of a pointer.\n", errout.str());

        check("void f( int a[3] ) {\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Using 'sizeof' on array given as "
                      "function argument returns size of a pointer.\n", errout.str());

        check("typedef char Fixname[1000];\n"
              "int f2(Fixname& f2v) {\n"
              "  int i = sizeof(f2v);\n"
              "  printf(\"sizeof f2v %d\", i);\n"
              "   }");
        ASSERT_EQUALS("", errout.str());

        check("void f(int *p) {\n"
              "    p[0] = 0;\n"
              "    int unused = sizeof(p);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    char p[] = \"test\";\n"
              "    int unused = sizeof(p);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        // ticket #2495
        check("void f() {\n"
              "    static float col[][3]={\n"
              "      {1,0,0},\n"
              "      {0,0,1},\n"
              "      {0,1,0},\n"
              "      {1,0,1},\n"
              "      {1,0,1},\n"
              "      {1,0,1},\n"
              "    };\n"
              "    const int COL_MAX=sizeof(col)/sizeof(col[0]);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        // ticket #155
        check("void f() {\n"
              "    char buff1[1024*64],buff2[sizeof(buff1)*2];\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        // ticket #2510
        check("void f( int a[], int b) {\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Using 'sizeof' on array given as "
                      "function argument returns size of a pointer.\n", errout.str());

        // ticket #2510
        check("void f( int a[3] , int b[2] ) {\n"
              "    std::cout << sizeof(a) / sizeof(int) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Using 'sizeof' on array given as "
                      "function argument returns size of a pointer.\n", errout.str());

        // ticket #2510
        check("void f() {\n"
              "    char buff1[1024*64],buff2[sizeof(buff1)*(2+1)];\n"
              "}");
        ASSERT_EQUALS("", errout.str());

    }

    void sizeofForNumericParameter() {
        check("void f() {\n"
              "    std::cout << sizeof(10) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Suspicious usage of 'sizeof' with a numeric constant as parameter.\n", errout.str());

        check("void f() {\n"
              "    std::cout << sizeof(-10) << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Suspicious usage of 'sizeof' with a numeric constant as parameter.\n", errout.str());

        check("void f() {\n"
              "    std::cout << sizeof 10  << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Suspicious usage of 'sizeof' with a numeric constant as parameter.\n", errout.str());

        check("void f() {\n"
              "    std::cout << sizeof -10  << std::endl;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Suspicious usage of 'sizeof' with a numeric constant as parameter.\n", errout.str());
    }

    void suspiciousSizeofCalculation() {
        check("void f() {\n"
              "  int* p;\n"
              "  return sizeof(p)/5;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning, inconclusive) Division of result of sizeof() on pointer type.\n", errout.str());

        check("void f() {\n"
              "  unknown p;\n"
              "  return sizeof(p)/5;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  return sizeof(unknown)/5;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  int p;\n"
              "  return sizeof(p)/5;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  int* p[5];\n"
              "  return sizeof(p)/5;\n"
              "}");
        ASSERT_EQUALS("", errout.str());


        check("void f() {\n"
              "  return sizeof(foo)*sizeof(bar);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning, inconclusive) Multiplying sizeof() with sizeof() indicates a logic error.\n", errout.str());

        check("void f() {\n"
              "  return (foo)*sizeof(bar);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  return sizeof(foo)*bar;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  return (end - source) / sizeof(encode_block_type) * sizeof(encode_block_type);\n"
              "}");
        ASSERT_EQUALS("", errout.str());
    }

    void checkPointerSizeof() {
        check("void f() {\n"
              "    char *x = malloc(10);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(*x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = (int*)malloc(sizeof(x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = static_cast<int*>(malloc(sizeof(x)));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(&x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int*));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    free(x);\n"
              "    int **y = malloc(sizeof(int*));\n"
              "    free(y);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(100 * sizeof(x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(x) * 100);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof *x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(100 * sizeof x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = calloc(1, sizeof(*x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = calloc(1, sizeof *x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = calloc(1, sizeof(x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = calloc(1, sizeof x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = calloc(1, sizeof(int));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    char x[10];\n"
              "    memset(x, 0, sizeof(x));\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    char* x[10];\n"
              "    memset(x, 0, sizeof(x));\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    char x[10];\n"
              "    memset(x, 0, sizeof x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    memset(x, 0, sizeof(int));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    memset(x, 0, sizeof(*x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    memset(x, 0, sizeof *x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    memset(x, 0, sizeof x);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int));\n"
              "    memset(x, 0, sizeof(x));\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int) * 10);\n"
              "    memset(x, 0, sizeof(x) * 10);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int) * 10);\n"
              "    memset(x, 0, sizeof x * 10);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (warning) Size of pointer 'x' used instead of size of its data.\n", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int) * 10);\n"
              "    memset(x, 0, sizeof(*x) * 10);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int) * 10);\n"
              "    memset(x, 0, sizeof *x * 10);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    int *x = malloc(sizeof(int) * 10);\n"
              "    memset(x, 0, sizeof(int) * 10);\n"
              "    free(x);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check(
            "int fun(const char *buf1)\n"
            "{\n"
            "  const char *buf1_ex = \"foobarbaz\";\n"
            "  return strncmp(buf1, buf1_ex, sizeof(buf1_ex)) == 0;\n"
            "}");
        ASSERT_EQUALS("[test.cpp:4]: (warning) Size of pointer 'buf1_ex' used instead of size of its data.\n", errout.str());

        check(
            "int fun(const char *buf1) {\n"
            "  return strncmp(buf1, foo(buf2), sizeof(buf1)) == 0;\n"
            "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'buf1' used instead of size of its data.\n", errout.str());

        check("int fun(const char *buf2) {\n"
              "  return strncmp(buf1, buf2, sizeof(char*)) == 0;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Size of pointer 'buf2' used instead of size of its data.\n", errout.str());

        // #ticket 3874
        check("void f()\n"
              "{\n"
              " int * pIntArray[10];\n"
              " memset(pIntArray, 0, sizeof(pIntArray));\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void FreeFileName(const char *s) {\n"
              "  CxString tbuf;\n"
              "  const char *p;\n"
              "  memcpy(s, siezof(s));\n" // non-standard memcpy
              "}");
        ASSERT_EQUALS("", errout.str());

        check("int f() {\n"
              "  module_config_t *tab = module;\n"
              "  memset(tab + confsize, 0, sizeof(tab[confsize]));\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("int f(char* aug) {\n"
              "  memmove(aug + extra_string, aug, buf - (bfd_byte *)aug);\n" // #7100
              "}");
        ASSERT_EQUALS("", errout.str());

        // #7518
        check("bool create_iso_definition(cpp_reader *pfile, cpp_macro *macro) {\n"
              "  cpp_token *token;\n"
              "  cpp_hashnode **params = malloc(sizeof(cpp_hashnode *) * macro->paramc);\n"
              "  memcpy(params, macro->params, sizeof(cpp_hashnode *) * macro->paramc);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void* foo() {\n"
              "  void* AtomName = malloc(sizeof(char *) * 34);\n"
              "  return AtomName;\n"
              "}");
        ASSERT_EQUALS("", errout.str());
    }

    void checkPointerSizeofStruct() {
        check("void f() {\n"
              "    struct foo *ptr;\n"
              "    memset( ptr->bar, 0, sizeof ptr->bar );\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    struct foo {\n"
              "        char bar[10];\n"
              "    }* ptr;\n"
              "    memset( ptr->bar, 0, sizeof ptr->bar );\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "    struct foo {\n"
              "        char *bar;\n"
              "    }* ptr;\n"
              "    memset( ptr->bar, 0, sizeof ptr->bar );\n"
              "}");
        ASSERT_EQUALS("[test.cpp:5]: (warning) Size of pointer 'bar' used instead of size of its data.\n", errout.str());
    }

    void sizeofDivisionMemset() {
        check("void foo(memoryMapEntry_t* entry, memoryMapEntry_t* memoryMapEnd) {\n"
              "    memmove(entry, entry + 1, (memoryMapEnd - entry) / sizeof(entry));\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Division by result of sizeof(). memmove() expects a size in bytes, did you intend to multiply instead?\n", errout.str());

        check("Foo* allocFoo(int num) {\n"
              "    return malloc(num / sizeof(Foo));\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (warning) Division by result of sizeof(). malloc() expects a size in bytes, did you intend to multiply instead?\n", errout.str());

        check("void f() {\n"
              "  char str[100];\n"
              "  strncpy(str, xyz, sizeof(str)/sizeof(str[0]));\n"
              "}");
        ASSERT_EQUALS("", errout.str());
    }

    void sizeofVoid() {
        check("void f() {\n"
              "  int size = sizeof(void);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (portability) Behaviour of 'sizeof(void)' is not covered by the ISO C standard.\n", errout.str());

        check("void f() {\n"
              "  void* p;\n"
              "  int size = sizeof(*p);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (portability) '*p' is of type 'void', the behaviour of 'sizeof(void)' is not covered by the ISO C standard.\n", errout.str());

        check("void f() {\n"
              "  void* p = malloc(10);\n"
              "  int* p2 = p + 4;\n"
              "  int* p3 = p - 1;\n"
              "  int* p4 = 1 + p;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:3]: (portability) 'p' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n"
                      "[test.cpp:4]: (portability) 'p' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n"
                      "[test.cpp:5]: (portability) 'p' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("void f() {\n"
              "  void* p1 = malloc(10);\n"
              "  void* p2 = malloc(5);\n"
              "  p1--;\n"
              "  p2++;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:4]: (portability) 'p1' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n"
                      "[test.cpp:5]: (portability) 'p2' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("void f() {\n"
              "  void* p1 = malloc(10);\n"
              "  void* p2 = malloc(5);\n"
              "  p1-=4;\n"
              "  p2+=4;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:4]: (portability) 'p1' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n"
                      "[test.cpp:5]: (portability) 'p2' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("void f() {\n"
              "  void* p = malloc(10);\n"
              "  int* p2 = &p + 4;\n"
              "  int* p3 = &p - 1;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  void** p1 = malloc(10);\n"
              "  p1--;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  void** p1;\n"
              "  int j = sizeof(*p1);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f() {\n"
              "  void* p1[5];\n"
              "  int j = sizeof(*p1);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        // Calculations on void* with casts

        check("void f(void *data) {\n"
              "  *((unsigned char *)data + 1) = 0;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f(void *data) {\n"
              "  *((unsigned char *)(data) + 1) = 0;\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("void f(void *data) {\n"
              "  unsigned char* c = (unsigned char *)(data + 1);\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (portability) 'data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("void f(void *data) {\n"
              "  unsigned char* c = (unsigned char *)data++;\n"
              "  unsigned char* c2 = (unsigned char *)++data;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (portability) 'data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n"
                      "[test.cpp:3]: (portability) 'data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("void f(void *data) {\n"
              "  void* data2 = data + 1;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:2]: (portability) 'data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        // #4908 (void pointer as a member of a struct/class)
        check("struct FOO {\n"
              "  void *data;\n"
              "};\n"
              "char f(struct FOO foo) {\n"
              "  char x = *((char*)(foo.data+1));\n"
              "  foo.data++;\n"
              "  return x;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:5]: (portability) 'foo.data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n"
                      "[test.cpp:6]: (portability) 'foo.data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("struct FOO {\n"
              "  void *data;\n"
              "};\n"
              "char f(struct FOO foo) {\n"
              "  char x = *((char*)foo.data+1);\n"
              "  return x;\n"
              "}\n"
              "char f2(struct FOO foo) {\n"
              "  char x = *((char*)((FOO)foo).data + 1);\n"
              "  return x;\n"
              "}\n"
              "char f3(struct FOO* foo) {\n"
              "  char x = *((char*)foo->data + 1);\n"
              "  return x;\n"
              "}\n"
              "struct BOO {\n"
              "  FOO data;\n"
              "};\n"
              "void f4(struct BOO* boo) {\n"
              "  char c = *((char*)boo->data.data + 1);\n"
              "}");
        ASSERT_EQUALS("", errout.str());

        check("struct FOO {\n"
              "  void *data;\n"
              "};\n"
              "char f(struct FOO* foo) {\n"
              "  *(foo[1].data + 1) = 0;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:5]: (portability) 'foo[1].data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        check("struct FOO {\n"
              "  void *data;\n"
              "};\n"
              "void f2(struct FOO* foo) {\n"
              "  (foo[0]).data++;\n"
              "}");
        ASSERT_EQUALS("[test.cpp:5]: (portability) '(foo[0]).data' is of type 'void *'. When using void pointers in calculations, the behaviour is undefined.\n", errout.str());

        // #6050 arithmetic on void**
        check("void* array[10];\n"
              "void** b = array + 3;");
        ASSERT_EQUALS("", errout.str());
    }

    void customStrncat() {
        // Ensure we don't crash on custom-defined strncat, ticket #5875
        check("char strncat ();\n"
              "int main () {\n"
              "  return strncat ();\n"
              "}");
    }

};

REGISTER_TEST(TestSizeof)

