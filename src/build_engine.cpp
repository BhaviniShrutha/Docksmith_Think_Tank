#include <filesystem>
#include <iostream>
#include <cstdlib>
#include "parser.h"
#include <fstream>
#include <ctime>

namespace fs = std::filesystem;

void executeBuild(){

    std::string tempRoot = "/tmp/docksmith_build";
    std::string currentDir = tempRoot;

    // clean old build
    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot);

    auto instructions = parseDocksmithfile("sample_app/Docksmithfile");

    for(auto &ins : instructions){

        std::cout << "Instruction: " << ins.type << " -> " << ins.value << std::endl;

        // ✅ WORKDIR
        if(ins.type == "WORKDIR"){

            std::string path = ins.value;

            // remove leading spaces
            path.erase(0, path.find_first_not_of(" "));

            // remove leading '/'
            if(path.size() > 0 && path[0] == '/'){
                path = path.substr(1);
            }

            currentDir = tempRoot + "/" + path;

            fs::create_directories(currentDir);

            std::cout << "WORKDIR set to " << currentDir << std::endl;
        }

        // ✅ COPY
        else if(ins.type == "COPY"){

            std::cout << "Executing COPY..." << std::endl;

            for (const auto & entry : fs::directory_iterator("sample_app")) {
                fs::copy(entry.path(),
                         currentDir + "/" + entry.path().filename().string(),
                         fs::copy_options::recursive |
                         fs::copy_options::overwrite_existing);
            }
        }

        // ✅ RUN
        else if(ins.type == "RUN"){

            std::cout << "Executing RUN..." << std::endl;

            std::string command =
                "cd " + currentDir + " && " + ins.value;

            system(command.c_str());
        }
    }

    // ✅ MANIFEST SYSTEM
    std::string imageDir = std::string(getenv("HOME")) + "/.docksmith/images";
    fs::create_directories(imageDir);

    std::string imagePath = imageDir + "/myapp_latest.json";

    std::ofstream file(imagePath);

    file << "{\n";
    file << "  \"name\": \"myapp\",\n";
    file << "  \"tag\": \"latest\",\n";
    file << "  \"created\": " << time(0) << "\n";
    file << "}\n";

    file.close();

    std::cout << "Image metadata saved at " << imagePath << std::endl;

    std::cout << "Build completed" << std::endl;
}