#include <iostream>
#include <filesystem>
using namespace std;
namespace fs = std::filesystem;

void init_state() {
    string home = getenv("HOME");
    fs::create_directories(home + "/.docksmith/images");
    fs::create_directories(home + "/.docksmith/layers");
    fs::create_directories(home + "/.docksmith/cache");
}

int main(int argc, char* argv[]) {

    init_state();

    if(argc < 2){
        cout<<"Docksmith CLI\n";
        return 0;
    }

    string cmd = argv[1];

    if(cmd == "build"){
        cout<<"Build command\n";
    }
    else if(cmd == "run"){
        cout<<"Run command\n";
    }
    else if(cmd == "images"){
        cout<<"Images command\n";
    }
    else if(cmd == "rmi"){
        cout<<"Remove image command\n";
    }
    else{
        cout<<"Unknown command\n";
    }

    return 0;
}
