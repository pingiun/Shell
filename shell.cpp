/**
 * Implementation of a limited shell in C++
 *
 * This shell implementation uses a lexer and parser to parse command lines. A recursive datatype was chosen for the
 * commands. This makes the parser a recursive function, and the executor too. The recursive functions make for a
 * beautiful program flow because less state has to be saved, only a file descriptor and command have to be passed
 * through.
 *
 * The parser design has been largely influenced by http://thinkingeek.com/gcc-tiny/. I understand that a parser such
 * as implemented in this shell is more complicated than needed for the easy syntax. However using this parser, it was
 * really easy to add the >> operator, and even more syntax elements could be easily added.
 *
 * Jelle Besseling (s4743636)
 */

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <map>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * UnkownCommandException is thrown whenever a command is executed that can't be found
 */
class UnkownCommandException : public std::exception {

} UnkownCommandException;

/**
 * TokenId represents the token type found in the input string.
 * IDENT: Identifier, a command or file for example
 * PIPE: Literal pipe character
 * REDIR_IN: Input redirection character (<)
 * REDIR_OUT: Output redirection character (>)
 * APPEND_OUT: Output redirection with append (>>)
 * BG: Run as background character (&)
 * END: End of the string
 */
enum TokenId {
    IDENT,
    PIPE,
    REDIR_IN,
    REDIR_OUT,
    APPEND_OUT,
    BG,
    END,
};

/**
 * Implementation of the Token type, supports a string value for identifiers.
 */
struct Token {
private:
    TokenId token_id;
    std::string *str;

    explicit Token(TokenId token_id_)
            : token_id(token_id_), str(nullptr) {}

    explicit Token(TokenId token_id_, const std::string &str_)
            : token_id(token_id_), str(new std::string(str_)) {}

    Token();

public:
    /**
     * Convenience method for creating a non-identifier
     * @param id The token to create
     * @return A newly created Token struct
     */
    static Token *make(TokenId id) {
        return new Token(id);
    }

    /**
     * Convenience method for creating an identifier
     * @param str The string to attach to the identifier
     * @return A newly created Token struct
     */
    static Token *makeIdent(const std::string &str) {
        return new Token(TokenId::IDENT, str);
    }

    bool operator==(const Token &rhs) const {
        if (str == nullptr) {
            return token_id == rhs.token_id && rhs.str == nullptr;
        }
        if (rhs.str == nullptr) {
            return token_id == rhs.token_id;
        }
        return token_id == rhs.token_id &&
               *str == *rhs.str;
    }

    bool operator!=(const Token &rhs) const {
        return !(rhs == *this);
    }

    TokenId get_id() {
        return this->token_id;
    }

    std::string *get_str() const {
        return this->str;
    }
};

/**
 * Consumes a bit of the input string and return the token that it represents
 */
Token *buildToken(std::string &input) {
    for (;;) {
        if (input.empty()) {
            return Token::make(TokenId::END);
        }
        int current_char = input.front();
        input.erase(input.begin());
        switch (current_char) {
            case ' ':
                continue;
            case '>':
                if (input.front() == '>') {
                    input.erase(input.begin());
                    return Token::make(TokenId::APPEND_OUT);
                } else {
                    return Token::make(TokenId::REDIR_OUT);
                }
            case '<':
                return Token::make(TokenId::REDIR_IN);
            case '|':
                return Token::make(TokenId::PIPE);
            case '&':
                return Token::make(TokenId::BG);
            default:
                std::string str;
                str += static_cast<char>(current_char);
                size_t found = input.find_first_of(" ><|&");
                if (found == std::string::npos) {
                    str += input;
                    input.erase(input.begin(), input.end());
                    return Token::makeIdent(str);
                }
                str += input.substr(0, found);
                input = input.erase(0, found);
                return Token::makeIdent(str);
        }
    }
}

/**
 * Measures the length of a NULL terminated array
 * @param array
 * @return length of array
 */
size_t arrlen(char **array) {
    size_t count = 0;
    while (*(array++) != nullptr) {
        count++;
    }
    return count;
}

/**
 * String equality but supports NULL pointers
 * @param x
 * @param y
 * @return true if the strings are equal
 */
bool strEqOrNull(const char *x, const char *y) {
    if (x == nullptr && y == nullptr) {
        return true;
    }
    return x != nullptr && y != nullptr && strcmp(x, y) == 0;
}

/**
 * Implementation of the Command struct
 *
 * Uses a recursive structure, if the commandline has multiple commands chained with pipes, the Command struct will
 * have a child Command in pipe_to. This means the executeCommand function can also be executed recursively.
 *
 * The command itself is stored in a char* so it can be easily passed to execvp.
 * The command arguments are stored in a vector so C++ methods can be used for simplicity.
 * bg flag is set when the command is proceeded by an ampersand. This is ignored if the command isn't the last command.
 * append flag is set when redir_out is an appending file redirection
 * redir_in and redir_out are set to filenames when input and output redirection are used, otherwise they are NULL
 */
struct Command {
    const char *command;
    std::vector<std::string *> *args;
    bool bg;
    bool append;
    const char *redir_in;
    const char *redir_out;

    Command *pipe_to;

    /**
     * Constructor for the empty command
     */
    explicit Command() : command(nullptr), args(nullptr), bg(false), redir_in(nullptr), redir_out(nullptr),
                         pipe_to(nullptr) {}

    bool operator==(const Command &rhs) const {
        if (!strEqOrNull(command, rhs.command)) {
            return false;
        }
        if (!strEqOrNull(redir_in, rhs.redir_in)) {
            return false;
        }
        if (!strEqOrNull(redir_out, rhs.redir_out)) {
            return false;
        }
        if (bg != rhs.bg) {
            return false;
        }
        if (append != rhs.append) {
            return false;
        }
        if (args->size() != rhs.args->size()) {
            return false;
        }
        for (size_t i = 0; i < args->size(); i++) {
            if (args[i] == rhs.args[i]) {
                return false;
            }
        }
        if ((pipe_to == nullptr && rhs.pipe_to != nullptr) || (pipe_to != nullptr && rhs.pipe_to == nullptr)) {
            return false;
        }
        if (pipe_to == nullptr && rhs.pipe_to == nullptr) {
            return true;
        }
        return *pipe_to == *rhs.pipe_to;
    }

    bool operator!=(const Command &rhs) const {
        return !(rhs == *this);
    }
};

/**
 * Repeatedly calls buildToken to convert the input string to a vector of tokens
 * @param commandLine input string
 * @return vector of tokens
 */
std::vector<Token *> tokenList(std::string &commandLine) {
    std::vector<Token *> token_list;
    Token *cur_token = buildToken(commandLine);

    while (cur_token->get_id() != TokenId::END) {
        token_list.push_back(cur_token);
        cur_token = buildToken(commandLine);
    }
    return token_list;
}

/**
 * Converts a list of tokens to the recursive Command struct
 * @param tokens vector of tokens
 * @return the root command with optional subcommands
 */
Command *buildCommands(std::vector<Token *> tokens) {
    if (tokens.empty()) {
        return nullptr;
    }
    auto *command = new Command;
    Token *cur_token = tokens.front();
    tokens.erase(tokens.begin());
    if (cur_token->get_id() != TokenId::IDENT) {
        return nullptr;
    }
    for (;;) {
        switch (cur_token->get_id()) {
            case TokenId::BG:
                command->bg = true;
                break;
            case TokenId::REDIR_IN: {
                if (tokens.empty()) {
                    return nullptr;
                }
                Token *peek = tokens.front();
                if (peek->get_id() != TokenId::IDENT) {
                    return nullptr; // File is an ident
                }
                command->redir_in = peek->get_str()->c_str();
                tokens.erase(tokens.begin());
                break;
            }
            case TokenId::APPEND_OUT: {
                command->append = true;
                // Fall through
            }
            case TokenId::REDIR_OUT: {
                if (tokens.empty()) {
                    return nullptr;
                }
                Token *peek = tokens.front();
                if (peek->get_id() != TokenId::IDENT) {
                    return nullptr; // File is an ident
                }
                command->redir_out = peek->get_str()->c_str();
                tokens.erase(tokens.begin());
                break;
            }
            case TokenId::PIPE:
                command->pipe_to = buildCommands(tokens);
                if (command->pipe_to == nullptr) {
                    return nullptr;
                }
                return command;
            case TokenId::END:
                return command;
            case TokenId::IDENT: {
                Token *peek = tokens.front();
                command->command = cur_token->get_str()->c_str();
                auto args = new std::vector<std::string *>;
                args->push_back(cur_token->get_str());
                while (peek->get_id() == TokenId::IDENT) {
                    if (tokens.empty()) {
                        break;
                    }
                    tokens.erase(tokens.begin());
                    args->push_back(peek->get_str());
                    peek = tokens.front();
                }
                command->args = args;
                break;
            }
        }
        if (tokens.empty() && command->command == nullptr) {
            return nullptr;
        }
        if (tokens.empty()) {
            return command;
        }
        cur_token = tokens.front();
        tokens.erase(tokens.begin());
    }
}

/**
 * Tries to execute command as a builtin
 * @param command the command to try to execute
 * @return true if the command was executed as a builtin
 */
bool executeBuiltin(Command *command) {
    if (command->pipe_to != nullptr) {
        return false;
    }
    if (strcmp(command->command, "exit") == 0) {
        exit(0);
    }
    std::vector<std::string *> &args = *(command->args);
    if (args.size() == 2 && strcmp(command->command, "cd") == 0) {
        if (chdir(args[1]->c_str()) < 0) {
            perror("cd");
        }
        return true;
    }
    return false;
}

/**
 * Calls itself recursively until the last command, needs to be called by executeCommand(Command)
 * @param command command to execute
 * @param input output end from previous pipe
 * @return output end from current pipe
 */
int executeCommand(Command *command, int input) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    std::vector<std::string *> &args = *(command->args);
    auto **c_args = new char *[args.size() + 2];
    for (size_t i = 1; i < args.size(); i++)
        c_args[i] = strdup(args[i]->c_str());
    c_args[0] = strdup(command->command);
    c_args[args.size()] = nullptr;

    pid_t child_pid = fork();
    if (child_pid == 0) {
        dup2(input, STDIN_FILENO);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);

        execvp(command->command, c_args);

        for (size_t i = 0; i < args.size(); ++i)
            free(c_args[i]);
        delete[] c_args;

        throw UnkownCommandException;
    } else {
        close(pipefd[1]);
        if (command->pipe_to == nullptr) {
            return pipefd[0];
        } else {
            return executeCommand(command->pipe_to, pipefd[0]);
        }
    }
}

/**
 * @param pCommand some command in a chain of commands
 * @return the last command of a recursive chain of commands
 */
Command *lastCommand(Command *pCommand) {
    while (pCommand->pipe_to != nullptr) {
        pCommand = pCommand->pipe_to;
    }
    return pCommand;
}

/**
 * This function recursively calls executeCommand(Command, int), it also correctly redirects the first and last commands
 * to stdin and stdout or file redirects.
 * @param command the command to execute
 */
void executeCommand(Command *command) {
    int output;
    char buf;
    if (command->redir_in != nullptr) {
        int inputfile = open(command->redir_in, O_RDONLY);
        if (inputfile == -1) {
            perror("open");
            return;
        }
        output = executeCommand(command, inputfile);
    } else {
        output = executeCommand(command, STDIN_FILENO);
    }
    Command *last_command = lastCommand(command);
    pid_t child = fork();
    if (child == 0) {
        if (last_command->redir_out != nullptr) {
            int append = last_command->append ? O_APPEND : O_TRUNC; // Truncate the file if not appending
            int outputfile = open(last_command->redir_out, O_WRONLY | O_CREAT | append, S_IRUSR | S_IWUSR);
            if (outputfile == -1) {
                perror("open");
            } else {
                while (read(output, &buf, 1) > 0)
                    write(outputfile, &buf, 1);
            }
        } else {
            while (read(output, &buf, 1) > 0)
                write(STDOUT_FILENO, &buf, 1);
        }
        exit(0);
    }
    if (!command->bg) {
        waitpid(child, NULL, 0);
    } else {
        printf("No need to wait");
    }
}

/**
 * Builds the dir name for the prompt, replacing user home with ~
 * @param dir current dir
 * @return current dir, but home replaced
 */
char *getDirName(char *dir) {
    char *home = getenv("HOME");
    char *found = strstr(dir, home);
    if (found == nullptr) {
        return dir;
    }
    found = found + strlen(home) - 1;
    found[0] = '~';
    return found;
}

/**
 * Show the prompt with dir name, plus # if the user is root, $ otherwise
 */
void displayPrompt() {
    char buffer[512];
    char *dir = getcwd(buffer, sizeof(buffer));
    if (dir) {
        // the strings starting with '\e' are escape codes,
        // that the terminal application interpets in this case as "set color to green"/"set color to default"
        std::cout << "\e[32m" << getDirName(dir) << "\e[39m";
    }
    if (getuid() == 0) {
        std::cout << "# ";
    } else {
        std::cout << "$ ";
    }
    std::flush(std::cout);
}

/**
 * Show the prompt if showPrompt and get a command input line from stdin
 * @param showPrompt
 * @return command input line
 */
std::string requestCommandLine(bool showPrompt) {
    if (showPrompt)
        displayPrompt();
    std::string retval;
    getline(std::cin, retval);
    return retval;
}

/**
 * Main loop of the shell
 * @param showPrompt set to false if the prompt shouldn't be shown, only one command will be executed
 * @return 0
 */
int shell(bool showPrompt) {
    do {
        std::string commandLine = requestCommandLine(showPrompt);
        if (commandLine == "") {
            continue;
        }
        std::vector<Token *> tokens = tokenList(commandLine);
        Command *command = buildCommands(tokens);
        if (command == nullptr) {
            std::cerr << "Error in command syntax" << std::endl;
            continue;
        }
        if (executeBuiltin(command)) {
            continue;
        }
        try {
            executeCommand(command);
        } catch (class UnkownCommandException &e) {
            std::cerr << "shell: command not found\n" << std::endl;
            exit(0);
        }
    } while (showPrompt);
    return 0;
}
