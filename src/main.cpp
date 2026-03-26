#include <iostream>
#include "parser.h"

using namespace std;

// declare build engine function
void executeBuild();

int main(int argc, char* argv[]) {

    if(argc < 2){
        cout << "Docksmith CLI" << endl;
        cout << "Commands: build | run | images | rmi" << endl;
        return 0;
    }

    string cmd = argv[1];

    if(cmd == "build"){
        cout << "Starting build..." << endl;
        executeBuild();     // ⭐ call real build engine
    }
    else if(cmd == "run"){
        cout << "Run command (next phase)" << endl;
    }
    else if(cmd == "images"){
        cout << "Images command (later phase)" << endl;
    }
    else if(cmd == "rmi"){
        cout << "Remove image command (later phase)" << endl;
    }
    else{
        cout << "Unknown command" << endl;
    }

    return 0;
}