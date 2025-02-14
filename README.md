# lk - Enhanced Directory Listing for Windows

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

**`lk`** is a powerful and user-friendly command-line tool for Windows, designed as an enhanced alternative to the standard `ls` or `dir` commands. It provides a rich set of features for listing directories and files with improved readability, sorting, filtering, and advanced customization options.

## ✨ Key Features

`lk` goes beyond basic directory listing, offering a range of options to tailor the output to your needs:

*   **Colorized Output:** Visually distinguish file types with colors (binaries, folders, symlinks).
*   **Detailed Information:** Display file attributes, sizes (human-readable), modification and creation times, and even file owners in long listing format.
*   **Versatile Sorting:** Sort files by name (natural sort for numbers), size, modification time, or extension, in forward or reverse order.
*   **Directory Grouping:** Option to group directories first for clearer organization.
*   **Recursive Listing:** Explore subdirectories recursively with `-R` option.
*   **Tree View:** Visualize directory structure as a tree with `-T` option for a hierarchical overview.
*   **File Filtering:** Filter files by name using a simple pattern.
*   **Summary Information:** Get a quick summary of directories, files, and total size within a directory.
*   **File Preview:** Preview the first 10 lines of text files directly in the terminal.
*   **Full Path Display:** Show the full path of files for clarity.
*   **Hidden Files:** Option to show hidden files with `-a`.

## ❌ Removed Feature

- **Interactive Mode:** Removed due to lack of necessity and low usage.

## 🚀 Usage

To use `lk`, simply run it from your command prompt or PowerShell.

```bash
lk [options] [directory ...]
```

If no directory is specified, `lk` will list the contents of the current directory. You can provide one or more directory paths as arguments.

### Command Line Options

`lk` supports a variety of options to customize its behavior. Here's a comprehensive list:

```
Usage: lk [options] [path ...]

Options:
  -a, --all         Show hidden files
  -s, --short       Use short format (disable long listing)
  -R                Recursively list subdirectories
  -S                Sort by file size
  -t                Sort by modification time
  -x                Sort by file extension
  -r                Reverse sort order
  -b, --bytes       Show file sizes in raw bytes (default: human-readable)
  -F                Append file type indicator (default: on)
  -d                List directory entry itself, not its contents
  -n, --no-group    Do not group directories first (default: grouped)
  -E                Show file creation time
  -T                Tree view of directory structure
  -N                Disable natural sorting
  -P                Show full file path
  -O                Display file owner
  -M                Show summary (default: on)
  -h, --help        Display this help message
  -v, --version     Display version information
```

## 🛠️ Compilation

To compile `lk`, you'll need a C compiler for Windows, such as:

*   **MinGW-w64 (GCC for Windows):** Recommended for its ease of use and compatibility.
*   **Visual Studio:** If you have Visual Studio installed, you can use its command-line compiler.

**Using MinGW-w64 (GCC):**

1.  **Install MinGW-w64:** Download and install MinGW-w64 from [https://mingw-w64.org/](https://mingw-w64.org/). Make sure to add the MinGW-w64 `bin` directory to your system's `PATH` environment variable.
2.  **Save the code:** Save the provided C code as `lk.c`.
3.  **Compile:** Open a command prompt or PowerShell in the directory where you saved `lk.c` and run the following command:

    ```bash
    gcc lk.c -o lk.exe
    ```

    This will compile `lk.c` and create an executable file named `lk.exe`.

**Using Visual Studio (Developer Command Prompt):**

1.  **Open Developer Command Prompt:** Open the "Developer Command Prompt for VS [version]" from your Start Menu.
2.  **Navigate to directory:** Navigate to the directory where you saved `lk.c` using the `cd` command.
3.  **Compile:** Run the following command:

    ```bash
    cl lk.c
    ```

    This will compile `lk.c` and create an executable file named `lk.exe` (or potentially `lk.obj` and you might need to link it with `link lk.obj`).

After compilation, you can run `lk.exe` from the command line. You might want to add the directory containing `lk.exe` to your system's `PATH` environment variable so you can run `lk` from any directory.

## 📜 License

This project is licensed under the **MIT License**. See the `LICENSE` file for details.

---

Made with ❤️ for Windows command-line users. Enjoy!

