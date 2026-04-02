#pragma once
#include <vector>
#include <string>

struct Instruction {
    std::string type;
    std::string value;
    int lineNumber;
};

std::vector<Instruction> parseDocksmithfile(const std::string& path);
