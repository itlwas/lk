# lk - Enhanced Directory Listing for Windows

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

**`lk`** is a powerful and user-friendly command-line tool for Windows, designed as an enhanced alternative to the standard `ls` or `dir` commands. It provides a rich set of features for listing directories and files with improved readability, sorting, filtering, and interactive capabilities.

## ✨ Key Features

`lk` goes beyond basic directory listing, offering a range of options to tailor the output to your needs:

*   **Colorized Output:**  Visually distinguish file types with colors (binaries, folders, symlinks).
*   **Detailed Information:**  Display file attributes, sizes (human-readable), modification and creation times, and even file owners in long listing format.
*   **Versatile Sorting:** Sort files by name (natural sort for numbers), size, modification time, or extension, in forward or reverse order.
*   **Directory Grouping:**  Option to group directories first for clearer organization.
*   **Recursive Listing:**  Explore subdirectories recursively with `-R` option.
*   **Tree View:** Visualize directory structure as a tree with `-T` option for a hierarchical overview.
*   **File Filtering:** Filter files by name using a simple pattern.
*   **Interactive Mode:**  Engage with your file system directly from the command line with interactive commands for navigation, file operations, and option toggling.
*   **Summary Information:** Get a quick summary of directories, files, and total size within a directory.
*   **File Preview:** Preview the first 10 lines of text files directly in the terminal.
*   **Full Path Display:** Show the full path of files for clarity.
*   **Hidden Files:** Option to show hidden files with `-a`.

## 🚀 Usage

To use `lk`, simply run it from your command prompt or PowerShell.

```bash
lk [options] [directory ...]
```

If no directory is specified, `lk` will list the contents of the current directory. You can provide one or more directory paths as arguments.

### Command Line Options

`lk` supports a variety of options to customize its behavior. Here's a comprehensive list:

| Option | Description                                      |
| :----- | :----------------------------------------------- |
| `-a`   | Show hidden files.                               |
| `-l`   | Use long listing format (attributes, size, dates). |
| `-R`   | Recursively list subdirectories.                  |
| `-S`   | Sort by file size.                               |
| `-t`   | Sort by modification time.                        |
| `-X`   | Sort by file extension.                          |
| `-r`   | Reverse sort order.                             |
| `-H`   | Use human-readable file sizes (e.g., KB, MB, GB). |
| `-F`   | Append file type indicator (`/`, `@`, `*`).     |
| `-d`   | List directory entry itself, not its contents.   |
| `-G`   | Group directories first.                          |
| `-E`   | Show file creation time (long format).           |
| `-T`   | Tree view of directory structure.                |
| `-N`   | Natural sorting (numbers compared naturally).    |
| `-P`   | Show full file path.                             |
| `-O`   | Display file owner in long listing.              |
| `-M`   | Show summary (directories, files, total size).   |
| `-I`   | Run in interactive mode.                          |
| `-h`, `--help` | Display this help message.                  |
| `-v`, `--version` | Display version information.               |

### Examples

*   **Basic listing of the current directory:**
    ```bash
    lk
    ```

*   **Long listing format with human-readable sizes:**
    ```bash
    lk -lh
    ```

*   **Recursive listing of a specific directory, sorted by size:**
    ```bash
    lk -RS C:\path\to\directory
    ```

*   **Start interactive mode:**
    ```bash
    lk -I
    ```

## 🕹️ Interactive Mode

Run `lk` with the `-I` option to enter interactive mode. This mode provides a command-line interface within `lk` to navigate and interact with your file system.

**Interactive Commands:**

| Command             | Description                                            |
| :------------------ | :----------------------------------------------------- |
| `cd <path>`         | Change the current directory.                           |
| `preview <file>`    | Preview the first 10 lines of a text file.             |
| `filter <pattern>`  | Set file name filter (or `filter` to clear).            |
| `summary`           | Show directory summary (directories, files, size).     |
| `open <file>`       | Open file with associated application.                 |
| `rename <old> <new>`| Rename a file or directory.                           |
| `delete <file>`     | Delete a file or directory.                             |
| `mkdir <dir>`       | Create a new directory.                                |
| `touch <file>`      | Create an empty file.                                  |
| `copy <src> <dst>`  | Copy a file from source to destination.                 |
| `move <src> <dst>`  | Move (rename) a file from source to destination.        |
| `exec <command>`    | Execute a system command.                              |
| `info <file>`       | Display detailed file information.                      |
| `:<flags>`          | Toggle options using flags (e.g., `:alHr`). See flags below. |
| `help`              | Display interactive command help.                       |
| `q` or `:q`        | Quit interactive mode.                                 |

**Interactive Flags (toggled with `:` prefix):**

| Flag | Option                      |
| :--- | :-------------------------- |
| `a`  | Show hidden files           |
| `l`  | Long listing format         |
| `R`  | Recursive listing           |
| `S`  | Sort by size                |
| `t`  | Sort by time                |
| `X`  | Sort by extension           |
| `r`  | Reverse sort order          |
| `H` or `h`| Human-readable sizes      |
| `F`  | File type indicator         |
| `d`  | List directories themselves |
| `G`  | Group directories first     |
| `E`  | Show creation time          |
| `T`  | Tree view                   |
| `N`  | Natural sorting             |
| `P`  | Show full path              |
| `O`  | Display file owner          |
| `M` or `m`| Show summary              |

**Example in Interactive Mode:**

```bash
lk> :lH  // Enable long listing and human-readable sizes
lk> cd C:\Users\Documents
lk> preview my_document.txt
lk> summary
lk> :q     // Quit interactive mode
```

## 🛠️ Compilation

To compile `lk`, you'll need a C compiler for Windows, such as:

*   **MinGW-w64 (GCC for Windows):**  Recommended for its ease of use and compatibility.
*   **Visual Studio:**  If you have Visual Studio installed, you can use its command-line compiler.

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

Made with ❤️ for Windows command-line users.  Enjoy!
