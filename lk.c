#include <windows.h>
#include <shellapi.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Forward declarations */
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize);
static inline WORD getColorForFile(const WIN32_FIND_DATAA *data, WORD defaultAttr);

/* Macro for initial capacity of dynamic arrays */
#define INITIAL_CAPACITY 128

/* Console color definitions */
#define BINARY_COLOR   (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define FOLDER_COLOR   (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define SYMLINK_COLOR  (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define GRAY_TEXT      (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

/* Application options structure */
typedef struct Options {
    int showAll;           // Show hidden files.
    int longFormat;        // Use detailed listing format.
    int recursive;         // List directories recursively.
    int sortBySize;        // Sort by file size.
    int sortByTime;        // Sort by modification time.
    int sortByExtension;   // Sort by file extension.
    int reverseSort;       // Reverse the sort order.
    int humanSize;         // Display sizes in human-readable format.
    int fileTypeIndicator; // Append file type indicator.
    int listDirs;          // List directory entry itself, not its contents.
    int groupDirs;         // Group directories first.
    int showCreationTime;  // Show file creation time.
    int treeView;          // Tree view of directory structure.
    int naturalSort;       // Use natural sorting.
    int showFullPath;      // Display full file path.
    int showOwner;         // Display file owner.
    int showSummary;       // Display summary information.
    char filterPattern[256]; // Filename filter (empty = no filter).
} Options;

/* Global options with default settings */
static Options g_options = {
    .showAll = 0, .longFormat = 1, .recursive = 0, .sortBySize = 0,
    .sortByTime = 0, .sortByExtension = 0, .reverseSort = 0, .humanSize = 1,
    .fileTypeIndicator = 1, .listDirs = 0, .groupDirs = 1, .showCreationTime = 0,
    .treeView = 0, .naturalSort = 1, .showFullPath = 0, .showOwner = 0,
    .showSummary = 1, .filterPattern = ""
};

/* Structure wrapping WIN32_FIND_DATAA for each file/directory entry */
typedef struct {
    WIN32_FIND_DATAA findData;
} FileEntry;

/* Dynamic array of FileEntry */
typedef struct {
    FileEntry *entries;
    size_t count;
    size_t capacity;
} FileList;

/* Function prototypes for internal functions */
static void fatalError(const char *msg);
static void initFileList(FileList *list);
static void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data);
static void freeFileList(FileList *list);
static inline void joinPath(const char *base, const char *child, char *result, size_t size);
static inline int isBinaryFile(const char *filename);
static inline void formatAttributes(DWORD attr, int isDir, char *outStr, size_t size);
static void fileTimeToString(const FILETIME *ft, char *buffer, size_t size);
static void formatSize(ULONGLONG size, char *buffer, size_t bufferSize, int humanReadable);
static int naturalCompare(const char *a, const char *b);
static int compareEntries(const void *a, const void *b);
static void clearLineToEnd(HANDLE hConsole, WORD attr);
static void printFileEntry(const char *directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr);
static void readDirectory(const char *path, FileList *list);
static inline void printHeader(const char *path);
static void listDirectory(const char *path, HANDLE hConsole, WORD defaultAttr);
static void listDirectorySelf(const char *path, HANDLE hConsole, WORD defaultAttr);
static void treeDirectory(const char *path, HANDLE hConsole, WORD defaultAttr, int indent);

/* Helper function: Prints a fatal error message and exits the program */
static void fatalError(const char *msg) {
    fprintf(stderr, "Fatal error: %s\n", msg);
    exit(EXIT_FAILURE);
}

/* Initializes a FileList with an initial capacity */
static void initFileList(FileList *list) {
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    list->entries = (FileEntry *)malloc(list->capacity * sizeof(FileEntry));
    if (!list->entries)
        fatalError("Memory allocation error for FileList.");
}

/* Adds a file entry to the FileList */
static void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        FileEntry *temp = (FileEntry *)realloc(list->entries, list->capacity * sizeof(FileEntry));
        if (!temp)
            fatalError("Memory reallocation error for FileList.");
        list->entries = temp;
    }
    list->entries[list->count++].findData = *data;
}

/* Frees the memory allocated for the FileList */
static void freeFileList(FileList *list) {
    free(list->entries);
    list->entries = NULL;
    list->count = list->capacity = 0;
}

/* Fast ASCII lower-case conversion (optimized for performance) */
static inline int fast_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c | 0x20) : c;
}

/* Optimized path-joining function.
 * Safely concatenates a base path and a child component into the result buffer.
 */
static inline void joinPath(const char *base, const char *child, char *result, size_t size) {
    if (!base || !child || !result || size == 0)
        return;
    size_t baseLen = strlen(base);
    int needsSlash = (baseLen && (base[baseLen - 1] != '\\' && base[baseLen - 1] != '/'));
    if (needsSlash)
        snprintf(result, size, "%s\\%s", base, child);
    else
        snprintf(result, size, "%s%s", base, child);
}

/* Optimized check to determine if a file is binary based on its extension */
static inline int isBinaryFile(const char *filename) {
    static const char *extensions[] = { ".exe", ".dll", ".bin", ".com", ".bat", ".cmd", NULL };
    const char *dot = strrchr(filename, '.');
    if (!dot)
        return 0;
    for (int i = 0; extensions[i]; i++) {
        if (_stricmp(dot, extensions[i]) == 0)
            return 1;
    }
    return 0;
}

/* Formats file attributes into a short string representation */
static inline void formatAttributes(DWORD attr, int isDir, char *outStr, size_t size) {
    if (size < 6) return;
    outStr[0] = isDir ? 'd' : '-';
    outStr[1] = (attr & FILE_ATTRIBUTE_READONLY) ? 'R' : '-';
    outStr[2] = (attr & FILE_ATTRIBUTE_HIDDEN)   ? 'H' : '-';
    outStr[3] = (attr & FILE_ATTRIBUTE_SYSTEM)     ? 'S' : '-';
    outStr[4] = (attr & FILE_ATTRIBUTE_ARCHIVE)    ? 'A' : '-';
    outStr[5] = '\0';
}

/* Converts FILETIME to a string in the format "YYYY-MM-DD HH:MM:SS" */
static void fileTimeToString(const FILETIME *ft, char *buffer, size_t size) {
    SYSTEMTIME stUTC, stLocal;
    FileTimeToSystemTime(ft, &stUTC);
    SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
    if (size >= 20)
        snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
                 stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                 stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
}

/* Formats the file size. If humanReadable is true, scales the size to appropriate units */
static void formatSize(ULONGLONG size, char *buffer, size_t bufferSize, int humanReadable) {
    if (!humanReadable) {
        snprintf(buffer, bufferSize, "%llu", size);
        return;
    }
    const char *suffixes[] = { "B", "K", "M", "G", "T", "P" };
    int i = 0;
    double s = (double)size;
    while (s >= 1024 && i < 5) { s /= 1024; i++; }
    snprintf(buffer, bufferSize, "%.1f%s", s, suffixes[i]);
}

/* Optimized natural string comparison function.
 * Compares two strings, treating numerical substrings in a natural order.
 */
static int naturalCompare(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            unsigned long numA = 0, numB = 0;
            while (isdigit((unsigned char)*a))
                numA = numA * 10 + (*a++ - '0');
            while (isdigit((unsigned char)*b))
                numB = numB * 10 + (*b++ - '0');
            if (numA != numB)
                return (numA < numB) ? -1 : 1;
        } else {
            int ca = fast_tolower((unsigned char)*a);
            int cb = fast_tolower((unsigned char)*b);
            if (ca != cb)
                return ca - cb;
            a++; 
            b++;
        }
    }
    return (*a) ? 1 : ((*b) ? -1 : 0);
}

/* Optimized comparison function for file entries.
 * Supports grouping directories, sorting by time, size, extension, and natural/lexicographical order.
 */
static int compareEntries(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    int aIsDir = (fa->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    int bIsDir = (fb->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (g_options.groupDirs && aIsDir != bIsDir)
        return aIsDir ? -1 : 1;

    if (g_options.sortByTime) {
        ULONGLONG ta = (((ULONGLONG)fa->findData.ftLastWriteTime.dwHighDateTime << 32) |
                        fa->findData.ftLastWriteTime.dwLowDateTime);
        ULONGLONG tb = (((ULONGLONG)fb->findData.ftLastWriteTime.dwHighDateTime << 32) |
                        fb->findData.ftLastWriteTime.dwLowDateTime);
        if (ta != tb)
            return g_options.reverseSort ? ((ta < tb) ? 1 : -1) : ((ta < tb) ? -1 : 1);
    }
    if (g_options.sortBySize) {
        ULONGLONG sa = (((ULONGLONG)fa->findData.nFileSizeHigh << 32) | fa->findData.nFileSizeLow);
        ULONGLONG sb = (((ULONGLONG)fb->findData.nFileSizeHigh << 32) | fb->findData.nFileSizeLow);
        if (sa != sb)
            return g_options.reverseSort ? ((sa < sb) ? 1 : -1) : ((sa < sb) ? -1 : 1);
    }
    if (g_options.sortByExtension) {
        const char *extA = strrchr(fa->findData.cFileName, '.');
        const char *extB = strrchr(fb->findData.cFileName, '.');
        if (extA && extB) {
            int result = _stricmp(extA, extB);
            if (result)
                return g_options.reverseSort ? -result : result;
        }
    }
    int result = g_options.naturalSort ?
                 naturalCompare(fa->findData.cFileName, fb->findData.cFileName) :
                 _stricmp(fa->findData.cFileName, fb->findData.cFileName);
    return g_options.reverseSort ? -result : result;
}

/* Clears the remainder of the current console line */
static void clearLineToEnd(HANDLE hConsole, WORD attr) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return;
    int curCol = csbi.dwCursorPosition.X, width = csbi.dwSize.X;
    if (curCol < width) {
        DWORD written;
        FillConsoleOutputCharacterA(hConsole, ' ', width - curCol, csbi.dwCursorPosition, &written);
        FillConsoleOutputAttribute(hConsole, attr, width - curCol, csbi.dwCursorPosition, &written);
    }
}

/* Optimized function to print a file or directory entry.
 * Displays detailed information based on the specified options.
 */
static void printFileEntry(const char *directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr) {
    const WIN32_FIND_DATAA *data = &entry->findData;
    const int isDir = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const WORD baseBG = defaultAttr & 0xF0;
    const WORD rowBG = (index & 1) ? (baseBG | BACKGROUND_INTENSITY) : baseBG;

    /* Set alternate row background for improved readability */
    SetConsoleTextAttribute(hConsole, GRAY_TEXT | rowBG);
    printf("%3d. ", index);

    if (g_options.longFormat) {
        char attrStr[6];
        formatAttributes(data->dwFileAttributes, isDir, attrStr, sizeof(attrStr));

        char sizeStr[32];
        if (isDir)
            strncpy(sizeStr, "<DIR>", sizeof(sizeStr) - 1);
        else {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = data->nFileSizeLow;
            fileSize.HighPart = data->nFileSizeHigh;
            formatSize(fileSize.QuadPart, sizeStr, sizeof(sizeStr), g_options.humanSize);
        }

        char modTimeStr[32];
        fileTimeToString(&data->ftLastWriteTime, modTimeStr, sizeof(modTimeStr));

        if (g_options.showOwner) {
            char fullPath[MAX_PATH];
            joinPath(directory, data->cFileName, fullPath, MAX_PATH);
            char owner[256] = "Unknown";
            getFileOwner(fullPath, owner, sizeof(owner));
            if (g_options.showCreationTime) {
                char createTimeStr[32];
                fileTimeToString(&data->ftCreationTime, createTimeStr, sizeof(createTimeStr));
                printf("%-6s %12s %20s %20s %-20s ", attrStr, sizeStr, createTimeStr, modTimeStr, owner);
            } else {
                printf("%-6s %12s %20s %-20s ", attrStr, sizeStr, modTimeStr, owner);
            }
        } else {
            if (g_options.showCreationTime) {
                char createTimeStr[32];
                fileTimeToString(&data->ftCreationTime, createTimeStr, sizeof(createTimeStr));
                printf("%-6s %12s %20s %20s ", attrStr, sizeStr, createTimeStr, modTimeStr);
            } else {
                printf("%-6s %12s %20s ", attrStr, sizeStr, modTimeStr);
            }
        }
    }

    if (g_options.fileTypeIndicator) {
        if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            putchar('/');
        else if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            putchar('@');
    }

    /* Set file color based on its type */
    WORD fileColor = getColorForFile(data, defaultAttr);
    SetConsoleTextAttribute(hConsole, (fileColor & 0x0F) | rowBG);
    printf("%s", data->cFileName);

    if (g_options.showFullPath) {
        char fullPath[MAX_PATH];
        joinPath(directory, data->cFileName, fullPath, MAX_PATH);
        printf(" (%s)", fullPath);
    }

    clearLineToEnd(hConsole, defaultAttr);
    putchar('\n');
    SetConsoleTextAttribute(hConsole, defaultAttr);
}

/* Returns a color based on the file type */
static inline WORD getColorForFile(const WIN32_FIND_DATAA *data, WORD defaultAttr) {
    if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return SYMLINK_COLOR;
    if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return FOLDER_COLOR;
    return isBinaryFile(data->cFileName) ? BINARY_COLOR : defaultAttr;
}

/* Optimized wildcard pattern matching function.
 * Supports '*' (matches zero or more characters) and '?' (matches exactly one character).
 */
static int wildcardMatch(const char *pattern, const char *str) {
    const char *star = NULL;
    const char *s = str, *p = pattern;
    while (*s) {
        if (*p == '?' || fast_tolower((unsigned char)*p) == fast_tolower((unsigned char)*s)) {
            p++;
            s++;
        } else if (*p == '*') {
            star = p++;
        } else if (star) {
            p = star + 1;
            s++;
        } else {
            return 0;
        }
    }
    while (*p == '*')
        p++;
    return (*p == '\0');
}

/* Optimized directory reading function with optional pattern filtering.
 * Reads the contents of the specified directory and applies a wildcard filter if provided.
 */
static void readDirectory(const char *path, FileList *list) {
    char directory[MAX_PATH] = {0};
    char wildcard[256] = {0};

    if (strchr(path, '*') || strchr(path, '?')) {
        const char *sep = strrchr(path, '\\');
        if (!sep)
            sep = strrchr(path, '/');
        if (sep) {
            size_t dirLen = sep - path;
            if (dirLen >= MAX_PATH)
                dirLen = MAX_PATH - 1;
            memcpy(directory, path, dirLen);
            directory[dirLen] = '\0';
            strncpy(wildcard, sep + 1, sizeof(wildcard) - 1);
            wildcard[sizeof(wildcard) - 1] = '\0';
        } else {
            strcpy(directory, ".");
            strncpy(wildcard, path, sizeof(wildcard) - 1);
            wildcard[sizeof(wildcard) - 1] = '\0';
        }
    } else {
        strcpy(directory, path);
        if (g_options.filterPattern[0])
            strncpy(wildcard, g_options.filterPattern, sizeof(wildcard) - 1);
        else
            wildcard[0] = '\0';
    }

    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", directory);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileExA(searchPath, FindExInfoStandard, &findData,
                                     FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Unable to open directory %s (Error %lu)\n", directory, GetLastError());
        return;
    }

    const size_t wildcardLen = strlen(wildcard);
    do {
        /* Skip the "." and ".." entries */
        if (findData.cFileName[0] == '.' &&
           (findData.cFileName[1] == '\0' ||
            (findData.cFileName[1] == '.' && findData.cFileName[2] == '\0')))
            continue;
        if (!g_options.showAll && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
            continue;
        if (wildcardLen && !wildcardMatch(wildcard, findData.cFileName))
            continue;
        addFileEntry(list, &findData);
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

/* Prints the header for a directory listing */
static inline void printHeader(const char *path) {
    char absPath[MAX_PATH] = {0};
    if (!GetFullPathNameA(path, MAX_PATH, absPath, NULL))
        strncpy(absPath, path, MAX_PATH - 1);
    printf("\n[%s]:\n", absPath);
    if (g_options.longFormat) {
        if (g_options.showOwner) {
            if (g_options.showCreationTime)
                printf("    %-6s %12s %20s %20s %-20s %s\n", "Attr", "Size", "Created", "Modified", "Owner", "Name");
            else
                printf("    %-6s %12s %20s %-20s %s\n", "Attr", "Size", "Modified", "Owner", "Name");
            printf("    -----------------------------------------------------------------------------------------------\n");
        } else {
            if (g_options.showCreationTime)
                printf("    %-6s %12s %20s %20s %s\n", "Attr", "Size", "Created", "Modified", "Name");
            else
                printf("    %-6s %12s %20s %s\n", "Attr", "Size", "Modified", "Name");
            printf("    --------------------------------------------------------------------------------\n");
        }
    }
}

/* Lists directory contents and prints a summary */
static void listDirectory(const char *path, HANDLE hConsole, WORD defaultAttr) {
    FileList list;
    initFileList(&list);
    readDirectory(path, &list);
    
    if (list.count > 0)
        qsort(list.entries, list.count, sizeof(FileEntry), compareEntries);
    
    printHeader(path);
    
    for (size_t i = 0; i < list.count; i++)
        printFileEntry(path, (int)(i + 1), &list.entries[i], hConsole, defaultAttr);
    
    if (g_options.showSummary) {
        int dirCount = 0, fileCount = 0;
        ULONGLONG totalSize = 0;
        for (size_t i = 0; i < list.count; i++) {
            const WIN32_FIND_DATAA *d = &list.entries[i].findData;
            if (d->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                dirCount++;
            else {
                fileCount++;
                ULARGE_INTEGER fs;
                fs.LowPart = d->nFileSizeLow;
                fs.HighPart = d->nFileSizeHigh;
                totalSize += fs.QuadPart;
            }
        }
        char sizeStr[32] = {0};
        formatSize(totalSize, sizeStr, sizeof(sizeStr), g_options.humanSize);
        printf("\nSummary: %d directories, %d files, total size: %s\n", dirCount, fileCount, sizeStr);
    }
    
    if (g_options.recursive && !g_options.treeView) {
        for (size_t i = 0; i < list.count; i++) {
            const WIN32_FIND_DATAA *data = &list.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                char newPath[MAX_PATH] = {0};
                joinPath(path, data->cFileName, newPath, MAX_PATH);
                listDirectory(newPath, hConsole, defaultAttr);
            }
        }
    }
    
    freeFileList(&list);
}

/* Lists a single directory entry (used if listDirs option is enabled) */
static void listDirectorySelf(const char *path, HANDLE hConsole, WORD defaultAttr) {
    WIN32_FIND_DATAA data;
    HANDLE hFind = FindFirstFileA(path, &data);
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error retrieving info for: %s (Error code: %lu)\n", path, GetLastError());
        return;
    }
    FindClose(hFind);
    printHeader(path);
    FileEntry entry;
    entry.findData = data;
    printFileEntry(path, 1, &entry, hConsole, defaultAttr);
}

/* Displays the directory structure in a tree format */
static void treeDirectory(const char *path, HANDLE hConsole, WORD defaultAttr, int indent) {
    FileList list;
    initFileList(&list);
    readDirectory(path, &list);
    
    if (list.count > 0)
        qsort(list.entries, list.count, sizeof(FileEntry), compareEntries);
    
    char indentBuf[64] = {0};
    int indentCount = (indent < 31) ? indent : 31;
    int indentLength = indentCount * 2;
    if (indentLength >= (int)sizeof(indentBuf))
        indentLength = sizeof(indentBuf) - 1;
    memset(indentBuf, ' ', indentLength);
    indentBuf[indentLength] = '\0';
    
    for (size_t i = 0; i < list.count; i++) {
        const WIN32_FIND_DATAA *data = &list.entries[i].findData;
        char typeIndicator = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'D' : 'F';
        if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            typeIndicator = '@';
        printf("%s|- [%c] %s\n", indentBuf, typeIndicator, data->cFileName);
    }
    
    if (g_options.recursive) {
        for (size_t i = 0; i < list.count; i++) {
            const WIN32_FIND_DATAA *data = &list.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                char newPath[MAX_PATH] = {0};
                joinPath(path, data->cFileName, newPath, MAX_PATH);
                printf("%s|\n", indentBuf);
                treeDirectory(newPath, hConsole, defaultAttr, indent + 1);
            }
        }
    }
    freeFileList(&list);
}

/* Retrieves the file owner with robust error handling */
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize) {
    DWORD dwSize = 0;
    if (!GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, NULL, 0, &dwSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        fprintf(stderr, "Error: Unable to retrieve security info for %s (Error %lu)\n", filePath, GetLastError());
        return 0;
    }
    PSECURITY_DESCRIPTOR psd = (PSECURITY_DESCRIPTOR)malloc(dwSize);
    if (!psd) {
        fprintf(stderr, "Error: Memory allocation failed for security descriptor.\n");
        return 0;
    }
    if (!GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, psd, dwSize, &dwSize)) {
        fprintf(stderr, "Error: Failed to get security descriptor for %s (Error %lu)\n", filePath, GetLastError());
        free(psd);
        return 0;
    }
    PSID pSid = NULL;
    BOOL ownerDefaulted = FALSE;
    if (!GetSecurityDescriptorOwner(psd, &pSid, &ownerDefaulted)) {
        fprintf(stderr, "Error: Failed to retrieve owner from security descriptor for %s\n", filePath);
        free(psd);
        return 0;
    }
    char name[256] = {0}, domain[256] = {0};
    DWORD nameSize = sizeof(name), domainSize = sizeof(domain);
    SID_NAME_USE sidType;
    if (!LookupAccountSidA(NULL, pSid, name, &nameSize, domain, &domainSize, &sidType)) {
        fprintf(stderr, "Error: LookupAccountSid failed for %s (Error %lu)\n", filePath, GetLastError());
        free(psd);
        return 0;
    }
    snprintf(owner, ownerSize, "%s\\%s", domain, name);
    free(psd);
    return 1;
}

/* Main function: parses command-line options, converts paths to absolute, and lists directories/files */
int main(int argc, char *argv[]) {
    const char *helpText =
        "\nUsage: lk [options] [path ...]\n\n"
        "Options:\n"
        "  -a, --all         Show hidden files\n"
        "  -s, --short       Use short format (disable long listing)\n"
        "  -R                Recursively list subdirectories\n"
        "  -S                Sort by file size\n"
        "  -t                Sort by modification time\n"
        "  -x                Sort by file extension\n"
        "  -r                Reverse sort order\n"
        "  -b, --bytes       Show file sizes in raw bytes (default: human-readable)\n"
        "  -F                Append file type indicator (default: on)\n"
        "  -d                List directory entry itself, not its contents\n"
        "  -n, --no-group    Do not group directories first (default: grouped)\n"
        "  -E                Show file creation time\n"
        "  -T                Tree view of directory structure\n"
        "  -N                Disable natural sorting\n"
        "  -P                Show full file path\n"
        "  -O                Display file owner\n"
        "  -M                Show summary (default: on)\n"
        "  -h, --help        Display this help message\n"
        "  -v, --version     Display version information\n\n"
        "Examples:\n"
        "  lk -s\n"
        "  lk -b\n"
        "  lk -n\n"
        "  lk -R C:\\path\\to\\directory\n\n";

    int fileCount = 0, filesCapacity = 16;
    char **files = (char **)malloc(filesCapacity * sizeof(char *));
    if (!files)
        fatalError("Memory allocation error for files array.");
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == '-') {
                if (!strcmp(argv[i], "--all"))
                    g_options.showAll = 1;
                else if (!strcmp(argv[i], "--short"))
                    g_options.longFormat = 0;
                else if (!strcmp(argv[i], "--bytes"))
                    g_options.humanSize = 0;
                else if (!strcmp(argv[i], "--no-group"))
                    g_options.groupDirs = 0;
                else if (!strcmp(argv[i], "--help")) {
                    printf("%s", helpText);
                    free(files);
                    return EXIT_SUCCESS;
                }
                else if (!strcmp(argv[i], "--version")) {
                    printf("lk version 1.4\n");
                    free(files);
                    return EXIT_SUCCESS;
                }
                else {
                    fprintf(stderr, "Unknown option: %s\n", argv[i]);
                    printf("%s", helpText);
                    free(files);
                    return EXIT_FAILURE;
                }
            } else {
                size_t len = strlen(argv[i]);
                for (size_t j = 1; j < len; j++) {
                    switch (argv[i][j]) {
                        case 'a': g_options.showAll = 1; break;
                        case 's': g_options.longFormat = 0; break;
                        case 'R': g_options.recursive = 1; break;
                        case 'S': g_options.sortBySize = 1; break;
                        case 't': g_options.sortByTime = 1; break;
                        case 'x': g_options.sortByExtension = 1; break;
                        case 'r': g_options.reverseSort = 1; break;
                        case 'b': g_options.humanSize = 0; break;
                        case 'F': g_options.fileTypeIndicator = 1; break;
                        case 'd': g_options.listDirs = 1; break;
                        case 'n': g_options.groupDirs = 0; break;
                        case 'E': g_options.showCreationTime = 1; break;
                        case 'T': g_options.treeView = 1; break;
                        case 'N': g_options.naturalSort = 0; break;
                        case 'P': g_options.showFullPath = 1; break;
                        case 'O': g_options.showOwner = 1; break;
                        case 'M': g_options.showSummary = 1; break;
                        case 'h':
                            printf("%s", helpText);
                            free(files);
                            return EXIT_SUCCESS;
                        case 'v':
                            printf("lk version 1.4\n");
                            free(files);
                            return EXIT_SUCCESS;
                        default:
                            fprintf(stderr, "Unknown option: -%c\n", argv[i][j]);
                            printf("%s", helpText);
                            free(files);
                            return EXIT_FAILURE;
                    }
                }
            }
        } else {
            if (fileCount >= filesCapacity) {
                filesCapacity *= 2;
                char **temp = (char **)realloc(files, filesCapacity * sizeof(char *));
                if (!temp)
                    fatalError("Memory reallocation error for files array.");
                files = temp;
            }
            files[fileCount++] = argv[i];
        }
    }
    if (fileCount == 0) {
        files[0] = ".";
        fileCount = 1;
    }
    char **absPaths = (char **)malloc(fileCount * sizeof(char *));
    if (!absPaths)
        fatalError("Memory allocation error for absolute paths array.");
    for (int i = 0; i < fileCount; i++) {
        absPaths[i] = (char *)malloc(MAX_PATH);
        if (!absPaths[i])
            fatalError("Memory allocation error for an absolute path.");
        if (!GetFullPathNameA(files[i], MAX_PATH, absPaths[i], NULL)) {
            strncpy(absPaths[i], files[i], MAX_PATH - 1);
            absPaths[i][MAX_PATH - 1] = '\0';
        }
    }
    free(files);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        csbi.wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    WORD defaultAttr = csbi.wAttributes;
    for (int i = 0; i < fileCount; i++) {
        if (fileCount > 1)
            printf("==> %s <==\n", absPaths[i]);
        if (g_options.listDirs)
            listDirectorySelf(absPaths[i], hConsole, defaultAttr);
        else if (g_options.treeView)
            treeDirectory(absPaths[i], hConsole, defaultAttr, 0);
        else
            listDirectory(absPaths[i], hConsole, defaultAttr);
        if (i < fileCount - 1)
            printf("\n");
        free(absPaths[i]);
    }
    free(absPaths);
    return EXIT_SUCCESS;
}
