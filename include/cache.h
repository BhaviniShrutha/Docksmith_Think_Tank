#pragma once
#include <string>
#include <map>
#include <vector>

// Compute cache key from build state + instruction + optional file hashes
std::string computeCacheKey(const std::string& prevDigest,
                            const std::string& instruction,
                            const std::string& workdir,
                            const std::map<std::string, std::string>& env,
                            const std::vector<std::string>& fileHashes = {});

// Returns layer digest if cache hit (and layer tar exists), else ""
std::string checkCache(const std::string& key);

// Store cache mapping: key -> layer digest
void storeCache(const std::string& key, const std::string& layerDigest);