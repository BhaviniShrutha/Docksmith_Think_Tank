#include "parser.h"
#include <fstream>
#include <sstream>

std::vector<Instruction> parseDocksmithfile(std::string path){

    std::vector<Instruction> instructions;
    std::ifstream file(path);
    std::string line;

    while(std::getline(file,line)){
        if(line.empty()) continue;

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        Instruction ins;
        ins.type = cmd;
        ins.value = line.substr(cmd.length());

        instructions.push_back(ins);
    }

    return instructions;
}
