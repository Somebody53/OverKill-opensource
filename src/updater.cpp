#include "updater.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
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

void runCommand(std::shared_ptr<UpdateTask> t, const std::string &cmd) {
#ifdef _WIN32
    FILE *p = _popen(cmd.c_str(), "r");
#else
    FILE *p = popen(cmd.c_str(), "r");
#endif
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
#ifdef _WIN32
    t->exitCode = _pclose(p);
#else
    const int status = pclose(p);
    t->exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    t->running = false;
}

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

#ifdef _WIN32
std::string winQuote(const std::string &s) {
    return "\"" + s + "\"";
}

std::string windowsBuildScript() {
    return R"PS(param([string]$OutDir, [string]$ToolsDir)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$headers = @{ 'User-Agent' = 'OverKillUpdater' }

function Fail([string]$msg) {
    Write-Output "ERR $msg"
    throw $msg
}

Write-Output 'STAGE tools'
New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null

# Reuse tools downloaded by an earlier update.
$devBin = Join-Path $ToolsDir 'w64devkit\bin'
$cargoHome = Join-Path $ToolsDir 'cargo'
$rustupHome = Join-Path $ToolsDir 'rustup'
if (Test-Path (Join-Path $devBin 'g++.exe')) { $env:Path = "$devBin;$env:Path" }
if (Test-Path (Join-Path $cargoHome 'bin\cargo.exe')) {
    $env:CARGO_HOME = $cargoHome
    $env:RUSTUP_HOME = $rustupHome
    $env:Path = (Join-Path $cargoHome 'bin') + ";$env:Path"
}

# g++ and make: w64devkit is downloaded only when no suitable compiler is installed.
$gppOk = (Get-Command g++ -ErrorAction SilentlyContinue) -and (Get-Command make -ErrorAction SilentlyContinue) -and (((& g++ -dumpmachine) | Out-String) -match 'x86_64-w64-mingw')
if (-not $gppOk) {
    $rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/skeeto/w64devkit/releases/latest' -Headers $headers
    $asset = $rel.assets | Where-Object { $_.name -like 'w64devkit-x64-*.7z.exe' } | Select-Object -First 1
    if (-not $asset) { Fail 'the MinGW compiler release was not found' }
    $sfx = Join-Path $ToolsDir $asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $sfx -Headers $headers
    & $sfx "-o$ToolsDir" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail 'the MinGW compiler could not be extracted' }
    Remove-Item -LiteralPath $sfx -Force
    if (-not (Test-Path (Join-Path $devBin 'g++.exe'))) { Fail 'the MinGW compiler folder was not found' }
    $env:Path = "$devBin;$env:Path"
}

# Rust (cargo) for the AI library.
if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
    $env:CARGO_HOME = $cargoHome
    $env:RUSTUP_HOME = $rustupHome
    $init = Join-Path $ToolsDir 'rustup-init.exe'
    Invoke-WebRequest -Uri 'https://win.rustup.rs/x86_64' -OutFile $init
    & $init -y --profile minimal --default-toolchain stable --default-host x86_64-pc-windows-gnu --no-modify-path
    if ($LASTEXITCODE -ne 0) { Fail 'Rust could not be installed' }
    $env:Path = (Join-Path $cargoHome 'bin') + ";$env:Path"
}
if (Get-Command rustup -ErrorAction SilentlyContinue) {
    & rustup target add x86_64-pc-windows-gnu | Out-Null
}

Write-Output 'STAGE download'
$tmp = Join-Path $env:TEMP ('overkill-update-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    $zip = Join-Path $tmp 'src.zip'
    Invoke-WebRequest -Uri 'https://github.com/Somebody53/OverKill-opensource/archive/refs/heads/main.zip' -OutFile $zip -Headers $headers
    Write-Output 'STAGE extract'
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp
    $mk = Get-ChildItem -LiteralPath $tmp -Filter Makefile -Recurse -Depth 3 | Select-Object -First 1
    if (-not $mk) { Fail 'no Makefile in the downloaded source' }
    Set-Location -LiteralPath $mk.DirectoryName

    $cfg = @('WIN_CXX=g++', 'WIN_CC=gcc', 'WIN_AR=ar')
    $goals = @('windows', 'mods')
    $dry = & make -n @cfg @goals 2>$null
    $total = @($dry | Where-Object { $_ -like '* -c *' }).Count
    Write-Output "TOTAL $total"
    Write-Output 'STAGE build'
    & make -j $env:NUMBER_OF_PROCESSORS @cfg @goals
    if ($LASTEXITCODE -ne 0) { Fail "make failed with exit code $LASTEXITCODE" }

    Write-Output 'STAGE copy'
    $winOut = Join-Path $OutDir 'windows'
    $modsOut = Join-Path $OutDir 'mods'
    New-Item -ItemType Directory -Force -Path $winOut, $modsOut | Out-Null
    Copy-Item -LiteralPath 'build\windows\OverKillOpensource.exe' -Destination $winOut -Force
    Copy-Item -Path 'build\mods\*' -Destination $modsOut -Recurse -Force
    foreach ($dir in @('music', 'assets')) {
        if (Test-Path $dir) {
            $dest = Join-Path $OutDir $dir
            if (Test-Path $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
            Copy-Item -LiteralPath $dir -Destination $OutDir -Recurse -Force
        }
    }
    Write-Output "OUTPUT $OutDir"
    Write-Output 'STAGE done'
}
finally {
    Set-Location -LiteralPath $env:TEMP
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
)PS";
}
#endif

} // namespace

void startUpdateBuild(std::shared_ptr<UpdateTask> task, const std::string &outDir, const std::string &toolsDir) {
    {
        std::lock_guard<std::mutex> lock(task->m);
        task->lines.clear();
        task->stage.clear();
    }
    task->total = 0;
    task->done = 0;
    task->exitCode = -1;

    std::string cmd;
#ifdef _WIN32
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "overkill-update.ps1";
    {
        std::ofstream out(script);
        out << windowsBuildScript();
    }
    cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File " + winQuote(script.string()) + " " +
          winQuote(outDir) + " " + winQuote(toolsDir) + " 2>&1";
#else
    (void)toolsDir;
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "overkill-update.sh";
    {
        std::ofstream out(script);
        out << buildScript(outDir);
    }
    cmd = "sh " + shellQuote(script.string()) + " 2>&1";
#endif
    task->running = true;
    std::thread([task, cmd] { runCommand(task, cmd); }).detach();
}

bool installUpdate(const std::string &stagingDir, const std::string &installDir) {
#ifdef _WIN32
    namespace fs = std::filesystem;
    if (!fs::exists(fs::path(stagingDir) / "windows" / "OverKillOpensource.exe")) return false;

    // Waits for this process to exit, then copies the files and restarts the game.
    const fs::path script = fs::temp_directory_path() / "overkill-install.cmd";
    {
        std::ofstream out(script);
        out << "@echo off\r\n"
            << "set \"PID=" << GetCurrentProcessId() << "\"\r\n"
            << "set \"INSTALL=" << installDir << "\"\r\n"
            << "set \"STAGE=" << stagingDir << "\"\r\n"
            << ":wait\r\n"
            << "tasklist /FI \"PID eq %PID%\" 2>nul | find \"%PID%\" >nul\r\n"
            << "if not errorlevel 1 (ping -n 2 127.0.0.1 >nul & goto wait)\r\n"
            << "call :install > \"%INSTALL%\\update.log\" 2>&1\r\n"
            << "if errorlevel 1 exit /b 1\r\n"
            << "start \"\" /D \"%INSTALL%\" OverKillOpensource.exe\r\n"
            << "exit /b 0\r\n"
            << ":install\r\n"
            << "copy /Y \"%STAGE%\\windows\\OverKillOpensource.exe\" \"%INSTALL%\\OverKillOpensource.exe\" || exit /b 1\r\n"
            << "if not exist \"%INSTALL%\\mods\" mkdir \"%INSTALL%\\mods\"\r\n"
            << "xcopy /E /I /Y \"%STAGE%\\mods\" \"%INSTALL%\\mods\" || exit /b 1\r\n"
            << "if exist \"%STAGE%\\music\" (rmdir /S /Q \"%INSTALL%\\music\" & xcopy /E /I /Y \"%STAGE%\\music\" \"%INSTALL%\\music\" || exit /b 1)\r\n"
            << "if exist \"%STAGE%\\assets\" (rmdir /S /Q \"%INSTALL%\\assets\" & xcopy /E /I /Y \"%STAGE%\\assets\" \"%INSTALL%\\assets\" || exit /b 1)\r\n"
            << "rmdir /S /Q \"%STAGE%\"\r\n"
            << "exit /b 0\r\n";
    }
    std::string cmdline = "cmd.exe /c \"" + script.string() + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (!started) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
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
