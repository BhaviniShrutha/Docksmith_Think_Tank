#include "../include/runtime.h"
#include <iostream>
#include "parser.h"

using namespace std;

// declare build engine function
void executeBuild();

int main(int argc, char* argv[]) {

    if (argc < 2) {
        cout << "Docksmith CLI" << endl;
        cout << "Commands: build | run | images | rmi" << endl;
        return 0;
    }

    string cmd = argv[1];

    if (cmd == "build") {
        cout << "Starting build..." << endl;
        executeBuild();  // call real build engine
    }
    else if (cmd == "run") {
        if (argc < 3) {
            cout << "Usage: docksmith run name:tag\n";
            return 1;
        }

        string input = argv[2];

        size_t pos = input.find(":");
        if (pos == string::npos) {
            cout << "Invalid format\n";
            return 1;
        }

        string name = input.substr(0, pos);
        string tag = input.substr(pos + 1);

        run_container(name, tag);
    }
    else if (cmd == "images") {
        cout << "Images command (later phase)" << endl;
    }
    else if (cmd == "rmi") {
        cout << "Remove image command (later phase)" << endl;
    }
    else {
        cout << "Unknown command" << endl;
    }

    return 0;
}