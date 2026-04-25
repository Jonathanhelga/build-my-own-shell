#include <cstdlib>
// #include <cerrno>
// #include <csignal>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#define HISTSIZE 500

namespace fs = std::filesystem;
std::vector<std::string> history_memory;
int session_start = 0;
int history_last_appended = 0;


// g++ -std=c++17 -o shell src/main.cpp -lreadline

struct ParseResult {
    std::vector<std::string> tokens;
    bool redirect_out = false;
    bool redirect_err = false;
    bool append_out   = false;
    bool append_err   = false;
};

struct BackgroundJob {
  int job_id;
  pid_t pid;
  std::vector<std::string> command;
};

struct JobNumber {
    bool empty = true;
};

std::vector<BackgroundJob> bg_jobs;

bool checkBackslash(char quoteChar, const std::string &input, size_t &i, std::string &current);
ParseResult tokenize(const std::string &input);
std::string findExecPath(const std::string &program_name);
void execSegment(const std::vector<std::string> &seg);
void runPipeline(const std::vector<std::vector<std::string>> &segments);
std::vector<std::vector<std::string>> splitByPipe(const std::vector<std::string> &tokens);
char* builtin_completer(const char* text, int state);
char** shell_completer(const char* text, int start, int end);
std::string getHistoryPath();
void storeHistoryMemory();
void loadHistoryMemory();
void runBuiltin(const std::string& program_name, const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
void reapingJob(std::vector<BackgroundJob>& bg_jobs);

const std::set<std::string> builtins = {"exit", "echo", "type", "pwd", "cd", "history", "jobs"};

void builtin_echo(const std::vector<std::string>& args, std::ostream& out) {
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) out << ' ';
        out << args[i];
    }
    out << '\n';
}
void builtin_pwd(std::ostream& out){
    char buffer[1024];
    char *p = getcwd(buffer, sizeof(buffer));
    if (p) out << p << std::endl;
    else std::cerr << "pwd: error retrieving current directory\n";
}
void builtin_cat(const std::vector<std::string>& args, std::ostream& out, std::ostream & err){
    if (args.empty()) {
        out << std::cin.rdbuf();
        return;
    }
    for (const auto& fileName : args) {
        std::ifstream file(fileName);
        if (!file) { err << "cat: " << fileName << ": No such file or directory\n"; continue; }
        out << file.rdbuf();
    }
}
void builtin_cd(const std::vector<std::string>& args, std::ostream& err) {
    if (args.size() > 1) {
        err << "cd: too many arguments\n";
        return;
    }
    if (args.empty() || args[0] == "~") {
        const char* home = std::getenv("HOME");
        if (home && chdir(home) != 0)
            err << "cd: " << home << ": No such file or directory\n";
        return;
    }
    if (chdir(args[0].c_str()) != 0)
        err << "cd: " << args[0] << ": No such file or directory\n";
}
void builtin_type(const std::vector<std::string>& args, std::ostream& out, std::ostream& err){
    if(args.empty()){ err << "type: missing argument\n"; }
    else if(builtins.count(args[0])){ out << args[0] << " is a shell builtin\n"; }
    else{
        std::string p = findExecPath(args[0]);
        if(!p.empty()) out << args[0] << " is " << p << '\n';
        else err << args[0] << ": not found\n";
    }
}
void builtin_history(const std::vector<std::string>& args, std::ostream& out, std::ostream& err){
    int total = (int)history_memory.size();
    if(!args.empty()){
        if(args[0] == "-r"){
            if (args.size() < 2) { err << "history: -r: missing filename\n"; return; }
            std::ifstream hf(args[1]);
            if (!hf) { err << "history: " << args[1] << ": cannot open file\n"; return; }
            std::string line;
            while (std::getline(hf, line)) {
                if (!line.empty()) { history_memory.push_back(line); add_history(line.c_str()); }
            }
        }
        else if(args[0] == "-w"){
            if (args.size() < 2) { err << "history: -w: missing filename\n"; return; }
            std::ofstream hf(args[1]);
            if (!hf) { err << "history: " << args[1] << ": cannot open file\n"; return; }
            for(const auto& cmd: history_memory){ hf << cmd << '\n'; }
        }
        else if(args[0] == "-a"){
            if (args.size() < 2) { err << "history: -a: missing filename\n"; return; }
            std::ofstream hf(args[1], std::ios::app);
            if (!hf) { err << "history: " << args[1] << ": cannot open file\n"; return; }
            for (int i = history_last_appended; i < total; i++) { hf << history_memory[i] << '\n'; }
            history_last_appended = total;
        }
        else{
            int show = std::stoi(args[0]);
            int startIdx = std::max(0, total - show);
            for (int i = startIdx; i < total; i++) { out << "  " << (i + 1) << "  " << history_memory[i] << '\n'; }
        }
    }
    else{
        for (int i = 0; i < total; i++) { out << "  " << (i + 1) << "  " << history_memory[i] << '\n'; }
    }
}
bool checkBackslash(char quoteChar, const std::string& input, size_t& i, std::string& current){
  if(quoteChar == '\"'){
    i++; // skip the '\'
    if(i < input.size()){
      char next = input[i];
      if(next == '\\' || next == '"' || next == '$' || next == '`' || next == '\n'){
        current += next;
      } else {
        current += '\\';
        current += next;
      }
      i++;
    }
    return true;
  }
  return false; 
}

ParseResult tokenize(const std::string &input){
  ParseResult result;
  std::string current;
  size_t i = 0;
  while(i < input.size()){
    char c = input[i];
    if(c == '\'' || c == '\"'){
      char character = c;
      i++;
      while(i < input.size() && (input[i] != character)){
        if(input[i] == '\\') {
          if(checkBackslash(c, input, i, current)) continue;
        }
        current += input[i];
        i++;
      }
      if(i < input.size()) i++; // skip closing quote
    }
    else if(c == '\\'){
      i++;
      if(i < input.size()) current += input[i++];
    }
    else if (c == ' ' || c == '\t'){
      if(current == ">" || current == "1>")       { result.redirect_out = true; }
      else if(current == "2>")                    { result.redirect_err = true; }
      else if(current == ">>" || current == "1>>") { result.append_out  = true; }
      else if(current == "2>>")                   { result.append_err   = true; }

      if(!current.empty()){
        result.tokens.push_back(current);
        current.clear();
      }
      i++;
    }
    else{
      current += c;
      i++;
    }
  }
  if(!current.empty()){ result.tokens.push_back(current); }
  return result;
}

std::string findExecPath(const std::string &program_name){
    char *path_env = std::getenv("PATH");
    if(!path_env) return "";
    std::string path_str(path_env);                                                                                                  
    std::stringstream ss(path_str);
    std::string dir;

    while(std::getline(ss, dir, ':')){
      if(!fs::exists(dir)) continue;
      fs::path full_path = fs::path(dir) / program_name;
      if (fs::exists(full_path)) {                                                                                                  
          auto perms = fs::status(full_path).permissions();
          if ((perms & fs::perms::owner_exec) != fs::perms::none ||                                                                 
              (perms & fs::perms::group_exec) != fs::perms::none ||                                                                 
              (perms & fs::perms::others_exec) != fs::perms::none) {
              return full_path.string();                                                                                            
          }                                                                                                                       
      }   
    }
    return "";
}

std::vector<std::vector<std::string>> splitByPipe(const std::vector<std::string> &tokens) {
    std::vector<std::vector<std::string>> segments;
    std::vector<std::string> cur;
    for (const auto &tok : tokens) {
        if (tok == "|") {
            if (!cur.empty()) segments.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(tok);
        }
    }
    if (!cur.empty()) segments.push_back(cur);
    return segments;
}
void runPipeline(const std::vector<std::vector<std::string>> &segments) {
    int n = segments.size();
    std::vector<std::array<int,2>> pipes(n - 1);
    for (auto &p : pipes) pipe(p.data());

    std::vector<pid_t> pids;
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Wire up stdin from the previous pipe
            if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
            // Wire up stdout to the next pipe
            if (i < n-1) dup2(pipes[i][1], STDOUT_FILENO);

            // Close all pipe fds in the child
            for (auto &p : pipes) { 
                close(p[0]); 
                close(p[1]); 
            }
            execSegment(segments[i]);
            exit(1);
        }
        pids.push_back(pid);
    }
    // Parent closes all pipe ends so children can detect EOF
    for (auto &p : pipes) { close(p[0]); close(p[1]); }
    for (auto pid : pids) waitpid(pid, nullptr, 0);
}
void execSegment(const std::vector<std::string> &seg) {
    if (seg.empty()) exit(0);
    const std::string cmd = seg[0];
    std::vector<std::string> args(seg.begin() + 1, seg.end());
    if(builtins.count(cmd)){
        runBuiltin(cmd, args, std::cout, std::cerr);
        exit(0);
    }
    std::string exec_path = findExecPath(cmd);
    if (exec_path.empty()) {
        std::cerr << cmd << ": not found\n";
        exit(127);
    }
    std::vector<char*> argv;
    argv.push_back((char*)cmd.c_str());
    for (auto &a : args) argv.push_back((char*)a.c_str());
    argv.push_back(nullptr);
    execv(exec_path.c_str(), argv.data());
    exit(1);
}

char* builtin_completer(const char* text, int state) {

    static std::vector<std::string> matches;
    static size_t match_index;

    if (state == 0) {  // first call — build the matches list
        matches.clear();
        match_index = 0;
        for (const auto& cmd : builtins) {
            if (cmd.rfind(text, 0) == 0) {  // starts with text
                matches.push_back(cmd);
            }
        }
        if(matches.size() < 1){
          std::vector<std::string> builtouts;
          char *path_env = std::getenv("PATH");                                                                            
          if(path_env != nullptr){
              std::string path_str = path_env;                                                                             
              std::stringstream ss(path_str);
              std::string dir;                                                                                             
              while(std::getline(ss, dir, ':')){
                  if(!fs::exists(dir)) continue;
                  for(const auto &entry : fs::directory_iterator(dir)){                                                    
                      if(!entry.is_regular_file()) continue;
                      auto perms = entry.status().permissions();                                                           
                      if((perms & fs::perms::owner_exec) != fs::perms::none ||
                         (perms & fs::perms::group_exec) != fs::perms::none ||                                             
                         (perms & fs::perms::others_exec) != fs::perms::none){                                             
                          builtouts.push_back(entry.path().filename().string());
                      }                                                                                                    
                  }       
              }                                                                                                            
          }
          for (const auto& exec : builtouts) {
            if (exec.rfind(text, 0) == 0) {  // starts with text
                matches.push_back(exec);
            }
          }
        }
    }

    if (match_index < matches.size()) {
        return strdup(matches[match_index++].c_str());
    }
    return nullptr;
}
char** shell_completer(const char* text, int start, int end) {
    if(start == 0){
      rl_attempted_completion_over = 1;  // don't fall back to file completion
      return rl_completion_matches(text, builtin_completer);
    }
    return nullptr;
}

std::string getHistoryPath(){
    const char* histfile = std::getenv("HISTFILE");
    if (histfile && histfile[0] != '\0') return std::string(histfile);
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.shell_history";
}

void storeHistoryMemory(){
    std::string history_path = getHistoryPath();
    if (history_path.empty()) return;
    std::ofstream history(history_path, std::ios::app);
    for (int i = session_start; i < (int)history_memory.size(); i++){
        history << history_memory[i] << '\n';
    }
}
void loadHistoryMemory(){
    std::string history_path = getHistoryPath();
    if (history_path.empty()) return;
    std::ifstream history(history_path);
    std::string command;
    while(std::getline(history, command)){  history_memory.push_back(command); }
    if ((int)history_memory.size() > HISTSIZE) 
        history_memory.erase(history_memory.begin(), history_memory.begin() + history_memory.size() - HISTSIZE);
}

void runBuiltin(const std::string& program_name, const std::vector<std::string>& args, std::ostream& out, std::ostream& err){
    if(program_name == "echo") { builtin_echo(args, out); }
    else if(program_name == "pwd")  { builtin_pwd(out); }
    else if(program_name == "cat")  { builtin_cat(args, out, err); }
    else if(program_name == "cd")   { builtin_cd(args, err); }
    else if(program_name == "type") { builtin_type(args, out, err); }
    else if(program_name == "history"){ builtin_history(args, out, err); }
}

std::vector <JobNumber> jobID_assigner;
int next_job_number = 1;
int job_number = 0;
void reapingJob(std::vector<BackgroundJob>& bg_jobs, std::vector <JobNumber>& jobID_assigner){
    int jobs_total = (int)bg_jobs.size();
    std::vector<BackgroundJob> remaining;
    for(int i = 0; i < jobs_total; i++){
        if(waitpid(bg_jobs[i].pid, nullptr, WNOHANG) != 0){
            char sign = (i == jobs_total - 1) ? '+' : (i == jobs_total - 2) ? '-' : ' ';
            const auto& toks = bg_jobs[i].command;
            int end = (int)toks.size() - 1;
            std::string cmd;
            for(int j = 0; j < end; j++){
                if(j > 0) cmd += " ";
                cmd += toks[j];
            }
            std::cout << "[" << bg_jobs[i].job_id << "]" << sign << "  " << "Done" << "                 " << cmd << std::endl;
            jobID_assigner[(bg_jobs[i].job_id)-1].empty = true;
        }
        else { remaining.push_back(bg_jobs[i]); }
    }
    bg_jobs = remaining; // moved outside the loop
}

int main(){
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    rl_attempted_completion_function = shell_completer;
    std::vector<std::string> session_history;

    loadHistoryMemory();
    session_start = history_memory.size();
    history_last_appended = session_start;
    for (const auto& cmd : history_memory) { add_history(cmd.c_str()); }   
    while(true){
        char *line = readline("$ ");
        if (!line) { storeHistoryMemory(); break; }
        std::string input(line);
        if (!input.empty()) {                                                                                                                                                                      
            history_memory.push_back(input);                                                                                                                                                     
            add_history(input.c_str());
        }
        free(line);

        auto result = tokenize(input);
        auto tokens = result.tokens;
        if (tokens.empty()) continue;
        bool background = false;
        std::vector<std::string> full_command;
        if (!tokens.empty() && tokens.back() == "&") {
            background = true;
            full_command = tokens; // includes "&" at the back
            job_number = -1;                                                                                                                         
            for(int i = 0; i < (int)jobID_assigner.size(); i++){
                if(jobID_assigner[i].empty){                                                                                                         
                    jobID_assigner[i].empty = false;  // mark as in-use
                    job_number = i + 1;                                                                                                              
                    break;
                }                                                                                                                                    
            }           
            if(job_number == -1){
                jobID_assigner.push_back({false});    // new slot, marked in-use
                job_number = (int)jobID_assigner.size();                                                                                             
            }  
            tokens.pop_back();
            if (tokens.empty()) continue;
        }

        bool has_pipe = false;
        for (const auto &tok : tokens) if (tok == "|") { has_pipe = true; break; }
        if (has_pipe) { runPipeline(splitByPipe(tokens)); continue; }

        std::ostringstream output_text;
        std::ostringstream output_error_text;
        bool output_handled = false;

        std::string program_name = tokens[0];
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        std::string redirect_file;
        if (result.redirect_out || result.redirect_err || result.append_out || result.append_err) {
            for (size_t i = 0; i < args.size(); i++) {
                if ((args[i] == ">" || args[i] == "1>" || args[i] == "2>" || args[i] == ">>" || args[i] == "1>>" || args[i] == "2>>") && i + 1 < args.size()) {
                    redirect_file = args[i + 1];
                    args.erase(args.begin() + i, args.begin() + i + 2);
                    break;
                }
            }
        }

        if(program_name == "exit"){  storeHistoryMemory(); break; }
        else if(builtins.count(program_name) && program_name != "exit" && program_name != "jobs") {
            if (background) {
                pid_t pid = fork();
                if (pid == 0) {
                    runBuiltin(program_name, args, std::cout, std::cerr);
                    exit(0);
                } else if (pid > 0) {
                    bg_jobs.push_back({job_number, pid, full_command});
                    std::cout << "[" << job_number << "] " << pid << std::endl;
                    output_handled = true;
                }
            } else {
                runBuiltin(program_name, args, output_text, output_error_text);
            }
        }
        else if(program_name == "jobs") {
            int jobs_total = (int)bg_jobs.size();
            std::vector<std::string> statuses(jobs_total);
            std::vector<BackgroundJob> remaining;

            // Single pass determine status of each job
            for(int i = 0; i < jobs_total; i++){
                if(waitpid(bg_jobs[i].pid, nullptr, WNOHANG) != 0){
                    statuses[i] = "Done";
                } else {
                    statuses[i] = "Running";
                    remaining.push_back(bg_jobs[i]);
                }
            }

            // Print all jobs with aligned columns
            for(int i = 0; i < jobs_total; i++){
                char sign = (i == jobs_total - 1) ? '+' : (i == jobs_total - 2) ? '-' : ' ';
                const auto& toks = bg_jobs[i].command;
                int end = (statuses[i] == "Done" && !toks.empty() && toks.back() == "&")
                          ? (int)toks.size() - 1
                          : (int)toks.size();
                std::string cmd;
                for(int j = 0; j < end; j++){
                    if(j > 0) cmd += " ";
                    cmd += toks[j];
                }
                std::cout << "[" << bg_jobs[i].job_id << "]" << sign
                          << "  " << statuses[i] << "                 " << cmd << std::endl;
            }

            // Remove done jobs
            bg_jobs = remaining;
            output_handled = true;
        }
        
        else{
            std::string exec_path = findExecPath(program_name);
            if(!exec_path.empty()){
                pid_t pid = fork();
                if(pid == 0){
                    if(!redirect_file.empty()){
                        int flags = O_WRONLY | O_CREAT | (result.append_out || result.append_err ? O_APPEND : O_TRUNC);
                        int fd = open(redirect_file.c_str(), flags, 0644);
                        if(result.redirect_err || result.append_err){ dup2(fd, STDERR_FILENO); }
                        else { dup2(fd, STDOUT_FILENO); }
                        close(fd);
                    }
                    std::vector<char *> argv;
                    argv.push_back((char *)program_name.c_str());
                    for(auto &a : args){ argv.push_back((char *)a.c_str()); }
                    argv.push_back(nullptr);
                    execv(exec_path.c_str(), argv.data());
                    exit(1);
                } else if(pid > 0){
                    if (!background) {
                        int status;
                        waitpid(pid, &status, 0);
                        output_handled = true;
                    } else {
                        // int job_num = next_job_number++;
                        bg_jobs.push_back({job_number, pid, full_command});
                        output_text << "[" << job_number << "] " << pid << std::endl;
                        output_handled = false;
                    }
                }
            } else {
                output_error_text << program_name << ": not found\n";
            }
        }
        if (!output_handled) {
            if(result.redirect_out || result.redirect_err || result.append_out || result.append_err){
                auto flags = result.append_out || result.append_err ? std::ios::app : std::ios::trunc;
                std::ofstream file(redirect_file, flags);
                if(result.redirect_out || result.append_out){
                    file << output_text.str();
                    std::cerr << output_error_text.str();
                }
                else{
                    file << output_error_text.str();
                    std::cout << output_text.str();
                }
            }
            else {
                std::cout << output_text.str();
                std::cerr << output_error_text.str();
            }
        }
        if (!bg_jobs.empty()){ reapingJob(bg_jobs); }
    }
}