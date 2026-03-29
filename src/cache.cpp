#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include "../include/cache.h"

using namespace std;
namespace fs = std::filesystem;

string sha256_key(const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)data.c_str(), data.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << hex << (int)hash[i];
    }
    return ss.str();
}

string generate_cache_key(const string& input) {
    return sha256_key(input);
}

string get_cache_dir() {
    string home = getenv("HOME");
    return home + "/.docksmith/cache/";
}

bool check_cache(const string& key) {

    string home = getenv("HOME");
    string path = home + "/.docksmith/cache/" + key;

    if (fs::exists(path)) {
        cout << "[CACHE HIT]\n";
        return true;
    }

    cout << "[CACHE MISS]\n";
    return false;
}

void store_cache(const string& key, const string& layerHash) {

    string home = getenv("HOME");
    string dir = home + "/.docksmith/cache/";

    fs::create_directories(dir);   // 🔥 ADD THIS LINE

    string path = dir + key;

    ofstream file(path);
    file << layerHash;
    file.close();
}