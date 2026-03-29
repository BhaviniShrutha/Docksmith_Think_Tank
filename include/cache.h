#pragma once
#include <string>

std::string generate_cache_key(const std::string& input);
bool check_cache(const std::string& key);
void store_cache(const std::string& key, const std::string& layerHash);