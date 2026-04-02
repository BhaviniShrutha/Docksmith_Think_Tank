#pragma once
#include <string>
#include <map>
#include <vector>

// Run a command in an isolated namespace (clone + chroot)
int runIsolated(const std::string& rootfs, const std::string& workdir,
                const std::map<std::string, std::string>& env,
                const std::vector<std::string>& cmd);

// High-level: load image, extract layers, run isolated
void runContainer(const std::string& name, const std::string& tag,
                  const std::map<std::string, std::string>& envOverrides,
                  const std::vector<std::string>& cmdOverride);

// List all images in ~/.docksmith/images/
void listImages();

// Remove an image and its layers
void removeImage(const std::string& name, const std::string& tag);