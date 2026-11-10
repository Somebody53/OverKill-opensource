#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// State shared between the background build thread and the UI.
struct UpdateTask {
    std::atomic<bool> running{false};
    std::atomic<int> exitCode{-1};
    std::atomic<int> total{0};  // compile steps expected
    std::atomic<int> done{0};   // compile steps finished
    std::mutex m;
    std::vector<std::string> lines;  // guarded by m
    std::string stage;               // guarded by m
};

// Downloads the newest source from GitHub and builds the Linux (and Windows, if MinGW exists) client into outDir.
void startUpdateBuild(std::shared_ptr<UpdateTask> task, const std::string &outDir);

// Replaces the installed game files from stagingDir and restarts the game once this process exits.
// Settings are not touched. Returns false if nothing could be installed.
bool installUpdate(const std::string &stagingDir, const std::string &installDir);

std::vector<std::string> tailLines(UpdateTask &task, size_t count);
std::string currentStage(UpdateTask &task);
