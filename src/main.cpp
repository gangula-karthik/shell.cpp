#include <iostream>
#include <string>
#include <unordered_set>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <functional>
#include <map>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#include <fstream>


static int job_num = 0;

namespace fs = std::filesystem;

struct Job {
    int job_num;
    std::string cmd;
    std::string status; 
    pid_t pid;
};
std::vector<Job> jobs_list;
int curr_job;
int prev_job;

std::vector<std::string> history_list;
static size_t history_last_appended = 0;

std::vector<std::string> path_dirs() {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return {};
    std::stringstream ss(path_env);
    std::string token;
    std::vector<std::string> dirs;
    while (std::getline(ss, token, ':'))
        dirs.push_back(token);
    return dirs;
}

bool is_executable(const fs::path& p) {
    if (!fs::exists(p) || !fs::is_regular_file(p)) return false;
    fs::perms prms = fs::status(p).permissions();
    return (prms & (fs::perms::owner_exec |
                    fs::perms::group_exec |
                    fs::perms::others_exec)) != fs::perms::none;
}

fs::path find_in_path(const std::string& name) {
    for (const auto& dir : path_dirs()) {
        fs::path full = fs::path(dir) / name;
        if (is_executable(full)) return full;
    }
    return {};
}

std::vector<std::string> parse_args(const std::string& inp) {
    std::vector<std::string> args;
    std::string current;
    bool in_single = false, in_double = false, started = false;
    size_t i = 0;
    while (i < inp.size()) {
        char c = inp[i];
        if (in_single) {
            if (c == '\'') in_single = false;
            else { current += c; }
        } else if (in_double) {
            if (c == '"') in_double = false;
            else if (c == '\\' && i + 1 < inp.size() &&
                     (inp[i+1] == '"' || inp[i+1] == '\\')) {
                current += inp[++i];
            } else { current += c; }
        } else {
            if (c == '\\') {
                if (++i < inp.size()) { current += inp[i]; started = true; }
            } else if (c == '\'') { in_single = true; started = true; }
            else if (c == '"')    { in_double = true;  started = true; }
            else if (c == ' ' || c == '\t') {
                if (started) { args.push_back(current); current.clear(); started = false; }
            } else { current += c; started = true; }
        }
        i++;
    }
    if (started) args.push_back(current);
    return args;
}


static struct termios saved_termios;

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &saved_termios);
    struct termios raw = saved_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

bool read_line(std::string& line, const std::unordered_set<std::string>& builtins) {
    line.clear();
    enable_raw_mode();

    auto completions = [&](const std::string& prefix) {
        std::vector<std::string> matches;
        for (const auto& b : builtins)
            if (b.size() >= prefix.size() && b.compare(0, prefix.size(), prefix) == 0)
                matches.push_back(b);

        const char* path_env = std::getenv("PATH");
        if (path_env) {
            std::stringstream ss(path_env);
            std::string dir;
            while (std::getline(ss, dir, ':')) {
                std::error_code ec;
                for (auto& e : fs::directory_iterator(dir, ec)) {
                    std::string name = e.path().filename().string();
                    if (name.size() >= prefix.size() &&
                        name.compare(0, prefix.size(), prefix) == 0 &&
                        is_executable(e.path())) {
                        if (std::find(matches.begin(), matches.end(), name) == matches.end())
                            matches.push_back(name);
                    }
                }
            }
        }
        std::sort(matches.begin(), matches.end());
        return matches;
    };

    char c;
    bool last_was_tab = false;
    int history_idx = (int)history_list.size(); 
    std::string saved_line;
    while (true) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            disable_raw_mode();
            return false;
        }
        if (c == '\n' || c == '\r') {
            std::cout << '\n';
            break;
        } else if (c == '\t') {
            auto matches = completions(line);
            if (matches.size() == 1) {
                std::string suffix = matches[0].substr(line.size());
                std::cout << suffix << ' ';
                line = matches[0] + ' ';
                last_was_tab = false;
            } else if (matches.empty()) {
                std::cout << '\a';
                last_was_tab = false;
            } else if (last_was_tab) {
                // second tab: show all matches
                std::cout << '\n';
                for (const auto& match : matches)
                    std::cout << match << "  ";
                std::cout << '\n' << "$ " << line;
                std::cout.flush();
                last_was_tab = false;
            } else {
                // first tab with multiple matches: find common prefix
                std::string common = matches[0];
                for (const auto& m : matches)
                    while (m.compare(0, common.size(), common) != 0)
                        common.pop_back();
                if (common.size() > line.size()) {
                    std::string suffix = common.substr(line.size());
                    std::cout << suffix;
                    line = common;
                } else {
                    std::cout << '\a';
                }
                last_was_tab = true;
            }
        } else if (c == 127 || c == '\b') {
            if (!line.empty()) {
                line.pop_back();
                std::cout << "\b \b";
            }
            last_was_tab = false;
        } else if (c == 4) { // Ctrl-D
            disable_raw_mode();
            return false;
        } else if (c == '\x1b') {
            char seq[2];
            read(STDIN_FILENO, &seq[0], 1);
            read(STDIN_FILENO, &seq[1], 1);

            if (seq[0] == '[') {
                std::string* entry = nullptr;
                if (seq[1] == 'A') { 
                    if (history_idx == (int)history_list.size())
                        saved_line = line;
                    if (history_idx > 0)
                        entry = &history_list[--history_idx];
                } else if (seq[1] == 'B') {
                    if (history_idx < (int)history_list.size()) {
                        ++history_idx;
                        if (history_idx == (int)history_list.size()) {
                            entry = &saved_line; 
                        } else {
                            entry = &history_list[history_idx];
                        }
                    }
                }
                if (entry) {
                    std::cout << "\r$ " << std::string(line.size(), ' ') << "\r$ " << *entry;
                    std::cout.flush();
                    line = *entry;
                }
            }
            last_was_tab = false;
        } else {
            line += c;
            std::cout << c;
            last_was_tab = false;
        }
    }

    disable_raw_mode();
    return true;
}


void cmd_pwd() {
    std::cout << fs::current_path().string() << "\n";
}

void cmd_cd(const std::vector<std::string>& args) {
    std::string target = (args.size() > 1) ? args[1] : "";
    if (target == "~" || target.empty()) {
        const char* home = std::getenv("HOME");
        if (home) fs::current_path(home);
    } else {
        try {
            fs::current_path(target);
        } catch (const fs::filesystem_error&) {
            std::cout << "cd: " << target << ": No such file or directory\n";
        }
    }
}

void cmd_echo(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) std::cout << ' ';
        std::cout << args[i];
    }
    std::cout << "\n";
}

void cmd_type(const std::vector<std::string>& args,
              const std::unordered_set<std::string>& builtins) {
    if (args.size() < 2) return;
    const std::string& target = args[1];
    if (builtins.contains(target)) {
        std::cout << target << " is a shell builtin\n";
        return;
    }
    fs::path p = find_in_path(target);
    if (!p.empty()) std::cout << target << " is " << p.string() << "\n";
    else             std::cout << target << ": not found\n";
}

void cmd_external(const std::string& cmd,
                  const std::vector<std::string>& args,
                  const std::string& raw_inp,
                  bool exec_only = false) {
    fs::path full = find_in_path(cmd);
    if (full.empty()) { std::cout << raw_inp << ": command not found\n"; return; }

    std::vector<const char*> argv;
    for (const auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    if (exec_only) {
        execv(full.c_str(), const_cast<char* const*>(argv.data()));
        _exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        execv(full.c_str(), const_cast<char* const*>(argv.data()));
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

void reap_jobs(bool print_running = false) {
    std::vector<Job> remaining;
    for (auto job : ::jobs_list) {
        int wstatus;
        pid_t result = waitpid(job.pid, &wstatus, WNOHANG);
        if (result > 0 && WIFEXITED(wstatus))
            job.status = "Done";

        bool done = (job.status == "Done");
        if (done || print_running) {
            char marker = (job.job_num == ::curr_job) ? '+' :
                          (job.job_num == ::prev_job) ? '-' : ' ';
            std::cout << '[' << job.job_num << ']' << marker << "  "
                      << std::left << std::setw(24) << job.status
                      << job.cmd << (done ? "" : " &") << '\n';
        }

        if (!done) remaining.push_back(job);
    }
    ::jobs_list = std::move(remaining);
    ::curr_job = ::jobs_list.empty() ? 0 : ::jobs_list.back().job_num;
    ::prev_job = (::jobs_list.size() < 2) ? 0 : ::jobs_list[::jobs_list.size() - 2].job_num;
    ::job_num = ::jobs_list.empty() ? 0 : ::jobs_list.back().job_num;
}

bool is_number(const std::string& s)
{
    std::string::const_iterator it = s.begin();
    while (it != s.end() && std::isdigit(*it)) ++it;
    return !s.empty() && it == s.end();
}

void history_load(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Error: could not open the file." << std::endl;
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) break;
        history_list.push_back(line);
    }
    history_last_appended = history_list.size();
}

void history_save(const std::string& file_path) {
    std::ofstream file(file_path, std::ios::app);
    if (!file) {
        std::cerr << "Error: could not open the file." << std::endl;
        return;
    }
    for (size_t i = history_last_appended; i < history_list.size(); i++)
        file << history_list[i] << '\n';
    history_last_appended = history_list.size();
}

void cmd_history(std::vector<std::string> args){
    size_t h = history_list.size();
    int num = 0;
    bool read_file = false;
    bool write_file = false;
    std::string file_path;
    if (args.size() > 1){
        for (size_t i = 0; i < args.size(); i++){
            if (args[i] == "-r"){
                file_path = args[i + 1];
                read_file = true;
            } else if (is_number(args[i])){
                num = std::stoi(args[i]);
                num = h - num + 1;
            } else if (args[i] == "-w" || args[i] == "-a"){
                file_path = args[i + 1];
                write_file = true;
            }
        }
   }

    if (!read_file && !write_file){
        for (size_t i{0uz}; i < h; i++){
            if ((i + 1) >= (size_t)num) std::cout << "    " << (i+1) << "  " << history_list[i] << '\n';
        }
    } else if (read_file) {
        history_load(file_path);
    } else if (write_file) {
        history_save(file_path);
    }
}

void cmd_jobs() {
    reap_jobs(true);
}

pid_t run_in_background(std::function<void()> f){
    pid_t pid = fork();
    if (pid == 0) {
        f();
        _exit(0);
    }
    return pid;
}


int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    const std::unordered_set<std::string> builtins {"exit", "echo", "type", "pwd", "cd", "jobs", "history"};

    const char* histfile = std::getenv("HISTFILE");
    if (histfile) history_load(histfile);

    while (true) {
        reap_jobs();
        std::string inp;
        std::cout << "$ ";
        std::cout.flush();
        if (!read_line(inp, builtins)) break;

        std::vector<std::string> args = parse_args(inp);
        if (args.empty()) continue;

        const std::string& cmd = args[0];

        bool background = (args.back() == "&");
        if (background) args.pop_back();

        auto run = [&](std::function<void()> f) {
            history_list.push_back(inp);

            if (background) {
                pid_t pid = run_in_background(f);
                if (pid > 0){
                    std::cout << '[' << ((::jobs_list.size() == 0) ? (job_num = 1) : ++job_num) << "] " << pid << '\n';
                    std::string cmd_str = inp;
                    while (!cmd_str.empty() && (cmd_str.back() == '&' || cmd_str.back() == ' '))
                        cmd_str.pop_back();
                    ::jobs_list.push_back({job_num, cmd_str, "Running", pid});
                    if (!curr_job) ::curr_job = job_num;
                    else {
                        ::prev_job = ::curr_job;
                        ::curr_job = job_num; 
                    }
                }
            } else {
                f();
            }

        };

        if (cmd == "exit") {
            history_list.push_back(inp);
            if (histfile) history_save(histfile);
            break;
        }
        else if (cmd == "pwd")  run([=](){ cmd_pwd(); });
        else if (cmd == "cd")   run([=](){ cmd_cd(args); });
        else if (cmd == "echo") run([=](){ cmd_echo(args); });
        else if (cmd == "type") run([=, &builtins](){ cmd_type(args, builtins); });
        else if (cmd == "jobs") run([=](){ cmd_jobs(); });
        else if (cmd == "history") run([=](){ cmd_history(args); });
        else                    run([=](){ cmd_external(cmd, args, inp, background); });
    }
}
