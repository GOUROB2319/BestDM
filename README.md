# BestDM

BestDM is a Qt 6/C++20 download manager in active development. Its visual system is derived from the supplied blue, aqua, and green download-mark icon, while retaining accessible contrast in both themes.

## Current working slice

- Qt Quick light/dark application shell using the official supplied icon
- HTTP/HTTPS download with TLS verification left at Qt's secure default
- streamed disk writes to a `.part` file, then atomic-style rename on completion
- pause, resume (HTTP Range where the server supports it), cancel, progress, and errors
- persistent current-download metadata through `QSettings`; an incomplete `.part` download is restored as paused after restart

This is intentionally an honest first vertical slice. Multi-download queues, segmented downloads, FTP/SFTP, media extraction, torrent, browser integration, and scheduling are not yet implemented.

## Build

The repository includes the existing Visual Studio/Qt project and a CMake entry point. The validated configuration is Windows x64 with Qt 6.12.0 MSVC 2022:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' BestDM.slnx /t:Build /p:Configuration=Debug /p:Platform=x64
```

For CMake, install CMake and point `CMAKE_PREFIX_PATH` at your Qt installation (for example `C:/Qt/6.12.0/msvc2022_64`).

## Architecture

`DownloadManager` owns the network reply and file handle. The QML layer only invokes its small command surface and observes properties/signals; no networking code exists in QML. The next expansion should introduce a task model and persistence repository before adding multi-download queues.
