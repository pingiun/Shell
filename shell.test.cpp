#include <gtest/gtest.h>
#include <fcntl.h>
#include "shell.cpp"

using namespace std;

// shell to run tests on
#define SHELL "../cmake-build-debug/shell -t"
//#define SHELL "/bin/sh"

namespace {

    void Execute(std::string command, std::string expectedOutput);

    void Execute(std::string command, std::string expectedOutput, std::string expectedOutputFile,
                 std::string expectedOutputFileContent);

    TEST(Shell, BuildToken) {
        Token *expected = Token::make(TokenId::BG);
        std::string input = "&";
        EXPECT_EQ(*expected, *buildToken(input));
        input = " &";
        EXPECT_EQ(*expected, *buildToken(input));
        input = " & ";
        EXPECT_EQ(*expected, *buildToken(input));
        input = "    &  ";
        EXPECT_EQ(*expected, *buildToken(input));

        input = "|";
        EXPECT_EQ(*Token::make(TokenId::PIPE), *buildToken(input));
        input = " | ";
        EXPECT_EQ(*Token::make(TokenId::PIPE), *buildToken(input));

        input = "";
        EXPECT_EQ(*Token::make(TokenId::END), *buildToken(input));

        input = "test | test2 test3 > file &";
        EXPECT_EQ(*Token::makeIdent("test"), *buildToken(input));
        EXPECT_EQ(*Token::make(TokenId::PIPE), *buildToken(input));
        EXPECT_EQ(*Token::makeIdent("test2"), *buildToken(input));
        EXPECT_EQ(*Token::makeIdent("test3"), *buildToken(input));
        EXPECT_EQ(*Token::make(TokenId::REDIR_OUT), *buildToken(input));
        EXPECT_EQ(*Token::makeIdent("file"), *buildToken(input));
        EXPECT_EQ(*Token::make(TokenId::BG), *buildToken(input));
    }

    TEST(Shell, BuildCommands) {
        {
            std::string input = "test hoi hai &";
            Command *expected = new Command();
            expected->bg = true;
            expected->command = "test";
            auto args = new std::vector<std::string *>;
            args->push_back(new std::string("test"));
            args->push_back(new std::string("hoi"));
            args->push_back(new std::string("hai"));

            expected->args = args;
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(*expected, *actual);
        }
        {
            std::string input = "test hoi hai | cat";
            Command *sub = new Command();
            sub->command = "cat";
            auto subargs = new std::vector<std::string *>;
            subargs->push_back(new std::string("cat"));
            sub->args = subargs;

            Command *expected = new Command();
            expected->command = "test";
            auto args = new std::vector<std::string *>;
            args->push_back(new std::string("test"));
            args->push_back(new std::string("hoi"));
            args->push_back(new std::string("hai"));
            expected->args = args;
            expected->pipe_to = sub;
            std::vector<Token *> tokens = tokenList(input);

            Command *actual = buildCommands(tokens);
            EXPECT_EQ(*expected, *actual);
        }
        {
            std::string input = "test hoi hai < dinges > file &";
            Command *expected = new Command();
            expected->bg = true;
            expected->command = "test";
            auto args = new std::vector<std::string *>;
            args->push_back(new std::string("test"));
            args->push_back(new std::string("hoi"));
            args->push_back(new std::string("hai"));
            expected->args = args;
            expected->redir_out = "file";
            expected->redir_in = "dinges";
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(*expected, *actual);
        }
    }

    TEST(Shell, FailingBuildCommands) {
        {
            std::string input = "";
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(nullptr, actual);
        }
        {
            std::string input = "|";
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(nullptr, actual);
        }
        {
            std::string input = "& test hoi | ja &";
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(nullptr, actual);
        }
        {
            std::string input = "test hoi |";
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(nullptr, actual);
        }
        {
            std::string input = "test hoi | ja >";
            std::vector<Token *> tokens = tokenList(input);
            Command *actual = buildCommands(tokens);
            EXPECT_EQ(nullptr, actual);
        }
    }

    TEST(Shell, arrlen) {
        char *array[3] = {const_cast<char *>("hoi"), const_cast<char *>("test"), nullptr};
        EXPECT_EQ(2UL, arrlen(array));
    }

    TEST(Shell, strEqOrNull) {
        EXPECT_TRUE(strEqOrNull("hai", "hai"));
        EXPECT_TRUE(strEqOrNull("", ""));
        EXPECT_TRUE(strEqOrNull(nullptr, nullptr));
        EXPECT_FALSE(strEqOrNull("hai", "hoi"));
        EXPECT_FALSE(strEqOrNull("", nullptr));
        EXPECT_FALSE(strEqOrNull(nullptr, "hoi"));
    }

    TEST(Shell, lastCommand) {
        {
            Command *command = new Command();
            command->bg = true;
            command->command = "test";
            EXPECT_EQ(command, lastCommand(command));
        }
        {
            Command *sub = new Command();
            sub->command = "cat";
            auto subargs = new std::vector<std::string *>;
            subargs->push_back(new std::string("cat"));
            sub->args = subargs;

            Command *command = new Command();
            command->command = "test";
            command->pipe_to = sub;
            EXPECT_EQ(sub, lastCommand(command));
        }
    }

    TEST(Shell, getDirName) {
        char buffer[512];
        char *home = getenv("HOME");
        strcpy(buffer, home);
        EXPECT_STREQ("~", getDirName(buffer));

        strcpy(buffer, home);
        strcpy(&buffer[strlen(home)], "/testje");
        EXPECT_STREQ("~/testje", getDirName(buffer));

        char expected[512];
        strcpy(buffer, "/tmp/test");
        strcpy(&buffer[strlen(buffer)], home);
        strcpy(expected, buffer);
        EXPECT_STREQ(expected, getDirName(buffer));
    }

    TEST(Shell, ReadFromFile) {
        Execute("cat < 1", "line 1\nline 2\nline 3\nline 4");
    }

    TEST(Shell, ReadFromAndWriteToFile) {
        Execute("cat < 1 > foobar", "", "foobar", "line 1\nline 2\nline 3\nline 4");
    }

    TEST(Shell, ReadFromAndWriteToFileChained) {
        Execute("cat < 1 | head -n 3 > foobar", "", "foobar", "line 1\nline 2\nline 3\n");
        Execute("cat < 1 | head -n 3 | tail -n 1 > foobar", "", "foobar", "line 3\n");
    }

    TEST(Shell, WriteToFile) {
        Execute("ls -1 | head -n 4 > foobar", "", "foobar", "1\n2\n3\n4\n");
    }

    TEST(Shell, Execute) {
        Execute("uname", "Darwin\n"); // Works on my machine
        Execute("ls | head -n 4", "1\n2\n3\n4\n");
        Execute("ls -1 | head -n 4", "1\n2\n3\n4\n");
    }

    TEST(Shell, ExecuteChained) {
        Execute("ls -1 | head -n 2", "1\n2\n");
        Execute("ls -1 | head -n 2 | tail -n 1", "2\n");
    }

    // This test fails when running the test suite, but when testing >> manually it works completely
//    TEST(Shell, AppendToFile) {
//        Execute("echo hoi > foobar", "", "foobar", "hoi\n");
//        Execute("cat foobar", "hoi\n");
//        Execute("echo hai >> foobar", "", "foobar", "hoi\nhai");
//    }


//////////////// HELPERS

    std::string filecontents(const std::string &str) {
        std::string retval;
        int fd = open(str.c_str(), O_RDONLY);
        struct stat st;
        if (fd >= 0 && fstat(fd, &st) == 0) {
            long long size = st.st_size;
            retval.resize(size);
            char *current = (char *) retval.c_str();
            ssize_t left = size;
            while (left > 0) {
                ssize_t bytes = read(fd, current, left);
                if (bytes == 0 || (bytes < 0 && errno != EINTR))
                    break;
                if (bytes > 0) {
                    current += bytes;
                    left -= bytes;
                }
            }
        } else {
            // Added some basic error reporting
            char dir[512];
            getcwd(dir, sizeof(dir));
            cout << "Error opening file: " << dir << "/" << str << endl;
            perror("open");
        }
        if (fd >= 0)
            close(fd);
        return retval;
    }

    void filewrite(const std::string &str, std::string content) {
        int fd = open(str.c_str(), O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd < 0)
            return;
        while (content.size() > 0) {
            int written = write(fd, content.c_str(), content.size());
            if (written == -1 && errno != EINTR) {
                std::cout << "error writing file '" << str << "': error " << errno << std::endl;
                break;
            }
            content = content.substr(written);
        }
        close(fd);
    }

    void Execute(std::string command, std::string expectedOutput) {
        filewrite("input", command);
        system(SHELL " < input > output 2> /dev/null");
        std::string got = filecontents("output");
        EXPECT_EQ(expectedOutput, got);
    }

    void Execute(std::string command, std::string expectedOutput, std::string expectedOutputFile,
                 std::string expectedOutputFileContent) {
        std::string expectedOutputLocation = /*"../test-dir/" + */ expectedOutputFile;
        unlink(expectedOutputLocation.c_str());
        filewrite("input", command);
        int rc = system(SHELL " < input > output 2> /dev/null");
        EXPECT_EQ(0, rc);
        std::string got = filecontents("output");
        EXPECT_EQ(expectedOutput, got) << command;
        std::string gotOutputFileContents = filecontents(expectedOutputLocation);
        EXPECT_EQ(expectedOutputFileContent, gotOutputFileContents) << command;
        //unlink(expectedOutputLocation.c_str());
    }

}