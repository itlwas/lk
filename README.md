# lk - Enhanced Directory Listing for Windows

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/itlwas/lk/actions)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-1.3-blue.svg)](https://github.com/itlwas/lk/releases)
[![Issues](https://img.shields.io/github/issues/itlwas/lk.svg)](https://github.com/itlwas/lk/issues)
[![Stars](https://img.shields.io/github/stars/itlwas/lk.svg)](https://github.com/itlwas/lk/stargazers)
[![Forks](https://img.shields.io/github/forks/itlwas/lk.svg)](https://github.com/itlwas/lk/network)

**lk** is a powerful and user-friendly command-line tool for Windows, designed as an enhanced alternative to the standard `dir` or `ls` commands. It offers a comprehensive set of features for listing directories and files with improved readability, sorting, filtering, and advanced customization.

## Table of Contents
- [Features](#features)
- [Installation](#installation)
  - [Using MinGW-w64 (GCC for Windows)](#using-mingw-w64-gcc-for-windows)
  - [Using Visual Studio](#using-visual-studio)
- [Usage](#usage)
- [Command-Line Options](#command-line-options)
- [Examples](#examples)
- [License](#license)

## Features

- **Colorized Output**: Easily distinguish file types with intuitive color coding.
- **Detailed Information**: Display file attributes, human-readable sizes, modification and creation times, and even file ownership.
- **Advanced Sorting**: Sort by name (with natural sorting), size, modification time, or extension, with support for reverse order.
- **Directory Grouping**: Optionally group directories for a clearer display.
- **Recursive & Tree Views**: Recursively list subdirectories or display a hierarchical tree view.
- **File Filtering**: Filter files by name using simple patterns.
- **Summary Statistics**: Get an overview of the number of directories, files, and total size.
- **File Preview**: Preview the first 10 lines of text files directly in the terminal.
- **Full Path Display**: Option to show the complete file path.

> **Note:** The interactive mode feature has been removed due to low usage.

## Installation

### Requirements

- Windows operating system.
- A C compiler such as [MinGW-w64](https://mingw-w64.org/) or [Visual Studio](https://visualstudio.microsoft.com/).

### Using MinGW-w64 (GCC for Windows)

1. **Install MinGW-w64**: Download and install from [mingw-w64.org](https://mingw-w64.org/). Ensure its `bin` directory is added to your system's `PATH`.
2. **Compile**: Open a command prompt or PowerShell in the directory containing `lk.c` and run:
    ```bash
    gcc lk.c -o lk.exe
    ```
3. **Run**: Execute `lk.exe` from the command line.

### Using Visual Studio

1. **Open Developer Command Prompt**: Launch the "Developer Command Prompt for VS".
2. **Navigate to the Project Directory**: Use `cd` to move to the directory containing `lk.c`.
3. **Compile**: Run:
    ```bash
    cl lk.c
    ```
4. **Run**: Execute the compiled executable.

## Usage

Run `lk` from the command line with the desired options and directories:
```bash
lk [options] [directory ...]
```
If no directory is specified, `lk` will list the contents of the current directory.

## Command-Line Options

```
Usage: lk [options] [path ...]

Options:
  -a, --all         Show hidden files.
  -s, --short       Use short format (disable detailed long listing).
  -R                Recursively list subdirectories.
  -S                Sort by file size.
  -t                Sort by modification time.
  -x                Sort by file extension.
  -r                Reverse sort order.
  -b, --bytes       Display file sizes in raw bytes (default: human-readable).
  -F                Append file type indicators (e.g., "/" for directories, "@" for symlinks).
  -d                List directory entry itself rather than its contents.
  -n, --no-group    Do not group directories first.
  -E                Show file creation time.
  -T                Display a tree view of directory structure.
  -N                Disable natural sorting.
  -P                Show full file path.
  -O                Display file owner.
  -M                Show summary of directory contents.
  -h, --help        Display this help message.
  -v, --version     Display version information.
```

## License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

Made with ❤️ for Windows command-line enthusiasts. Enjoy!
