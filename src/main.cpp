#include <iostream>
#include "parser.h"

using namespace std;

int main(int argc, char* argv[]) {

    if(argc < 2){
        cout<<"Docksmith CLI"<<endl;
        return 0;
    }

    string cmd = argv[1];

    if(cmd == "build"){

        auto instructions = parseDocksmithfile("sample_app/Docksmithfile");

        for(auto &i : instructions){
            cout << i.type << " -> " << i.value << endl;
        }
    }
    else if(cmd == "run"){
        cout<<"Run command"<<endl;
    }
    else if(cmd == "images"){
        cout<<"Images command"<<endl;
    }
    else if(cmd == "rmi"){
        cout<<"Remove image command"<<endl;
    }
    else{
        cout<<"Unknown command"<<endl;
    }

    return 0;
}
