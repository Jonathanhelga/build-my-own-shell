#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <vector>
// g++ -std=c++17 -o shell src/main.cpp
//./shell
namespace fs = std::filesystem;

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while(true){
    std::cout << "$ ";
    std::string input;
    std::set<std::string> commands = {"type", "exit", "echo"};

    std::getline(std::cin, input);
    if(input == "exit"){  break;  } 
    else if(input.substr(0, 5) == "type "){
      std::string command = input.substr(5);
      if(commands.find(command) != commands.end()){ std::cout << command << " is a shell builtin" << std::endl; }
      else{ 
        // std::cout << tempo << ": not found" << std::endl; 
        char *path_env = std::getenv("PATH");
        if(path_env != nullptr){
          std::string path_str = path_env; //same as std::string path_str(path_env);
          std::vector <std::string> path_dirs;
          std::stringstream ss(path_str);
          std::string dir;

          while(std::getline(ss, dir, ':')){ path_dirs.push_back(dir); }

          bool found = false;
          for(const auto &dir : path_dirs){
            fs::path full_path = fs::path(dir) / command;
            if(fs::exists(full_path)){
              auto perms = fs::status(full_path).permissions();
              if((perms & fs::perms::owner_exec) != fs::perms::none ||
                 (perms & fs::perms::group_exec) != fs::perms::none ||
                 (perms & fs::perms::others_exec) != fs::perms::none) {
                    std::cout << command << " is " << full_path.string() << std::endl;
                    found = true;
                    break;
                 }
            }
          }

          if(!found){ std::cout << command << ": not found" << std::endl; }
          else{ std::cout << command << ": not found" << std::endl; }
        }
      }
    }
    else if(input.substr(0, 5) == "echo ") {  std::cout << input.substr(5) << std::endl; }
    else { std::cout << input << ": command not found" << std::endl; }
  }
}
