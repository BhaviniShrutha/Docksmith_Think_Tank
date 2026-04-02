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

        // FIX 1a: Save the full trimmed line as rawText BEFORE splitting
        string rawLine = line;

        // Extract instruction token
        stringstream ss(line);
        string cmd;
        ss >> cmd;

        if (valid.find(cmd) == valid.end()) {
            cerr << "Error: Unknown instruction '" << cmd
                 << "' at line " << lineNum << " in " << path << endl;
            exit(1);
        }

        // Get rest of line, trim leading space
        string rest;
        getline(ss, rest);
        if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);

        // FIX 1c: CMD must be JSON array form only
        if (cmd == "CMD") {
            size_t vs = rest.find_first_not_of(" \t");
            size_t ve = rest.find_last_not_of(" \t");
            if (vs == string::npos || rest[vs] != '[' || rest[ve] != ']') {
                cerr << "Error line " << lineNum
                     << ": CMD requires JSON array form e.g. [\"/bin/sh\",\"app.sh\"]" << endl;
                exit(1);
            }
        }

        Instruction ins;
        ins.type       = cmd;
        ins.value      = rest;
        ins.rawText    = rawLine;   // FIX 1a
        ins.lineNumber = lineNum;

        instructions.push_back(ins);
    }

    // FIX 1b: FROM must be first instruction
    if (instructions.empty() || instructions[0].type != "FROM") {
        cerr << "Error: Docksmithfile must begin with a FROM instruction" << endl;
        exit(1);
    }

    return instructions;
}
