// build_engine.cpp — Docksmith build pipeline
// Delta capture for RUN:
//   1. Before running: snapshot rootfs → map<relPath, sha256|DIR|LINK:target>
//   2. runIsolated() executes command in clone+chroot environment
//   3. After running: re-walk rootfs, find changes (new/modified/deleted/mode-changes)
//   4. Stage only the delta + OCI whiteout files for deletions
//   5. createTarLayer() on the staging dir → reproducible layer

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
#include <set>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/stat.h>

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
// FIX 10: Snapshot — regular files (hash), dirs ("DIR:mode"),
// symlinks ("LINK:<target>"). Captures deletions, mode changes.
// ─────────────────────────────────────────────────────────────
static map<string, string> snapshotDir(const string& root) {
    map<string, string> snap;
    if (!fs::exists(root)) return snap;
    for (auto& p : fs::recursive_directory_iterator(root,
             fs::directory_options::skip_permission_denied)) {
        string rel = fs::relative(p.path(), root).string();
        if (fs::is_symlink(p.path())) {
            auto target = fs::read_symlink(p.path());
            snap[rel] = "LINK:" + target.string();
        } else if (fs::is_directory(p.path())) {
            struct stat st;
            ::lstat(p.path().c_str(), &st);
            snap[rel] = "DIR:" + to_string(st.st_mode & 07777);
        } else if (fs::is_regular_file(p.path())) {
            struct stat st;
            ::lstat(p.path().c_str(), &st);
            snap[rel] = sha256_file(p.path().string()) + ":" + to_string(st.st_mode & 07777);
        }
    }
    return snap;
}

// ─────────────────────────────────────────────────────────────
// FIX 10: Compute full delta — new/modified/deleted/mode-changed.
// Returns {changedPaths, deletedPaths}.
// ─────────────────────────────────────────────────────────────
static pair<vector<string>, vector<string>>
computeFullDelta(const map<string, string>& before,
                 const map<string, string>& after) {
    vector<string> changed, deleted;
    for (auto& [path, val] : after) {
        auto it = before.find(path);
        if (it == before.end() || it->second != val)
            changed.push_back(path);
    }
    for (auto& [path, val] : before) {
        if (after.find(path) == after.end())
            deleted.push_back(path);
    }
    return {changed, deleted};
}

// Legacy wrapper used by older COPY path (returns changed paths only)
static vector<string> computeDelta(const map<string, string>& before,
                                    const map<string, string>& after) {
    return computeFullDelta(before, after).first;
}

// ─────────────────────────────────────────────────────────────
// Helper: Stage changed/new files + whiteouts for deleted paths
// ─────────────────────────────────────────────────────────────
static void stageFiles(const string& srcRoot, const vector<string>& relPaths,
                        const string& stageDir) {
    fs::create_directories(stageDir);
    for (auto& rel : relPaths) {
        fs::path src = fs::path(srcRoot) / rel;
        fs::path dst = fs::path(stageDir) / rel;
        fs::create_directories(dst.parent_path());
        // symlinks need copy_symlink, dirs need create_directory
        if (fs::is_symlink(src)) {
            if (fs::exists(dst) || fs::is_symlink(dst)) fs::remove(dst);
            fs::copy_symlink(src, dst);
        } else if (fs::is_directory(src)) {
            fs::create_directories(dst);
        } else if (fs::is_regular_file(src)) {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }
    }
}

// ─────────────────────────────────────────────────────────────
// FIX 10: Stage whiteout entries for OCI-deleted paths
// ─────────────────────────────────────────────────────────────
static void stageWhiteouts(const vector<string>& deletedPaths,
                            const string& stageDir) {
    for (auto& rel : deletedPaths) {
        fs::path p(rel);
        string whiteoutName = ".wh." + p.filename().string();
        fs::path whiteoutPath = fs::path(stageDir) / p.parent_path() / whiteoutName;
        fs::create_directories(whiteoutPath.parent_path());
        ofstream wf(whiteoutPath); // empty file = whiteout marker
    }
}

// ─────────────────────────────────────────────────────────────
// FIX 7: Resolve COPY sources with glob support (* and **)
// ─────────────────────────────────────────────────────────────
static vector<string> resolveGlob(const string& contextDir, const string& pattern) {
    vector<string> results;
    bool hasDoubleStar = pattern.find("**") != string::npos;
    bool hasGlob = pattern.find('*') != string::npos || pattern.find('?') != string::npos;

    if (!hasGlob) {
        // Plain path — return as-is if it exists
        string full = contextDir + "/" + pattern;
        if (fs::exists(full)) results.push_back(full);
    } else {
        for (auto& entry : fs::recursive_directory_iterator(contextDir,
                 fs::directory_options::skip_permission_denied)) {
            string rel = fs::relative(entry.path(), contextDir).string();
            // For single *, skip entries in subdirectories
            if (!hasDoubleStar && entry.path().parent_path() != fs::path(contextDir))
                continue;
            if (fnmatch(pattern.c_str(), rel.c_str(), FNM_PATHNAME) == 0)
                results.push_back(entry.path().string());
        }
    }
    sort(results.begin(), results.end());
    return results;
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
            }

            // FIX 2: Use the manifest-level digest for the first cache key anchor
            previousLayerDigest = base.digest; // manifest digest, not last layer digest

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

            // FIX 9: Do NOT create the directory now. It will be created
            // immediately before the next COPY or RUN step executes.
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
            // FIX 9: Create WORKDIR in rootfs before the COPY executes
            if (!currentWorkdir.empty() && currentWorkdir != "/") {
                fs::create_directories(fs::path(tempRootfs) / currentWorkdir.substr(1));
            }

            // Parse "src dest" — split on last space-separated token (dest)
            string copySrc, copyDst;
            auto sp = ins.value.rfind(' ');
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

            // FIX 7: Resolve source with glob support
            vector<string> resolvedSrcs;
            if (copySrc == "." || copySrc == "./") {
                // Copy entire context dir
                for (auto& e : fs::recursive_directory_iterator(contextDir,
                         fs::directory_options::skip_permission_denied)) {
                    if (fs::is_regular_file(e))
                        resolvedSrcs.push_back(e.path().string());
                }
                sort(resolvedSrcs.begin(), resolvedSrcs.end());
            } else {
                resolvedSrcs = resolveGlob(contextDir, copySrc);
                if (resolvedSrcs.empty()) {
                    // Fall back: check if it's a literal directory
                    string srcAbs = contextDir + "/" + copySrc;
                    if (fs::is_directory(srcAbs)) {
                        for (auto& e : fs::recursive_directory_iterator(srcAbs,
                                 fs::directory_options::skip_permission_denied)) {
                            if (fs::is_regular_file(e))
                                resolvedSrcs.push_back(e.path().string());
                        }
                        sort(resolvedSrcs.begin(), resolvedSrcs.end());
                    } else {
                        cerr << "Error: COPY source not found: " << srcAbs << "\n";
                        fs::remove_all(tempRootfs); exit(1);
                    }
                }
            }

            // Build srcFiles: {absPath, relToContext}
            vector<pair<string,string>> srcFiles;
            for (auto& absPath : resolvedSrcs) {
                if (!fs::is_regular_file(absPath)) continue;
                // Check if inside a resolved sub-directory or just a file match
                string srcAbs = contextDir + "/" + copySrc;
                string rel;
                if (fs::is_directory(srcAbs)) {
                    rel = fs::relative(absPath, srcAbs).string();
                } else {
                    rel = fs::path(absPath).filename().string();
                }
                srcFiles.push_back({absPath, rel});
            }
            sort(srcFiles.begin(), srcFiles.end());

            // FIX 4: Hash only raw file bytes (not path+hash)
            vector<string> fileHashes;
            for (auto& [abs, rel] : srcFiles) {
                fileHashes.push_back(sha256_file(abs));
            }

            // FIX 3: Use rawText for cache key
            string cacheKey = computeCacheKey(previousLayerDigest,
                                              ins.rawText,
                                              currentWorkdir,
                                              currentEnv,
                                              fileHashes);

            string cachedDigest = "";
            if (!noCache && !cacheBroken) {
                cachedDigest = checkCache(cacheKey);
            }

            auto stepStart = Clock::now();
            string instructionText = ins.rawText;

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
                cacheBroken = true;

                string tmpStage = "/tmp/docksmith_copy_stage_" + to_string(getpid());
                fs::remove_all(tmpStage);
                fs::create_directories(tmpStage);

                // Strip leading '/' from dstAbs for relative staging path
                string dstRelPath = dstAbs;
                if (!dstRelPath.empty() && dstRelPath[0] == '/')
                    dstRelPath = dstRelPath.substr(1);

                // FIX 8: Determine if dest is a file or directory
                bool destIsDir = (!dstAbs.empty() && dstAbs.back() == '/')
                              || (srcFiles.size() > 1)
                              || fs::is_directory(tempRootfs + dstAbs);

                // Physical location inside tempRootfs
                string dstPhys = tempRootfs + dstAbs;

                for (auto& [abs, rel] : srcFiles) {
                    fs::path stageDst, rootfsDst;

                    if (destIsDir) {
                        stageDst  = fs::path(tmpStage) / dstRelPath / rel;
                        rootfsDst = fs::path(dstPhys) / rel;
                    } else {
                        // Single file → exact dest path
                        stageDst  = fs::path(tmpStage) / dstRelPath;
                        rootfsDst = fs::path(dstPhys);
                    }

                    fs::create_directories(stageDst.parent_path());
                    fs::copy_file(abs, stageDst, fs::copy_options::overwrite_existing);
                    fs::create_directories(rootfsDst.parent_path());
                    fs::copy_file(abs, rootfsDst, fs::copy_options::overwrite_existing);
                }

                string newDigest = createTarLayer(tmpStage, instructionText);
                fs::remove_all(tmpStage);

                // FIX 5: Only write cache if --no-cache is NOT set
                if (!noCache) {
                    storeCache(cacheKey, newDigest);
                }

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

            // FIX 9: Create WORKDIR in rootfs before RUN executes
            if (!currentWorkdir.empty() && currentWorkdir != "/") {
                fs::create_directories(fs::path(tempRootfs) / currentWorkdir.substr(1));
            }

            // FIX 3: Use rawText for cache key
            string cacheKey = computeCacheKey(previousLayerDigest,
                                              ins.rawText,
                                              currentWorkdir,
                                              currentEnv);

            string cachedDigest = "";
            if (!noCache && !cacheBroken) {
                cachedDigest = checkCache(cacheKey);
            }

            auto stepStart = Clock::now();
            string instructionText = ins.rawText;

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
                // Cache miss — execute command and capture full delta
                cacheBroken = true;

                // ── STEP 1: Snapshot rootfs before execution ──
                auto beforeSnap = snapshotDir(tempRootfs);

                // ── STEP 2: Run command inside isolated namespace ──
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

                // ── STEP 4: FIX 10 — Full delta: new/modified files + whiteouts ──
                auto [deltaFiles, deletedFiles] = computeFullDelta(beforeSnap, afterSnap);

                if (deltaFiles.empty() && deletedFiles.empty())
                    cerr << "Warning: RUN produced no filesystem changes\n";

                // ── STEP 5: Stage delta files + OCI whiteout markers ──
                string tmpStage = "/tmp/docksmith_run_stage_" + to_string(getpid());
                fs::remove_all(tmpStage);
                stageFiles(tempRootfs, deltaFiles, tmpStage);
                stageWhiteouts(deletedFiles, tmpStage);

                // ── STEP 6: Create a tar layer from staged delta ──
                string newDigest = createTarLayer(tmpStage, instructionText);
                fs::remove_all(tmpStage);

                // FIX 5: Only write cache if --no-cache is NOT set
                if (!noCache) {
                    storeCache(cacheKey, newDigest);
                }

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

    // ── FIX 6: Preserve 'created' timestamp on full cache-hit rebuild ──
    // If all steps were cache hits (cacheBroken is still false), try to
    // load the existing manifest's created timestamp.
    string existingCreated;
    {
        string imagePath0 = home + "/.docksmith/images/" + name + "_" + tag + ".json";
        if (fs::exists(imagePath0)) {
            Manifest existing = loadManifest(imagePath0);
            existingCreated = existing.created;
        }
    }
    bool allCacheHits = !cacheBroken;
    if (createdAt.empty()) {
        createdAt = (allCacheHits && !existingCreated.empty()) ? existingCreated : utcNow();
    }

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