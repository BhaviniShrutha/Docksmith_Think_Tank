#pragma once
#include <vector>
#include <string>

struct Instruction {
    std::string type;
    std::string value;
};

std::vector<Instruction> parseDocksmithfile(std::string path);
