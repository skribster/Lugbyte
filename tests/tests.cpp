#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "luggage.hpp"

namespace filesystem = std::filesystem;

/**
 * This function reads a file and returns an std::string with 
 * its contents.
 */
std::string readFile(const filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


/**
 * This function gets a list of all Luggage scripts in a given directory.
 */
std::vector<filesystem::path> getLuggageScripts(filesystem::path directory) {
    std::vector<filesystem::path> scripts;

    /*
        We wil loop through the luggage test directory and add all
        the files that end in .lug to our scripts list.
    */
    for (const auto& entry : filesystem::recursive_directory_iterator(directory)) {
        if (entry.path().extension() == ".lug") {
            scripts.push_back(entry.path());
        }
    }

    return scripts;
}


/**
 * Our name generator function that we will use to generate a name for
 * each test for each .lug function.
 */
std::string getTestName(const testing::TestParamInfo<filesystem::path>& info) {
    /*
        Since tests cannot have slashes in them, we will replace them with
        an underscore '_'. We also strip away the file extension (.lug) 
        so it just leaves us with the bare name.
    */

    filesystem::path relative = filesystem::relative(info.param, LUG_TEST_DIR);
    relative.replace_extension("");

    std::string name = relative.generic_string();
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return name;
}


/**
 * This is our fixture we will use for the test.
 */
class LugScriptTest : public testing::TestWithParam<filesystem::path> {};


/**
 * This is the test we will be using for every single .lug file.
 * TEST_P stands for parameterized, meaning that this test has a 
 * parameter which is just the file path of the .lug file.
 */
TEST_P(LugScriptTest, TestSingleFile) {
    // Our fixture has one parameter, which is just the path of the 
    // current test's file.
    filesystem::path path = GetParam();
    std::string source = readFile(path);

    // Expected output is stored as an .out file with the same
    // name as the .lug file.
    path.replace_extension(".out");
    std::string expected = readFile(path);

    // Capture anything the script prints
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    Luggage::VM vm;
    vm.LoadLibrary(Luggage::LibInclude::ALL);
    Luggage::InterpretResult success = vm.Interpret(source, "tests");
    
    std::string output = testing::internal::GetCapturedStdout();
    if (success != Luggage::InterpretResult::OK) {
        output += vm.LastError().message;
    }

    if (expected == "[[ignoreoutput]]") {
        std::cout << output;
        return;
    }

    EXPECT_TRUE(output == expected) 
        << path 
        << " failed."
        << "\n========  Output  ======== \n" 
        << output
        << "\n======== Expected =======\n"
        << expected
        << "\n========   ENDS   =======\n"
    ;
}


/*
    To actually pass in all the files, we will use a generator function
    of testing::ValuesIn. It's basically just a list of all the scripts
    we will use, and it will run TEST_P for each of those tests.
*/
INSTANTIATE_TEST_SUITE_P(
    Scripts,                                            // Instantiation name
    LugScriptTest,                                      // Test suite name (defined in TEST_P)
    testing::ValuesIn(getLuggageScripts(LUG_TEST_DIR)), // A GTest function (testing::Values, testing::Range) that yields the values for the test
    getTestName                                         // Name generator
);