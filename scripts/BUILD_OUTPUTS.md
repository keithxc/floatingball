# Local build output boundary

On mm4 these wrappers require the deployed `~/mygit/tools/build-paths.py` and
its verified external-volume identity/free-space checks. Missing resolver,
missing/wrong disk, insufficient reserve or internal override fails before
creating output or launching build/test tools. Other hosts and standalone CI
checkouts discover the shared resolver when installed and retain a portable
fallback when it is absent. Explicit output roots are validated as exact roots;
they do not acquire another project/checkout hierarchy. Compiler caches, SwiftPM
scratch/cache/config/security (where used), Xcode DerivedData/package caches,
Python bytecode, temporary files, evidence and tool outputs are configured via
actual flags/environment. Application-producing paths require `.noindex`.

Use `bash scripts/build.sh [build|test]` for local CMake builds. It resolves
and validates FLOATINGBALL_BINARY_DIR (default external `cmake.noindex`) before
CMake; CTest output, JUnit and logs are explicit external paths. Existing CMake
binary-directory test fixtures and actool temp/assets remain beneath that
validated binary tree. `generate-icons.py --output ABSOLUTE_PATH` validates
its staging destination before image-tool execution and sets compiler/image
temp/cache paths. There is no existing CPack/Android/Xcode project wrapper here;
Nix derivations and SDK/dependency management remain outside this change.

These are wrapper guarantees. Direct `swift`, `xcodebuild`, `cmake`, IDE/GUI
builds or direct invocation of a generated benchmark binary bypass this setup;
the caller must supply the same paths and preflight itself. SDKs, runtimes,
source-generation outputs and app runtime storage retain their managed paths.
No historical output is automatically migrated or deleted.
