#include "updater.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr const char *SourceUrl =
    "https://github.com/Somebody53/OverKill-opensource/archive/refs/heads/main.tar.gz";
constexpr size_t MaxLines = 2000;

void pushLine(UpdateTask &t, const std::string &line) {
    std::lock_guard<std::mutex> lock(t.m);
    t.lines.push_back(line);
    if (t.lines.size() > MaxLines) t.lines.erase(t.lines.begin(), t.lines.begin() + MaxLines / 4);
}

// Reads the protocol lines printed by the build script.
void handleLine(UpdateTask &t, const std::string &line) {
    if (line.rfind("TOTAL ", 0) == 0) {
        t.total = std::atoi(line.c_str() + 6);
        return;
    }
    if (line.rfind("STAGE ", 0) == 0) {
        std::lock_guard<std::mutex> lock(t.m);
        t.stage = line.substr(6);
        return;
    }
    if (line.find(" -c ") != std::string::npos) ++t.done;
    pushLine(t, line);
}

#ifndef _WIN32
void runCommand(std::shared_ptr<UpdateTask> t, const std::string &cmd) {
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) {
        pushLine(*t, "could not start the shell");
        t->exitCode = -1;
        t->running = false;
        return;
    }
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) {
        std::string s = buf;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        handleLine(*t, s);
    }
    const int status = pclose(p);
    t->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    t->running = false;
}
#endif

std::string shellQuote(const std::string &s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return out + "'";
}

std::string buildScript(const std::string &outDir) {
    std::ostringstream s;
    s << "exec 2>&1\n"
      << "set -e\n"
      << "OUT=" << shellQuote(outDir) << "\n";
    s << R"SH(
URL='https://github.com/Somebody53/OverKill-opensource/archive/refs/heads/main.tar.gz'
echo "STAGE download"
tmp=$(mktemp -d /tmp/overkill-update.XXXXXX)
trap 'rm -rf "$tmp"' EXIT
curl -fL --max-time 600 -o "$tmp/src.tar.gz" "$URL"
echo "STAGE extract"
tar -xzf "$tmp/src.tar.gz" -C "$tmp"
mk=$(find "$tmp" -maxdepth 3 -name Makefile | head -n 1)
if [ -z "$mk" ]; then echo "ERR no Makefile in the downloaded source"; exit 1; fi
cd "$(dirname "$mk")"
command -v cargo >/dev/null 2>&1 || { echo "ERR cargo is needed for the AI library (install Rust)"; exit 1; }
goals="build/OverKillOpensource mods"
win=0
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then win=1; goals="$goals windows"; fi
total=$(make -n $goals 2>/dev/null | grep -c -- ' -c ' || true)
echo "TOTAL $total"
echo "STAGE build"
make -j"$(nproc)" $goals
echo "STAGE copy"
mkdir -p "$OUT/linux" "$OUT/windows" "$OUT/mods"
cp build/OverKillOpensource "$OUT/linux/"
if [ "$win" = 1 ]; then cp build/windows/OverKillOpensource.exe "$OUT/windows/"; fi
cp -r build/mods/. "$OUT/mods/"
if [ -d music ]; then rm -rf "$OUT/music"; cp -r music "$OUT/"; fi
if [ -d assets ]; then rm -rf "$OUT/assets"; cp -r assets "$OUT/"; fi
echo "OUTPUT $OUT"
echo "STAGE done"
)SH";
    return s.str();
}

} // namespace

void startUpdateBuild(std::shared_ptr<UpdateTask> task, const std::string &outDir) {
    {
        std::lock_guard<std::mutex> lock(task->m);
        task->lines.clear();
        task->stage.clear();
    }
    task->total = 0;
    task->done = 0;
    task->exitCode = -1;

#ifdef _WIN32
    pushLine(*task, "The in-game update is only available on Linux for now.");
    task->exitCode = 1;
#else
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "overkill-update.sh";
    {
        std::ofstream out(script);
        out << buildScript(outDir);
    }
    task->running = true;
    const std::string cmd = "sh " + shellQuote(script.string()) + " 2>&1";
    std::thread([task, cmd] { runCommand(task, cmd); }).detach();
#endif
}

bool installUpdate(const std::string &stagingDir, const std::string &installDir) {
#ifdef _WIN32
    (void)stagingDir;
    (void)installDir;
    return false;
#else
    namespace fs = std::filesystem;
    if (!fs::exists(fs::path(stagingDir) / "linux" / "OverKillOpensource")) return false;

    // Runs after this process exits, so the old binary is no longer in use.
    const fs::path script = fs::temp_directory_path() / "overkill-install.sh";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\n"
            << "while kill -0 " << getpid() << " 2>/dev/null; do sleep 1; done\n"
            << "INSTALL=" << shellQuote(installDir) << "\n"
            << "STAGE=" << shellQuote(stagingDir) << "\n"
            << "exec >\"$INSTALL/update.log\" 2>&1\n"
            << "set -e\n"
            << "rm -f \"$INSTALL/OverKillOpensource\"\n"
            << "cp \"$STAGE/linux/OverKillOpensource\" \"$INSTALL/\"\n"
            << "chmod +x \"$INSTALL/OverKillOpensource\"\n"
            << "mkdir -p \"$INSTALL/mods\"\n"
            << "cp -r \"$STAGE/mods/.\" \"$INSTALL/mods/\"\n"
            << "if [ -d \"$STAGE/music\" ]; then rm -rf \"$INSTALL/music\"; cp -r \"$STAGE/music\" \"$INSTALL/\"; fi\n"
            << "if [ -d \"$STAGE/assets\" ]; then rm -rf \"$INSTALL/assets\"; cp -r \"$STAGE/assets\" \"$INSTALL/\"; fi\n"
            << "rm -rf \"$STAGE\"\n"
            << "cd \"$INSTALL\" && exec ./OverKillOpensource\n";
    }
    const std::string cmd = "nohup sh " + shellQuote(script.string()) + " >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

std::vector<std::string> tailLines(UpdateTask &task, size_t count) {
    std::lock_guard<std::mutex> lock(task.m);
    const size_t start = task.lines.size() > count ? task.lines.size() - count : 0;
    return std::vector<std::string>(task.lines.begin() + start, task.lines.end());
}

std::string currentStage(UpdateTask &task) {
    std::lock_guard<std::mutex> lock(task.m);
    return task.stage;
}
