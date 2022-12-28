// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <memory>
#include <regex>
#include <fnmatch.h>
#include <cstdlib>
#include <cassert>
#include <fcntl.h>

#include <readline/readline.h>
#include <boost/filesystem.hpp>
#include <unordered_set>

#include "builtin_parsers/builtin_parser.h"
#include "myshell_exit_codes.h"
#include "myshell_exceptions.h"
#include "myshell_commands.h"
#include "builtin_parsers/merrno_parser.h"
#include "builtin_parsers/mexport_parser.h"
#include "builtin_parsers/mexit_parser.h"
#include "builtin_parsers/mpwd_parser.h"
#include "builtin_parsers/mecho_parser.h"
#include "builtin_parsers/mcd_parser.h"
#include "builtin_parsers/dot_parser.h"

namespace fs = boost::filesystem;
using cmd_args = std::vector<std::string>;

static int exit_status = 0;


int close_wrapper(int fd) {
    int status;
    while (true) {
        status = close(fd);
        if (status == -1) {
            if (errno != EINTR) {
                perror("close");
                return -1;
            }
        }
        else return status;
    }
}


static int open_wrapper(const char *pathname, int flags, mode_t mode) {
    int fd;
    while (true) {
        fd = open(pathname, flags, mode);
        if (fd == -1) {
            if (errno != EINTR) {
                perror("open");
                return -1;
            }
        }
        else return fd;
    }
}

/**
 * Duplicate fd. The resulting fd will have FD_CLOEXEC set.
 */
int dup_wrapper(int oldfd) {
    int fd;
    while (true) {
        fd = dup(oldfd);
        if (fd == -1) {
            if (errno != EINTR) {
                perror("dup");
                return -1;
            }
        } else break;
    }
    while (true) {
        int status = fcntl(fd, F_SETFD, FD_CLOEXEC);
        if (status == -1) {
            if (errno != EINTR) {
                perror("fcntl");
                return -1;
            }
        } else break;
    }
    return fd;
}

int dup2_wrapper(int oldfd, int newfd) {
    int fd;
    while (true) {
        fd = dup2(oldfd, newfd);
        if (fd == -1) {
            if (errno != EINTR) {
                perror("dup");
                return -1;
            }
        }
        else return fd;
    }
}

int pipe_wrapper(int pipefd[2], int flag) {
    int status = pipe(pipefd);
    if (status == 0){
        fcntl(pipefd[0], flag, FD_CLOEXEC);
        fcntl(pipefd[1], flag, FD_CLOEXEC);
    }
    return status;

}

static int link_fd_to_stdout(int fd) {
    if (fd == STDOUT_FILENO)
        return STDOUT_FILENO;
    int dup_stdout = dup_wrapper(STDOUT_FILENO);
    dup2_wrapper(fd, STDOUT_FILENO);
    return dup_stdout;
}

static int undo_link_fd_to_stdout(int old_stdout) {
    if (old_stdout == STDOUT_FILENO)
        return 1;
    return dup2_wrapper(old_stdout, STDOUT_FILENO);
}

/**
 * Run builtin command if it is a builtin command.
 * @param vector of strings - arguments (with command name included)
 * @return whether the command was a built-in command
 */
bool run_builtin_command(std::vector<std::string> &tokens, int fd_out) {
    std::unique_ptr<builtin_parser_t> builtinParser;
    const std::string &command = tokens[0];
    try {
        if (command == "merrno") {
            builtinParser = std::make_unique<merrno_parser_t>();
        } else if (command == "mexport") {
            builtinParser = std::make_unique<mexport_parser_t>();
        } else if (command == "mexit") {
            builtinParser = std::make_unique<mexit_parser_t>();
        } else if(command == "mpwd") {
            builtinParser = std::make_unique<mpwd_parser_t>();
        } else if(command == "mecho") {
            builtinParser = std::make_unique<mecho_parser_t>();
        } else if(command == "mcd") {
            builtinParser = std::make_unique<mcd_parser_t>();
        } else if(command == ".") {
            builtinParser = std::make_unique<dot_parser_t>();
        } else {
            return false;
        }
        builtinParser->setup_description();
        std::vector<std::string> arguments(tokens.begin() + 1, tokens.end());
        builtinParser->parse(arguments);
    } catch (std::exception &ex) {
        std::cerr << ex.what() << std::endl;
        exit_status = ExitCodes::EOTHER;
        return true;
    }

    int dup_stdout = link_fd_to_stdout(fd_out);

    if (builtinParser->get_help_flag()) {
        builtinParser->print_help_message();
        exit_status = 0;
        undo_link_fd_to_stdout(dup_stdout);
        return true;
    }

    if (command == "merrno") {
        std::cout << exit_status << std::endl;
        exit_status = 0;
    } else if (command == "mexport") {
        mexport_parser_t &merrno_parser = dynamic_cast<mexport_parser_t &>(*builtinParser);
        const std::vector<std::string> &assignments = merrno_parser.get_assignments();

        for (const std::string &assignment : assignments) { // TODO: check without '='
            const auto str_eq = assignment.find_first_of('=');
            if (str_eq == std::string::npos) continue;

            std::string varname = assignment.substr(0, str_eq);
            std::string val = assignment.substr(str_eq + 1, assignment.size());
            int status = setenv(varname.c_str(), val.c_str(), 1);
            if (status == -1) {
                perror("Failed to set variable");
                exit_status = EFAILSET;
            }
            val.clear();
        }
        exit_status = 0;
    } else if(command == "mexit") {
        mexit_parser_t &mexit_parser = dynamic_cast<mexit_parser_t &>(*builtinParser);
        exit(mexit_parser.get_code());
    } else if(command == "mpwd") {
        std::cout << fs::current_path().string() << std::endl;
        exit_status = 0;
    } else if(command == "mecho") {
        mecho_parser_t &mecho_parser = dynamic_cast<mecho_parser_t &>(*builtinParser);
        const std::vector<std::string> &texts = mecho_parser.get_texts();
        for (size_t i = 0; i < texts.size() - 1; ++i) {
            std::cout << texts[i] << " ";
        }
        std::cout << texts.back() << std::endl;
        exit_status = 0;
    } else if(command == "mcd") {
        mcd_parser_t &mcd_parser = dynamic_cast<mcd_parser_t &>(*builtinParser);
        const std::string &path = mcd_parser.get_path();
        if (std::filesystem::is_directory(path)) {
            std::filesystem::current_path(path);
        }
        else if(path == "~") {
            auto path_ptr = getenv("HOME");
            if (path_ptr != nullptr)
                std::filesystem::current_path(path_ptr);
        }
        else{
            exit_status = ENOTADIR;
            std::cerr << "mcd: not a directory: " << path << std::endl;
        }
    } else if(command == ".") {
        dot_parser_t &dot_parser = dynamic_cast<dot_parser_t &>(*builtinParser);
        const std::string &path = dot_parser.get_path();
        if(std::filesystem::is_regular_file(path)) {
            std::ifstream script_file(path);
            try {
                exec_shell_lines(script_file);
            } catch (std::exception &ex) {
                std::cerr << ex.what() << '\n';
                exit_status = EXIT_FAILURE;
            }
        }
        else {
            std::cerr << "coud not find necessary script" << std::endl;
            exit_status = ENOTASCRIPT;
        }
    } else {
        assert(false && "This should not execute");
    }
    undo_link_fd_to_stdout(dup_stdout);
    return true;
}

std::string remove_spaces(const std::string &com_line) {
    // remove leading spaces and comment
    auto str_begin = com_line.find_first_not_of(' ');
    if (str_begin == std::string::npos) {
        return "";
    }
    auto str_end = com_line.find_first_of('#');
    if (str_end == std::string::npos) {
        str_end = com_line.size();
    }
    auto str_range = str_end - str_begin;
    std::string clean_com_line = com_line.substr(str_begin, str_range);

    // remove multiple spaces
    size_t pos;
    while((pos = clean_com_line.find("  ")) != std::string::npos)
    {
        clean_com_line.replace(pos, 2, " ");
    }

    return clean_com_line;
}

std::vector<std::string> get_matched_filenames(std::string value) {
    std::vector<std::string> matched_filenames;

    fs::path wildc_file_path{value};

    // set searching path
    fs::path wildc_parent_path{"."};
    if (wildc_file_path.has_parent_path()) {
        wildc_parent_path = wildc_file_path.parent_path();
    }

    // check for existence
    if (!fs::exists(wildc_parent_path)) {
        matched_filenames.push_back(value);
        return matched_filenames;
    }

    // iterate over path
    std::string wildc_filename = wildc_file_path.filename().string();
    fs::path cur_file_path;
    for (const auto &entry: fs::directory_iterator(wildc_parent_path)) {
        // compare file name and regex
        cur_file_path = entry.path();

        int res;
        res = fnmatch(wildc_filename.c_str(), cur_file_path.filename().c_str(), FNM_PATHNAME | FNM_PERIOD);

        if (res == 0) {
            matched_filenames.push_back(cur_file_path.string());
        } else if (res != FNM_NOMATCH) {
            std::string error_str{strerror(errno)};
            throw fnmatch_error{"Error in globbing: " + error_str};
        }
    }

    return matched_filenames;
}

std::string expand_variables(std::string value) {
    auto var_begin = value.find_first_of('$');
    if (var_begin != std::string::npos) {
        var_begin++;

        // replace env variables
        auto var_ptr = getenv(value.substr(var_begin, value.size() - var_begin).c_str());
        std::string var_val;
        if (var_ptr != nullptr) {
            var_val = var_ptr;
        }
        value = value.substr(0, var_begin - 1) + var_val;
    }

    return value;
}

std::string readout_fd(int fd) {
    const size_t BUF_SIZE = 16384;
    std::string buf_str(BUF_SIZE, '\0');
    char *buf = buf_str.data();

    std::string file_content;

    ssize_t num_read;
    while ((num_read = read(fd, buf, BUF_SIZE))) {
        if (num_read == -1) {
            if (errno == EINTR)
                continue;
            perror("read");
            return file_content;
        } else {
            file_content.append(buf, num_read);
        }

    }
    return file_content;
}

static void replace_whitespaces_with_spaces(std::string &str) {
    for (char &ch : str) {
        if (std::isspace(ch)) {
            ch = ' ';
        }
    }
}

static void substitute_commands(std::string &com_line) {
    size_t begin = 0;

    while ((begin = com_line.find("$(", begin)) != std::string::npos) {
        size_t end = com_line.find(')', begin);
        if (end == std::string::npos) {
            break;
        }
        std::string subcommand = com_line.substr(begin + 2, end - begin - 2);

        int pfd[2];
        pipe_wrapper(pfd, F_SETFD);

        int stdout_dup = dup_wrapper(STDOUT_FILENO);

        dup2(pfd[1], STDOUT_FILENO);
        close_wrapper(pfd[1]);

        exec_shell_line(subcommand);
        dup2_wrapper(stdout_dup, STDOUT_FILENO);
        close_wrapper(stdout_dup);

        std::string content = readout_fd(pfd[0]);
        replace_whitespaces_with_spaces(content);
        close_wrapper(pfd[0]);
        com_line.replace(begin, end - begin + 1, content);
        begin += com_line.size();
    }
}

//TODO: look through https://www.gnu.org/software/bash/manual/html_node/Shell-Operation.html
std::vector<std::string> parse_com_line(const std::string &com_line) {
    std::vector<std::string> args;
    std::string fd = "1", errfd = "2", fdin = "0";

    std::string clean_com_line = remove_spaces(com_line);
    if (clean_com_line.empty()) {
        return args;
    }

    substitute_commands(clean_com_line);

    // split by space and expand
    std::stringstream streamData(clean_com_line);
    size_t arg_num = 0;
    std::string value;
    bool get_next_file_fd = false, get_next_file_errfd = false, get_next_fdin = false;
    while (std::getline(streamData, value, ' ')) {

        size_t begin = 0;

        if ((value.find('<', begin)) != std::string::npos) {
            if (arg_num)
                continue;
            get_next_fdin = true;
            continue;
        }

        if ((value.find('>', begin)) != std::string::npos) {
            if (value == "&>" || value == ">&") {
                get_next_file_fd = true;
                get_next_file_errfd = true;
            }
            if (value == "2>&1" || value == "2&>1")
                errfd = fd;
            if (value == "1>&2" || value == "1&>2")
                fd = errfd;
            if (value == ">" || value == "1>")
                get_next_file_fd = true;
            if (value == "2>")
                get_next_file_errfd = true;
            continue;
        }

        if (get_next_file_errfd || get_next_file_fd) {
            if (get_next_file_errfd) {
                errfd = value;
                get_next_file_errfd = false;
            }
            if (get_next_file_fd) {
                fd = value;
                get_next_file_fd = false;
            }
            continue;
        }

        if (get_next_fdin) {
            fdin = value;
            get_next_fdin = false;
            continue;
        }

        // expand variable if exists
        value = expand_variables(value);

        if (arg_num == 0) {
            args.push_back(value);
        }
        else {
            // replace with wildcard
            std::vector<std::string> matched_filenames = get_matched_filenames(value);
            // add to args
            if (!matched_filenames.empty()) {
                args.insert(args.end(), matched_filenames.begin(), matched_filenames.end());
            } else {
                args.push_back(value);
            }
        }

        arg_num += 1;
    }


    args.push_back(fdin);
    args.push_back(fd);
    args.push_back(errfd);


    return args;
}

void execute_command(std::string &file_for_exec, cmd_args &args) {
    std::vector<const char *> args_for_exec;
    for (const auto &str: args) {
        args_for_exec.push_back(str.c_str());
    }
    args_for_exec.push_back(nullptr);

    execvp(file_for_exec.c_str(), const_cast<char *const *>(args_for_exec.data()));
    perror("Exec failed");
}

std::vector<cmd_args> split_shell_line(std::string &shell_line) {
    size_t begin = 0, end;
    std::string delim = " | ";
    std::vector<std::string> cmd_lines;
    std::string cmd_line;
    while ((end = shell_line.find(delim, begin)) != std::string::npos) {
        cmd_line = shell_line.substr(begin, end - begin);

        cmd_lines.push_back(cmd_line);
        begin = end + delim.length();
    }
    cmd_line = shell_line.substr(begin, end);
    cmd_lines.push_back(cmd_line);

    std::vector<cmd_args> cmds_args;
    for (const std::string &line: cmd_lines) {
        cmd_args args = parse_com_line(line);
        if (args.empty()) {
            continue;
        }
        cmds_args.push_back(args);
    }

    return cmds_args;
}

/**
 * @param string
 * @returns whether the passed string is the name of the builtin command.
 */
static bool check_builtin(const std::string &s) {
    std::unordered_set<std::string> builtins{"merrno", "mexport", "mexit", "mpwd", "mecho", "mcd", "."};
    return builtins.count(s);
}

static int get_fd(const std::string &fd, int flag) {
    if (fd == "0" || fd == "1" || fd == "2")
        return stoi(fd);
    while (true) {
        int cur_fd =  open_wrapper(fd.c_str(), flag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (cur_fd == -1) {
            if (errno != EINTR) {
                perror("fd error");
                return -1;
            }
        } else {
            return cur_fd;
        }
    }
}

/**
 * Remove " &" at the end.
 * @return
 *      -1 if " &" occures not at the end
 *      0 if " &" does not occur in the string
 *      1 if " &" occurs at the end of the shell line
 */
int strip_ampersand(std::string &shell_line) {
    while (!shell_line.empty() && isspace(shell_line.back())) {
        shell_line.pop_back();
    }
    size_t pos = shell_line.find(" &");
    if (pos == std::string::npos) {
        // " &" does not occur in the string
        return 0;
    }
    if (pos == shell_line.size() - 2) {
        shell_line.erase(pos);
        // " &" occurs at the end of the shell line
        return 1;
    }
    return 0;
//    // " &" occurs at the end of the shell line
//    return -1;
}

void exec_shell_line(std::string &shell_line) {
    bool must_wait = false;
    switch (strip_ampersand(shell_line)) {
        case -1:
            std::cerr << "Wrong use of &: should be on redirections" << std::endl;
            return;
        case 0:
            must_wait = true;
            break;
        case 1:
            must_wait = false;
            break;
    }
    std::vector<cmd_args> cmds_args = split_shell_line(shell_line);
    if (cmds_args.empty()) {
        return;
    }

    std::vector<pid_t> child_pids;

    int dup_stdout = dup_wrapper(STDOUT_FILENO);
    while (cmds_args.size() > 1) {
        cmd_args cur_command_line = cmds_args.back(); cmds_args.pop_back();

        int pfd[2];
        pipe_wrapper(pfd, F_SETFD);

        pid_t child_pid = fork();
        if (child_pid == 0) {
            if (check_builtin(cur_command_line[0])) {
                std::cerr << "builtin command in the middle of the pipe" << std::endl;
                exit(EXIT_FAILURE);
            }

            dup2_wrapper(pfd[0], STDIN_FILENO);

//            ...configure redirections
            int errfd = get_fd(cur_command_line.back(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC); cur_command_line.pop_back();
            int fdout = get_fd(cur_command_line.back(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC); cur_command_line.pop_back();
//            int fdin = get_fd(cur_command_line.back(), O_RDONLY | O_CLOEXEC); cur_command_line.pop_back();
            dup2_wrapper(fdout, STDOUT_FILENO);
            dup2_wrapper(errfd, STDERR_FILENO);

            execute_command(cur_command_line[0], cur_command_line);
        }
        if (child_pid == -1) {
            perror("fork");
            continue;
        } else {
            child_pids.push_back(child_pid);
        }
        if (close_wrapper(pfd[0]) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
        dup2_wrapper(pfd[1], STDOUT_FILENO);

        if(close_wrapper(pfd[1]) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
    };
    auto cur_command_line = cmds_args.back();
    int errfd = get_fd(cur_command_line.back(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC); cur_command_line.pop_back();
    int fdout = get_fd(cur_command_line.back(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC); cur_command_line.pop_back();
    int fdin = get_fd(cur_command_line.back(), O_RDONLY | O_CLOEXEC); cur_command_line.pop_back();

    if (check_builtin(cur_command_line[0])) {
//        ...configure redirections
        dup2_wrapper(errfd, STDERR_FILENO);
        dup2_wrapper(fdout, STDOUT_FILENO);
        dup2_wrapper(fdin, STDIN_FILENO);

        run_builtin_command(cur_command_line, STDOUT_FILENO);
    } else {
//        std::cout << "CUR CMD FD: " << errfd << "  " << fdout << "  " << fdin << std::endl;
        pid_t child_pid = fork();
        if (child_pid == 0) {
//            ...configure redirections
            dup2_wrapper(fdout, STDOUT_FILENO);
            dup2_wrapper(fdin, STDIN_FILENO);
            dup2_wrapper(errfd, STDERR_FILENO);
// TODO: grep $(cat inpt) < ls_out doesnt work
            execute_command(cur_command_line[0], cur_command_line);
        }
        child_pids.push_back(child_pid);
    }
    dup2_wrapper(dup_stdout, STDOUT_FILENO);
    if (close_wrapper(dup_stdout) == -1) {
        perror("close");
        exit(EXIT_FAILURE);
    }

    if (errfd != STDERR_FILENO) {
        if (close_wrapper(errfd) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
    }
    if (fdout != STDOUT_FILENO) {
        if (close_wrapper(fdout) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
    }
    if (fdin != STDIN_FILENO) {
        if (close_wrapper(fdin) == -1) {
            perror("close");
            exit(EXIT_FAILURE);
        }
    }


    if (!must_wait) {
        return;
    }
    for (pid_t child_pid : child_pids) {
        int child_status;
        while (true) {
            int status = waitpid(child_pid, &child_status, 0);
            if (status == -1) {
                if (errno == ECHILD) {
                    goto end_outer_loop;
                } else if (errno != EINTR) {
                    perror("Unexpected error from waitpid");
                    std::cerr << errno << std::endl;
                    exit_status = ExitCodes::EOTHER;
                    return;
                }
            } else {
                break;
            }
            // here EINTR happened
        }
        if (WIFEXITED(child_status)) {
            exit_status = WEXITSTATUS(child_status);
        } else if (WIFSIGNALED(child_status)) {
            exit_status = ExitCodes::ESIGNALFAIL;
        }
        end_outer_loop:
        ;
    }
}

void exec_shell_lines(std::basic_istream<char> &com_stream) {
    std::string com_line;
    // don't use cin: 1. can't use later, 2. reads till ' '
    while (std::getline(com_stream, com_line)) {
        exec_shell_line(com_line);
    }
}

std::string get_prompt() {
    return fs::current_path().string() + " $ ";
}
