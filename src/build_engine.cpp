#include <filesystem>
#include <iostream>
#include <cstdlib>
#include "parser.h"
#include "../include/layer.h"
#include "../include/cache.h"
#include <fstream>
#include <ctime>

namespace fs = std::filesystem;

void executeBuild(){

    std::string tempRoot = "/tmp/docksmith_build";
    std::string currentDir = tempRoot;

    fs::remove_all(tempRoot);
    fs::create_directories(tempRoot);

    auto instructions = parseDocksmithfile("sample_app/Docksmithfile");

    for(auto &ins : instructions){

        std::cout << "Instruction: " << ins.type << " -> " << ins.value << std::endl;

        // 🔥 Generate cache key (instruction based)
        std::string cleanValue = ins.value;
        cleanValue.erase(0, cleanValue.find_first_not_of(" "));
        cleanValue.erase(cleanValue.find_last_not_of(" ") + 1);

        std::string cacheInput = ins.type + "_" + cleanValue;
        std::string key = generate_cache_key(cacheInput);

        // ================= WORKDIR =================
        if(ins.type == "WORKDIR"){

            std::string path = ins.value;
            path.erase(0, path.find_first_not_of(" "));

            if(path.size() > 0 && path[0] == '/'){
                path = path.substr(1);
            }

            currentDir = tempRoot + "/" + path;
            fs::create_directories(currentDir);

            std::cout << "WORKDIR set to " << currentDir << std::endl;
        }

        // ================= COPY =================
        else if(ins.type == "COPY"){

            std::cout << "Executing COPY..." << std::endl;

            if(!check_cache(key)){

                for (const auto & entry : fs::directory_iterator("sample_app")) {
                    fs::copy(entry.path(),
                             currentDir + "/" + entry.path().filename().string(),
                             fs::copy_options::recursive |
                             fs::copy_options::overwrite_existing);
                }

                std::string hash = create_layer(currentDir);
                store_cache(key, hash);
            }
        }

        // ================= RUN =================
        else if(ins.type == "RUN"){

            std::cout << "Executing RUN..." << std::endl;

            if(!check_cache(key)){

                std::string command =
                    "cd " + currentDir + " && " + ins.value;

                system(command.c_str());

                std::string hash = create_layer(currentDir);
                store_cache(key, hash);
            }
        }
    }

    // ================= MANIFEST =================
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