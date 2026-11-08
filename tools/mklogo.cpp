#include "raylib.h"
#include "../src/logo.h"

#include <cstdio>

// Writes the logo PNG as a C byte array so the game binary carries its own icon.
int main(int argc, char **argv) {
    if (argc < 3) {
        return 1;
    }
    Image logo = makeLogoImage(256);
    if (!ExportImage(logo, argv[1])) {
        UnloadImage(logo);
        return 1;
    }
    int size = 0;
    unsigned char *data = ExportImageToMemory(logo, ".png", &size);
    UnloadImage(logo);
    if (data == nullptr) {
        return 1;
    }
    FILE *out = std::fopen(argv[2], "w");
    if (out == nullptr) {
        MemFree(data);
        return 1;
    }
    std::fprintf(out, "constexpr unsigned int kLogoPngSize = %d;\n", size);
    std::fprintf(out, "constexpr unsigned char kLogoPng[] = {");
    for (int i = 0; i < size; ++i) {
        std::fprintf(out, "%s%u,", i % 24 == 0 ? "\n    " : " ", static_cast<unsigned>(data[i]));
    }
    std::fprintf(out, "\n};\n");
    std::fclose(out);
    MemFree(data);
    return 0;
}
