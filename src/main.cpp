#include <iostream>
#include <string>
#include <algorithm> 
#include <vector>

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while(true){
    std::cout << "$ ";
    std::string input;
    // std::string commands[] = {"type", "exit", "echo"};

    std::vector<std::string> commands = {"type", "exit", "echo"};

    std::getline(std::cin, input);
    if(input == "exit"){  break;  } 
    else if(input.substr(0, 5) == "type "){
      std::string tempo = input.substr(5);
      auto it = std::find(std::begin(commands), std::end(commands), tempo);
      if(it != std::end(commands)){ std::cout << tempo << " is a shell builtin"; }
      else{ std::cout << tempo << ": not found"; }
    }
    else if(input.substr(0, 5) == "echo ") {  std::cout << input.substr(5) << std::endl; }
    else { std::cout << input << ": command not found" << std::endl; }
  }
}

// $ type echo
// echo is a shell builtin
// $ type exit
// exit is a shell builtin
// $ type invalid_command
// invalid_command: not found