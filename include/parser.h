#pragma once
#include <vector>
#include <string>

struct Instruction {
    std::string type;
    std::string value;
    std::string rawText;   // exact line as written (after leading-whitespace strip)
    int lineNumber;
};

std::vector<Instruction> parseDocksmithfile(const std::string& path);
