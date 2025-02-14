#include <windows.h>
#include <shellapi.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Forward declarations */
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize);
static WORD getColorForFile(const WIN32_FIND_DATAA *data, WORD defaultAttr);

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
static void ClearLineToEnd(HANDLE hConsole, WORD attr);
static void printFileEntry(const char *directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr);
static void readDirectory(const char *path, FileList *list);
static inline void printHeader(const char *path);
static void listDirectory(const char *path, HANDLE hConsole, WORD defaultAttr);
static void listDirectorySelf(const char *path, HANDLE hConsole, WORD defaultAttr);
static void treeDirectory(const char *path, HANDLE hConsole, WORD defaultAttr, int indent);

/* Advanced error handler: prints error message and exits */
static void fatalError(const char *msg) {
    fprintf(stderr, "Fatal error: %s\n", msg);
    exit(EXIT_FAILURE);
}

/* Initializes a FileList with an initial capacity */
static void initFileList(FileList *list) {
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    list->entries = malloc(list->capacity * sizeof(FileEntry));
    if (!list->entries)
        fatalError("Memory allocation error for FileList.");
}

/* Adds a file entry to the FileList */
static void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        FileEntry *temp = realloc(list->entries, list->capacity * sizeof(FileEntry));
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

/* Joins a base path and a child component into a result buffer */
static inline void joinPath(const char *base, const char *child, char *result, size_t size) {
    size_t baseLen = strlen(base), childLen = strlen(child);
    if (baseLen == 0) {
        if (childLen + 1 <= size)
            memcpy(result, child, childLen + 1);
        return;
    }
    memcpy(result, base, baseLen);
    if (base[baseLen - 1] != '\\' && base[baseLen - 1] != '/')
        if (baseLen + 1 < size) result[baseLen++] = '\\';
    if (baseLen + childLen + 1 <= size)
        memcpy(result + baseLen, child, childLen + 1);
}

/* Checks if a file is considered binary based on its extension */
static inline int isBinaryFile(const char *filename) {
    static const char *extensions[] = { ".exe", ".dll", ".bin", ".com", ".bat", ".cmd", NULL };
    const char *dot = strrchr(filename, '.');
    if (dot) {
        for (int i = 0; extensions[i]; i++)
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
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
                stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
}

/* Formats the file size. If humanReadable is true, scales the size to appropriate units */
static void formatSize(ULONGLONG size, char *buffer, size_t bufferSize, int humanReadable) {
    if (!humanReadable) {
        sprintf(buffer, "%llu", size);
        return;
    }
    const char *suffixes[] = { "B", "K", "M", "G", "T", "P" };
    int i = 0;
    double s = (double)size;
    while (s >= 1024 && i < 5) { s /= 1024; i++; }
    sprintf(buffer, "%.1f%s", s, suffixes[i]);
}

/* Compares two strings using natural order */
static int naturalCompare(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            char *endA, *endB;
            long numA = strtol(a, &endA, 10);
            long numB = strtol(b, &endB, 10);
            if (numA != numB)
                return (numA < numB) ? -1 : 1;
            a = endA; b = endB;
        } else {
            char ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
            if (ca != cb)
                return (ca < cb) ? -1 : 1;
            a++; b++;
        }
    }
    return (*a == *b) ? 0 : ((*a) ? 1 : -1);
}

/* Comparison function for sorting file entries */
static int compareEntries(const void *a, const void *b) {
    const FileEntry *fa = a, *fb = b;
    if (g_options.groupDirs) {
        int aIsDir = (fa->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        int bIsDir = (fb->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        if (aIsDir != bIsDir)
            return (aIsDir > bIsDir) ? -1 : 1;
    }
    if (g_options.sortByTime) {
        ULONGLONG ta = (((ULONGLONG)fa->findData.ftLastWriteTime.dwHighDateTime << 32) |
                         fa->findData.ftLastWriteTime.dwLowDateTime);
        ULONGLONG tb = (((ULONGLONG)fb->findData.ftLastWriteTime.dwHighDateTime << 32) |
                         fb->findData.ftLastWriteTime.dwLowDateTime);
        if (ta != tb)
            return (ta < tb) ? (g_options.reverseSort ? 1 : -1)
                             : (g_options.reverseSort ? -1 : 1);
    } else if (g_options.sortBySize) {
        ULONGLONG sa = (((ULONGLONG)fa->findData.nFileSizeHigh << 32) | fa->findData.nFileSizeLow);
        ULONGLONG sb = (((ULONGLONG)fb->findData.nFileSizeHigh << 32) | fb->findData.nFileSizeLow);
        if (sa != sb)
            return (sa < sb) ? (g_options.reverseSort ? 1 : -1)
                             : (g_options.reverseSort ? -1 : 1);
    } else if (g_options.sortByExtension) {
        const char *extA = strrchr(fa->findData.cFileName, '.');
        const char *extB = strrchr(fb->findData.cFileName, '.');
        if (extA && extB) {
            int cmp = _stricmp(extA, extB);
            if (cmp)
                return g_options.reverseSort ? -cmp : cmp;
        }
    }
    int cmp = (g_options.naturalSort) ? naturalCompare(fa->findData.cFileName, fb->findData.cFileName)
                                      : _stricmp(fa->findData.cFileName, fb->findData.cFileName);
    return g_options.reverseSort ? -cmp : cmp;
}

/* Clears the remainder of the current console line */
static void ClearLineToEnd(HANDLE hConsole, WORD attr) {
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

/* Prints a single file entry according to options */
static void printFileEntry(const char *directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr) {
    const WIN32_FIND_DATAA *data = &entry->findData;
    int isDir = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    WORD baseBG = defaultAttr & 0xF0;
    WORD rowBG = (index % 2) ? (baseBG | BACKGROUND_INTENSITY) : baseBG;
    SetConsoleTextAttribute(hConsole, GRAY_TEXT | rowBG);
    printf("%3d. ", index);
    if (g_options.longFormat) {
        char attrStr[8];
        formatAttributes(data->dwFileAttributes, isDir, attrStr, sizeof(attrStr));
        LARGE_INTEGER filesize;
        filesize.LowPart = data->nFileSizeLow;
        filesize.HighPart = data->nFileSizeHigh;
        char sizeStr[32];
        if (isDir)
            strcpy(sizeStr, "<DIR>");
        else
            formatSize(filesize.QuadPart, sizeStr, sizeof(sizeStr), g_options.humanSize);
        char modTimeStr[32], createTimeStr[32] = "";
        fileTimeToString(&data->ftLastWriteTime, modTimeStr, sizeof(modTimeStr));
        if (g_options.showCreationTime)
            fileTimeToString(&data->ftCreationTime, createTimeStr, sizeof(createTimeStr));
        if (g_options.showOwner) {
            if (g_options.showCreationTime)
                printf("%-6s %12s %20s %20s ", attrStr, sizeStr, createTimeStr, modTimeStr);
            else
                printf("%-6s %12s %20s ", attrStr, sizeStr, modTimeStr);
            char fullPath[MAX_PATH];
            joinPath(directory, data->cFileName, fullPath, MAX_PATH);
            char owner[256] = "";
            printf("%-20s ", getFileOwner(fullPath, owner, sizeof(owner)) ? owner : "Unknown");
        } else {
            if (g_options.showCreationTime)
                printf("%-6s %12s %20s %20s ", attrStr, sizeStr, createTimeStr, modTimeStr);
            else
                printf("%-6s %12s %20s ", attrStr, sizeStr, modTimeStr);
        }
    }
    /* File type indicator: only for directories and reparse points */
    if (g_options.fileTypeIndicator) {
        if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            printf("/");
        else if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            printf("@");
    }
    WORD fileColor = getColorForFile(data, defaultAttr);
    SetConsoleTextAttribute(hConsole, (fileColor & 0x0F) | rowBG);
    printf("%s", data->cFileName);
    if (g_options.showFullPath) {
        char fullPath[MAX_PATH];
        joinPath(directory, data->cFileName, fullPath, MAX_PATH);
        printf(" (%s)", fullPath);
    }
    ClearLineToEnd(hConsole, defaultAttr);
    printf("\n");
    SetConsoleTextAttribute(hConsole, defaultAttr);
}

/* Returns a color based on the file type */
static WORD getColorForFile(const WIN32_FIND_DATAA *data, WORD defaultAttr) {
    if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return SYMLINK_COLOR;
    if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return FOLDER_COLOR;
    return isBinaryFile(data->cFileName) ? BINARY_COLOR : defaultAttr;
}

/* Reads the contents of a directory into the FileList, applying filters */
static void readDirectory(const char *path, FileList *list) {
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*", path);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileExA(searchPath, FindExInfoStandard, &findData,
                                     FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    size_t filterLen = strlen(g_options.filterPattern);
    do {
        if (!strcmp(findData.cFileName, ".") || !strcmp(findData.cFileName, ".."))
            continue;
        if (!g_options.showAll && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
            continue;
        if (filterLen && !strstr(findData.cFileName, g_options.filterPattern))
            continue;
        addFileEntry(list, &findData);
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

/* Prints the header for a directory listing */
static inline void printHeader(const char *path) {
    char absPath[MAX_PATH];
    if (!GetFullPathNameA(path, MAX_PATH, absPath, NULL))
        strcpy(absPath, path);
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

/* Lists directory contents (with recursion if enabled) */
static void listDirectory(const char *path, HANDLE hConsole, WORD defaultAttr) {
    FileList list;
    initFileList(&list);
    readDirectory(path, &list);
    if (list.count)
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
                LARGE_INTEGER fs;
                fs.LowPart = d->nFileSizeLow;
                fs.HighPart = d->nFileSizeHigh;
                totalSize += fs.QuadPart;
            }
        }
        char sizeStr[32];
        formatSize(totalSize, sizeStr, sizeof(sizeStr), g_options.humanSize);
        printf("\nSummary: %d directories, %d files, total size: %s\n", dirCount, fileCount, sizeStr);
    }
    if (g_options.recursive && !g_options.treeView) {
        for (size_t i = 0; i < list.count; i++) {
            const WIN32_FIND_DATAA *data = &list.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                char newPath[MAX_PATH];
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

/* Displays directory structure in a tree format */
static void treeDirectory(const char *path, HANDLE hConsole, WORD defaultAttr, int indent) {
    FileList list;
    initFileList(&list);
    readDirectory(path, &list);
    if (list.count)
        qsort(list.entries, list.count, sizeof(FileEntry), compareEntries);
    char indentBuf[64] = {0};
    int indentCount = indent < 31 ? indent : 31;
    for (int i = 0; i < indentCount; i++) {
        strcat(indentBuf, "  ");
    }
    for (size_t i = 0; i < list.count; i++) {
        const WIN32_FIND_DATAA *data = &list.entries[i].findData;
        printf("%s|- [%c] %s\n", indentBuf, (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 'D' : 'F', data->cFileName);
    }
    if (g_options.recursive) {
        for (size_t i = 0; i < list.count; i++) {
            const WIN32_FIND_DATAA *data = &list.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                char newPath[MAX_PATH];
                joinPath(path, data->cFileName, newPath, MAX_PATH);
                printf("%s|\n", indentBuf);
                treeDirectory(newPath, hConsole, defaultAttr, indent + 1);
            }
        }
    }
    freeFileList(&list);
}

/* Retrieves the file owner. Returns 1 on success, 0 otherwise */
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize) {
    DWORD dwSize = 0;
    if (!GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, NULL, 0, &dwSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 0;
    PSECURITY_DESCRIPTOR psd = malloc(dwSize);
    if (!psd) return 0;
    if (!GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, psd, dwSize, &dwSize)) {
        free(psd);
        return 0;
    }
    PSID pSid = NULL;
    BOOL ownerDefaulted;
    if (!GetSecurityDescriptorOwner(psd, &pSid, &ownerDefaulted)) {
        free(psd);
        return 0;
    }
    char name[256] = {0}, domain[256] = {0};
    DWORD nameSize = sizeof(name), domainSize = sizeof(domain);
    SID_NAME_USE sidType;
    if (!LookupAccountSidA(NULL, pSid, name, &nameSize, domain, &domainSize, &sidType)) {
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
    char **files = malloc(filesCapacity * sizeof(char *));
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
                    printf("lk version 1.3\n");
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
                            printf("lk version 1.2\n");
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
                char **temp = realloc(files, filesCapacity * sizeof(char *));
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
    char **absPaths = malloc(fileCount * sizeof(char *));
    if (!absPaths)
        fatalError("Memory allocation error for absolute paths array.");
    for (int i = 0; i < fileCount; i++) {
        absPaths[i] = malloc(MAX_PATH);
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
