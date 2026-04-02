#include "layer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <cstring>
#include <openssl/sha.h>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>

namespace fs = std::filesystem;
using namespace std;

// ──────────────────────────────────────────────
// SHA-256 with proper zero-padded hex output
// ──────────────────────────────────────────────
string sha256_hex(const string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << hex << setfill('0') << setw(2) << (int)hash[i];
    }
    return ss.str();
}

string sha256_file(const string& path) {
    ifstream f(path, ios::binary);
    if (!f.is_open()) return "";
    string content((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    return sha256_hex(content);
}

// ──────────────────────────────────────────────
// Create a tar layer from a staging directory
// ──────────────────────────────────────────────
string createTarLayer(const string& stagingDir, const string& /*createdBy*/) {
    // 1. Collect all entries (regular files, symlinks, directories)
    vector<string> relPaths;
    for (auto& p : fs::recursive_directory_iterator(stagingDir,
             fs::directory_options::skip_permission_denied)) {
        string rel = fs::relative(p.path(), stagingDir).string();
        // Include regular files, symlinks, and directories
        if (fs::is_regular_file(p) || fs::is_symlink(p.path()) || fs::is_directory(p))
            relPaths.push_back(rel);
    }
    // 2. Sort lexicographically (reproducibility)
    sort(relPaths.begin(), relPaths.end());

    // 3. Write tar to a temp file
    string tempTar = "/tmp/docksmith_layer_" + to_string(getpid()) + "_tmp.tar";

    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    if (archive_write_open_filename(a, tempTar.c_str()) != ARCHIVE_OK) {
        cerr << "Error: Failed to create tar: " << archive_error_string(a) << endl;
        archive_write_free(a);
        exit(1);
    }

    for (auto& rel : relPaths) {
        string fullPath = stagingDir + "/" + rel;

        struct stat st;
        if (::lstat(fullPath.c_str(), &st) != 0) continue;  // lstat to detect symlinks

        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, rel.c_str());

        // FIX 11: Normalize all metadata for reproducibility
        archive_entry_set_uid(entry, 0);
        archive_entry_set_gid(entry, 0);
        archive_entry_set_uname(entry, "root");
        archive_entry_set_gname(entry, "root");
        archive_entry_set_mtime(entry, 0, 0);
        archive_entry_set_atime(entry, 0, 0);
        archive_entry_set_ctime(entry, 0, 0);

        if (S_ISLNK(st.st_mode)) {
            // FIX 10 support: symlink entry
            char linkBuf[4096] = {};
            ssize_t len = ::readlink(fullPath.c_str(), linkBuf, sizeof(linkBuf) - 1);
            if (len < 0) { archive_entry_free(entry); continue; }
            archive_entry_set_filetype(entry, AE_IFLNK);
            archive_entry_set_symlink(entry, linkBuf);
            archive_entry_set_size(entry, 0);
            archive_entry_set_perm(entry, 0777);
            archive_write_header(a, entry);
        } else if (S_ISDIR(st.st_mode)) {
            // FIX 10 support: directory entry
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_size(entry, 0);
            archive_entry_set_perm(entry, st.st_mode & 07777);
            archive_write_header(a, entry);
        } else {
            // Regular file (including whiteout files)
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_size(entry, st.st_size);
            archive_entry_set_perm(entry, st.st_mode & 07777);
            archive_write_header(a, entry);

            ifstream f(fullPath, ios::binary);
            char buf[8192];
            while (f) {
                f.read(buf, sizeof(buf));
                if (f.gcount() > 0) archive_write_data(a, buf, f.gcount());
            }
        }

        archive_entry_free(entry);
    }

    archive_write_close(a);
    archive_write_free(a);

    // 4. Hash the tar bytes
    string tarHash = sha256_file(tempTar);
    string digest = "sha256:" + tarHash;

    // 5. Move to layers directory
    string home = getenv("HOME");
    string layerDir = home + "/.docksmith/layers/";
    fs::create_directories(layerDir);
    string destPath = layerDir + digest + ".tar";

    if (fs::exists(destPath)) {
        fs::remove(tempTar);
    } else {
        fs::rename(tempTar, destPath);
    }

    return digest;
}

// ──────────────────────────────────────────────
// Extract a tar layer into a destination directory
// ──────────────────────────────────────────────
void extractLayer(const string& tarPath, const string& destDir) {
    struct archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_format_gnutar(a);
    archive_read_support_filter_none(a);

    if (archive_read_open_filename(a, tarPath.c_str(), 10240) != ARCHIVE_OK) {
        cerr << "Error: Cannot open tar " << tarPath << ": "
             << archive_error_string(a) << endl;
        archive_read_free(a);
        return;
    }

    struct archive* ext = archive_write_disk_new();
    archive_write_disk_set_options(ext,
        ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME);

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        string origPath = archive_entry_pathname(entry);

        // ── FIX 1: OCI whiteout handling ──────────────────────
        // A whiteout entry (.wh.<name>) marks the deletion of the real
        // file <name> from a lower layer. We must delete the real file
        // and NOT extract the whiteout marker itself.
        string filename  = fs::path(origPath).filename().string();
        string parentDir = fs::path(origPath).parent_path().string();
        if (filename.size() > 4 && filename.substr(0, 4) == ".wh.") {
            string realName = filename.substr(4); // strip .wh. prefix
            fs::path toDelete = fs::path(destDir) / parentDir / realName;
            if (fs::exists(toDelete)) {
                fs::remove_all(toDelete);
            }
            // Skip this entry — do not write the whiteout file to disk
            archive_read_data_skip(a);
            continue;
        }
        // ── end whiteout handling ──────────────────────────────

        string fullPath = destDir + "/" + origPath;
        archive_entry_set_pathname(entry, fullPath.c_str());

        // Ensure parent dir exists
        fs::create_directories(fs::path(fullPath).parent_path());

        archive_write_header(ext, entry);

        const void* buff;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            archive_write_data_block(ext, buff, size, offset);
        }
        archive_write_finish_entry(ext);
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
}

// ──────────────────────────────────────────────
// Simple JSON field extractors (for our fixed schema)
// ──────────────────────────────────────────────
static string jsonStr(const string& json, const string& key) {
    string patterns[] = {
        "\"" + key + "\": \"",
        "\"" + key + "\":\"",
    };
    for (auto& search : patterns) {
        auto pos = json.find(search);
        if (pos != string::npos) {
            pos += search.length();
            auto end = json.find("\"", pos);
            return json.substr(pos, end - pos);
        }
    }
    return "";
}

static vector<string> jsonStrArr(const string& json, const string& key) {
    vector<string> result;
    string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == string::npos) return result;
    pos = json.find("[", pos);
    if (pos == string::npos) return result;
    auto end = json.find("]", pos);
    string arr = json.substr(pos + 1, end - pos - 1);
    size_t s = 0;
    while ((s = arr.find("\"", s)) != string::npos) {
        s++;
        auto c = arr.find("\"", s);
        if (c == string::npos) break;
        result.push_back(arr.substr(s, c - s));
        s = c + 1;
    }
    return result;
}

static size_t jsonUInt(const string& json, const string& key) {
    string patterns[] = {
        "\"" + key + "\": ",
        "\"" + key + "\":",
    };
    for (auto& search : patterns) {
        auto pos = json.find(search);
        if (pos != string::npos) {
            pos += search.length();
            while (pos < json.size() && json[pos] == ' ') pos++;
            string num;
            while (pos < json.size() && isdigit(json[pos])) num += json[pos++];
            if (!num.empty()) return stoull(num);
        }
    }
    return 0;
}

// ──────────────────────────────────────────────
// Load a manifest JSON file
// ──────────────────────────────────────────────
Manifest loadManifest(const string& path) {
    ifstream f(path);
    string json((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());

    Manifest m;
    m.name      = jsonStr(json, "name");
    m.tag       = jsonStr(json, "tag");
    m.digest    = jsonStr(json, "digest");
    m.created   = jsonStr(json, "created");
    m.workingDir= jsonStr(json, "WorkingDir");

    // Parse config section
    auto cfgPos = json.find("\"config\"");
    if (cfgPos != string::npos) {
        // (cfgEnd unused — using depth-counting loop below instead)
        // Find the second } (end of config object - may span lines)
        int depth = 0;
        size_t ce = cfgPos;
        while (ce < json.size()) {
            if (json[ce] == '{') depth++;
            else if (json[ce] == '}') { depth--; if (depth == 0) break; }
            ce++;
        }
        string cfg = json.substr(cfgPos, ce - cfgPos + 1);
        m.env = jsonStrArr(cfg, "Env");
        m.cmd = jsonStrArr(cfg, "Cmd");
        if (m.workingDir.empty()) m.workingDir = jsonStr(cfg, "WorkingDir");
    }

    // Parse layers array
    auto layPos = json.find("\"layers\"");
    if (layPos != string::npos) {
        size_t lp = json.find("[", layPos);
        size_t arrEnd = lp;
        int depth = 1;
        arrEnd++;
        while (arrEnd < json.size() && depth > 0) {
            if (json[arrEnd] == '[') depth++;
            else if (json[arrEnd] == ']') depth--;
            arrEnd++;
        }
        string arr = json.substr(lp, arrEnd - lp);
        size_t objStart = 0;
        while ((objStart = arr.find("{", objStart)) != string::npos) {
            auto objEnd = arr.find("}", objStart);
            if (objEnd == string::npos) break;
            string obj = arr.substr(objStart, objEnd - objStart + 1);
            LayerEntry e;
            e.digest    = jsonStr(obj, "digest");
            e.size      = jsonUInt(obj, "size");
            e.createdBy = jsonStr(obj, "createdBy");
            m.layers.push_back(e);
            objStart = objEnd + 1;
        }
    }

    return m;
}