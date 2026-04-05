#include "app.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[])
{
    // Parse command-line timeout parameter: --timeout <seconds>
    int timeoutSeconds = 0;  // 0 = no timeout (default)
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            int parsed = std::atoi(argv[i + 1]);
            if (parsed > 0) {
                timeoutSeconds = parsed;
                printf("Starting with timeout: %d seconds\n", timeoutSeconds);
            }
            ++i;  // Skip the next argument
        }
    }

    App app;
    app.setTimeout(timeoutSeconds);
    return app.run();
}
