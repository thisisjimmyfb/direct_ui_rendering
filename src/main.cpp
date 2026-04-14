#include "app.h"
#include <cstdio>
#include <cstdlib>

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(android_app* state)
{
    App app;
    app.setAndroidApp(state);
    app.run();
}

#else

int main(int argc, char* argv[])
{
    int timeoutSeconds = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            int parsed = std::atoi(argv[i + 1]);
            if (parsed > 0) {
                timeoutSeconds = parsed;
                printf("Starting with timeout: %d seconds\n", timeoutSeconds);
            }
            ++i;
        }
    }

    App app;
    app.setTimeout(timeoutSeconds);
    return app.run();
}

#endif
