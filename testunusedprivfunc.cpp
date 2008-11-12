/*
 * c++check - c/c++ syntax checking
 * Copyright (C) 2007 Daniel Marjamäki
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */


#include "tokenize.h"
#include "CheckClass.h"
#include "testsuite.h"

#include <sstream>

extern std::ostringstream errout;
extern bool ShowAll;

class TestUnusedPrivateFunction : public TestFixture
{
public:
    TestUnusedPrivateFunction() : TestFixture("TestUnusedPrivateFunction")
    { }

private:
    void run()
    {
        TEST_CASE( test1 );

        // [ 2236547 ] False positive --style unused function, called via pointer
        TEST_CASE( func_pointer );
    }


    void check( const char code[] )
    {
        // Tokenize..
        tokens = tokens_back = NULL;
        std::istringstream istr(code);
        Tokenizer tokenizer;
        tokenizer.TokenizeCode( istr );

        // Clear the error buffer..
        errout.str("");

        // Check for unused private functions..
        CheckClass checkClass( &tokenizer );
        checkClass.CheckUnusedPrivateFunctions();

        tokenizer.DeallocateTokens();
    }



    void test1()
    {
        check( "class Fred\n"
               "{\n"
               "private:\n"
               "    unsigned int f();\n"
               "public:\n"
               "    Fred();\n"
               "};\n"
               "\n"
               "Fred::Fred()\n"
               "{ }\n"
               "\n"
               "unsigned int Fred::f()\n"
               "{ }\n" );

        ASSERT_EQUALS( std::string("Class 'Fred', unused private function: 'f'\n"), errout.str() );
    }






    void func_pointer()
    {
        check( "class Fred\n"
               "{\n"
               "private:\n"
               "    typedef void (*testfp)();\n"
               "\n"
               "    testfp get()\n"
               "    {\n"
               "        return test;\n"
               "    }\n"
               "\n"
               "    static void test()\n"
               "    { }\n"
               "\n"
               "public:\n"
               "    Fred();\n"
               "};\n"
               "\n"
               "Fred::Fred()\n"
               "{}\n" );

        std::string str( errout.str() );

        ASSERT_EQUALS( std::string("Class 'Fred', unused private function: 'get'\n"), str );
    }



};

REGISTER_TEST( TestUnusedPrivateFunction )

