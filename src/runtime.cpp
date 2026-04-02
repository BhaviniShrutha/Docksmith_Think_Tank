#include "runtime.h"
#include "layer.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <signal.h>
#include <set>

namespace fs = std::filesystem;
using namespace std;

#define STACK_SIZE (1024 * 1024)

// ── Data passed to the clone'd child ──
struct ChildData {
    const char* rootfs;
    const char* workdir;
    char** envp;
    char** argv;
};

static int childFunc(void* arg) {
    ChildData* d = (ChildData*)arg;

    if (chroot(d->rootfs) != 0) { perror("chroot"); _exit(1); }
    if (chdir(d->workdir) != 0) { perror("chdir"); _exit(1); }

    // Set environment
    clearenv();
    for (char** e = d->envp; *e; e++) {
        string s = *e;
        auto eq = s.find('=');
        if (eq != string::npos)
            setenv(s.substr(0, eq).c_str(), s.substr(eq + 1).c_str(), 1);
    }

    execvp(d->argv[0], d->argv);
    perror("execvp");
    _exit(1);
}

// ──────────────────────────────────────────────
// Run a command in an isolated Linux namespace
// ──────────────────────────────────────────────
int runIsolated(const string& rootfs, const string& workdir,
                const map<string,string>& env, const vector<string>& cmd) {
    if (cmd.empty()) { cerr << "Error: No command specified\n"; return -1; }

    string wd = workdir.empty() ? "/" : workdir;

    // Build env C-strings
    vector<string> envStrs;
    bool hasPath = false;
    for (auto& [k, v] : env) {
        envStrs.push_back(k + "=" + v);
        if (k == "PATH") hasPath = true;
    }
    if (!hasPath)
        envStrs.push_back("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");

    vector<char*> envp;
    for (auto& s : envStrs) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    // Build argv C-strings
    vector<char*> argv;
    for (auto& s : cmd) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    ChildData data;
    data.rootfs  = rootfs.c_str();
    data.workdir = wd.c_str();
    data.envp    = envp.data();
    data.argv    = argv.data();

    void* stack = malloc(STACK_SIZE);
    if (!stack) { perror("malloc"); return -1; }

    pid_t pid = clone(childFunc, (char*)stack + STACK_SIZE,
                      CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET | SIGCHLD,
                      &data);  // FIX 15: CLONE_NEWNET isolates network
    if (pid == -1) { perror("clone"); free(stack); return -1; }

    int status;
    waitpid(pid, &status, 0);
    free(stack);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// ──────────────────────────────────────────────
// docksmith run
// ──────────────────────────────────────────────
void runContainer(const string& name, const string& tag,
                  const map<string,string>& envOverrides,
                  const vector<string>& cmdOverride) {
    string home = getenv("HOME");
    string mpath = home + "/.docksmith/images/" + name + "_" + tag + ".json";

    if (!fs::exists(mpath)) {
        cerr << "Error: Image " << name << ":" << tag << " not found\n";
        exit(1);  // FIX 14
    }

    Manifest m = loadManifest(mpath);

    // Extract layers into temp rootfs
    string rootfs = "/tmp/docksmith_run_" + to_string(getpid());
    fs::remove_all(rootfs);
    fs::create_directories(rootfs);

    for (auto& layer : m.layers) {
        string tp = home + "/.docksmith/layers/" + layer.digest + ".tar";
        if (!fs::exists(tp)) {
            cerr << "Error: Missing layer " << layer.digest << "\n";
            fs::remove_all(rootfs);
            exit(1);  // FIX 14
        }
        extractLayer(tp, rootfs);
    }

    // Ensure workdir exists
    if (!m.workingDir.empty())
        fs::create_directories(rootfs + m.workingDir);

    // Merge env: image defaults + overrides
    map<string,string> merged;
    for (auto& e : m.env) {
        auto eq = e.find('=');
        if (eq != string::npos) merged[e.substr(0, eq)] = e.substr(eq + 1);
    }
    for (auto& [k, v] : envOverrides) merged[k] = v;

    // Determine command
    vector<string> finalCmd = cmdOverride.empty() ? m.cmd : cmdOverride;
    if (finalCmd.empty()) {
        cerr << "Error: No command specified and image has no default CMD\n";
        fs::remove_all(rootfs);
        exit(1);  // FIX 14
    }

    int exitCode = runIsolated(rootfs, m.workingDir, merged, finalCmd);
    cout << "Container exited with code: " << exitCode << endl;

    fs::remove_all(rootfs);
}

// ──────────────────────────────────────────────
// docksmith images
// ──────────────────────────────────────────────
void listImages() {
    string home = getenv("HOME");
    string imageDir = home + "/.docksmith/images/";

    if (!fs::exists(imageDir) || fs::is_empty(imageDir)) {
        cout << "No images found.\n";
        return;
    }

    cout << left << setw(15) << "NAME"
         << setw(15) << "TAG"
         << setw(18) << "ID"
         << "CREATED" << endl;

    for (auto& entry : fs::directory_iterator(imageDir)) {
        if (entry.path().extension() != ".json") continue;
        Manifest m = loadManifest(entry.path().string());
        string id = m.digest.length() > 7 ? m.digest.substr(7, 12) : m.digest;
        cout << left << setw(15) << m.name
             << setw(15) << m.tag
             << setw(18) << id
             << m.created << endl;
    }
}

// ──────────────────────────────────────────────
// docksmith rmi
// ──────────────────────────────────────────────
void removeImage(const string& name, const string& tag) {
    string home = getenv("HOME");
    string mpath = home + "/.docksmith/images/" + name + "_" + tag + ".json";

    if (!fs::exists(mpath)) {
        cerr << "Error: Image " << name << ":" << tag << " not found\n";
        exit(1);  // FIX 14
    }

    Manifest m = loadManifest(mpath);

    // FIX 13: Delete all layers unconditionally (no reference counting per spec)
    for (auto& layer : m.layers) {
        string lp = home + "/.docksmith/layers/" + layer.digest + ".tar";
        if (fs::exists(lp)) {
            fs::remove(lp);
            cout << "Deleted layer: " << layer.digest << endl;
        }
    }

    fs::remove(mpath);
    cout << "Removed image: " << name << ":" << tag << endl;
}