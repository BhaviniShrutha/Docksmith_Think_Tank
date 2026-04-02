#pragma once
#include <string>
#include <vector>

struct LayerEntry {
    std::string digest;
    size_t size;
    std::string createdBy;
};

// SHA-256 utilities (zero-padded hex output)
std::string sha256_hex(const std::string& data);
std::string sha256_file(const std::string& path);

// Create a tar layer from staged files; returns "sha256:<hex>"
std::string createTarLayer(const std::string& stagingDir, const std::string& createdBy);

// Extract a .tar layer into destDir
void extractLayer(const std::string& tarPath, const std::string& destDir);

// ── Manifest I/O ──
struct Manifest {
    std::string name, tag, digest, created;
    std::vector<std::string> env;
    std::vector<std::string> cmd;
    std::string workingDir;
    std::vector<LayerEntry> layers;
};

Manifest loadManifest(const std::string& path);