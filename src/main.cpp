#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <set>
#include <vector>

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
      std::string tempo = input.substr(5);
      if(commands.find(tempo) != commands.end()){ std::cout << tempo << " is a shell builtin" << std::endl; }
      else{ 
        // std::cout << tempo << ": not found" << std::endl; 
        char *path_env = std::getenv("PATH");
        bool found = false;

        if(path_env == nullptr){
          std::string path_str = path_env;
          std::cout << path_str << std::endl ;
        }
      }
    }
    else if(input.substr(0, 5) == "echo ") {  std::cout << input.substr(5) << std::endl; }
    else { std::cout << input << ": command not found" << std::endl; }
  }
}
