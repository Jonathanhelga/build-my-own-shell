#include <cstdlib>
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

namespace fs = std::filesystem;
// g++ -std=c++17 -o shell src/main.cpp

bool checkBackslash(char quoteChar, const std::string &input, size_t &i, std::string &current){
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

std::vector <std::string> tokenize(const std::string &input, bool &is_redirect_exists, bool &is_redirect_error_exists){
  std::vector <std::string> tokens;
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
      if(i < input.size()) i++; // skip closing '
    }
    else if(c == '\\'){
      i++;
      if(input[i] == '\\') {current += input[i++]; }
      current += input[i];
      i++;
    }
    else if (c == ' ' || c == '\t'){
      if(current == ">" || current == "1>"){ is_redirect_exists = true; }
      else if(current == "2>") { is_redirect_error_exists = true; }

      if(!current.empty()){
        tokens.push_back(current);
        current.clear();
      }
      i++;
    }
    else{
      current += c;
      i++;
    }
  }
  if(!current.empty()){ tokens.push_back(current); }
  return tokens;
}

int main(){
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    while(true){
        std::cout << "$ ";
        std::string input;
        std::set<std::string> commands = {"exit", "echo", "type", "pwd", "cd"};
        std::getline(std::cin, input);

        bool is_redirect_exists = false;
        bool is_redirect_error_exists = false;
        auto tokens = tokenize(input, is_redirect_exists, is_redirect_error_exists);
        if (tokens.empty()) continue;
        std::ostringstream output_text;
        std::ostringstream output_error_text;
        bool output_handled = false;
        std::string program_name = tokens[0];
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        std::string redirect_file;
        if (is_redirect_exists || is_redirect_error_exists) {
            for (size_t i = 0; i < args.size(); i++) {
                if ((args[i] == ">" || args[i] == "1>" || args[i] == "2>") && i + 1 < args.size()) {
                    redirect_file = args[i + 1];
                    args.erase(args.begin() + i, args.begin() + i + 2);
                    break;
                }
            }
        }
        if(program_name == "exit"){  break;  } 
        else if(program_name == "echo") {
          for(size_t i = 0; i < args.size(); i++){ output_text << args[i] << ' '; }
          output_text << '\n';
        }
        else if(program_name == "pwd") { 
          char buffer[1024];
          char *p;
          p = getcwd(buffer, sizeof(buffer)); //get Current Working Directory
          output_text << p << std::endl;
        }
        else if(program_name == "cat"){
          for(const auto &fileName : args){ 
            std::ifstream file(fileName);
            if(!file){
              output_error_text << "cat: " << fileName << ": No such file or directory\n";
              // std::cerr << "cat: " << fileName << ": No such file or directory\n";
              continue;
            }
            output_text << file.rdbuf();
          }
        }
        else if(program_name == "cd"){
          if(args.size() > 1) {
            output_error_text << "cd: too many arguments\n";
            // std::cerr << "cd: too many arguments\n";
          }
          else if(args[0] == "~" || args.empty()){
            const char *home = std::getenv("HOME");
            if(home && chdir(home) != 0){
              output_error_text << "cd: " << home << ": No such file or directory\n";
              // std::cerr << "cd: " << home << ": No such file or directory\n";
            }
          }
          else{
            if(chdir(args[0].c_str()) != 0){
              output_error_text << "cd: " << args[0] << ": No such file or directory\n";
              // std::cerr << "cd: " << args[0] << ": No such file or directory\n";
            }
          }
        }
        else{
            if(args.empty()){ 
              output_text << program_name << ": not found" << std::endl; 
            }
            else if(commands.find(args[0]) != commands.end()){ 
              output_text << args[0] << " is a shell builtin" << std::endl; 
            }
            else{
              char *path_env = std::getenv("PATH");
              std::string exec_path;
              bool found = false;

              if(path_env != nullptr){
                  std::string path_str = path_env; //same as std::string path_str(path_env);
                  std::stringstream ss(path_str);
                  std::vector <std::string> path_dirs;
                  std::string dir;
                  
                  while(std::getline(ss, dir, ':')){ path_dirs.push_back(dir); }
                  std::string searchingWord;

                  if(program_name == "type"){ searchingWord = args[0]; }
                  else{ searchingWord = program_name; }

                  for(const auto &dir : path_dirs){
                      fs::path full_path = fs::path(dir) / searchingWord;
                      if(fs::exists(full_path)){
                          auto perms = fs::status(full_path).permissions();
                          if((perms & fs::perms::owner_exec) != fs::perms::none ||
                             (perms & fs::perms::group_exec) != fs::perms::none ||
                             (perms & fs::perms::others_exec) != fs::perms::none){
                              exec_path = full_path.string();
                              found = true;
                              break;
                          }
                      }
                  }
                  if(found){
                    if(program_name == "type"){ 
                      output_text << searchingWord << " is " << exec_path << std::endl; 
                      // std::cout << searchingWord << " is " << exec_path << std::endl; 
                    }
                    else{
                        pid_t pid = fork();
                        if(pid == 0){
                            if(!redirect_file.empty()){
                                int fd = open(redirect_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                if(is_redirect_error_exists){
                                    dup2(fd, STDERR_FILENO);
                                } else {
                                    dup2(fd, STDOUT_FILENO);
                                }
                                close(fd);
                            }
                            std::vector<char *> argv;
                            argv.push_back((char *)program_name.c_str());
                            for(auto &a : args){
                                argv.push_back((char *)a.c_str());
                            }
                            argv.push_back(nullptr);
                            execv(exec_path.c_str(), argv.data());
                            exit(1);
                        }else if(pid > 0){
                            int status;
                            waitpid(pid, &status, 0);
                            output_handled = true;
                        }
                    }
                  }else{ 
                    output_error_text << searchingWord << ": not found" << std::endl; 
                    // output_text << searchingWord << ": not found" << std::endl; 
                    // std::cout << searchingWord << ": not found" << std::endl; 
                  }

              }else{ 
                output_error_text << program_name << ": not found" << std::endl; 
                // output_text << program_name << ": not found" << std::endl; 
                // std::cout << program_name << ": not found" << std::endl; 
              }
            }
        }
        
        if (!output_handled) {
            if (is_redirect_exists) {
                std::ofstream file(redirect_file);
                file << output_text.str();
            }
            else if(is_redirect_error_exists){
                std::ofstream file(redirect_file);
                file << output_error_text.str();
            }
            else {
                std::cout << output_text.str();
            }
        }
    }
}