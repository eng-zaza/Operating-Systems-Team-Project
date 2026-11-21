# FileDesk - C++ File Management System

A C++17 desktop project using the native Windows API (Win32). Python and third-party GUI libraries are not required.

## Run

Extract the project ZIP, then double-click FileDesk.exe or launch.bat on 64-bit Windows. Keep the project in a writable folder. Files persist in storage/ beside the executable; ZIP backups go into backups/.

## Build from source

Install Visual Studio 2022 or its Build Tools with Desktop development with C++ and a Windows SDK. Run build.bat. It locates MSVC, builds the application, builds the C++ tests, and runs them. The executable uses the static C++ runtime (/MT).

- src/main.cpp: native window, file table, dialogs, editor, and event handlers.
- src/store.hpp: filesystem service, UTF-8 conversion, validation, and ZIP writer.
- tests/store_tests.cpp: storage tests using temporary directories.
- deliverables/: PDF report, editable text, slide outline, and demo script.

## Features and usage

Create files and nested folders; edit UTF-8 text up to 2 MB; rename, copy, move, and delete items; filter names; view sizes and modification times; create ZIP backups.

Double-click to open an item. Home and Up navigate folders. Copy and Move ask for an existing destination relative to Storage, such as Documents, or . for the root. Refresh or F5 reloads the listing. The editor supports Ctrl+S and asks before discarding changes. Delete requires confirmation and is permanent. Saves replace the original after successfully writing and flushing a temporary file.

## Course connection

The GUI calls FileStore, which requests host OS services through std::filesystem, C++ streams, and Win32 calls. RAII streams close file handles when they leave scope. The host OS manages physical storage and account permissions. This is an application-level system program, not a kernel or disk filesystem implementation.

Sources: supplied 1.pdf, slide 1.39; 2.pdf, slides 2.4, 2.21, 2.29.

## Verification

The MSVC build passes at warning level 4. Ten C++ test groups cover file lifecycle, Unicode persistence, duplicate protection, invalid names, path boundaries, root protection, recursive-copy rejection, ZIP backup, binary-file rejection, and deletion. An independent ZIP reader checked CRC integrity, Unicode content, and empty directories. A native GUI smoke test checked startup, the file table, Home, and Refresh. Visual inspection of the native GUI could not be completed because this session returns black screenshots.

## Limits

Windows-only GUI; single user; current-folder filtering; UTF-8 editing only. Binary files can be copied and backed up. Operations run synchronously, so large backups can temporarily block the interface. ZIP output is uncompressed, under 4 GB total, and limited to 65,535 entries. ZIP metadata does not retain original timestamps or ACLs. Restore manually by extracting the ZIP. Host account permissions remain authoritative. Storage containment is not a hardened sandbox against concurrent filesystem changes. Avoid editing the same files with another program while operations run.

## Submission

The updated editable presentation is at page://page_b725aa5ad6b88191a28e213d990a7514; export it as PowerPoint from the viewer. Add team names and IDs. The demo video still needs recording; use deliverables/demo-script.txt. The registration form has not been submitted.
