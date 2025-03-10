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

#include "check.h"
#include "config.h"
#include "errortypes.h"
#include "mathlib.h"
#include "settings.h"
#include "testsuite.h"
#include "token.h"
#include "tokenize.h"

#include <iosfwd>
#include <list>
#include <string>


class TestGarbage : public TestFixture {
public:
    TestGarbage() : TestFixture("TestGarbage") {}

private:
    Settings settings;

    void run() OVERRIDE {
        settings.debugwarnings = true;
        settings.severity.fill();
        settings.certainty.fill();

        // don't freak out when the syntax is wrong

        TEST_CASE(final_class_x);
        TEST_CASE(wrong_syntax1);
        TEST_CASE(wrong_syntax2);
        TEST_CASE(wrong_syntax3); // #3544
        TEST_CASE(wrong_syntax4); // #3618
        TEST_CASE(wrong_syntax_if_macro);  // #2518 - if MACRO()
        TEST_CASE(wrong_syntax_class_x_y); // #3585 - class x y { };
        TEST_CASE(wrong_syntax_anonymous_struct);
        TEST_CASE(syntax_case_default);
        TEST_CASE(garbageCode1);
        TEST_CASE(garbageCode2); // #4300
        TEST_CASE(garbageCode3); // #4869
        TEST_CASE(garbageCode4); // #4887
        TEST_CASE(garbageCode5); // #5168
        TEST_CASE(garbageCode6); // #5214
        TEST_CASE(garbageCode7);
        TEST_CASE(garbageCode8); // #5511
        TEST_CASE(garbageCode9); // #5604
        TEST_CASE(garbageCode10); // #6127
        TEST_CASE(garbageCode12);
        TEST_CASE(garbageCode13); // #2607
        TEST_CASE(garbageCode15); // #5203
        TEST_CASE(garbageCode16);
        TEST_CASE(garbageCode17);
        TEST_CASE(garbageCode18);
        TEST_CASE(garbageCode20);
        TEST_CASE(garbageCode21);
        TEST_CASE(garbageCode22);
        TEST_CASE(garbageCode23);
        TEST_CASE(garbageCode24); // #6361
        TEST_CASE(garbageCode25);
        TEST_CASE(garbageCode26);
        TEST_CASE(garbageCode27);
        TEST_CASE(garbageCode28);
        TEST_CASE(garbageCode30); // #5867
        TEST_CASE(garbageCode31); // #6539
        TEST_CASE(garbageCode33); // #6613
        TEST_CASE(garbageCode34); // #6626
        TEST_CASE(garbageCode35); // #2604
        TEST_CASE(garbageCode36); // #6334
        TEST_CASE(garbageCode37); // #5166
        TEST_CASE(garbageCode38); // #6666
        TEST_CASE(garbageCode40); // #6620
        TEST_CASE(garbageCode41); // #6685
        TEST_CASE(garbageCode42); // #5760
        TEST_CASE(garbageCode43); // #6703
        TEST_CASE(garbageCode44); // #6704
        TEST_CASE(garbageCode45); // #6608
        TEST_CASE(garbageCode46); // #6705
        TEST_CASE(garbageCode47); // #6706
        TEST_CASE(garbageCode48); // #6712
        TEST_CASE(garbageCode49); // #6715
        TEST_CASE(garbageCode51); // #6719
        TEST_CASE(garbageCode53); // #6721
        TEST_CASE(garbageCode54); // #6722
        TEST_CASE(garbageCode55); // #6724
        TEST_CASE(garbageCode56); // #6713
        TEST_CASE(garbageCode57); // #6733
        TEST_CASE(garbageCode58); // #6732
        TEST_CASE(garbageCode59); // #6735
        TEST_CASE(garbageCode60); // #6736
        TEST_CASE(garbageCode61);
        TEST_CASE(garbageCode63);
        TEST_CASE(garbageCode64);
        TEST_CASE(garbageCode65);
        TEST_CASE(garbageCode66);
        TEST_CASE(garbageCode68);
        TEST_CASE(garbageCode69);
        TEST_CASE(garbageCode70);
        TEST_CASE(garbageCode71);
        TEST_CASE(garbageCode72);
        TEST_CASE(garbageCode73);
        TEST_CASE(garbageCode74);
        TEST_CASE(garbageCode76);
        TEST_CASE(garbageCode77);
        TEST_CASE(garbageCode78);
        TEST_CASE(garbageCode79);
        TEST_CASE(garbageCode80);
        TEST_CASE(garbageCode81);
        TEST_CASE(garbageCode82);
        TEST_CASE(garbageCode83);
        TEST_CASE(garbageCode84);
        TEST_CASE(garbageCode85);
        TEST_CASE(garbageCode86);
        TEST_CASE(garbageCode87);
        TEST_CASE(garbageCode88);
        TEST_CASE(garbageCode90);
        TEST_CASE(garbageCode91);
        TEST_CASE(garbageCode92);
        TEST_CASE(garbageCode94);
        TEST_CASE(garbageCode95);
        TEST_CASE(garbageCode96);
        TEST_CASE(garbageCode97);
        TEST_CASE(garbageCode98);
        TEST_CASE(garbageCode99);
        TEST_CASE(garbageCode100);
        TEST_CASE(garbageCode101); // #6835
        TEST_CASE(garbageCode102); // #6846
        TEST_CASE(garbageCode103); // #6824
        TEST_CASE(garbageCode104); // #6847
        TEST_CASE(garbageCode105); // #6859
        TEST_CASE(garbageCode106);
        TEST_CASE(garbageCode107);
        TEST_CASE(garbageCode108);
        TEST_CASE(garbageCode109);
        TEST_CASE(garbageCode110);
        TEST_CASE(garbageCode111);
        TEST_CASE(garbageCode112);
        TEST_CASE(garbageCode114); // #2118
        TEST_CASE(garbageCode115); // #5506
        TEST_CASE(garbageCode116); // #5356
        TEST_CASE(garbageCode117); // #6121
        TEST_CASE(garbageCode118); // #5600
        TEST_CASE(garbageCode119); // #5598
        TEST_CASE(garbageCode120); // #4927
        TEST_CASE(garbageCode121); // #2585
        TEST_CASE(garbageCode122); // #6303
        TEST_CASE(garbageCode123);
        TEST_CASE(garbageCode125); // 6782, 6834
        TEST_CASE(garbageCode126); // #6997
        TEST_CASE(garbageCode127); // #6667
        TEST_CASE(garbageCode128); // #7018
        TEST_CASE(garbageCode129); // #7020
        TEST_CASE(garbageCode130); // #7021
        TEST_CASE(garbageCode131); // #7023
        TEST_CASE(garbageCode132); // #7022
        TEST_CASE(garbageCode133);
        TEST_CASE(garbageCode134);
        TEST_CASE(garbageCode135); // #4994
        TEST_CASE(garbageCode136); // #7033
        TEST_CASE(garbageCode137); // #7034
        TEST_CASE(garbageCode138); // #6660
        TEST_CASE(garbageCode139); // #6659
        TEST_CASE(garbageCode140); // #7035
        TEST_CASE(garbageCode141); // #7043
        TEST_CASE(garbageCode142); // #7050
        TEST_CASE(garbageCode143); // #6922
        TEST_CASE(garbageCode144); // #6865
        TEST_CASE(garbageCode146); // #7081
        TEST_CASE(garbageCode147); // #7082
        TEST_CASE(garbageCode148); // #7090
        TEST_CASE(garbageCode149); // #7085
        TEST_CASE(garbageCode150); // #7089
        TEST_CASE(garbageCode151); // #4911
        TEST_CASE(garbageCode152); // travis after 9c7271a5
        TEST_CASE(garbageCode153);
        TEST_CASE(garbageCode154); // #7112
        TEST_CASE(garbageCode156); // #7120
        TEST_CASE(garbageCode157); // #7131
        TEST_CASE(garbageCode158); // #3238
        TEST_CASE(garbageCode159); // #7119
        TEST_CASE(garbageCode160); // #7190
        TEST_CASE(garbageCode161); // #7200
        TEST_CASE(garbageCode162); // #7208
        TEST_CASE(garbageCode163); // #7228
        TEST_CASE(garbageCode164); // #7234
        TEST_CASE(garbageCode165); // #7235
        TEST_CASE(garbageCode167); // #7237
        TEST_CASE(garbageCode168); // #7246
        TEST_CASE(garbageCode169); // #6731
        TEST_CASE(garbageCode170);
        TEST_CASE(garbageCode171);
        TEST_CASE(garbageCode172);
        TEST_CASE(garbageCode173); // #6781
        TEST_CASE(garbageCode174); // #7356
        TEST_CASE(garbageCode175);
        TEST_CASE(garbageCode176); // #7527
        TEST_CASE(garbageCode181);
        TEST_CASE(garbageCode182); // #4195
        TEST_CASE(garbageCode183); // #7505
        TEST_CASE(garbageCode184); // #7699
        TEST_CASE(garbageCode185); // #6011
        TEST_CASE(garbageCode186); // #8151
        TEST_CASE(garbageCode187);
        TEST_CASE(garbageCode188);
        TEST_CASE(garbageCode189); // #8317
        TEST_CASE(garbageCode190); // #8307
        TEST_CASE(garbageCode191); // #8333
        TEST_CASE(garbageCode192); // #8386 (segmentation fault)
        TEST_CASE(garbageCode193); // #8740
        TEST_CASE(garbageCode194); // #8384
        TEST_CASE(garbageCode195); // #8709
        TEST_CASE(garbageCode196); // #8265
        TEST_CASE(garbageCode197); // #8385
        TEST_CASE(garbageCode198); // #8383
        TEST_CASE(garbageCode199); // #8752
        TEST_CASE(garbageCode200); // #8757
        TEST_CASE(garbageCode201); // #8873
        TEST_CASE(garbageCode202); // #8907
        TEST_CASE(garbageCode203); // #8972
        TEST_CASE(garbageCode204);
        TEST_CASE(garbageCode205);
        TEST_CASE(garbageCode206);
        TEST_CASE(garbageCode207); // #8750
        TEST_CASE(garbageCode208); // #8753
        TEST_CASE(garbageCode209); // #8756
        TEST_CASE(garbageCode210); // #8762
        TEST_CASE(garbageCode211); // #8764
        TEST_CASE(garbageCode212); // #8765
        TEST_CASE(garbageCode213); // #8758
        TEST_CASE(garbageCode214);
        TEST_CASE(garbageCode215); // daca@home script with extension .c
        TEST_CASE(garbageCode216); // #7884
        TEST_CASE(garbageCode217); // #10011
        TEST_CASE(garbageCode218); // #8763
        TEST_CASE(garbageCode219); // #10101

        TEST_CASE(garbageCodeFuzzerClientMode1); // test cases created with the fuzzer client, mode 1

        TEST_CASE(garbageValueFlow);
        TEST_CASE(garbageSymbolDatabase);
        TEST_CASE(garbageAST);
        TEST_CASE(templateSimplifierCrashes);
        TEST_CASE(syntaxErrorFirstToken); // Make sure syntax errors are detected and reported
        TEST_CASE(syntaxErrorLastToken); // Make sure syntax errors are detected and reported
        TEST_CASE(syntaxErrorCase);
        TEST_CASE(syntaxErrorFuzzerCliType1);
        TEST_CASE(cliCode);
        TEST_CASE(enumTrailingComma);

        TEST_CASE(nonGarbageCode1); // #8346
    }

#define checkCodeInternal(code, filename) checkCodeInternal_(code, filename, __FILE__, __LINE__)
    std::string checkCode(const std::string &code, bool cpp = true) {
        // double the tests - run each example as C as well as C++
        const char* const filename = cpp ? "test.cpp" : "test.c";
        const char* const alternatefilename = cpp ? "test.c" : "test.cpp";

        // run alternate check first. It should only ensure stability - so we catch exceptions here.
        try {
            checkCodeInternal(code, alternatefilename);
        } catch (const InternalError&) {}

        return checkCodeInternal(code, filename);
    }

    std::string checkCodeInternal_(const std::string &code, const char* filename, const char* file, int line) {
        errout.str("");

        // tokenize..
        Tokenizer tokenizer(&settings, this);
        std::istringstream istr(code);
        ASSERT_LOC(tokenizer.tokenize(istr, filename), file, line);

        // call all "runChecks" in all registered Check classes
        for (std::list<Check *>::const_iterator it = Check::instances().begin(); it != Check::instances().end(); ++it) {
            (*it)->runChecks(&tokenizer, &settings, this);
        }

        return tokenizer.tokens()->stringifyList(false, false, false, true, false, nullptr, nullptr);
    }

#define getSyntaxError(code) getSyntaxError_(code, __FILE__, __LINE__)
    std::string getSyntaxError_(const char code[], const char* file, int line) {
        Tokenizer tokenizer(&settings, this);
        std::istringstream istr(code);
        try {
            ASSERT_LOC(tokenizer.tokenize(istr, "test.cpp"), file, line);
        } catch (InternalError& e) {
            if (e.id != "syntaxError")
                return "";
            return "[test.cpp:" + MathLib::toString(e.token->linenr()) + "] " + e.errorMessage;
        }
        return "";
    }


    void final_class_x() {

        const char code[] = "class __declspec(dllexport) x final { };";
        {
            errout.str("");
            Tokenizer tokenizer(&settings, this);
            std::istringstream istr(code);
            ASSERT(tokenizer.tokenize(istr, "test.cpp"));
            ASSERT_EQUALS("", errout.str());
        }
    }

    void wrong_syntax1() {
        {
            const char code[] ="TR(kvmpio, PROTO(int rw), ARGS(rw), TP_(aa->rw;))";
            ASSERT_THROW(checkCode(code), InternalError);
            ASSERT_EQUALS("", errout.str());
        }

        {
            const char code[] ="struct A { template<int> struct { }; };";
            ASSERT_THROW(checkCode(code), InternalError);
        }

        {
            const char code[] ="enum ABC { A,B, typedef enum { C } };";
            ASSERT_THROW(checkCode(code), InternalError);
        }
    }

    void wrong_syntax2() {   // #3504
        const char code[] = "void f() {\n"
                            "    X<int> x;\n"
                            "    Y<int, int, int, int, int, char> y;\n"
                            "}\n"
                            "\n"
                            "void G( template <typename T> class (j) ) {}";

        // don't segfault..
        ASSERT_THROW(checkCode(code), InternalError);
    }


    void wrong_syntax3() {   // #3544
        const char code[] = "X #define\n"
                            "{\n"
                            " (\n"
                            "  for(  #endif typedef typedef cb[N] )\n"
                            "        ca[N]; =  cb[i]\n"
                            " )\n"
                            "}";

        Tokenizer tokenizer(&settings, this);
        std::istringstream istr(code);
        try {
            ASSERT(tokenizer.tokenize(istr, "test.cpp"));
            assertThrowFail(__FILE__, __LINE__);
        } catch (InternalError& e) {
            ASSERT_EQUALS("syntax error", e.errorMessage);
            ASSERT_EQUALS("syntaxError", e.id);
            ASSERT_EQUALS(4, e.token->linenr());
        }
    }

    void wrong_syntax4() {   // #3618
        const char code[] = "typedef void (x) (int);    return x&";

        ASSERT_THROW(checkCode(code), InternalError);
    }

    void wrong_syntax_if_macro() {
        // #2518 #4171
        ASSERT_THROW(checkCode("void f() { if MACRO(); }"), InternalError);

        // #4668 - note there is no semicolon after MACRO()
        ASSERT_THROW(checkCode("void f() { if (x) MACRO() {} }"), InternalError);

        // #4810 - note there is no semicolon after MACRO()
        ASSERT_THROW(checkCode("void f() { if (x) MACRO() else ; }"), InternalError);
    }

    void wrong_syntax_class_x_y() {
        // #3585
        const char code[] = "class x y { };";

        {
            errout.str("");
            Tokenizer tokenizer(&settings, this);
            std::istringstream istr(code);
            ASSERT(tokenizer.tokenize(istr, "test.c"));
            ASSERT_EQUALS("", errout.str());
        }
        {
            errout.str("");
            Tokenizer tokenizer(&settings, this);
            std::istringstream istr(code);
            ASSERT(tokenizer.tokenize(istr, "test.cpp"));
            ASSERT_EQUALS("[test.cpp:1]: (information) The code 'class x y {' is not handled. You can use -I or --include to add handling of this code.\n", errout.str());
        }
    }

    void wrong_syntax_anonymous_struct() {
        ASSERT_THROW(checkCode("struct { int x; } = {0};"), InternalError);
        ASSERT_THROW(checkCode("struct { int x; } * = {0};"), InternalError);
    }

    void syntax_case_default() {
        ASSERT_THROW(checkCode("void f() {switch (n) { case: z(); break;}}"), InternalError);

        ASSERT_THROW(checkCode("void f() {switch (n) { case;: z(); break;}}"), InternalError);

        ASSERT_THROW(checkCode("void f() {switch (n) { case {}: z(); break;}}"), InternalError);

        ASSERT_THROW(checkCode("void f() {switch (n) { case 0?{1}:{2} : z(); break;}}"), InternalError);

        ASSERT_THROW(checkCode("void f() {switch (n) { case 0?1;:{2} : z(); break;}}"), InternalError);

        ASSERT_THROW(checkCode("void f() {switch (n) { case 0?(1?{3:4}):2 : z(); break;}}"), InternalError);

        //ticket #4234
        ASSERT_THROW(checkCode("( ) { switch break ; { switch ( x ) { case } y break ; : } }"), InternalError);

        //ticket #4267
        ASSERT_THROW(checkCode("f ( ) { switch break; { switch ( x ) { case } case break; -6: ( ) ; } }"), InternalError);

        // Missing semicolon
        ASSERT_THROW(checkCode("void foo () { switch(0) case 0 : default : }"), InternalError);
    }

    void garbageCode1() {
        checkCode("struct x foo_t; foo_t typedef y;");
    }

    void garbageCode2() { //#4300 (segmentation fault)
        TODO_ASSERT_THROW(checkCode("enum { D = 1  struct  { } ; }  s.b = D;"), InternalError);
    }

    void garbageCode3() { //#4849 (segmentation fault in Tokenizer::simplifyStructDecl (invalid code))
        TODO_ASSERT_THROW(checkCode("enum {  D = 2 s ; struct y  { x } ; } { s.a = C ; s.b = D ; }"), InternalError);
    }

    void garbageCode4() { // #4887
        ASSERT_THROW(checkCode("void f ( ) { = a ; if ( 1 ) if = ( 0 ) ; }"), InternalError);
    }

    void garbageCode5() { // #5168
        checkCode("( asm : ; void : );");
    }

    void garbageCode6() { // #5214
        ASSERT_THROW(checkCode("int b = ( 0 ? ? ) 1 : 0 ;"), InternalError);
        ASSERT_THROW(checkCode("int a = int b = ( 0 ? ? ) 1 : 0 ;"), InternalError);
    }

    void garbageCode7() {
        ASSERT_THROW(checkCode("1 (int j) { return return (c) * sizeof } y[1];"), InternalError);
        ASSERT_THROW(checkCode("foo(Args&&...) fn void = { } auto template<typename... bar(Args&&...)"), InternalError);
    }

    void garbageCode8() { // #5604
        TODO_ASSERT_THROW(checkCode("{ enum struct : };"), InternalError);
        TODO_ASSERT_THROW(checkCode("int ScopedEnum{ template<typename T> { { e = T::error }; };\n"
                                    "ScopedEnum1<int> se1; { enum class E : T { e = 0 = e ScopedEnum2<void*> struct UnscopedEnum3 { T{ e = 4 }; };\n"
                                    "arr[(int) E::e]; }; UnscopedEnum3<int> e2 = f()\n"
                                    "{ { e = e1; T::error } int test1 ue2; g() { enum class E { e = T::error }; return E::e; } int test2 = }\n"
                                    "namespace UnscopedEnum { template<typename T> struct UnscopedEnum1 { E{ e = T::error }; }; UnscopedEnum1<int> { enum E : { e = 0 }; };\n"
                                    "UnscopedEnum2<void*> ue3; template<typename T> struct UnscopedEnum3 { enum { }; }; int arr[E::e]; };\n"
                                    "UnscopedEnum3<int> namespace template<typename T> int f() { enum E { e }; T::error }; return (int) E(); } int test1 int g() { enum E { e = E };\n"
                                    "E::e; } int test2 = g<int>(); }"), InternalError);
    }

    void garbageCode9() {
        TODO_ASSERT_THROW(checkCode("enum { e = { } } ( ) { { enum { } } } { e } "), InternalError);
    }

    void garbageCode10() { // #6127
        ASSERT_THROW(checkCode("for( rl=reslist; rl!=NULL; rl=rl->next )"), InternalError);
    }

    void garbageCode12() { // do not crash
        checkCode("{ g; S (void) { struct } { } int &g; }");
    }

    void garbageCode13() {
        checkCode("struct C {} {} x");
    }

    void garbageCode15() { // Ticket #5203
        ASSERT_THROW(checkCode("int f ( int* r ) { {  int s[2] ; f ( s ) ; if ( ) } }"), InternalError);
    }

    void garbageCode16() {
        checkCode("{ } A() { delete }"); // #6080
    }

    void garbageCode17() {
        ASSERT_THROW(checkCode("void h(int l) {\n"
                               "    while\n" // Don't crash (#3870)
                               "}"), InternalError);
    }

    void garbageCode18() {
        ASSERT_THROW(checkCode("switch(){case}"), InternalError);
    }

    void garbageCode20() {
        // #3953 (valgrind errors on garbage code)
        ASSERT_EQUALS("void f ( 0 * ) ;", checkCode("void f ( 0 * ) ;"));
    }

    void garbageCode21() {
        // Ticket #3486 - Don't crash garbage code
        ASSERT_THROW(checkCode("void f()\n"
                               "{\n"
                               "  (\n"
                               "    x;\n"
                               "    int a, a2, a2*x; if () ;\n"
                               "  )\n"
                               "}"), InternalError);
    }

    void garbageCode22() {
        // Ticket #3480 - Don't crash garbage code
        ASSERT_THROW(checkCode("int f()\n"
                               "{\n"
                               "    return if\n"
                               "}"), InternalError);
    }

    void garbageCode23() {
        //garbage code : don't crash (#3481)
        checkCode("{\n"
                  "    if (1) = x\n"
                  "    else abort s[2]\n"
                  "}");
        ASSERT_EQUALS("", errout.str());
    }

    void garbageCode24() {
        // don't crash (example from #6361)
        ASSERT_THROW(checkCode("float buffer[64];\n"
                               "main (void)\n"
                               "{\n"
                               "  char *cptr;\n"
                               "  cptr = (char *)buffer;\n"
                               "  cptr += (-(long int) buffer & (16 * sizeof (float) - 1));\n"
                               "}\n"), InternalError);
    }

    void garbageCode25() {
        // Ticket #2386 - Segmentation fault upon strange syntax
        ASSERT_THROW(checkCode("void f() {\n"
                               "    switch ( x ) {\n"
                               "        case struct Tree : break;\n"
                               "    }\n"
                               "}"), InternalError);
    }

    void garbageCode26() {
        // See tickets #2518 #2555 #4171
        ASSERT_THROW(checkCode("void f() {\n"
                               "    switch MAKEWORD(1)\n"
                               "    {\n"
                               "    case 0:\n"
                               "        return;\n"
                               "    }\n"
                               "}"), InternalError);
    }

    void garbageCode27() {
        ASSERT_THROW(checkCode("int f() {\n"
                               "    return if\n"
                               "}"), InternalError);
    }

    void garbageCode28() {
        // 5702
        checkCode("struct R1 {\n"
                  "  int a;\n"
                  "  R1 () : a { }\n"
                  "};");
    }

    void garbageCode30() {
        // simply survive - a syntax error would be even better (#5867)
        checkCode("void f(int x) {\n"
                  " x = 42\n"
                  "}");
    }

    void garbageCode31() {
        ASSERT_THROW(checkCode("typedef struct{}x[([],)]typedef e y;(y,x 0){}"), InternalError);
    }

    void garbageCode33() { // #6613
        checkCode("main(()B{});");
    }

    // Bug #6626 crash: Token::astOperand2() const ( do while )
    void garbageCode34() {
        const char code[] = "void foo(void) {\n"
                            " do\n"
                            " while (0);\n"
                            "}";
        ASSERT_THROW(checkCode(code), InternalError);
    }

    void garbageCode35() {
        // ticket #2604 segmentation fault
        ASSERT_THROW(checkCode("sizeof <= A"), InternalError);
    }

    void garbageCode36() { // #6334
        ASSERT_THROW(checkCode("{ } < class template < > , { = } ; class... >\n"
                               "struct Y { }\n"
                               "class Types { }\n"
                               "( X < int > \"uses template\" ) ( < ( ) \"uses ;"
                               "( int int ::primary \"uses template\" ) int double \"uses )"
                               "::primary , \"uses template\" ;\n"), InternalError);
    }

    void garbageCode37() {
        // #5166 segmentation fault (invalid code) in lib/checkother.cpp:329 ( void * f { } void b ( ) { * f } )
        checkCode("void * f { } void b ( ) { * f }");
    }

    void garbageCode38() { // Ticket #6666
        checkCode("{ f2 { } } void f3 () { delete[] } { }");
    }

    void garbageCode40() { // #6620
        checkCode("{ ( ) () { virtual } ; { } E } A { : { } ( ) } * const ( ) const { }");
        // test doesn't seem to work on any platform: ASSERT_THROW(checkCode("{ ( ) () { virtual } ; { } E } A { : { } ( ) } * const ( ) const { }", "test.c"), InternalError);
    }

    void garbageCode41() { // #6685
        checkCode(" { } { return } *malloc(__SIZE_TYPE__ size); *memcpy(void n); static * const () { memcpy (*slot, 3); } { (); } { }");
    }

    void garbageCode42() { // #5760
        checkCode("{  } * const ( ) { }");
    }

    void garbageCode43() { // #6703
        ASSERT_THROW(checkCode("int { }; struct A<void> a = { }"), InternalError);
    }

    void garbageCode44() { // #6704
        ASSERT_THROW(checkCode("{ { }; }; { class A : }; public typedef b;"), InternalError);
    }

    void garbageCode45() { // #6608
        ASSERT_THROW(checkCode("struct true template < > { = } > struct Types \"s\" ; static_assert < int > ;"), InternalError);
    }

    void garbageCode46() { // #6705
        checkCode(" { bar(char *x); void foo (int ...) { struct } va_list ap; va_start(ap, size); va_arg(ap, (d)); }");
    }

    void garbageCode47() { // #6706
        checkCode(" { { }; }; * new private: B: B;");
    }

    void garbageCode48() { // #6712
        checkCode(" { d\" ) d ...\" } int main ( ) { ( ) catch ( A a ) { { } catch ( ) \"\" } }");
    }

    void garbageCode49() { // #6715
        ASSERT_THROW(checkCode(" ( ( ) ) { } ( { ( __builtin_va_arg_pack ( ) ) ; } ) { ( int { ( ) ( ( ) ) } ( ) { } ( ) ) += ( ) }"), InternalError);
    }

    void garbageCode51() { // #6719
        ASSERT_THROW(checkCode(" (const \"C\" ...); struct base { int f2; base (int arg1, int arg2); }; global_base(0x55, 0xff); { ((global_base.f1 0x55) (global_base.f2 0xff)) { } } base::base(int arg1, int arg2) { f2 = }"), InternalError);
    }

    void garbageCode53() { // #6721
        ASSERT_THROW(checkCode("{ { } }; void foo (struct int i) { x->b[i] = = }"), InternalError);
    }

    void garbageCode54() { // #6722
        ASSERT_THROW(checkCode("{ typedef long ((pf) p) (); }"), InternalError);
    }

    void garbageCode55() { // #6724
        ASSERT_THROW(checkCode("() __attribute__((constructor)); { } { }"), InternalError);
    }

    void garbageCode56() { // #6713
        ASSERT_THROW(checkCode("void foo() { int a = 0; int b = ???; }"), InternalError);
    }

    void garbageCode57() { // #6731
        ASSERT_THROW(checkCode("{ } if () try { } catch (...) B::~B { }"), InternalError);
    }

    void garbageCode58() { // #6732, #6762
        ASSERT_THROW(checkCode("{ }> {= ~A()^{} }P { }"), InternalError);
        ASSERT_THROW(checkCode("{= ~A()^{} }P { } { }> is"), InternalError);
    }

    void garbageCode59() { // #6735
        ASSERT_THROW(checkCode("{ { } }; char font8x8[256][8]"), InternalError);
    }

    void garbageCode60() { // #6736
        ASSERT_THROW(checkCode("{ } { } typedef int int_array[]; int_array &right ="), InternalError);
    }

    void garbageCode61() { // #6737
        ASSERT_THROW(checkCode("{ (const U&) }; { }; { }; struct U : virtual public"), InternalError);
    }

    void garbageCode63() { // #6739
        ASSERT_THROW(checkCode("{ } { } typedef int u_array[]; typedef u_array &u_array_ref; (u_array_ref arg) { } u_array_ref u_array_ref_gbl_obj0"), InternalError);
    }

    void garbageCode64() { // #6740
        ASSERT_THROW(checkCode("{ } foo(void (*bar)(void))"), InternalError);
    }

    void garbageCode65() { // #6741
        ASSERT_THROW(checkCode("{ } { } typedef int u_array[]; typedef u_array &u_array_ref; (u_array_ref arg) { } u_array_ref"), InternalError);
    }

    void garbageCode66() { // #6742
        ASSERT_THROW(checkCode("{ { } }; { { } }; { }; class bar : public virtual"), InternalError);
    }

    void garbageCode68() { // #6745
        checkCode("(int a[3]); typedef void (*fp) (void); fp");
    }

    void garbageCode69() { // #6746
        ASSERT_THROW(checkCode("{ (make_mess, aux); } typedef void F(void); aux(void (*x)()) { } (void (*y)()) { } F*"), InternalError);
    }

    void garbageCode70() { // #6747
        ASSERT_THROW(checkCode("{ } __attribute__((constructor)) void"), InternalError);
    }

    void garbageCode71() { // #6748
        ASSERT_THROW(checkCode("( ) { } typedef void noattr_t ( ) ; noattr_t __attribute__ ( )"), InternalError);
    }

    void garbageCode72() { // #6749
        ASSERT_THROW(checkCode("{ } { } typedef void voidfn(void); <voidfn&"), InternalError);
    }

    void garbageCode73() { // #6750
        ASSERT_THROW(checkCode("typedef int IRT[2]; IRT&"), InternalError);
    }

    void garbageCode74() { // #6751
        ASSERT_THROW(checkCode("_lenraw(const char* digits) { } typedef decltype(sizeof(0)) { } operator"), InternalError);
    }

    void garbageCode76() { // #6754
        ASSERT_THROW(checkCode(" ( ) ( ) { ( ) [ ] } TEST ( ) { ( _broadcast_f32x4 ) ( ) ( ) ( ) ( ) if ( ) ( ) ; } E mask = ( ) [ ] ( ) res1.x ="), InternalError);
    }

    void garbageCode77() { // #6755
        ASSERT_THROW(checkCode("void foo (int **p) { { { };>= } } unsigned *d = (b b--) --*d"), InternalError);
    }

    void garbageCode78() { // #6756
        ASSERT_THROW(checkCode("( ) { [ ] } ( ) { } const_array_of_int ( ) { } typedef int A [ ] [ ] ; A a = { { } { } }"), InternalError);
    }

    void garbageCode79() { // #6757
        ASSERT_THROW(checkCode("{ } { } typedef void ( func_type ) ( ) ; func_type & ( )"), InternalError);
    }

    void garbageCode80() { // #6759
        ASSERT_THROW(checkCode("( ) { ; ( ) ; ( * ) [ ] ; [ ] = ( ( ) ( ) h ) ! ( ( ) ) } { ; } { } head heads [ ] = ; = & heads [ 2 ]"), InternalError);
    }

    void garbageCode81() { // #6760
        ASSERT_THROW(checkCode("{ } [ ] { ( ) } { } typedef void ( *fptr1 ) ( ) const"), InternalError);
    }

    void garbageCode82() { // #6761
        ASSERT_THROW(checkCode("p(\"Hello \" 14) _yn(const size_t) typedef bool pfunk (*pfunk)(const size_t)"), InternalError);
    }

    void garbageCode83() { // #6771
        ASSERT_THROW(checkCode("namespace A { class } class A { friend C ; } { } ;"), InternalError);
    }

    void garbageCode84() { // #6780
        ASSERT_THROW(checkCode("int main ( [ ] ) { " " [ ] ; int i = 0 ; do { } ; } ( [ ] ) { }"), InternalError); // do not crash
    }

    void garbageCode85() { // #6784
        ASSERT_THROW(checkCode("{ } { } typedef void ( *VoidFunc() ) ( ) ; VoidFunc"), InternalError); // do not crash
    }

    void garbageCode86() { // #6785
        ASSERT_THROW(checkCode("{ } typedef char ( *( X ) ( void) , char ) ;"), InternalError); // do not crash
    }

    void garbageCode87() { // #6788
        ASSERT_THROW(checkCode("((X (128))) (int a) { v[ = {} (x 42) a] += }"), InternalError); // do not crash
    }

    void garbageCode88() { // #6786
        ASSERT_THROW(checkCode("( ) { ( 0 ) { ( ) } } g ( ) { i( ( false ?) ( ) : 1 ) ; } ;"), InternalError); // do not crash
    }

    void garbageCode90() { // #6790
        ASSERT_THROW(checkCode("{ } { } typedef int u_array [[ ] ; typedef u_array & u_array_ref] ( ) { } u_array_ref_gbl_obj0"), InternalError); // do not crash
    }

    void garbageCode91() { // #6791
        ASSERT_THROW(checkCode("typedef __attribute__((vector_size (16))) { return[ (v2df){ } ;] }"), InternalError); // throw syntax error
    }

    void garbageCode92() { // #6792
        ASSERT_THROW(checkCode("template < typename _Tp ( ( ) ; _Tp ) , decltype > { } { ( ) ( ) }"), InternalError); // do not crash
    }

    void garbageCode94() { // #6803
        //checkCode("typedef long __m256i __attribute__ ( ( ( ) ) )[ ; ( ) { } typedef __m256i __attribute__ ( ( ( ) ) ) < ] ( ) { ; }");
        ASSERT_THROW(checkCode("typedef long __m256i __attribute__ ( ( ( ) ) )[ ; ( ) { } typedef __m256i __attribute__ ( ( ( ) ) ) < ] ( ) { ; }"), InternalError);
    }

    void garbageCode95() { // #6804
        ASSERT_THROW(checkCode("{ } x x ; { } h h [ ] ( ) ( ) { struct x ( x ) ; int __attribute__ ( ) f ( ) { h - > first = & x ; struct x * n = h - > first ; ( ) n > } }"), InternalError); // do not crash
    }

    void garbageCode96() { // #6807
        ASSERT_THROW(checkCode("typedef J J[ ; typedef ( ) ( ) { ; } typedef J J ;] ( ) ( J cx ) { n } ;"), InternalError); // throw syntax error
    }

    void garbageCode97() { // #6808
        ASSERT_THROW(checkCode("namespace A {> } class A{ { }} class A : T< ;"), InternalError);
    }

    void garbageCode98() { // #6838
        ASSERT_THROW(checkCode("for (cocon To::ta@Taaaaaforconst oken aaaaaaaaaaaa5Dl()\n"
                               "const unsigned in;\n"
                               "fon *tok = f);.s(Token i = d-)L;"), InternalError);
    }

    void garbageCode99() { // #6726
        ASSERT_THROW(checkCode("{ xs :: i(:) ! ! x/5 ! !\n"
                               "i, :: a :: b integer, } foo2(x) :: j(:)\n"
                               "b type(*), d(:), a x :: end d(..), foo end\n"
                               "foo4 b d(..), a a x type(*), b foo2 b"), InternalError);
    }

    void garbageCode100() { // #6840
        ASSERT_THROW(checkCode("( ) { ( i< ) } int foo ( ) { int i ; ( for ( i => 1 ) ; ) }"), InternalError);
    }

    void garbageCode101() { // #6835
        // Reported case
        ASSERT_THROW(checkCode("template < class , =( , int) X = 1 > struct A { } ( ) { = } [ { } ] ( ) { A < void > 0 }"), InternalError);
        // Reduced case
        ASSERT_THROW(checkCode("template < class =( , ) X = 1> struct A {}; A<void> a;"), InternalError);
    }

    void garbageCode102() { // #6846
        checkCode("struct Object { ( ) ; Object & operator= ( Object ) { ( ) { } if ( this != & b ) } }");
    }

    void garbageCode103() { // #6824
        ASSERT_THROW(checkCode("a f(r) int * r; { { int s[2]; [f(s); if () ]  } }"), InternalError);
    }

    void garbageCode104() { // #6847
        ASSERT_THROW(checkCode("template < Types > struct S {> ( S < ) S >} { ( ) { } } ( ) { return S < void > ( ) } { ( )> >} { ( ) { } } ( ) { ( ) }"), InternalError);
    }

    void garbageCode105() { // #6859
        ASSERT_THROW(checkCode("void foo (int i) { int a , for (a 1; a( < 4; a++) if (a) (b b++) (b);) n++; }"), InternalError);
    }

    void garbageCode106() { // #6880
        ASSERT_THROW(checkCode("[ ] typedef typedef b_array b_array_ref [ ; ] ( ) b_array_ref b_array_ref_gbl_obj0 { ; { b_array_ref b_array_ref_gbl_obj0 } }"), InternalError);
    }

    void garbageCode107() { // #6881
        TODO_ASSERT_THROW(checkCode("enum { val = 1{ }; { const} }; { } Bar { const int A = val const } ;"), InternalError);
    }

    void garbageCode108() { //  #6895 "segmentation fault (invalid code) in CheckCondition::isOppositeCond"
        ASSERT_THROW(checkCode("A( ) { } bool f( ) { ( ) F; ( ) { ( == ) if ( !=< || ( !A( ) && r[2] ) ) ( !A( ) ) ( ) } }"), InternalError);
    }

    void garbageCode109() { //  #6900 "segmentation fault (invalid code) in CheckStl::runSimplifiedChecks"
        checkCode("( *const<> (( ) ) { } ( *const ( ) ( ) ) { } ( * const<> ( size_t )) ) { } ( * const ( ) ( ) ) { }");
    }

    void garbageCode110() { //  #6902 "segmentation fault (invalid code) in CheckStl::string_c_str"
        ASSERT_THROW(checkCode("( *const<> ( size_t ) ; foo ) { } * ( *const ( size_t ) ( ) ;> foo )< { }"), InternalError);
    }

    void garbageCode111() { //  #6907
        TODO_ASSERT_THROW(checkCode("enum { FOO = 1( ,) } {{ FOO }} ;"), InternalError);
    }

    void garbageCode112() { //  #6909
        TODO_ASSERT_THROW(checkCode("enum { FOO = ( , ) } {{ }}>> enum { FOO< = ( ) } { { } } ;"), InternalError);
    }

    void garbageCode114() { // #2118
        checkCode("Q_GLOBAL_STATIC_WITH_INITIALIZER(Qt4NodeStaticData, qt4NodeStaticData, {\n"
                  "    for (unsigned i = 0 ; i < count; i++) {\n"
                  "    }\n"
                  "});");
    }

    void garbageCode115() { // #5506
        ASSERT_THROW(checkCode("A template < int { int = -1 ; } template < int N > struct B { int [ A < N > :: zero ] ;  } ; B < 0 > b ;"), InternalError);
    }

    void garbageCode116() { // #5356
        ASSERT_THROW(checkCode("struct template<int { = }; > struct B { }; B < 0 > b;"), InternalError);
    }

    void garbageCode117() { // #6121
        TODO_ASSERT_THROW(checkCode("enum E { f = {} };\n"
                                    "int a = f;"), InternalError);
    }

    void garbageCode118() { // #5600 - missing include causes invalid enum
        ASSERT_THROW(checkCode("enum {\n"
                               "    NUM_OPCODES =\n"
                               // #include "definition"
                               "};\n"
                               "struct bytecode {};\n"
                               "jv jq_next() { opcode = ((opcode) +NUM_OPCODES);\n"
                               "}"), InternalError);
    }

    void garbageCode119() { // #5598
        checkCode("{ { void foo() { struct }; template <typename> struct S { Used x; void bar() } auto f = [this] { }; } };");
    }

    void garbageCode120() { // #4927
        checkCode("int main() {\n"
                  "   return 0\n"
                  "}");
        ASSERT_EQUALS("", errout.str());
    }

    void garbageCode121() { // #2585
        ASSERT_THROW(checkCode("abcdef?" "?<"
                               "123456?" "?>"
                               "+?" "?="), InternalError);
    }

    void garbageCode122() { // #6303
        checkCode("void foo() {\n"
                  "char *a = malloc(10);\n"
                  "a[0]\n"
                  "}");
    }

    void garbageCode123() {
        checkCode("namespace pr16989 {\n"
                  "    class C {\n"
                  "        C tpl_mem(T *) { return }\n"
                  "    };\n"
                  "}");
    }

    void garbageCode125() {
        ASSERT_THROW(checkCode("{ T struct B : T valueA_AA ; } T : [ T > ( ) { B } template < T > struct A < > : ] { ( ) { return valueA_AC struct { : } } b A < int > AC ( ) a_aa.M ; ( ) ( ) }"), InternalError);
        ASSERT_THROW(checkCode("template < Types > struct S :{ ( S < ) S >} { ( ) { } } ( ) { return S < void > ( ) }"),
                     InternalError);
    }

    void garbageCode126() {
        ASSERT_THROW(checkCode("{ } float __ieee754_sinhf ( float x ) { float t , , do { gf_u ( jx ) { } ( 0 ) return ; ( ) { } t } ( 0x42b17180 ) { } }"),
                     InternalError);
    }

    void garbageCode127() { // #6667
        checkCode("extern \"C\" int printf(const char* fmt, ...);\n"
                  "class A {\n"
                  "public:\n"
                  "  int Var;\n"
                  "  A(int arg) { Var = arg; }\n"
                  "  ~A() { printf(\"A d'tor\\n\"); }\n"
                  "};\n"
                  " const A& foo(const A& arg) { return arg; }\n"
                  " foo(A(12)).Var");
    }

    void garbageCode128() {
        TODO_ASSERT_THROW(checkCode("enum { FOO = ( , ) } {{ }} enum {{ FOO << = } ( ) } {{ }} ;"),
                          InternalError);
    }

    void garbageCode129() {
        ASSERT_THROW(checkCode("operator - ( { } typedef typename x ; ( ) ) { ( { { ( ( ) ) } ( { } ) } ) }"),
                     InternalError);
    }

    void garbageCode130() {
        TODO_ASSERT_THROW(checkCode("enum { FOO = ( , ){ } { { } } { { FOO} = } ( ) } { { } } enumL\" ( enumL\" { { FOO } ( ) } { { } } ;"),
                          InternalError);
    }

    void garbageCode131() {
        ASSERT_THROW(checkCode("( void ) { ( ) } ( ) / { ( ) }"), InternalError);
        // actually the invalid code should trigger an syntax error...
    }

    void garbageCode132() { // #7022
        ASSERT_THROW(checkCode("() () { } { () () ({}) i() } void i(void(*ptr) ()) { ptr(!) () }"), InternalError);
    }

    void garbageCode133() {
        ASSERT_THROW(checkCode("void f() {{}"), InternalError);

        ASSERT_THROW(checkCode("void f()) {}"), InternalError);

        ASSERT_THROW(checkCode("void f()\n"
                               "{\n"
                               " foo(;\n"
                               "}\n"), InternalError);

        ASSERT_THROW(checkCode("void f()\n"
                               "{\n"
                               " for(;;){ foo();\n"
                               "}\n"), InternalError);

        ASSERT_THROW(checkCode("void f()\n"
                               "{\n"
                               " a[10;\n"
                               "}\n"), InternalError);

        {
            const char code[] = "{\n"
                                "   a(\n" // <- error
                                "}\n"
                                "{\n"
                                "   b());\n"
                                "}\n";
            ASSERT_EQUALS("[test.cpp:2] Unmatched '('. Configuration: ''.", getSyntaxError(code));
        }

        {
            const char code[] = "void f() {\n"
                                "   int x = 3) + 0;\n" // <- error: unmatched )
                                "}\n";
            ASSERT_EQUALS("[test.cpp:2] Unmatched ')'. Configuration: ''.", getSyntaxError(code));
        }

        {
            const char code[] = "void f() {\n"
                                "   int x = (3] + 0;\n" // <- error: unmatched ]
                                "}\n";
            ASSERT_EQUALS("[test.cpp:2] Unmatched ']'. Configuration: ''.", getSyntaxError(code));
        }

        {
            const char code[] = "void f() {\n" // <- error: unmatched {
                                "   {\n"
                                "}\n";
            ASSERT_EQUALS("[test.cpp:1] Unmatched '{'. Configuration: ''.", getSyntaxError(code));
        }
    }

    void garbageCode134() {
        // Ticket #5605, #5759, #5762, #5774, #5823, #6059
        ASSERT_THROW(checkCode("foo() template<typename T1 = T2 = typename = unused, T5 = = unused> struct tuple Args> tuple<Args...> { } main() { foo<int,int,int,int,int,int>(); }"), InternalError);
        ASSERT_THROW(checkCode("( ) template < T1 = typename = unused> struct Args { } main ( ) { foo < int > ( ) ; }"), InternalError);
        ASSERT_THROW(checkCode("() template < T = typename = x > struct a {} { f <int> () }"), InternalError);
        ASSERT_THROW(checkCode("template < T = typename = > struct a { f <int> }"), InternalError);
        checkCode("struct S { int i, j; }; "
                  "template<int S::*p, typename U> struct X {}; "
                  "X<&S::i, int> x = X<&S::i, int>(); "
                  "X<&S::j, int> y = X<&S::j, int>();");
        checkCode("template <typename T> struct A {}; "
                  "template <> struct A<void> {}; "
                  "void foo(const void* f = 0) {}");
        checkCode("template<typename... T> struct A { "
                  "  static const int s = 0; "
                  "}; "
                  "A<int> a;");
        checkCode("template<class T, class U> class A {}; "
                  "template<class T = A<int, int> > class B {}; "
                  "template<class T = B<int> > class C { "
                  "    C() : _a(0), _b(0) {} "
                  "    int _a, _b; "
                  "};");
        checkCode("template<class... T> struct A { "
                  "  static int i; "
                  "}; "
                  "void f() { A<int>::i = 0; }");
    }

    void garbageCode135() { // #4994
        checkCode("long f () {\n"
                  "  return a >> extern\n"
                  "}\n"
                  "long a = 1 ;\n"
                  "long b = 2 ;");
    }

    void garbageCode136() { // #7033
        ASSERT_THROW(checkCode("{ } () { void f() { node_t * n; for (; -n) {} } } { }"),
                     InternalError);
    }

    void garbageCode137() { // #7034
        ASSERT_THROW(checkCode("\" \" typedef signed char f; \" \"; void a() { f * s = () &[]; (; ) (; ) }"), InternalError);
    }

    void garbageCode138() { // #6660
        checkCode("CS_PLUGIN_NAMESPACE_BEGIN(csparser)\n"
                  "{\n"
                  "    struct foo\n"
                  "    {\n"
                  "      union\n"
                  "      {};\n"
                  "    } halo;\n"
                  "}\n"
                  "CS_PLUGIN_NAMESPACE_END(csparser)");
    }

    void garbageCode139() { // #6659 heap user after free: kernel: sm750_accel.c
        ASSERT_THROW(checkCode("void hw_copyarea() {\n"
                               "   de_ctrl = (nDirection == RIGHT_TO_LEFT) ?\n"
                               "    ( (0 & ~(((1 << (1 - (0 ? DE_CONTROL_DIRECTION))) - 1) << (0 ? DE_CONTROL_DIRECTION))) )\n"
                               "    : 42;\n"
                               "}"), InternalError);
    }

    void garbageCode140() { // #7035
        ASSERT_THROW(checkCode("int foo(int align) { int off(= 0 % align;  return off) ? \\ align - off  :  0;  \\ }"), InternalError);
    }

    void garbageCode141() { // #7043
        TODO_ASSERT_THROW(checkCode("enum { X = << { X } } enum { X = X } = X ;"), InternalError);
    }

    void garbageCode142() { // #7050
        checkCode("{ } (  ) { void mapGraphs ( ) { node_t * n ; for (!oid n ) { } } } { }");
    }

    void garbageCode143() { // #6922
        ASSERT_THROW(checkCode("void neoProgramShadowRegs() {\n"
                               "    int i;\n"
                               "    Bool noProgramShadowRegs;\n"
                               "    if (noProgramShadowRegs) {\n"
                               "    } else {\n"
                               "        switch (nPtr->NeoPanelWidth) {\n"
                               "        case 1280:\n"
                               "            VGAwCR(0x64,0x?? );\n"
                               "        }\n"
                               "    }\n"
                               "}"), InternalError);
    }

    void garbageCode144() { // #6865
        ASSERT_THROW(checkCode("template < typename > struct A { } ; template < typename > struct A < INVALID > : A < int[ > { }] ;"), InternalError);
    }

    void garbageCode146() { // #7081
        ASSERT_THROW(checkCode("void foo() {\n"
                               "    ? std::cout << pow((, 1) << std::endl;\n"
                               "    double <ip = NUO ip) << std::end;\n"
                               "}"), InternalError);
    }

    void garbageCode147() { // #7082
        ASSERT_THROW(checkCode("free(3();\n"
                               "$  vWrongAllocp1) test1<int, -!>() ^ {\n"
                               "    int *p<ynew int[n];\n"
                               "    delete[]p;\n"
                               "    int *p1 = (int*)malloc(n*sizeof(int));\n"
                               "    free(p1);\n"
                               "}\n"
                               "void est2() {\n"
                               "    for (int ui = 0; ui < 1z; ui++)\n"
                               "        ;\n"
                               "}"), InternalError);

        ASSERT_THROW(checkCode("; void f ^ { return } int main ( ) { }"), InternalError); // #4941
    }

    void garbageCode148() { // #7090
        ASSERT_THROW(checkCode("void f_1() {\n"
                               "    typedef S0 b[][1][1] != 0\n"
                               "};\n"
                               "b[K][0] S0 b[][1][1] != 4{ 0 };\n"
                               "b[0][0]"), InternalError);
    }

    void garbageCode149() { // #7085
        checkCode("int main() {\n"
                  "    for (j = 0; j < 1; j)\n"
                  "        j6;\n"
                  "}");
    }

    void garbageCode150() { // #7089
        ASSERT_THROW(checkCode("class A {\n"
                               "    pl vFoo() {\n"
                               "        A::\n"
                               "    };\n"
                               "    A::\n"
                               "}\n"), InternalError);
    }

    void garbageCode151() { // #4911 - bad simplification => don't crash
        checkCode("void f() {\n"
                  "    int a;\n"
                  "    do { a=do_something() } while (a);\n"
                  "}");
    }

    void garbageCode152() { // happened in travis, originally from llvm clang code
        const char* code = "template <bool foo = std::value &&>\n"
                           "static std::string foo(char *Bla) {\n"
                           "    while (Bla[1] && Bla[1] != ',') }\n";
        checkCode(code);
    }

    void garbageCode153() {
        TODO_ASSERT_THROW(checkCode("enum { X = << { X } } { X X } enum { X = << { ( X ) } } { } X */"), InternalError);
    }

    void garbageCode154() {
        checkCode("\"abc\"[];");
    }

    void garbageCode156() { // #7120
        ASSERT_THROW(checkCode("struct {}a; d f() { c ? : } {}a.p"), InternalError);
    }

    void garbageCode157() { // #7131
        ASSERT_THROW(checkCode("namespace std {\n"
                               "  template < typename >\n"
                               "  void swap();\n"
                               "}"
                               "template std::swap\n"), InternalError);
    }

    void garbageCode158() { // #3238
        checkCode("__FBSDID(\"...\");");
    }

    void garbageCode159() { // #7119
        ASSERT_THROW(checkCode("({}typedef typename x;typename x!){({{}()})}"), InternalError);
    }

    void garbageCode160() { // #7190
        ASSERT_THROW(checkCode("f(a,b,c,d)float [  a[],d;int ]  b[],c;{} "), InternalError); // don't hang
    }


    void garbageCodeFuzzerClientMode1() {
        ASSERT_THROW(checkCode("void f() { x= name2 & name3 name2 = | 0.1 , | 0.1 , | 0.1 name4 <= >( ); }"), InternalError);
        ASSERT_THROW(checkCode("void f() { x = , * [ | + 0xff | > 0xff]; }"), InternalError);
        ASSERT_THROW(checkCode("void f() {  x = , | 0xff , 0.1 < ; }"), InternalError);
        ASSERT_THROW(checkCode("void f() { x = [ 1 || ] ; }"), InternalError);
        ASSERT_THROW(checkCode("void f1() { x = name6 1 || ? name3 [  ( 1 || +) ] ; }"), InternalError);
    }

    void garbageValueFlow() {
        // #6089
        const char* code = "{} int foo(struct, x1, struct x2, x3, int, x5, x6, x7)\n"
                           "{\n"
                           "    (foo(s, , 2, , , 5, , 7)) abort()\n"
                           "}\n";
        ASSERT_THROW(checkCode(code), InternalError);

        // 6122 survive garbage code
        code = "; { int i ; for ( i = 0 ; = 123 ; ) - ; }";
        ASSERT_THROW(checkCode(code), InternalError);

        code = "void f1() { for (int n = 0 n < 10 n++); }";
        checkCode(code);
    }

    void garbageSymbolDatabase() {
        checkCode("void f( { u = 1 ; } ) { }");

        ASSERT_THROW(checkCode("{ }; void namespace A::f; { g() { int } }"), InternalError);

        ASSERT_THROW(checkCode("class Foo {}; class Bar : public Foo"), InternalError);

        checkCode("YY_DECL { switch (yy_act) {\n"
                  "    case 65: YY_BREAK\n"
                  "    case YY_STATE_EOF(block):\n"
                  "        yyterminate();\n"
                  "} }"); // #5663
    }

    void garbageAST() {
        ASSERT_THROW(checkCode("N 1024 float a[N], b[N + 3], c[N]; void N; (void) i;\n"
                               "int #define for (i = avx_test i < c[i]; i++)\n"
                               "b[i + 3] = a[i] * {}"), InternalError); // Don't hang (#5787)

        checkCode("START_SECTION([EXTRA](bool isValid(const String &filename)))"); // Don't crash (#5991)

        // #8352
        ASSERT_THROW(checkCode("else return % name5 name2 - =name1 return enum | { - name3 1 enum != >= 1 >= ++ { { || "
                               "{ return return { | { - name3 1 enum != >= 1 >= ++ { name6 | ; ++}}}}}}}"), InternalError);
        ASSERT_THROW(checkCode("else return % name5 name2 - =name1 return enum | { - name3 1 enum != >= 1 >= ++ { { || "
                               "{ return return { | { - name3 1 enum != >= 1 >= ++ { { || ; ++}}}}}}}}"), InternalError);
    }

    void templateSimplifierCrashes() {
        checkCode( // #5950
            "struct A {\n"
            "  template <class T> operator T*();\n"
            "};\n"
            "\n"
            "template <> A::operator char*(){ return 0; } // specialization\n"
            "\n"
            "int main() {\n"
            "  A a;\n"
            "  int *ip = a.operator int*();\n"
            "}\n"
            "\n"
            "namespace PR5742 {\n"
            "  template <class T> struct A { };\n"
            "  struct S {\n"
            "    template <class T> operator T();\n"
            "  } s;\n"
            "  void f() {\n"
            "    s.operator A<A<int> >();\n"
            "  }\n"
            "}");

        checkCode( // #6034
            "template<template<typename...> class T, typename... Args>\n"
            "struct foo<T<Args...> > {\n"
            "    const bool value = true;\n"
            "};\n"
            "\n"
            "template<int I>\n"
            "struct int_\n"
            "{};\n"
            "\n"
            "int main() {\n"
            "  foo<int_<0> >::value;\n"
            "}");

        checkCode( // #6117
            "template <typename ...> struct something_like_tuple\n"
            "{};\n"
            "template <typename, typename> struct is_last {\n"
            "  static const bool value = false;\n"
            "};\n"
            "template <typename T, template <typename ...> class Tuple, typename ... Head>\n"
            "struct is_last<T, Tuple<Head ..., T>>\n"
            "{\n"
            "  static const bool value = true;\n"
            "};\n"
            "\n"
            "#define SA(X) static_assert (X, #X)\n"
            "\n"
            "typedef something_like_tuple<char, int, float> something_like_tuple_t;\n"
            "SA ((is_last<float, something_like_tuple_t>::value == false));\n"
            "SA ((is_last<int, something_like_tuple_t>::value == false));");

        checkCode( // #6225
            "template <typename...>\n"
            "void templ_fun_with_ty_pack() {}\n"
            "\n"
            "namespace PR20047 {\n"
            "        template <typename T>\n"
            "        struct A {};\n"
            "        using AliasA = A<T>;\n"
            "}");

        // #3449
        ASSERT_EQUALS("template < typename T > struct A ;\n"
                      "struct B { template < typename T > struct C } ;\n"
                      "{ } ;",
                      checkCode("template<typename T> struct A;\n"
                                "struct B { template<typename T> struct C };\n"
                                "{};"));
    }
    void garbageCode161() {
        //7200
        ASSERT_THROW(checkCode("{ }{ if () try { } catch (...)} B : : ~B { }"), InternalError);
    }

    void garbageCode162() {
        //7208
        ASSERT_THROW(checkCode("return <<  >>  x return <<  >>  x ", false), InternalError);
    }

    void garbageCode163() {
        //7228
        ASSERT_THROW(checkCode("typedef s f[](){typedef d h(;f)}", false), InternalError);
    }

    void garbageCode164() {
        //7234
        ASSERT_THROW(checkCode("class d{k p;}(){d::d():B<()}"), InternalError);
    }

    void garbageCode165() {
        //7235
        ASSERT_THROW(checkCode("for(;..)", false),InternalError);
    }

    void garbageCode167() {
        //7237
        ASSERT_THROW(checkCode("class D00i000{:D00i000::}i"),InternalError);
    }

    void garbageCode168() {
        // 7246
        checkCode("long foo(void) { return *bar; }", false);
    }

    void garbageCode169() {
        // 6713
        ASSERT_THROW(checkCode("( ) { ( ) ; { return } switch ( ) i\n"
                               "set case break ; default: ( ) }", false), InternalError);
    }

    void garbageCode170() {
        // 7255
        ASSERT_THROW(checkCode("d i(){{f*s=typeid(()0,)}}", false), InternalError);
    }

    void garbageCode171() {
        // 7270
        ASSERT_THROW(checkCode("(){case()?():}:", false), InternalError);
    }

    void garbageCode172() {
        // #7357
        ASSERT_THROW(checkCode("p<e T=l[<]<>>,"), InternalError);
    }

    void garbageCode173() {
        // #6781  heap corruption ;  TemplateSimplifier::simplifyTemplateInstantiations
        ASSERT_THROW(checkCode(" template < Types > struct S : >( S < ...Types... > S <) > { ( ) { } } ( ) { return S < void > ( ) }"), InternalError);
    }

    void garbageCode174() { // #7356
        ASSERT_THROW(checkCode("{r e() { w*constD = (())D = cast< }}"), InternalError);
    }

    void garbageCode175() { // #7027
        ASSERT_THROW(checkCode("int f() {\n"
                               "  int i , j;\n"
                               "  for ( i = t3 , i < t1 ; i++ )\n"
                               "    for ( j = 0 ; j < = j++ )\n"
                               "        return t1 ,\n"
                               "}"), InternalError);
    }

    void garbageCode176() { // #7527
        checkCode("class t { { struct } enum class f : unsigned { q } b ; operator= ( T ) { switch ( b ) { case f::q: } } { assert ( b ) ; } } { ; & ( t ) ( f::t ) ; } ;");
    }

    void garbageCode181() {
        ASSERT_THROW(checkCode("int test() { int +; }"), InternalError);
    }

    // #4195 - segfault for "enum { int f ( ) { return = } r = f ( ) ; }"
    void garbageCode182() {
        ASSERT_THROW(checkCode("enum { int f ( ) { return = } r = f ( ) ; }"), InternalError);
    }
    // #7505 - segfault
    void garbageCode183() {
        ASSERT_THROW(checkCode("= { int } enum return { r = f() f(); }"), InternalError);
    }

    void garbageCode184() { // #7699
        ASSERT_THROW(checkCode("unsigned int AquaSalSystem::GetDisplayScreenCount() {\n"
                               "    NSArray* pScreens = [NSScreen screens];\n"
                               "    return pScreens ? [pScreens count] : 1;\n"
                               "}"), InternalError);
    }

    void garbageCode185() { // #6011 crash in libreoffice failure to create proper AST
        checkCode(
            "namespace binfilter\n"
            "{\n"
            "       BOOL EnhWMFReader::ReadEnhWMF()\n"
            "       {\n"
            "               pOut->CreateObject( nIndex, GDI_BRUSH, new WinMtfFillStyle( ReadColor(), ( nStyle == BS_HOLLOW ) ? TRUE : FALSE ) );\n"
            "               return bStatus;\n"
            "       };\n"
            "}");
    }

    // #8151 - segfault due to incorrect template syntax
    void garbageCode186() {
        ASSERT_THROW(checkCode("A<B<><>C"), InternalError);
    }

    void garbageCode187() { // # 8152 - segfault in handling
        const std::string inp("0|\0|0>;\n", 8);
        ASSERT_THROW(checkCode(inp), InternalError);

        checkCode("template<class T> struct S : A< B<T> || C<T> > {};"); // No syntax error: #8390
        checkCode("static_assert(A<x> || B<x>, ab);");
    }

    void garbageCode188() { // #8255
        ASSERT_THROW(checkCode("{z r(){(){for(;<(x);){if(0==0)}}}}"), InternalError);
    }

    void garbageCode189() { // #8317
        checkCode("t&n(){()()[](){()}}$");
    }

    void garbageCode190() { // #8307
        ASSERT_THROW(checkCode("void foo() {\n"
                               "    int i;\n"
                               "    i *= 0;\n"
                               "    !i <;\n"
                               "}"),
                     InternalError);
    }

    void garbageCode191() { // #8333
        ASSERT_THROW(checkCode("struct A { int f(const); };"), InternalError);
        ASSERT_THROW(checkCode("struct A { int f(int, const, char); };"), InternalError);
        ASSERT_THROW(checkCode("struct A { int f(struct); };"), InternalError);

        // The following code is valid and should not trigger any error
        checkCode("struct A { int f ( char ) ; } ;");
    }

    void garbageCode192() { // #8386 (segmentation fault)
        ASSERT_THROW(checkCode("{(()[((0||0xf||))]0[])}"), InternalError);
    }

    // #8740
    void garbageCode193() {
        ASSERT_THROW(checkCode("d f(){!=[]&&0()!=0}"), InternalError);
    }

    // #8384
    void garbageCode194() {
        ASSERT_THROW(checkCode("{((()))(return 1||);}"), InternalError);
    }

    // #8709 - no garbage but to avoid stability regression
    void garbageCode195() {
        checkCode("a b;\n"
                  "void c() {\n"
                  "  switch (d) { case b:; }\n"
                  "  double e(b);\n"
                  "  if(e <= 0) {}\n"
                  "}");
    }

    // #8265
    void garbageCode196() {
        ASSERT_THROW(checkCode("0|,0<<V"), InternalError);
        ASSERT_THROW(checkCode(";|4|<0;"), InternalError);
    }

    // #8385
    void garbageCode197() {
        ASSERT_THROW(checkCode("(){e break,{(case)|{e:[()]}}}"), InternalError);
    }

    // #8383
    void garbageCode198() {
        ASSERT_THROW(checkCode("void f(){\n"
                               "x= ={(continue continue { ( struct continue { ( ++ name5 name5 ) ( name5 name5 n\n"
                               "ame5 ( name5 struct ( name5 name5 < ) ) ( default ) { name4 != name5 name5 name5\n"
                               " ( name5 name5 name5 ( { 1 >= void { ( ()) } 1 name3 return >= >= ( ) >= name5 (\n"
                               " name5 name6 :nam00 [ ()])}))})})})};\n"
                               "}"), InternalError);
    }

    // #8752
    void garbageCode199() {
        checkCode("d f(){e n00e0[]n00e0&" "0+f=0}");
    }

    // #8757
    void garbageCode200() {
        ASSERT_THROW(checkCode("(){e break,{(case)!{e:[]}}}"), InternalError);
    }

    // #8873
    void garbageCode201() {
        ASSERT_THROW(checkCode("void f() { std::string s=\"abc\"; return s + }"), InternalError);
    }

    // #8907
    void garbageCode202() {
        ASSERT_THROW(checkCode("void f() { UNKNOWN_MACRO(return); }"), InternalError);
        ASSERT_THROW(checkCode("void f() { UNKNOWN_MACRO(throw); }"), InternalError);
    }

    void garbageCode203() { // #8972
        checkCode("{ > () {} }");
        checkCode("template <> a > ::b();");
    }

    void garbageCode204() {
        ASSERT_THROW(checkCode("template <a, = b<>()> c; template <a> a as() {} as<c<>>();"), InternalError);
    }

    void garbageCode205() {
        checkCode("class CodeSnippetsEvent : public wxCommandEvent {\n"
                  "public :\n"
                  "    CodeSnippetsEvent ( wxEventType commandType =  wxEventType , int id = 0 ) ;\n"
                  "    CodeSnippetsEvent ( const CodeSnippetsEvent & event ) ;\n"
                  "virtual wxEvent * Clone ( ) const { return new CodeSnippetsEvent ( * this ) ; }\n"
                  "private :\n"
                  "    int m_SnippetID ;\n"
                  "} ;\n"
                  "const  wxEventType wxEVT_CODESNIPPETS_GETFILELINKS  =  wxNewEventType  (  )\n"
                  "CodeSnippetsEvent :: CodeSnippetsEvent ( wxEventType commandType , int id )\n"
                  ": wxCommandEvent ( commandType , id ) {\n"
                  "}\n"
                  "CodeSnippetsEvent :: CodeSnippetsEvent ( const CodeSnippetsEvent & Event )\n"
                  ": wxCommandEvent ( Event )\n"
                  ", m_SnippetID ( 0 ) {\n"
                  "}"); // don't crash
    }

    void garbageCode206() {
        ASSERT_EQUALS("[test.cpp:1] syntax error: operator", getSyntaxError("void foo() { for (auto operator new : int); }"));
        ASSERT_EQUALS("[test.cpp:1] syntax error: operator", getSyntaxError("void foo() { for (a operator== :) }"));
    }

    void garbageCode207() { // #8750
        ASSERT_THROW(checkCode("d f(){(.n00e0(return%n00e0''('')));}"), InternalError);
    }

    void garbageCode208() { // #8753
        ASSERT_THROW(checkCode("d f(){(for(((((0{t b;((((((((()))))))))}))))))}"), InternalError);
    }

    void garbageCode209() { // #8756
        ASSERT_THROW(checkCode("{(- -##0xf/-1 0)[]}"), InternalError);
    }

    void garbageCode210() { // #8762
        ASSERT_THROW(checkCode("{typedef typedef c n00e0[]c000(;n00e0&c000)}"), InternalError);
    }

    void garbageCode211() { // #8764
        ASSERT_THROW(checkCode("{typedef f typedef[]({typedef e e,>;typedef(((typedef<typedef|)))})}"), InternalError);
    }

    void garbageCode212() { // #8765
        ASSERT_THROW(checkCode("{(){}[]typedef r n00e0[](((n00e0 0((;()))))){(0 typedef n00e0 bre00 n00e0())}[]();typedef n n00e0()[],(bre00)}"), InternalError);
    }

    void garbageCode213() { // #8758
        ASSERT_THROW(checkCode("{\"\"[(1||)];}"), InternalError);
    }

    void garbageCode214() {
        checkCode("THIS FILE CONTAINS VARIOUS TEXT");
    }

    void garbageCode215() { // daca@home script with extension .c
        ASSERT_THROW(checkCode("a = [1,2,3];"), InternalError);
    }

    void garbageCode216() { // #7884
        checkCode("template<typename> struct A {};\n"
                  "template<typename...T> struct A<T::T...> {}; \n"
                  "A<int> a;");
    }

    void garbageCode217() { // #10011
        ASSERT_THROW(checkCode("void f() {\n"
                               "    auto p;\n"
                               "    if (g(p)) {}\n"
                               "    assert();\n"
                               "}"), InternalError);
    }

    void garbageCode218() { // #8763
        checkCode("d f(){t n0000 const[]n0000+0!=n0000,(0)}"); // don't crash
    }
    void garbageCode219() { // #10101
        checkCode("typedef void (*func) (addr) ;\n"
                  "void bar(void) {\n"
                  "    func f;\n"
                  "    f & = (func)42;\n"
                  "}\n"); // don't crash
    }

    void syntaxErrorFirstToken() {
        ASSERT_THROW(checkCode("&operator(){[]};"), InternalError); // #7818
        ASSERT_THROW(checkCode("*(*const<> (size_t); foo) { } *(*const (size_t)() ; foo) { }"), InternalError); // #6858
        ASSERT_THROW(checkCode(">{ x while (y) z int = }"), InternalError); // #4175
        ASSERT_THROW(checkCode("&p(!{}e x){({(0?:?){({})}()})}"), InternalError); // #7118
        ASSERT_THROW(checkCode("<class T> { struct { typename D4:typename Base<T*> }; };"), InternalError); // #3533
        ASSERT_THROW(checkCode(" > template < . > struct Y < T > { = } ;\n"), InternalError); // #6108
    }

    void syntaxErrorLastToken() {
        ASSERT_THROW(checkCode("int *"), InternalError); // #7821
        ASSERT_THROW(checkCode("x[y]"), InternalError); // #2986
        ASSERT_THROW(checkCode("( ) &"), InternalError);
        ASSERT_THROW(checkCode("|| #if #define <="), InternalError); // #2601
        ASSERT_THROW(checkCode("f::y:y : <x::"), InternalError); // #6613
        ASSERT_THROW(checkCode("a \"b\" not_eq \"c\""), InternalError); // #6720
        ASSERT_THROW(checkCode("(int arg2) { } { } typedef void (func_type) (int, int); typedef func_type&"), InternalError); // #6738
        ASSERT_THROW(checkCode("&g[0]; { (g[0] 0) } =", false), InternalError); // #6744
        ASSERT_THROW(checkCode("{ { void foo() { struct }; { }; } }; struct S { } f =", false), InternalError); // #6753
        ASSERT_THROW(checkCode("{ { ( ) } P ( ) ^ { } { } { } ( ) } 0"), InternalError); // #6772
        ASSERT_THROW(checkCode("+---+"), InternalError); // #6948
        ASSERT_THROW(checkCode("template<>\n"), InternalError);
        ASSERT_THROW(checkCode("++4++ +  + E++++++++++ + ch " "tp.oed5[.]"), InternalError); // #7074
        ASSERT_THROW(checkCode("d a(){f s=0()8[]s?():0}*()?:0", false), InternalError); // #7236
        ASSERT_THROW(checkCode("!2 : #h2 ?:", false), InternalError); // #7769
        ASSERT_THROW(checkCode("--"), InternalError);
        ASSERT_THROW(checkCode("volatile true , test < test < #ifdef __ppc__ true ,"), InternalError); // #4169
        ASSERT_THROW(checkCode("a,b--\n"), InternalError); // #2847
        ASSERT_THROW(checkCode("x a[0] ="), InternalError); // #2682
        ASSERT_THROW(checkCode("auto_ptr<x>\n"), InternalError); // #2967
        ASSERT_THROW(checkCode("char a[1]\n"), InternalError); // #2865
        ASSERT_THROW(checkCode("<><<"), InternalError); // #2612
        ASSERT_THROW(checkCode("z<y<x>"), InternalError); // #2831
        ASSERT_THROW(checkCode("><,f<i,"), InternalError); // #2835
        ASSERT_THROW(checkCode("0; (a) < (a)"), InternalError); // #2875
        ASSERT_THROW(checkCode(" ( * const ( size_t ) ; foo )"), InternalError); // #6135
        ASSERT_THROW(checkCode("({ (); strcat(strcat(() ()) ()) })"), InternalError); // #6686
        ASSERT_THROW(checkCode("%: return ; ()"), InternalError); // #3441
        ASSERT_THROW(checkCode("__attribute__((destructor)) void"), InternalError); // #7816
        ASSERT_THROW(checkCode("1 *p = const"), InternalError); // #3512
        ASSERT_THROW(checkCode("sizeof"), InternalError); // #2599
        ASSERT_THROW(checkCode(" enum struct"), InternalError); // #6718
        ASSERT_THROW(checkCode("{(){(())}}r&const"), InternalError); // #7321
        ASSERT_THROW(checkCode("int"), InternalError);
        ASSERT_THROW(checkCode("struct A :\n"), InternalError); // #2591
        ASSERT_THROW(checkCode("{} const const\n"), InternalError); // #2637
        ASSERT_THROW(checkCode("re2c: error: line 14, column 4: can only difference char sets"), InternalError);

        // ASSERT_THROW(  , InternalError)
    }

    void syntaxErrorCase() {
        // case must be inside switch block
        ASSERT_THROW(checkCode("void f() { switch (a) {}; case 1: }"), InternalError); // #8184
        ASSERT_THROW(checkCode("struct V : { public case {} ; struct U : U  void { V *f (int x) (x) } }"), InternalError); // #5120
        ASSERT_THROW(checkCode("void f() { 0 0; }"), InternalError);
        ASSERT_THROW(checkCode("void f() { true 0; }"), InternalError);
        ASSERT_THROW(checkCode("void f() { 'a' 0; }"), InternalError);
        ASSERT_THROW(checkCode("void f() { 1 \"\"; }"), InternalError);
    }

    void syntaxErrorFuzzerCliType1() {
        ASSERT_THROW(checkCode("void f(){x=0,return return''[]()}"), InternalError);
        ASSERT_THROW(checkCode("void f(){x='0'++'0'(return)[];}"), InternalError); // #9063
        checkCode("void f(){*(int *)42=0;}"); // no syntax error
        ASSERT_THROW(checkCode("void f() { x= 'x' > typedef name5 | ( , ;){ } (); }"), InternalError); // #9067
        ASSERT_THROW(checkCode("void f() { x= {}( ) ( 'x')[ ] (); }"), InternalError); // #9068
        ASSERT_THROW(checkCode("void f() { x= y{ } name5 y[ ] + y ^ name5 ^ name5 for ( ( y y y && y y y && name5 ++ int )); }"), InternalError); // #9069
    }

    void cliCode() {
        // #8913
        ASSERT_NO_THROW(checkCode(
                            "public ref class LibCecSharp : public CecCallbackMethods {\n"
                            "array<CecAdapter ^> ^ FindAdapters(String ^ path) {}\n"
                            "bool GetDeviceInformation(String ^ port, LibCECConfiguration ^configuration, uint32_t timeoutMs) {\n"
                            "bool bReturn(false);\n"
                            "}\n"
                            "};"));
    }

    void enumTrailingComma() {
        ASSERT_THROW(checkCode("enum ssl_shutdown_t {ssl_shutdown_none = 0,ssl_shutdown_close_notify = , } ;"), InternalError); // #8079
    }

    void nonGarbageCode1() {
        checkCode("template <class T> class List {\n"
                  "public:\n"
                  "   List();\n"
                  "   virtual ~List();\n"
                  "   template< class Predicate > u_int DeleteIf( const Predicate &pred );\n"
                  "};\n"
                  "template< class T >\n"
                  "template< class Predicate > int\n"
                  "List<T>::DeleteIf( const Predicate &pred )\n"
                  "{}");

        // #8749
        checkCode(
            "struct A {\n"
            "    void operator+=(A&) && = delete;\n"
            "};");

        // #8788
        checkCode(
            "struct foo;\n"
            "void f() {\n"
            "    auto fn = []() -> foo* { return new foo(); };\n"
            "}");
    }
};

REGISTER_TEST(TestGarbage)
