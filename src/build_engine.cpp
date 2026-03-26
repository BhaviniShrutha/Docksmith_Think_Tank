#include <filesystem>
#include <iostream>
#include "parser.h"

namespace fs = std::filesystem;

void executeBuild(){

    std::string tempRoot = "/tmp/docksmith_build";

    // remove old build
    fs::remove_all(tempRoot);

    // create new build root
    fs::create_directories(tempRoot);

    auto instructions = parseDocksmithfile("sample_app/Docksmithfile");

    for(auto &ins : instructions){

        if(ins.type == "COPY"){
            std::cout << "Executing COPY..." << std::endl;

            std::string source = "sample_app";

            for (const auto & entry : fs::directory_iterator(source)) {
                fs::copy(entry.path(),
                         tempRoot + "/" + entry.path().filename().string(),
                         fs::copy_options::recursive |
                         fs::copy_options::overwrite_existing);
            }
        }
    }

    std::cout << "Build filesystem ready at " << tempRoot << std::endl;
}
