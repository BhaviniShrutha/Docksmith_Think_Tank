#include "parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>

using namespace std;

vector<Instruction> parseDocksmithfile(const string& path) {
    vector<Instruction> instructions;
    ifstream file(path);

    if (!file.is_open()) {
        cerr << "Error: Cannot open " << path << endl;
        exit(1);
    }

    set<string> valid = {"FROM", "WORKDIR", "ENV", "CMD", "COPY", "RUN"};
    string line;
    int lineNum = 0;

    while (getline(file, line)) {
        lineNum++;

        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        line = line.substr(start);

        // Skip blank lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Extract instruction token
        stringstream ss(line);
        string cmd;
        ss >> cmd;

        if (valid.find(cmd) == valid.end()) {
            cerr << "Error: Unknown instruction '" << cmd
                 << "' at line " << lineNum << " in " << path << endl;
            exit(1);
        }

        Instruction ins;
        ins.type = cmd;
        ins.lineNumber = lineNum;

        // Get rest of line, trim leading space
        string rest;
        getline(ss, rest);
        if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
        ins.value = rest;

        instructions.push_back(ins);
    }

    return instructions;
}
