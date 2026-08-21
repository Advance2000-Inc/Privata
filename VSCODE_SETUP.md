# VS Code Setup for Privata Development

## Quick Start

### 1. Install Extensions

Open VS Code and install recommended extensions:
- **C/C++** (Microsoft) — IntelliSense, debugging
- **CMake Tools** (Microsoft) — configure/build/debug
- **CMake** (twxs) — syntax highlighting
- **GitLens** — git integration
- **Copilot** (optional) — AI assistance

Or: `Ctrl+Shift+X` → Type "C++" → Install

### 2. Initial Setup

```powershell
# Copy source to local drive (one-time)
robocopy "\\advance2000.com\Data\FolderRedirection\kblank\Documents\GitHub\Privata" C:\Privata-src /E /XD .git /NFL /NDL /NJH /NJS

# Open in VS Code
code C:\Privata-src
```

### 3. Configure CMake

In VS Code:
1. `Ctrl+Shift+P` → "CMake: Select a Kit"
2. Choose: `Visual Studio Community 2022 Release - cl` or `Ninja`
3. `Ctrl+Shift+P` → "CMake: Configure" (generates Ninja build files)

## Workflow

### Daily Development Loop

**Build (default):**
```
Ctrl+Shift+B  or  Ctrl+Shift+P → "CMake: Build"
```

**Run (with full dependencies):**
```
Ctrl+Shift+P → "CMake: Install" → F5 (Debug Privata installed)
```

**Test:**
```
Ctrl+Shift+P → "CMake: Run Tests"
```

## Debugging

### Set Breakpoint & Debug

1. Click left margin in editor to set breakpoint (red dot)
2. Press `F5` or `Ctrl+Shift+P` → "Debug: Start Debugging"
3. Choose launch config:
   - **"Debug Privata (installed)"** — Run with all Qt DLLs (recommended)
   - **"Debug Privata (build/privata/bin)"** — Run from build dir (add PATH first)
   - **"Run FolderTest"** — Debug unit tests

### Inspect Variables

While paused at breakpoint:
- Hover over variable to see value
- `Debug Console` tab (bottom) — evaluate expressions: `(gdb) print myVar->field`
- `Variables` panel — all local/global scope

### Call Stack & Threads

- `Call Stack` panel shows where you are in call chain
- Click any frame to jump to that code
- `Threads` panel for multi-threaded debugging

## IntelliSense & Navigation

**Go to Definition:** `F12` or `Ctrl+Click`
**Find All References:** `Shift+F12`
**Rename Symbol:** `F2` (renames all occurrences)
**Format Code:** `Shift+Alt+F`
**Quick Fix:** `Ctrl+.` (fix includes, format issues)

## Command Palette Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+Shift+P` | Command palette |
| `Ctrl+Shift+B` | Build (run default task) |
| `Ctrl+K Ctrl+M` | Change language mode |
| `Ctrl+J` | Toggle terminal |
| `Ctrl+`` | New terminal |
| `Ctrl+Shift+C` | New terminal here |

## CMake Tasks

All defined in `.vscode/tasks.json`:

| Task | Command |
|---|---|
| **build-privata** | Build privata.exe only (fast) |
| **build-all** | Build all 636 targets |
| **install-privata** | Install to C:\CraftRoot\bin |
| **run-tests** | Execute unit tests (FolderTest, etc.) |
| **clean-build** | Delete C:\CraftRoot\build\privata |

Run via: `Ctrl+Shift+P` → "Tasks: Run Task" → select task

## Tips & Tricks

### Sync from Network Repo

When you pull changes from `\\advance2000.com\...\Privata`:

```powershell
# In terminal (Ctrl+`):
robocopy "\\advance2000.com\Data\FolderRedirection\kblank\Documents\GitHub\Privata" C:\Privata-src /E /XD .git /NFL /NDL /NJH /NJS

# Then rebuild:
Ctrl+Shift+B
```

### Faster Incremental Builds

VS Code CMake Tools caches configurations. After first build:
- Small code changes → 10-30 seconds rebuild
- Header changes → 1-2 minutes rebuild
- CMakeLists.txt changes → Full reconfigure (~10 minutes with icons)

### View Compile Commands

After building, CMake generates `compile_commands.json` — useful for:
- Clang-based tools
- Custom linters
- Understanding compiler flags

Located at: `C:\CraftRoot\build\privata\compile_commands.json`

### Workspace Settings

Edit `.vscode/settings.json` to customize:
- Tab size, indentation
- C++ standard (C++17, C++20)
- Clang-Format / LLVM style
- File associations

## Troubleshooting

### IntelliSense says "Include not found"

CMake hasn't configured yet. Run: `Ctrl+Shift+P` → "CMake: Configure"

### Build fails but worked before

Clean and rebuild:
```
Ctrl+Shift+P → "Tasks: Run Task" → "clean-build"
Ctrl+Shift+B
```

### Debugger won't start

Ensure:
1. Build succeeded (`Ctrl+Shift+B`)
2. You've installed VS 2022 with C++ workload
3. `privata.exe` exists at path in launch.json

Check terminal for errors: `Ctrl+``

### Can't find header/symbols

1. Delete CMakeCache: `Ctrl+Shift+P` → "CMake: Delete Cache"
2. Reconfigure: `Ctrl+Shift+P` → "CMake: Configure"
3. Reload window: `Ctrl+Shift+P` → "Developer: Reload Window"

## Next Steps

- [ ] Install C++ and CMake Tools extensions
- [ ] Open source folder in VS Code
- [ ] Run CMake Configure
- [ ] Build successfully (Ctrl+Shift+B)
- [ ] Set a breakpoint and debug (F5)
- [ ] Read [Nextcloud Desktop Client docs](https://docs.nextcloud.com/desktop/latest/)
