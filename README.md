# BestDM

BestDM is a Qt 6/C++20 download manager in active development. Its visual system is derived from the supplied blue, aqua, and green download-mark icon, while retaining accessible contrast in both themes.

## Current implementation status

- Qt Quick light/dark shell using the supplied icon
- HTTP/HTTPS stream to a `.part` file with TLS verification left at Qt's secure default
- `QAbstractListModel` download list backed by a persistent SQLite queue
- HTTP/HTTPS batch intake, configurable concurrent transfers, pause/resume/cancel, retry, and history filters
- Configurable 1–500 URL batch bound, enforced during review and again before queueing
- Persisted Rename/Ask/Overwrite collision handling; replacement occurs only after a successful transfer
- Named download queues with selection, task reassignment, and per-queue list/counter filtering (runtime unverified)
- Streamed `.part` writes, validated HTTP Range resume, percentage/current and average speed/ETA, and collision-safe finalization
- Single-stream resume continues from the next verified offset when a server caps a valid `206` range; redirects use a no-less-safe policy and stop after 10 hops
- Configurable per-file 1–16 HTTP connections with resumable validated segments, ordered merge and single-stream fallback (source implemented, runtime unverified)
- Persisted System/Light/Dark theme, concurrency, retry, and shared global bandwidth limit preferences
- Editable extension/host category rules, persistent category labels, and category filtering
- Optional automatic category subfolders under the destination selected for each batch
- Asynchronous HTTP metadata probes for new downloads (size, content type, validators, advertised ranges, and safe server filename suggestions)
- HTTP server authentication prompts with session-only credentials for single-stream and segmented requests (not yet runtime-verified)
- Recurring weekday/time windows with overnight scheduling and automatic pause/resume at schedule boundaries
- Persisted default download folder with an override available for each batch
- Per-task destination changes, including completed outputs and resumable partial files; collision-safe with cross-volume copy fallback (runtime unverified)
- Multi-select task rows and apply bulk pause/resume/cancel/remove actions
- Import newline-separated URLs from bounded UTF-8 `.txt` files
- Drop one or more links onto the window to review them in the Add dialog
- Optional default-off clipboard URL prompt; links are never queued without confirmation
- Direct/system/manual HTTP or SOCKS5 proxy settings with an asynchronous connection test; Windows stores proxy passwords in Credential Manager
- Native system tray with configurable minimize/close behavior and filename-only completion/failure notifications (runtime unverified)
- Persisted after-download choice to do nothing, open the completed file, or open its folder (runtime unverified)
- Optional selected executable after completion receives the file path through `QProcess` without a shell and has a two-minute timeout (runtime unverified)
- Optional Microsoft Defender scan before other completion actions (runtime and policy behavior unverified)
- Optional Windows shutdown/sleep/hibernate after queue completion requires confirmation plus a 60-second cancelable countdown (runtime unverified)
- Optional Windows per-user `bestdm://` link registration; validated links open Add for review and single-instance forwarding keeps them in the running app (runtime unverified)
- Bounded application-local Qt logs with redacted, user-initiated text export (source implemented; tests and runtime unverified)

This remains an unverified early implementation: the Qt 6.12 toolchain is missing from the current environment, so the app, QML, and authored tests have not been compiled or run. Segmented HTTP, extension-changing server filenames, tray/notifications, per-task destination changes, file/folder/custom-program completion actions, optional Defender scanning, confirmed Windows queue power actions, and custom protocol/single-instance handling are implemented in source but unverified; FTP/SFTP, media extraction, browser integration, and packaging remain incomplete. See [docs/IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) and [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md) for evidence-based status.

## Build

The repository includes the Visual Studio/Qt project and a CMake entry point. The intended configuration is Windows x64 with Qt 6.12 MSVC 2022. Install that exact Qt kit, CMake, and Visual Studio C++ tools before building:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' BestDM.slnx /t:Build /p:Configuration=Debug /p:Platform=x64
```

For CMake, point `CMAKE_PREFIX_PATH` at the Qt installation:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH='C:/Qt/6.12.0/msvc2022_64' -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The current developer environment does not have the referenced Qt installation or CMake. The Visual Studio baseline stops because `rcc.exe` cannot be found. These commands have not yet been verified here.

## Tests

The CMake test build includes domain/persistence, list proxy, HTTP/queue, bandwidth limiter, category, schedule, and diagnostic logging suites. After configuring with `-DBUILD_TESTING=ON`, run them with:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Qt Test executables and the CTest workflow have not yet been compiled or run in this environment.

## Architecture

The architecture and current gaps are documented in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The QML observes the download list model and calls typed manager/queue actions; transfer behavior stays in C++.
