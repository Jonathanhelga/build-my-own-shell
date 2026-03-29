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

namespace fs = std::filesystem;
// g++ -std=c++17 -o shell src/main.cpp
std::vector <std::string> tokenize(const std::string &input){
  std::vector <std::string> tokens;
  std::string current;
  size_t i = 0;
  while(i < input.size()){
    char c = input[i];
    if( c== '\"'){
      i++;
      while(i < input.size() && input[i] != '\"'){
        current += input[i];
        i++;
      }
      if(i < input.size()) i++; // skip closing '
    }
    else if(c == '\''){
      i++;
      while(i < input.size() && input[i] != '\''){
        current += input[i];
        i++;
      }
      if(i < input.size()) i++; // skip closing '
    }
    else if (c == ' ' || c == '\t'){
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

        auto tokens = tokenize(input);
        if (tokens.empty()) continue;
        std::string program_name = tokens[0];
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        if(program_name == "exit"){  break;  } 
        else if(program_name == "echo") {

          for(size_t i = 0; i < args.size(); i++){
            std::cout << args[i] << ' ';
          }
          std::cout << '\n';
        }
        else if(program_name == "pwd") { 
          char buffer[1024];
          char *p;
          p = getcwd(buffer, sizeof(buffer)); //get Current Working Directory
          std::cout << p << std::endl;
        }
        else if(program_name == "cat"){
          for(const auto &fileName : args){ 
            std::ifstream file(fileName);
            if(!file){ std::cerr << "cat: " << fileName << ": No such file or directory\n"; continue; }
            std::cout << file.rdbuf();
          }
        }
        else if(program_name == "cd"){
          if(args.size() > 1) {  std::cerr << "cd: too many arguments\n"; }
          else if(args[0] == "~" || args.empty()){
            const char *home = std::getenv("HOME");
            if(home && chdir(home) != 0){ std::cerr << "cd: " << home << ": No such file or directory\n"; }
          }
          else{
            if(chdir(args[0].c_str()) != 0){ std::cerr << "cd: " << args[0] << ": No such file or directory\n"; }
          }
        }
        else{
            if(args.empty()){ std::cout << program_name << ": not found" << std::endl; }
            else if(commands.find(args[0]) != commands.end()){ std::cout << args[0] << " is a shell builtin" << std::endl; }
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
                    if(program_name == "type"){ std::cout << searchingWord << " is " << exec_path << std::endl; }
                    else{
                        pid_t pid = fork();
                        if(pid == 0){
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
                        }
                    }
                  }else{ std::cout << searchingWord << ": not found" << std::endl; }

              }else{ std::cout << program_name << ": not found" << std::endl; }
            }
        }
    }
}