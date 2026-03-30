#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include "../include/layer.h"

using namespace std;
namespace fs = std::filesystem;

string sha256(const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)data.c_str(), data.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << hex << (int)hash[i];
    }
    return ss.str();
}

string create_layer(const string& folderPath) {

    string combined = "";

    for (auto& p : fs::recursive_directory_iterator(folderPath)) {
        if (fs::is_regular_file(p.path())) {

            combined += p.path().string(); // 🔥 filename included

            ifstream file(p.path(), ios::binary);
            string content((istreambuf_iterator<char>(file)), {});
            combined += content;
        }
    }

    string hash = sha256(combined);

    string home = getenv("HOME");
    string layerDir = home + "/.docksmith/layers/";
    fs::create_directories(layerDir);

    ofstream out(layerDir + hash + ".layer");
    out << combined;
    out.close();

    cout << "Layer created: " << hash << endl;

    return hash;
}