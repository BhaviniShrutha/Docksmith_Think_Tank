// build_engine.cpp — Docksmith build pipeline
// Delta capture for RUN:
//   1. Before running: snapshot rootfs → map<relPath, sha256> for all regular files
//   2. runIsolated() executes command in clone+chroot environment
//   3. After running: re-walk rootfs, find new/changed files by comparing hashes
//   4. Copy only changed/new files into a fresh staging dir
//   5. createTarLayer() on the staging dir → reproducible layer with only the delta

#include "build_engine.h"
#include "parser.h"
#include "layer.h"
#include "cache.h"
#include "runtime.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace std;
using Clock = chrono::steady_clock;

// ─────────────────────────────────────────────────────────────
// Helper: ISO-8601 UTC timestamp string
// ─────────────────────────────────────────────────────────────
static string utcNow() {
    time_t t = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return string(buf);
}

// ─────────────────────────────────────────────────────────────
// Helper: Snapshot a directory — returns map<relPath, sha256>
// Only regular files are hashed; directories are ignored.
// ─────────────────────────────────────────────────────────────
static map<string, string> snapshotDir(const string& root) {
    map<string, string> snap;
    if (!fs::exists(root)) return snap;
    for (auto& p : fs::recursive_directory_iterator(root,
             fs::directory_options::skip_permission_denied)) {
        if (!fs::is_regular_file(p)) continue;
        string rel = fs::relative(p.path(), root).string();
        snap[rel] = sha256_file(p.path().string());
    }
    return snap;
}

// ─────────────────────────────────────────────────────────────
// Helper: Compute delta — files in 'after' that are new or changed
// vs 'before'. Returns list of relative paths.
// ─────────────────────────────────────────────────────────────
static vector<string> computeDelta(const map<string, string>& before,
                                    const map<string, string>& after) {
    vector<string> changed;
    for (auto& [path, hash] : after) {
        auto it = before.find(path);
        if (it == before.end() || it->second != hash) {
            changed.push_back(path);
        }
    }
    return changed;
}

// ─────────────────────────────────────────────────────────────
// Helper: Stage only changed/new files into a staging directory
// ─────────────────────────────────────────────────────────────
static void stageFiles(const string& srcRoot, const vector<string>& relPaths,
                        const string& stageDir) {
    fs::create_directories(stageDir);
    for (auto& rel : relPaths) {
        fs::path src = fs::path(srcRoot) / rel;
        fs::path dst = fs::path(stageDir) / rel;
        fs::create_directories(dst.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    }
}

// ─────────────────────────────────────────────────────────────
// Helper: Parse "KEY=VALUE" ENV string into map entry
// ─────────────────────────────────────────────────────────────
static pair<string,string> parseEnv(const string& kv) {
    auto eq = kv.find('=');
    if (eq == string::npos) return {kv, ""};
    return {kv.substr(0, eq), kv.substr(eq + 1)};
}

// ─────────────────────────────────────────────────────────────
// Helper: Parse CMD value — supports ["bin","arg"] and plain text
// ─────────────────────────────────────────────────────────────
static vector<string> parseCmd(const string& val) {
    vector<string> result;
    // JSON array form: ["sh", "-c", "echo hi"]
    if (!val.empty() && val[0] == '[') {
        string s = val;
        // Remove [ and ]
        s.erase(remove(s.begin(), s.end(), '['), s.end());
        s.erase(remove(s.begin(), s.end(), ']'), s.end());
        // Split by comma, strip quotes
        stringstream ss(s);
        string token;
        while (getline(ss, token, ',')) {
            // Trim whitespace and quotes
            auto start = token.find_first_not_of(" \t\"");
            auto end = token.find_last_not_of(" \t\"");
            if (start != string::npos)
                result.push_back(token.substr(start, end - start + 1));
        }
    } else {
        // Shell form: wrap in ["sh", "-c", val]
        result = {"/bin/sh", "-c", val};
    }
    return result;
}

// ─────────────────────────────────────────────────────────────
// Helper: Write manifest JSON
// ─────────────────────────────────────────────────────────────
static string serializeManifest(const string& name, const string& tag,
                                  const string& created, const string& digest,
                                  const map<string,string>& env,
                                  const vector<string>& cmd,
                                  const string& workingDir,
                                  const vector<LayerEntry>& layers) {
    ostringstream j;
    j << "{\n";
    j << "  \"name\": \"" << name << "\",\n";
    j << "  \"tag\": \"" << tag << "\",\n";
    j << "  \"digest\": \"" << digest << "\",\n";
    j << "  \"created\": \"" << created << "\",\n";
    j << "  \"config\": {\n";

    // Env array
    j << "    \"Env\": [";
    bool first = true;
    for (auto& [k, v] : env) {
        if (!first) j << ", ";
        j << "\"" << k << "=" << v << "\"";
        first = false;
    }
    j << "],\n";

    // Cmd array
    j << "    \"Cmd\": [";
    first = true;
    for (auto& c : cmd) {
        if (!first) j << ", ";
        j << "\"" << c << "\"";
        first = false;
    }
    j << "],\n";

    j << "    \"WorkingDir\": \"" << workingDir << "\"\n";
    j << "  },\n";

    // Layers array
    j << "  \"layers\": [\n";
    for (size_t i = 0; i < layers.size(); i++) {
        auto& l = layers[i];
        j << "    {\"digest\": \"" << l.digest
          << "\", \"size\": " << l.size
          << ", \"createdBy\": \"" << l.createdBy << "\"}";
        if (i + 1 < layers.size()) j << ",";
        j << "\n";
    }
    j << "  ]\n";
    j << "}\n";
    return j.str();
}

// ─────────────────────────────────────────────────────────────
// Helper: Get file size of a stored layer tar
// ─────────────────────────────────────────────────────────────
static size_t layerSize(const string& digest) {
    string home = getenv("HOME");
    string path = home + "/.docksmith/layers/" + digest + ".tar";
    if (fs::exists(path)) return fs::file_size(path);
    return 0;
}

// ─────────────────────────────────────────────────────────────
// executeBuild — main build pipeline
// ─────────────────────────────────────────────────────────────
void executeBuild(const string& name, const string& tag,
                  const string& contextDir, bool noCache) {

    auto buildStart = Clock::now();

    string docksmithfile = contextDir + "/Docksmithfile";
    auto instructions = parseDocksmithfile(docksmithfile);

    size_t total = instructions.size();
    cout << "Building " << name << ":" << tag
         << " from " << docksmithfile
         << " (" << total << " step" << (total != 1 ? "s" : "") << ")\n\n";

    // ── Build state ─────────────────────────────────────────
    string       currentWorkdir = "/";
    map<string,string> currentEnv;
    vector<string>     currentCmd;
    string       previousLayerDigest = "";
    bool         cacheBroken = false;   // once true, all subsequent steps are misses
    vector<LayerEntry> collectedLayers;
    string       createdAt = "";        // set on first build; preserved on full cache hit

    // Temp rootfs (accumulate layers as we build)
    string tempRootfs = "/tmp/docksmith_build_" + to_string(getpid());
    fs::remove_all(tempRootfs);
    fs::create_directories(tempRootfs);

    string home = getenv("HOME");

    for (size_t stepIdx = 0; stepIdx < instructions.size(); stepIdx++) {
        auto& ins = instructions[stepIdx];
        cout << "Step " << (stepIdx + 1) << "/" << total
             << " : " << ins.type << " " << ins.value << "\n";

        // ─── FROM ────────────────────────────────────────────
        if (ins.type == "FROM") {
            // Parse name:tag
            string fromName, fromTag;
            auto col = ins.value.find(':');
            if (col == string::npos) {
                fromName = ins.value; fromTag = "latest";
            } else {
                fromName = ins.value.substr(0, col);
                fromTag  = ins.value.substr(col + 1);
            }

            string mpath = home + "/.docksmith/images/" + fromName + "_" + fromTag + ".json";
            if (!fs::exists(mpath)) {
                cerr << "Error: Base image " << ins.value << " not found.\n"
                     << "       Run: ./scripts/import_base.sh <rootfs.tar> "
                     << fromName << " " << fromTag << "\n";
                fs::remove_all(tempRootfs);
                exit(1);
            }

            Manifest base = loadManifest(mpath);
            cout << " ---> Loading base image " << ins.value << "\n";

            // Extract base layers into tempRootfs
            for (auto& layer : base.layers) {
                string tp = home + "/.docksmith/layers/" + layer.digest + ".tar";
                if (!fs::exists(tp)) {
                    cerr << "Error: Missing base layer " << layer.digest << "\n";
                    fs::remove_all(tempRootfs);
                    exit(1);
                }
                extractLayer(tp, tempRootfs);
                collectedLayers.push_back(layer);
                previousLayerDigest = layer.digest;
            }

            // Inherit base image env/cmd/workdir
            for (auto& e : base.env) {
                auto [k, v] = parseEnv(e);
                currentEnv[k] = v;
            }
            if (!base.cmd.empty())       currentCmd = base.cmd;
            if (!base.workingDir.empty()) currentWorkdir = base.workingDir;

            cout << " ---> Base image extracted ("
                 << base.layers.size() << " layer"
                 << (base.layers.size() != 1 ? "s" : "") << ")\n";
        }

        // ─── WORKDIR ─────────────────────────────────────────
        else if (ins.type == "WORKDIR") {
            string path = ins.value;
            // Strip leading whitespace just in case
            path.erase(0, path.find_first_not_of(" \t"));

            // Absolute or relative?
            if (!path.empty() && path[0] == '/') {
                currentWorkdir = path;
            } else {
                currentWorkdir = currentWorkdir + "/" + path;
            }

            // Normalise double slashes
            while (currentWorkdir.find("//") != string::npos)
                currentWorkdir.replace(currentWorkdir.find("//"), 2, "/");

            // CREATE the directory inside tempRootfs if it doesn't exist
            string physicalPath = tempRootfs + currentWorkdir;
            fs::create_directories(physicalPath);

            cout << " ---> WORKDIR set to " << currentWorkdir << "\n";
        }

        // ─── ENV ─────────────────────────────────────────────
        else if (ins.type == "ENV") {
            auto [k, v] = parseEnv(ins.value);
            currentEnv[k] = v;
            cout << " ---> " << k << "=" << v << "\n";
        }

        // ─── CMD ─────────────────────────────────────────────
        else if (ins.type == "CMD") {
            currentCmd = parseCmd(ins.value);
            cout << " ---> CMD set\n";
        }

        // ─── COPY ────────────────────────────────────────────
        else if (ins.type == "COPY") {
            // Parse "src dest" — split on first space
            string copySrc, copyDst;
            auto sp = ins.value.find(' ');
            if (sp == string::npos) {
                cerr << "Error: COPY requires src and dest\n";
                fs::remove_all(tempRootfs); exit(1);
            }
            copySrc = ins.value.substr(0, sp);
            copyDst = ins.value.substr(sp + 1);

            // Resolve dest relative to currentWorkdir
            string dstAbs;
            if (!copyDst.empty() && copyDst[0] == '/') {
                dstAbs = copyDst;
            } else if (copyDst == ".") {
                dstAbs = currentWorkdir;
            } else {
                dstAbs = currentWorkdir + "/" + copyDst;
            }

            // Physical location inside tempRootfs
            string dstPhys = tempRootfs + dstAbs;
            fs::create_directories(dstPhys);

            // Collect source file hashes for cache key
            vector<string> fileHashParts;
            vector<pair<string,string>> srcFiles; // {srcAbs, relToDst}

            string srcAbs = contextDir + "/" + copySrc;
            if (copySrc == "." || copySrc == "./") {
                // Copy entire context dir
                for (auto& e : fs::recursive_directory_iterator(contextDir,
                         fs::directory_options::skip_permission_denied)) {
                    if (!fs::is_regular_file(e)) continue;
                    string rel = fs::relative(e.path(), contextDir).string();
                    srcFiles.push_back({e.path().string(), rel});
                }
            } else if (fs::is_directory(srcAbs)) {
                for (auto& e : fs::recursive_directory_iterator(srcAbs,
                         fs::directory_options::skip_permission_denied)) {
                    if (!fs::is_regular_file(e)) continue;
                    string rel = fs::relative(e.path(), srcAbs).string();
                    srcFiles.push_back({e.path().string(), rel});
                }
            } else if (fs::is_regular_file(srcAbs)) {
                srcFiles.push_back({srcAbs, fs::path(srcAbs).filename().string()});
            } else {
                cerr << "Error: COPY source not found: " << srcAbs << "\n";
                fs::remove_all(tempRootfs); exit(1);
            }

            // Sort by relative path for reproducibility
            sort(srcFiles.begin(), srcFiles.end());

            for (auto& [abs, rel] : srcFiles) {
                string h = sha256_file(abs);
                fileHashParts.push_back(rel + ":" + h);
            }
            sort(fileHashParts.begin(), fileHashParts.end());

            string instructionText = ins.type + " " + ins.value;
            string cacheKey = computeCacheKey(previousLayerDigest,
                                              instructionText,
                                              currentWorkdir,
                                              currentEnv,
                                              fileHashParts);

            string cachedDigest = "";
            if (!noCache && !cacheBroken) {
                cachedDigest = checkCache(cacheKey);
            }

            auto stepStart = Clock::now();

            if (!cachedDigest.empty()) {
                // Cache hit — reuse the stored layer
                auto ms = chrono::duration_cast<chrono::milliseconds>(
                    Clock::now() - stepStart).count();
                cout << " ---> [CACHE HIT] " << cachedDigest.substr(0, 19)
                     << "  (" << ms << "ms)\n";

                // Extract cached layer onto tempRootfs so downstream steps work
                string tp = home + "/.docksmith/layers/" + cachedDigest + ".tar";
                extractLayer(tp, tempRootfs);

                LayerEntry le;
                le.digest = cachedDigest;
                le.size = layerSize(cachedDigest);
                le.createdBy = instructionText;
                collectedLayers.push_back(le);
                previousLayerDigest = cachedDigest;
            } else {
                // Cache miss
                cacheBroken = true; // all subsequent steps must also miss

                // Stage files into tmpStage, then from there into tempRootfs
                string tmpStage = "/tmp/docksmith_copy_stage_" + to_string(getpid());
                fs::remove_all(tmpStage);
                fs::create_directories(tmpStage);

                // Strip leading '/' from dstAbs so we can use it as a relative
                // path inside tmpStage — e.g. "/app" -> "app"
                // This ensures the tar layer preserves full paths like "app/app.sh"
                // so they extract correctly into the rootfs.
                string dstRelPath = dstAbs;
                if (!dstRelPath.empty() && dstRelPath[0] == '/')
                    dstRelPath = dstRelPath.substr(1);

                for (auto& [abs, rel] : srcFiles) {
                    // Stage at tmpStage/<dstRelPath>/<rel>  e.g. tmpStage/app/app.sh
                    fs::path dst = fs::path(tmpStage) / dstRelPath / rel;
                    fs::create_directories(dst.parent_path());
                    fs::copy_file(abs, dst, fs::copy_options::overwrite_existing);
                    // Also copy into tempRootfs
                    fs::path rdst = fs::path(dstPhys) / rel;
                    fs::create_directories(rdst.parent_path());
                    fs::copy_file(abs, rdst, fs::copy_options::overwrite_existing);
                }

                string newDigest = createTarLayer(tmpStage, instructionText);
                fs::remove_all(tmpStage);

                storeCache(cacheKey, newDigest);

                auto ms = chrono::duration_cast<chrono::milliseconds>(
                    Clock::now() - stepStart).count();
                cout << " ---> [CACHE MISS] " << newDigest.substr(0, 19)
                     << "  (" << ms << "ms)\n";

                LayerEntry le;
                le.digest = newDigest;
                le.size = layerSize(newDigest);
                le.createdBy = instructionText;
                collectedLayers.push_back(le);
                previousLayerDigest = newDigest;
            }
        }

        // ─── RUN ─────────────────────────────────────────────
        else if (ins.type == "RUN") {

            string instructionText = ins.type + " " + ins.value;
            string cacheKey = computeCacheKey(previousLayerDigest,
                                              instructionText,
                                              currentWorkdir,
                                              currentEnv);

            string cachedDigest = "";
            if (!noCache && !cacheBroken) {
                cachedDigest = checkCache(cacheKey);
            }

            auto stepStart = Clock::now();

            if (!cachedDigest.empty()) {
                // Cache hit
                auto ms = chrono::duration_cast<chrono::milliseconds>(
                    Clock::now() - stepStart).count();
                cout << " ---> [CACHE HIT] " << cachedDigest.substr(0, 19)
                     << "  (" << ms << "ms)\n";

                // Extract cached layer onto tempRootfs
                string tp = home + "/.docksmith/layers/" + cachedDigest + ".tar";
                extractLayer(tp, tempRootfs);

                LayerEntry le;
                le.digest = cachedDigest;
                le.size = layerSize(cachedDigest);
                le.createdBy = instructionText;
                collectedLayers.push_back(le);
                previousLayerDigest = cachedDigest;
            } else {
                // Cache miss — execute command and capture delta
                cacheBroken = true; // cascade

                // ── STEP 1: Snapshot rootfs before execution ──
                auto beforeSnap = snapshotDir(tempRootfs);

                // ── STEP 2: Run command inside isolated namespace ──
                // Ensure workdir exists inside rootfs
                fs::create_directories(tempRootfs + currentWorkdir);

                // Build the command: shell form → /bin/sh -c "..."
                vector<string> runCmd = {"/bin/sh", "-c", ins.value};

                int exitCode = runIsolated(tempRootfs, currentWorkdir,
                                            currentEnv, runCmd);
                if (exitCode != 0) {
                    cerr << "Error: RUN command failed with exit code " << exitCode << "\n";
                    fs::remove_all(tempRootfs);
                    exit(1);
                }

                // ── STEP 3: Snapshot rootfs after execution ──
                auto afterSnap = snapshotDir(tempRootfs);

                // ── STEP 4: Compute delta (new + changed files only) ──
                vector<string> deltaFiles = computeDelta(beforeSnap, afterSnap);

                if (deltaFiles.empty()) {
                    cerr << "Warning: RUN produced no filesystem changes\n";
                }

                // ── STEP 5: Stage only the delta files ──
                string tmpStage = "/tmp/docksmith_run_stage_" + to_string(getpid());
                fs::remove_all(tmpStage);
                stageFiles(tempRootfs, deltaFiles, tmpStage);

                // ── STEP 6: Create a tar layer from staged delta ──
                string newDigest = createTarLayer(tmpStage, instructionText);
                fs::remove_all(tmpStage);

                storeCache(cacheKey, newDigest);

                auto ms = chrono::duration_cast<chrono::milliseconds>(
                    Clock::now() - stepStart).count();
                cout << " ---> [CACHE MISS] " << newDigest.substr(0, 19)
                     << "  (" << ms << "ms)\n";

                LayerEntry le;
                le.digest = newDigest;
                le.size = layerSize(newDigest);
                le.createdBy = instructionText;
                collectedLayers.push_back(le);
                previousLayerDigest = newDigest;
            }
        }
    } // end instruction loop

    // ── Write manifest ────────────────────────────────────────
    if (createdAt.empty()) createdAt = utcNow();

    // 1. Serialize with digest="" to compute digest
    string draftJson = serializeManifest(name, tag, createdAt, "",
                                          currentEnv, currentCmd,
                                          currentWorkdir, collectedLayers);
    string manifestDigest = "sha256:" + sha256_hex(draftJson);

    // 2. Serialize again with real digest
    string finalJson = serializeManifest(name, tag, createdAt, manifestDigest,
                                          currentEnv, currentCmd,
                                          currentWorkdir, collectedLayers);

    string imageDir = home + "/.docksmith/images";
    fs::create_directories(imageDir);
    string imagePath = imageDir + "/" + name + "_" + tag + ".json";

    ofstream f(imagePath);
    f << finalJson;
    f.close();

    // ── Cleanup temp rootfs ───────────────────────────────────
    fs::remove_all(tempRootfs);

    auto buildMs = chrono::duration_cast<chrono::milliseconds>(
        Clock::now() - buildStart).count();

    cout << "\nSuccessfully built " << manifestDigest.substr(0, 19) << "\n";
    cout << "Image: " << name << ":" << tag
         << "  [" << collectedLayers.size() << " layer"
         << (collectedLayers.size() != 1 ? "s" : "") << "]\n";
    cout << "Total build time: " << buildMs << "ms\n";
}