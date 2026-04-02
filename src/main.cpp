#include "build_engine.h"
#include "runtime.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>

using namespace std;

static void printUsage() {
    cerr << "Usage:\n"
         << "  docksmith build -t <name:tag> [--no-cache] <context_dir>\n"
         << "  docksmith images\n"
         << "  docksmith rmi <name:tag>\n"
         << "  docksmith run [-e KEY=VALUE ...] <name:tag> [cmd...]\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printUsage(); return 1; }

    string cmd = argv[1];

    if (cmd == "build") {
        string nameTag, contextDir;
        bool noCache = false;

        for (int i = 2; i < argc; i++) {
            string arg = argv[i];
            if (arg == "-t" && i + 1 < argc)       nameTag = argv[++i];
            else if (arg == "--no-cache")            noCache = true;
            else                                     contextDir = arg;
        }

        if (nameTag.empty() || contextDir.empty()) {
            cerr << "Error: build requires -t <name:tag> and <context_dir>\n";
            printUsage(); return 1;
        }
        auto cp = nameTag.find(':');
        if (cp == string::npos) { cerr << "Error: Invalid name:tag format\n"; return 1; }

        executeBuild(nameTag.substr(0, cp), nameTag.substr(cp + 1), contextDir, noCache);
    }
    else if (cmd == "images") {
        listImages();
    }
    else if (cmd == "rmi") {
        if (argc < 3) { cerr << "Error: rmi requires <name:tag>\n"; return 1; }
        string nt = argv[2];
        auto cp = nt.find(':');
        if (cp == string::npos) { cerr << "Error: Invalid name:tag format\n"; return 1; }
        removeImage(nt.substr(0, cp), nt.substr(cp + 1));
    }
    else if (cmd == "run") {
        map<string,string> envOvr;
        string nameTag;
        vector<string> cmdOvr;

        // FIX 16: Two-pass -e parsing so -e works anywhere before name:tag
        // Pass 1: collect all -e KEY=VALUE pairs; mark consumed indices
        set<int> consumed;
        for (int i = 2; i < argc; i++) {
            string arg = argv[i];
            if (arg == "-e" && i + 1 < argc) {
                string ev = argv[i + 1];
                auto eq = ev.find('=');
                if (eq != string::npos)
                    envOvr[ev.substr(0, eq)] = ev.substr(eq + 1);
                consumed.insert(i);
                consumed.insert(i + 1);
                i++;  // skip value
            }
        }
        // Pass 2: first non-consumed arg = name:tag; rest = cmdOvr
        bool foundImage = false;
        for (int i = 2; i < argc; i++) {
            if (consumed.count(i)) continue;
            if (!foundImage) {
                nameTag = argv[i];
                foundImage = true;
            } else {
                cmdOvr.push_back(argv[i]);
            }
        }

        if (nameTag.empty()) { cerr << "Error: run requires <name:tag>\n"; return 1; }
        auto cp = nameTag.find(':');
        if (cp == string::npos) { cerr << "Error: Invalid name:tag format\n"; return 1; }

        runContainer(nameTag.substr(0, cp), nameTag.substr(cp + 1), envOvr, cmdOvr);
    }
    else {
        cerr << "Error: Unknown command '" << cmd << "'\n";
        printUsage(); return 1;
    }

    return 0;
}