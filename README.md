# Local Search Command

This project provides a Windows command-line search tool built from
`search_dir_in_local.c`.

After setup, users can type commands like:

```powershell
search cmake
search cmake C:\msys64
search "new folder" "D:\New folder"
```

The tool searches local files and folders by name, then shows results in a
scrollable console viewer. Clicking a visible `Path:` line opens that file or
folder in File Explorer.

## Files Included

| File | Purpose |
| --- | --- |
| `search_dir_in_local.c` | Main C source code. |
| `search.exe` | Short command executable. Run this as `search`. |
| `search_dir_in_local.exe` | Same tool with the full executable name. |
| `install_search_command.ps1` | Adds this folder to the user `PATH`. |

## Requirements

### Runtime Requirements

To run the compiled `.exe` files:

- Windows 10 or Windows 11.
- A normal Windows terminal, PowerShell, Command Prompt, or Windows Terminal.
- No extra runtime libraries are required.

The program uses Windows APIs and File Explorer, so it is Windows-only.

### Build Requirements

To build from source:

- MinGW/GCC for Windows.
- Windows `shell32` library, included with the compiler toolchain.

Recommended compiler setup:

```text
MSYS2 + MinGW-w64 GCC
```

Download MSYS2:

```text
https://www.msys2.org/
```

Install GCC from the `MSYS2 UCRT64` terminal:

```bash
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-gcc
```

Check GCC:

```bash
gcc --version
```

## Build The Tool

Open PowerShell in the project folder and run:

```powershell
gcc .\search_dir_in_local.c -o .\search.exe -lshell32
gcc .\search_dir_in_local.c -o .\search_dir_in_local.exe -lshell32
```

The important output is `search.exe`. That is the file Windows will run when
you type `search`.

## Add It To PATH

Windows runs commands by looking through folders listed in the `PATH`
environment variable. Add the folder containing `search.exe`, not the `.exe`
file itself.

### Automatic Setup

From PowerShell in this folder:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\install_search_command.ps1
```

Then close and reopen the terminal.

Test it:

```powershell
search cmake
```

### Manual Setup

Add this folder to your user `PATH`:

```text
D:\New folder\New folder (3)
```

PowerShell command:

```powershell
[Environment]::SetEnvironmentVariable(
  "Path",
  [Environment]::GetEnvironmentVariable("Path", "User") + ";D:\New folder\New folder (3)",
  "User"
)
```

Close and reopen the terminal after changing `PATH`.

### Temporary PATH For Current Terminal Only

```powershell
$env:Path += ";D:\New folder\New folder (3)"
```

This works only until that terminal window is closed.

## How To Use

### Search All Local Drives

```powershell
search cmake
```

This scans all fixed local drives, such as `C:\` and `D:\`.

### Search In One Folder

```powershell
search cmake C:\msys64
```

### Search Names With Spaces

Use quotes:

```powershell
search "new folder" "D:\New folder"
```

### Interactive Mode

Run:

```powershell
search
```

Then type commands:

```text
search cmake
search cmake C:\msys64
search "new folder" "D:\New folder"
exit
```

## Result Viewer Controls

After a search, results appear in a paged viewer.

| Control | Action |
| --- | --- |
| Mouse wheel | Scroll results. |
| Up / Down | Move one result at a time. |
| PageUp / PageDown | Move one page at a time. |
| Home / End | Jump to first or last page. |
| Click `Path:` | Open the file or folder in File Explorer. |
| Enter / Esc / Q | Return to the command prompt. |

## What It Searches

- Files.
- Folders.
- Case-insensitive partial name matches.

Examples:

```powershell
search report
search .cmake
search project "D:\Work"
```

## Performance Notes

- Results are capped at 500 items.
- Reparse points and junction folders are skipped to avoid slow loops.
- File type metadata is loaded only for visible results.
- Searching all drives can still take time on large disks.

For faster searches, provide a folder path:

```powershell
search report D:\Documents
```

## Troubleshooting

### `search` Is Not Recognized

The folder containing `search.exe` is not in `PATH`, or the terminal was not
reopened after changing `PATH`.

Check:

```powershell
where.exe search
```

If nothing appears, add the folder to `PATH` again.

### `gcc` Is Not Recognized

Install MSYS2 and GCC, or add the GCC `bin` folder to `PATH`.

Common MSYS2 GCC path:

```text
C:\msys64\ucrt64\bin
```

### Search Is Slow

Searching all drives is expensive. Use a folder path:

```powershell
search name C:\Users
```

### Click Does Not Open A Path

Use the keyboard to scroll until the path is visible, then click the visible
`Path:` line. The tool opens paths with Windows File Explorer.

## Sharing This Tool

To share with another Windows user, provide:

- `search.exe`
- `install_search_command.ps1`
- `README_search_command.md`

If they want to rebuild from source, also include:

- `search_dir_in_local.c`

