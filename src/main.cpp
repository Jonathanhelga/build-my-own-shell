#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

int main(){
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    while(true){
        std::cout << "$ ";
        std::string input;
        std::set<std::string> commands = {"exit", "echo"};
        std::getline(std::cin, input);

        std::stringstream ss(input);
        std::string program_name;
        ss >> program_name; //kasih isi ss (stringstream) ke program name e.g type, echo, exit
        std::vector<std::string> args;
        std::string arg;
        while(ss >> arg){ args.push_back(arg);}
        if(program_name == "exit"){  break;  } 
        else if(program_name == "echo") {  std::cout << input.substr(5) << std::endl; }
        else{
            if(args.empty()){ std::cout << program_name << ": not found" << std::endl; }
            else if(commands.find(args[0]) != commands.end()){ std::cout << args[0] << " is a shell builtin" << std::endl; }
            
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
                           (perms & fs::perms::others_exec) != fs::perms::none) {
                            exec_path = full_path.string();
                            found = true;
                            break;
                        }
                    }
                }

                if(found){
                    if(program_name == "type"){
                        std::cout << searchingWord << " is " << exec_path << std::endl;
                    }else{
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