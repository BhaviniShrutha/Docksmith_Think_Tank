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

        // ── FIX 3: Full JSON-array validation for CMD ─────────
        if (cmd == "CMD") {
            string val = rest;
            // Trim leading/trailing whitespace
            val.erase(0, val.find_first_not_of(" \t"));
            if (!val.empty())
                val.erase(val.find_last_not_of(" \t") + 1);

            // Must start with [ and end with ]
            if (val.empty() || val.front() != '[' || val.back() != ']') {
                cerr << "Error line " << lineNum
                     << ": CMD requires JSON array form e.g. [\"/bin/sh\",\"app.sh\"]" << endl;
                exit(1);
            }
            // Must contain at least one quoted string element
            if (val.find('"') == string::npos) {
                cerr << "Error line " << lineNum
                     << ": CMD JSON array must contain quoted strings e.g. [\"cmd\",\"arg\"]" << endl;
                exit(1);
            }
            // Must not have unmatched brackets
            int depth = 0;
            for (char c : val) {
                if (c == '[') depth++;
                if (c == ']') depth--;
                if (depth < 0) {
                    cerr << "Error line " << lineNum
                         << ": CMD has malformed JSON array" << endl;
                    exit(1);
                }
            }
            if (depth != 0) {
                cerr << "Error line " << lineNum
                     << ": CMD has unmatched brackets" << endl;
                exit(1);
            }
        }
        // ── end CMD validation ─────────────────────────────────

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

    // ── FIX 5: Only one FROM is allowed ───────────────────────
    bool seenFrom = false;
    for (auto& ins : instructions) {
        if (ins.type == "FROM") {
            if (seenFrom) {
                cerr << "Error line " << ins.lineNumber
                     << ": only one FROM instruction is allowed" << endl;
                exit(1);
            }
            seenFrom = true;
        }
    }
    // ── end FROM check ─────────────────────────────────────────

    return instructions;
}
