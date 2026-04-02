#include "cache.h"
#include "layer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std;

// ──────────────────────────────────────────────
// Compute cache key from build state
// ──────────────────────────────────────────────
string computeCacheKey(const string& prevDigest,
                       const string& instruction,
                       const string& workdir,
                       const map<string, string>& env,
                       const vector<string>& fileHashes) {
    string input;
    input += prevDigest;
    input += instruction;
    input += workdir;

    // ENV state: sorted by key (std::map is already sorted)
    string envStr;
    for (auto& [k, v] : env) {
        if (!envStr.empty()) envStr += "\n";
        envStr += k + "=" + v;
    }
    input += envStr;

    // COPY file hashes (already sorted by caller)
    for (auto& h : fileHashes) {
        input += h;
    }

    return sha256_hex(input);
}

// ──────────────────────────────────────────────
// Check cache: return layer digest or ""
// ──────────────────────────────────────────────
string checkCache(const string& key) {
    string home = getenv("HOME");
    string cachePath = home + "/.docksmith/cache/" + key;

    if (!fs::exists(cachePath)) return "";

    ifstream f(cachePath);
    string layerDigest;
    getline(f, layerDigest);

    // Verify the layer tar actually exists
    string layerPath = home + "/.docksmith/layers/" + layerDigest + ".tar";
    if (!fs::exists(layerPath)) return "";

    return layerDigest;
}

// ──────────────────────────────────────────────
// Store cache mapping
// ──────────────────────────────────────────────
void storeCache(const string& key, const string& layerDigest) {
    string home = getenv("HOME");
    string dir = home + "/.docksmith/cache/";
    fs::create_directories(dir);

    ofstream f(dir + key);
    f << layerDigest;
    f.close();
}