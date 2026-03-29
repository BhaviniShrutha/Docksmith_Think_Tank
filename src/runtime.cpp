#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include "../include/runtime.h"

namespace fs = std::filesystem;
using namespace std;

void run_container(const string& name, const string& tag) {

    string home = getenv("HOME");

    // image path
    string imagePath = home + "/.docksmith/images/" + name + "_" + tag + ".json";

    if(!fs::exists(imagePath)) {
        cout << "Image not found\n";
        return;
    }

    cout << "Running container from " << name << ":" << tag << endl;

    // temp container root
    string tempRoot = "/tmp/docksmith_run";
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot);

    // copy sample_app files (simulate filesystem)
    for (const auto & entry : fs::directory_iterator("sample_app")) {
        fs::copy(entry.path(),
                 tempRoot + "/" + entry.path().filename().string(),
                 fs::copy_options::recursive |
                 fs::copy_options::overwrite_existing);
    }

    // simulate ENV
    cout << "Applying ENV variables..." << endl;

    // run CMD (hardcoded for now)
    cout << "Executing CMD..." << endl;

    string command = "cd " + tempRoot + " && echo Done";
    system(command.c_str());

    cout << "Container execution finished" << endl;
}