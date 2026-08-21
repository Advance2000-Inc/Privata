<!--
  - SPDX-FileCopyrightText: 2025 Nextcloud GmbH and Nextcloud contributors
  - SPDX-License-Identifier: GPL-2.0-or-later
-->
# How to Build Privata Desktop Client (Windows)

## Prerequisites

Install the following once before your first build:

| Tool | Version | Notes |
|---|---|---|
| **Python** | 3.12 | Install to `C:\Python312-x64`. Disable the Python App Execution Aliases in Windows Settings → Apps → Advanced app settings → App execution aliases |
| **Visual Studio 2022** | Community or higher | Workload: **Desktop development with C++** — [download](https://aka.ms/vs/17/release/vs_community.exe) |
| **Inkscape** | any | Required for SVG icon generation. Install via: `winget install --id Inkscape.Inkscape` |
| **KDE Craft** | master | Build dependency manager — see [Install KDE Craft](#install-kde-craft) below |

> **Note:** Visual Studio 2025 (v18.x) is not supported by Craft yet. VS 2022 (v17.x) is required.

---

## Install KDE Craft (once)

Open **PowerShell from `C:\CraftRoot`** (not from a UNC/network path) for all Craft operations.

```powershell
# 1. Allow scripts to run
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

# 2. Fix HTTPS cert errors
C:\Python312-x64\python.exe -m pip install --upgrade certifi

# 3. Download and run the Craft installer
$env:PATH = "C:\Python312-x64;C:\Python312-x64\Scripts;" + $env:PATH
iex ((new-object net.webclient).DownloadString('https://invent.kde.org/packaging/craft/-/raw/master/setup/install_craft.ps1'))
```

When prompted:
- **Install root:** `C:\CraftRoot\`
- **Compiler:** `1` (Microsoft Visual Studio 2022)
- **Short path root:** `C:\_`
- **Start Menu entry:** `0` (Yes)
- **Colored logs:** `0` (Yes)

### Add build tools to the user environment permanently

Install WiX Toolset v3 for MSI builds:

```powershell
winget install --id WiXToolset.WiXToolset --exact --source winget
```

Add the tools used by this project to the **user-level** `PATH`. Run this once in PowerShell, then restart PowerShell and VS Code:

```powershell
$pathsToAdd = @(
  "C:\CraftRoot\dev-utils\bin",
  "C:\Python312-x64",
  "C:\Python312-x64\Scripts",
  "C:\Program Files\Inkscape\bin",
  "C:\Program Files (x86)\WiX Toolset v3.14\bin"
)

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$currentPaths = $userPath -split ";" | Where-Object { $_ }

foreach ($path in $pathsToAdd) {
  if ((Test-Path $path) -and ($currentPaths -notcontains $path)) {
    $currentPaths += $path
  }
}

[Environment]::SetEnvironmentVariable("Path", ($currentPaths -join ";"), "User")
[Environment]::SetEnvironmentVariable(
  "WIX",
  "C:\Program Files (x86)\WiX Toolset v3.14",
  "User"
)
```

Verify the tools in a new PowerShell window:

```powershell
cmake --version
ninja --version
candle.exe -?
heat.exe -?
inkscape.com --version
python --version
```

Do not add all of `C:\CraftRoot\bin` to `PATH`. It contains development tools that can shadow unrelated system tools.

The persistent `PATH` does not replace Craft initialization. Before building, still run:

```powershell
Set-Location C:\CraftRoot
. C:\CraftRoot\craft\craftenv.ps1
```

Visual Studio's compiler environment also needs to be loaded in each new build terminal:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && set' > $env:TEMP\vcvars_env.txt
Get-Content $env:TEMP\vcvars_env.txt | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') {
    [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
  }
}
```

### Patch Craft for network/UNC path compatibility

Craft has a bug where subprocesses fail when PowerShell's working directory is a UNC path. Apply this fix to both files:

**`C:\CraftRoot\craft\bin\CraftSetupHelper.py`** — find `_getOutput` and add `cwd`:

```python
def _getOutput(command, shell=False):
    CraftCore.log.debug(f"SetupHelper._getOutput: {command}")
    # Use a local cwd to avoid UNC path issues with cmd.exe redirection
    _cwd = os.environ.get("SystemRoot", "C:\\Windows")
    p = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        shell=shell,
        universal_newlines=True,
        errors="backslashreplace",
        cwd=_cwd,
    )
```

### Add Nextcloud blueprints and install Inkscape blueprint dependency

```powershell
Set-Location C:\CraftRoot
. C:\CraftRoot\craft\craftenv.ps1
craft --add-blueprint-repository "https://github.com/nextcloud/craft-blueprints-nextcloud.git"
```

### Build all dependencies (one-time, ~1-2 hours)

```powershell
Set-Location C:\CraftRoot
. C:\CraftRoot\craft\craftenv.ps1
craft --set featureArguments="-DSVG_CONVERTER=C:/Program Files/Inkscape/bin/inkscape.com" nextcloud-client
craft nextcloud-client
```

This downloads and builds Qt 6, KDE Frameworks, OpenSSL, and all other dependencies via prebuilt caches.

---

## First Build of Privata

### 1. Copy source to a local path

Craft and CMake do not support UNC paths (`\\server\share\...`) as source directories.
Copy the repo to a local drive first:

```powershell
robocopy "\\advance2000.com\Data\FolderRedirection\kblank\Documents\GitHub\Privata" C:\Privata-src /E /XD .git /NFL /NDL /NJH /NJS
```

### 2. Initialize the Craft environment

**Always run Craft commands from `C:\CraftRoot`**, never from a UNC path:

```powershell
Set-Location C:\CraftRoot
. C:\CraftRoot\craft\craftenv.ps1
```

### 3. Configure with CMake

Run once (or after adding new CMakeLists.txt files):

```powershell
cmake -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_INSTALL_PREFIX=C:/CraftRoot `
  -DCMAKE_PREFIX_PATH=C:/CraftRoot `
  -DBUILD_SHARED_LIBS=ON `
  -DBUILD_WITH_WEBENGINE=ON `
  "-DSVG_CONVERTER=C:/Program Files/Inkscape/bin/inkscape.com" `
  -B C:\CraftRoot\build\privata `
  -S C:\Privata-src
```

> CMake runs Inkscape to generate icon PNGs from SVGs — this takes ~10 minutes on first configure.

### 4. Compile

```powershell
cmake --build C:\CraftRoot\build\privata --parallel
```

### 5. Install

Copies `privata.exe` and all required DLLs to `C:\CraftRoot\bin`:

```powershell
cmake --install C:\CraftRoot\build\privata
```

### 6. Run

```powershell
C:\CraftRoot\bin\privata.exe
```

### Batch build commands

The repository provides three Windows batch entry points. Run them from the repository
directory or by using their full path:

```bat
build-project.bat
```

Builds the already-configured project and installs the result into `C:\CraftRoot`.

```bat
build-installer.bat
```

Builds the MSI from the installed runtime files. The MSI is written to
`C:\CraftRoot\msi`.

```bat
build-all.bat
```

Synchronizes the network repository to `C:\Privata-src`, configures the MSI-enabled
build, compiles and installs the project, and generates the MSI. This is the complete
one-command workflow.

The batch files use the local source copy and automatically load the Visual Studio 2022
compiler environment. `build-all.bat` should be used after changing CMake options or
when setting up the build for the first time.

---

## Rebuild After Code Changes

```powershell
# Sync changes from the network repo to the local build source
robocopy "\\advance2000.com\Data\FolderRedirection\kblank\Documents\GitHub\Privata" C:\Privata-src /E /XD .git /NFL /NDL /NJH /NJS

# From a terminal already initialized with craftenv.ps1:
cmake --build C:\CraftRoot\build\privata --parallel
cmake --install C:\CraftRoot\build\privata
```

Re-run the CMake configure step only when:
- `CMakeLists.txt` files changed
- New source files were added
- Build options need to change

---

## Build Output

| File | Description |
|---|---|
| `C:\CraftRoot\bin\privata.exe` | Main application |
| `C:\CraftRoot\bin\privatasync.dll` | Core sync engine |
| `C:\CraftRoot\bin\privata_csync.dll` | Low-level sync library |
| `C:\CraftRoot\bin\privatasync_vfs_cfapi.dll` | Virtual files (CfAPI) plugin |

---

## Build Windows Installer

### Prerequisites

Install NSIS (Nullsoft Installer System) once:

```powershell
winget install NSIS.NSIS
```

### Create the installer

After a successful `cmake --install` (which places binaries at `C:\CraftRoot\bin`), build the installer:

```powershell
Set-Location C:\CraftRoot\build\privata
& "C:\Program Files (x86)\NSIS\makensis.exe" "C:\CraftRoot\build\privata\PrivataSimple.nsi"
```

This generates `Privata-3.11.0-windows-setup.exe` (~1.2 GB) in the build directory.

### Build an MSI installer

The MSI installer uses WiX Toolset v3. WiX v3 is required because the project uses
`candle.exe`, `heat.exe`, and `light.exe` from the WiX v3 toolchain. Install it once:

```powershell
winget install --id WiXToolset.WiXToolset --exact --source winget
```

Open a new PowerShell window after installation. If `WIX` is not already set, configure it:

```powershell
$env:WIX = "C:\Program Files (x86)\WiX Toolset v3.14"
$env:PATH = "$env:WIX\bin;C:\CraftRoot\dev-utils\bin;$env:PATH"
```

Initialize Craft from a local directory and reconfigure the build with MSI support enabled:

```powershell
Set-Location C:\CraftRoot
. C:\CraftRoot\craft\craftenv.ps1

cmake -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_INSTALL_PREFIX=C:/CraftRoot `
  -DCMAKE_PREFIX_PATH=C:/CraftRoot `
  -DBUILD_SHARED_LIBS=ON `
  -DBUILD_WITH_WEBENGINE=ON `
  -DBUILD_WIN_MSI=ON `
  "-DSVG_CONVERTER=C:/Program Files/Inkscape/bin/inkscape.com" `
  -B C:\CraftRoot\build\privata `
  -S C:\Privata-src
```

Build and install the application and MSI support files:

```powershell
cmake --build C:\CraftRoot\build\privata --parallel
cmake --install C:\CraftRoot\build\privata
```

Generate the MSI:

```powershell
Set-Location C:\CraftRoot\msi
.\make-msi.bat C:\CraftRoot\bin
```

The output is created in `C:\CraftRoot\msi` with an architecture-specific name:

```text
C:\CraftRoot\msi\Privata-<version>-daily-x64.msi
```

The MSI script automatically stages only runtime files. It excludes compiler tools,
development executables, debug symbols, import libraries, and source files. It also
includes the Qt `plugins` and `qml` directories required by the application on a clean
Windows installation. The resulting MSI is typically about 190 MiB instead of the
multi-gigabyte Craft-prefix harvest.

Verify the result explicitly:

```powershell
Get-ChildItem C:\CraftRoot\msi -Filter *.msi |
    Select-Object Name,@{Name="MiB";Expression={[math]::Round($_.Length / 1MB, 1)}}
```

If `make-msi.bat` reports that `WiSubStg.vbs` is missing, the MSI still builds as an
English-only installer. `WiSubStg.vbs` is only needed to embed additional language
transforms.

### NSIS installer contents

The installer bundles:
- `privata.exe` and all Privata DLLs
- 195 Qt6 framework DLLs
- Shell integration plugins (Context menu, Overlays)
- Start Menu shortcuts
- Uninstaller + registry entries

### User installation

Users run the `.exe` to install to `C:\Program Files\Privata`. The installer:
1. Extracts all files
2. Creates Start Menu and Desktop shortcuts
3. Registers with Windows (Add/Remove Programs)
4. Allows uninstall via Control Panel

For MSI distribution, provide the generated file from `C:\CraftRoot\msi`:

```powershell
Copy-Item "C:\CraftRoot\msi\Privata-<version>-daily-x64.msi" "\\myserver\distribution\"
```

Users can open the `.msi` directly, or install it from an elevated command prompt:

```powershell
msiexec.exe /i "C:\path\to\Privata-<version>-daily-x64.msi" /qn /norestart
```

To create a verbose MSI installation log:

```powershell
msiexec.exe /i "C:\path\to\Privata-<version>-daily-x64.msi" /L*V "$env:TEMP\Privata-msi-install.log"
```

### Distribution

Copy the generated `.exe` to your distribution server:

```powershell
Copy-Item "C:\CraftRoot\build\privata\Privata-3.11.0-windows-setup.exe" "\\myserver\distribution\Privata-3.11.0-windows-setup.exe"
```

Or upload to your update server (e.g., `https://ifcloud.advance2000.com/Client/`)

### Customization

To modify installer appearance, edit:
- `admin/win/nsi/installer.ico` — installer icon
- `admin/win/nsi/welcome.bmp` — welcome screen image  
- `admin/win/nsi/page_header.bmp` — header image
- `NEXTCLOUD.cmake` — application name, vendor, URLs

After editing images, re-run makensis. After editing NEXTCLOUD.cmake, re-run CMake configure first.

---

## Troubleshooting

**`Unable to locate Visual Studio`**  
Craft only supports VS 2022. If VS 2025 is installed, VS 2022 must also be installed.

**`JSONDecodeError: Expecting value`** during `craftenv.ps1`  
The UNC path patch to `CraftSetupHelper.py` was not applied, or was applied only to `craft-tmp` and not `C:\CraftRoot\craft\bin\CraftSetupHelper.py`. See [Patch Craft](#patch-craft-for-networkunc-path-compatibility).

**`Could not find SVG_CONVERTER`**  
Inkscape is not installed, or the CMake cache has a stale `NOTFOUND` entry. Delete `C:\CraftRoot\build\privata\CMakeCache.txt` and re-run configure.

**Terminal hangs on startup**  
A persistent `net use` drive mapping may be blocking PowerShell. Run `net use * /delete /yes` to remove all mapped drives, then reopen the terminal.
