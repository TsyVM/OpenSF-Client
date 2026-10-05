// PF-1, PF-11 (Docs/UniversalServerDeploy.md): the macOS and Android games hold no server of any
// kind, not "This PC", not a practice server. Every server header includes this, so a build that
// compiles any of the server for either system stops here instead of shipping it.
#pragma once

#if defined(__ANDROID__) || defined(__APPLE__)
#error "PF-11: the server is never built into a macOS or Android game (hosting is Windows and Linux only, D9)"
#endif
