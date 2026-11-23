# FileDesk - C++ File Management System

FileDesk is a C++17 desktop project using the native Windows API. It manages real files, searches nested folders, recovers recycled items, and performs long operations on a worker thread. No Python or third-party GUI library is required to run it.

## Run and build

Extract the complete project ZIP into a writable folder and open `FileDesk.exe` or `launch.bat` on 64-bit Windows. To compile, install Visual Studio 2022 or its Build Tools with **Desktop development with C++** and a Windows SDK, then run `build.bat`. The script builds with `/W4`, embeds the native-controls manifest, links the static C++ runtime (`/MT`), builds the tests, and runs them.

## Features

- Native file and folder icons, a resizable table, readable sizes, location columns, context menus, and helpful empty-state/status messages.
- Create folders and files; edit UTF-8 text up to 2 MB; rename, copy, and move items without overwriting existing destinations.
- Search names in the current folder or recursively through its subfolders. Results show each item's location and support normal file actions.
- Recycle files or complete folders and restore them later, including after restarting the app. Existing files are never replaced during restore.
- Background copying, moving, renaming, recycling, restoring, searching, listing, and ZIP backup. Progress animates while scanning and shows a percentage when the total is known.
- One background job at a time. Conflicting actions are disabled while the window remains responsive; closing waits for the job to finish.

## How to use it

Double-click a file or folder to open it. Home returns to Storage and Up goes to the parent. For Copy and Move, enter an existing destination relative to Storage (for example `Documents`), or `.` for the root. Enter a different destination name when copying into the same folder.

Type a filename or part of a name, then click Search or press Enter. **Include subfolders** is enabled by default. An empty search lists only the current folder. Search is by name, not file contents, and shows up to 5,000 matches. Clear returns to the current folder listing.

Recycle or Delete moves the selection into FileDesk's own recycle bin after confirmation. Click Recycle bin, select an item, and click Restore (or double-click it). Restore recreates missing parent folders. If the original name already exists, rename or move that active item and retry. There is no permanent-delete button or automatic bin cleanup.

Backup ZIP writes a timestamped archive containing active Storage items. Recycled items are excluded. ZIP restoration is manual extraction; the Restore button applies only to recycled items.

| Shortcut | Action |
| --- | --- |
| Ctrl+F | Focus search |
| Enter in search | Run search |
| Enter on a selected row | Open; restore in the recycle bin |
| F2 | Rename selected active item |
| Delete | Move selected active item to recycle bin |
| F5 | Refresh |
| Ctrl+S in editor | Save |

Right-click an item for its available actions. The editor asks before discarding unsaved changes. Saves write and flush a temporary file before replacing the previous file.

## Storage and source layout

`storage/` holds active files beside the executable. `storage.trash/` holds recycled payloads and their original paths/deletion dates. Keep both folders to preserve recovery across upgrades. `backups/` contains ZIP archives. These runtime folders are excluded from Git and the project ZIP.

- `src/main.cpp`: Win32 interface, dialogs, context menus, worker lifecycle, atomic progress counters, and completion polling.
- `src/store.hpp`: path validation, UTF-8 I/O, recursive search, recycle records, restore, chunked copying, and ZIP writer.
- `tests/store_tests.cpp`: isolated tests using temporary directories and injected failures.
- `deliverables/` in the local download package: report, presentation outline, demo script, and validation notes. Generated submission materials are kept outside the source repository to preserve the repository owner's removal of the previous documents.

## Architecture and OS concepts

The Win32 message loop runs on the UI thread. `std::async(std::launch::async)` starts a worker for one background job. The worker reports atomic counters and returns a `Result` through a future. A UI timer polls that future without waiting; all window updates stay on the UI thread. Serializing jobs avoids conflicting operations within one app instance.

FileStore uses `std::filesystem`, RAII streams, and Win32 file calls to request host OS services. Recycle records preserve the original relative path while moving the payload outside active storage. The OS manages physical storage and account permissions. This is a user-space application, not a kernel or disk filesystem.

Course references: supplied `1.pdf`, slide 1.39; `2.pdf`, slides 2.4, 2.21, 2.29. Worker/UI separation also demonstrates practical process/thread execution concepts.

## Validation

Run `build.bat` for the full build and C++ tests. Tests cover the original file lifecycle, Unicode persistence, naming/path rules, duplicate protection, ZIP creation, recursive search, recycle persistence, folder recovery, missing parents, restore conflicts, malformed/CRLF metadata, progress monotonicity, and cleanup after injected copy/backup failures. Independent ZIP checks validate CRCs and preserved contents. Native GUI checks covered the layout, icons, nested search, completed progress state, recycle-bin listing, restoration, and a roughly 50 MB background backup. The local package includes `deliverables/validation.txt` with details.

## Limits and future work

Windows-only and intended for one active instance. The managed directory is not a hardened sandbox against concurrent external filesystem changes. Linked paths and junctions are unsupported. Some inaccessible search entries are skipped and counted. Search results are capped at 5,000. Unicode case matching follows the C runtime's character conversion. Text editing is UTF-8 only, limited to 2 MB.

Background jobs cannot currently be cancelled. Small create/read/save actions and final table rendering run on the UI thread. Recycled items occupy disk space until restored or managed outside the app. Classic ZIP output is uncompressed, under 4 GB total, and limited to 65,535 entries; original timestamps and ACLs are not preserved in archive metadata. Host OS permissions remain authoritative.

Possible next additions: cancellable jobs, ZIP restore, file-content search, and a confirmed permanent-cleanup workflow for recycled data.

## Submission

The editable seven-slide deck is at page://page_b725aa5ad6b88191a28e213d990a7514; export to PowerPoint from the viewer. Its text outline is included in the local download package. Add team names and IDs, and record the revised demo script. The demo video and team registration remain pending.
