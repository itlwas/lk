#include <windows.h>
#include <shellapi.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAPACITY 128

// Colors for file type highlighting
#define BINARY_COLOR   (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define FOLDER_COLOR   (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define SYMLINK_COLOR  (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define GRAY_TEXT      (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

#define WAIT_ENTER() do { \
    printf("\nPress Enter to continue..."); \
    getchar(); \
} while(0)

// Options for command-line and interactive mode
typedef struct Options {
    int showAll;             // -a: show hidden files
    int longFormat;          // -l: long listing (attributes, size, dates)
    int recursive;           // -R: list subdirectories recursively
    int sortBySize;          // -S: sort by file size
    int sortByTime;          // -t: sort by modification time
    int sortByExtension;     // -X: sort by file extension
    int reverseSort;         // -r: reverse sort order
    int humanSize;           // -H: use human-readable file sizes
    int fileTypeIndicator;   // -F: append file type indicator
    int listDirs;            // -d: list directory entry itself, not its contents
    int groupDirs;           // -G: group directories first
    int showCreationTime;    // -E: show file creation time (long format)
    int treeView;            // -T: tree view of directory structure
    int naturalSort;         // -N: natural sorting (numbers compared naturally)
    int showFullPath;        // -P: show full file path
    int showOwner;           // -O: display file owner in long listing
    int showSummary;         // -M: show summary (directories/files/total size)
    char filterPattern[256]; // file name filter (empty = no filter)
} Options;

static Options g_options = {0};
static int g_interactive = 0;  // interactive mode flag

typedef struct {
    WIN32_FIND_DATAA findData;
} FileEntry;

typedef struct {
    FileEntry* entries;
    size_t count;
    size_t capacity;
} FileList;

/* Function prototypes */
void initFileList(FileList *list);
void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data);
void freeFileList(FileList *list);
static inline void joinPath(const char *base, const char *child, char *result, size_t size);
static inline int isBinaryFile(const char *filename);
static inline void formatAttributes(DWORD attr, int isDir, char *outStr, size_t size);
WORD getColorForFile(const WIN32_FIND_DATAA *data, WORD defaultAttr);
void fileTimeToString(const FILETIME *ft, char *buffer, size_t size);
void formatSize(ULONGLONG size, char *buffer, size_t bufferSize, int humanReadable);
int naturalCompare(const char *a, const char *b);
int compareEntries(const void *a, const void *b);
void printFileEntry(const char *directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr);
void readDirectory(const char *path, FileList *list);
void listDirectory(const char *path, HANDLE hConsole, WORD defaultAttr);
void listDirectorySelf(const char *path, HANDLE hConsole, WORD defaultAttr);
void treeDirectory(const char *path, HANDLE hConsole, WORD defaultAttr, int indent);
void printSummary(FileList *list);
void previewFile(const char *filepath);
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize);
void clearScreen(HANDLE hConsole, WORD defaultAttr);
void interactiveMode(char *currentPath, HANDLE hConsole, WORD defaultAttr);

/* Print usage information */
void printUsage() {
    printf("\nUsage: ik [options] [file ...]\n\n");
    printf("Options:\n");
    printf("  -a             Show hidden files\n");
    printf("  -l             Use long listing format (attributes, size, dates)\n");
    printf("  -R             Recursively list subdirectories\n");
    printf("  -S             Sort by file size\n");
    printf("  -t             Sort by modification time\n");
    printf("  -X             Sort by file extension\n");
    printf("  -r             Reverse sort order\n");
    printf("  -H             Use human-readable file sizes\n");
    printf("  -F             Append file type indicator\n");
    printf("  -d             List directory entry itself, not its contents\n");
    printf("  -G             Group directories first\n");
    printf("  -E             Show file creation time (long format)\n");
    printf("  -T             Tree view of directory structure\n");
    printf("  -N             Natural sorting (numbers compared naturally)\n");
    printf("  -P             Show full file path\n");
    printf("  -O             Display file owner in long listing\n");
    printf("  -M             Show summary (directories, files, total size)\n");
    printf("  -I             Run in interactive mode\n");
    printf("  -h, --help     Display this help message\n");
    printf("  -v, --version  Display version information\n\n");
    printf("Examples:\n");
    printf("  ik -l\n");
    printf("  ik -aR C:\\path\\to\\directory\n");
    printf("  ik -I\n\n");
}

/* Initialize file list */
void initFileList(FileList *list) {
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    list->entries = malloc(list->capacity * sizeof(FileEntry));
    if (!list->entries) {
        fprintf(stderr, "Memory allocation error.\n");
        exit(1);
    }
}
void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        FileEntry *temp = realloc(list->entries, list->capacity * sizeof(FileEntry));
        if (!temp) {
            fprintf(stderr, "Memory reallocation error.\n");
            free(list->entries);
            exit(1);
        }
        list->entries = temp;
    }
    list->entries[list->count].findData = *data;
    list->count++;
}
void freeFileList(FileList *list) {
    free(list->entries);
    list->entries = NULL;
    list->count = list->capacity = 0;
}

/* Join base and child paths */
static inline void joinPath(const char *base, const char *child, char *result, size_t size) {
    size_t baseLen = strlen(base), childLen = strlen(child);
    if (baseLen == 0) {
        if (childLen + 1 <= size)
            memcpy(result, child, childLen + 1);
        return;
    }
    memcpy(result, base, baseLen);
    if (base[baseLen - 1] != '\\' && base[baseLen - 1] != '/') {
        if (baseLen + 1 < size) {
            result[baseLen] = '\\';
            baseLen++;
        }
    }
    if (baseLen + childLen + 1 <= size)
        memcpy(result + baseLen, child, childLen + 1);
}

/* Check if file is binary by extension */
static inline int isBinaryFile(const char *filename) {
    static const char *extensions[] = { ".exe", ".dll", ".bin", ".com", ".bat", ".cmd", NULL };
    const char *dot = strrchr(filename, '.');
    if (dot) {
        for (int i = 0; extensions[i] != NULL; i++)
            if (_stricmp(dot, extensions[i]) == 0)
                return 1;
    }
    return 0;
}

/* Format file attributes as a short string */
static inline void formatAttributes(DWORD attr, int isDir, char *outStr, size_t size) {
    if (size < 6) return;
    outStr[0] = isDir ? 'd' : '-';
    outStr[1] = (attr & FILE_ATTRIBUTE_READONLY) ? 'R' : '-';
    outStr[2] = (attr & FILE_ATTRIBUTE_HIDDEN)   ? 'H' : '-';
    outStr[3] = (attr & FILE_ATTRIBUTE_SYSTEM)     ? 'S' : '-';
    outStr[4] = (attr & FILE_ATTRIBUTE_ARCHIVE)    ? 'A' : '-';
    outStr[5] = '\0';
}

/* Convert FILETIME to a string */
void fileTimeToString(const FILETIME *ft, char *buffer, size_t size) {
    SYSTEMTIME stUTC, stLocal;
    FileTimeToSystemTime(ft, &stUTC);
    SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
    if (size >= 20)
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
                stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
}

/* Format file size */
void formatSize(ULONGLONG size, char *buffer, size_t bufferSize, int humanReadable) {
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

/* Natural string comparison */
int naturalCompare(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit(*a) && isdigit(*b)) {
            char *endA, *endB;
            long numA = strtol(a, &endA, 10);
            long numB = strtol(b, &endB, 10);
            if (numA != numB)
                return (numA < numB) ? -1 : 1;
            a = endA; b = endB;
        } else {
            char ca = tolower(*a), cb = tolower(*b);
            if (ca != cb)
                return (ca < cb) ? -1 : 1;
            a++; b++;
        }
    }
    return (*a == *b) ? 0 : ((*a) ? 1 : -1);
}

/* Compare two file entries */
int compareEntries(const void *a, const void *b) {
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
            if (cmp != 0)
                return g_options.reverseSort ? -cmp : cmp;
        }
    }
    int cmp = (g_options.naturalSort) ?
              naturalCompare(fa->findData.cFileName, fb->findData.cFileName) :
              _stricmp(fa->findData.cFileName, fb->findData.cFileName);
    return g_options.reverseSort ? -cmp : cmp;
}

/* Clear the rest of the current console line */
void ClearLineToEnd(HANDLE hConsole, WORD attr) {
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

/* Print one file entry */
void printFileEntry(const char *directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr) {
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
            if (getFileOwner(fullPath, owner, sizeof(owner)))
                printf("%-20s ", owner);
            else
                printf("%-20s ", "Unknown");
        } else {
            if (g_options.showCreationTime)
                printf("%-6s %12s %20s %20s ", attrStr, sizeStr, createTimeStr, modTimeStr);
            else
                printf("%-6s %12s %20s ", attrStr, sizeStr, modTimeStr);
        }
    }
    WORD fileColor = getColorForFile(data, defaultAttr);
    SetConsoleTextAttribute(hConsole, (fileColor & 0x0F) | rowBG);
    printf("%s", data->cFileName);
    if (g_options.fileTypeIndicator) {
        if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            printf("/");
        else if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            printf("@");
        else if (isBinaryFile(data->cFileName))
            printf("*");
    }
    if (g_options.showFullPath) {
        char fullPath[MAX_PATH];
        joinPath(directory, data->cFileName, fullPath, MAX_PATH);
        printf(" (%s)", fullPath);
    }
    ClearLineToEnd(hConsole, defaultAttr);
    printf("\n");
    SetConsoleTextAttribute(hConsole, defaultAttr);
}

/* Return color based on file type */
WORD getColorForFile(const WIN32_FIND_DATAA *data, WORD defaultAttr) {
    if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return SYMLINK_COLOR;
    if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return FOLDER_COLOR;
    if (isBinaryFile(data->cFileName))
        return BINARY_COLOR;
    return defaultAttr;
}

/* Read directory contents into file list */
void readDirectory(const char *path, FileList *list) {
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*", path);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileExA(searchPath, FindExInfoStandard, &findData,
                                    FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!strcmp(findData.cFileName, ".") || !strcmp(findData.cFileName, ".."))
            continue;
        if (!g_options.showAll && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
            continue;
        if (strlen(g_options.filterPattern) && !strstr(findData.cFileName, g_options.filterPattern))
            continue;
        addFileEntry(list, &findData);
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

/* List directory contents */
void listDirectory(const char *path, HANDLE hConsole, WORD defaultAttr) {
    FileList fileList;
    initFileList(&fileList);
    readDirectory(path, &fileList);
    if (fileList.count > 0)
        qsort(fileList.entries, fileList.count, sizeof(FileEntry), compareEntries);

    printf("\n[%s]:\n", path);
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
    for (size_t i = 0; i < fileList.count; i++)
        printFileEntry(path, (int)(i + 1), &fileList.entries[i], hConsole, defaultAttr);
    if (g_options.showSummary)
        printSummary(&fileList);
    if (g_options.recursive && !g_options.treeView) {
        for (size_t i = 0; i < fileList.count; i++) {
            const WIN32_FIND_DATAA *data = &fileList.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                char newPath[MAX_PATH];
                joinPath(path, data->cFileName, newPath, MAX_PATH);
                listDirectory(newPath, hConsole, defaultAttr);
            }
        }
    }
    freeFileList(&fileList);
}

void listDirectorySelf(const char *path, HANDLE hConsole, WORD defaultAttr) {
    WIN32_FIND_DATAA data;
    HANDLE hFind = FindFirstFileA(path, &data);
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error retrieving info for: %s (Error code: %lu)\n", path, GetLastError());
        return;
    }
    FindClose(hFind);
    printf("\n[%s]:\n", path);
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
    FileEntry entry; 
    entry.findData = data;
    printFileEntry(path, 1, &entry, hConsole, defaultAttr);
}

void treeDirectory(const char *path, HANDLE hConsole, WORD defaultAttr, int indent) {
    FileList fileList;
    initFileList(&fileList);
    readDirectory(path, &fileList);
    if (fileList.count > 0)
        qsort(fileList.entries, fileList.count, sizeof(FileEntry), compareEntries);
    char indentBuf[64] = "";
    for (int i = 0; i < indent && i < 31; i++)
        strcat(indentBuf, "  ");
    for (size_t i = 0; i < fileList.count; i++) {
        const WIN32_FIND_DATAA *data = &fileList.entries[i].findData;
        if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            printf("%s|- [D] %s\n", indentBuf, data->cFileName);
        else
            printf("%s|- [F] %s\n", indentBuf, data->cFileName);
    }
    if (g_options.recursive) {
        for (size_t i = 0; i < fileList.count; i++) {
            const WIN32_FIND_DATAA *data = &fileList.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                char newPath[MAX_PATH];
                joinPath(path, data->cFileName, newPath, MAX_PATH);
                printf("%s|\n", indentBuf);
                treeDirectory(newPath, hConsole, defaultAttr, indent + 1);
            }
        }
    }
    freeFileList(&fileList);
}

/* Print summary if enabled */
void printSummary(FileList *list) {
    int fileCount = 0, dirCount = 0;
    ULONGLONG totalSize = 0;
    for (size_t i = 0; i < list->count; i++) {
        const WIN32_FIND_DATAA *data = &list->entries[i].findData;
        if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            dirCount++;
        else {
            fileCount++;
            LARGE_INTEGER filesize;
            filesize.LowPart = data->nFileSizeLow;
            filesize.HighPart = data->nFileSizeHigh;
            totalSize += filesize.QuadPart;
        }
    }
    char sizeStr[32];
    formatSize(totalSize, sizeStr, sizeof(sizeStr), g_options.humanSize);
    printf("\nSummary: %d directories, %d files, total size: %s\n", dirCount, fileCount, sizeStr);
}

/* Preview first 10 lines of a text file */
void previewFile(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "Error opening file for preview: %s\n", filepath);
        return;
    }
    printf("\n--- Preview of %s ---\n", filepath);
    char line[256];
    int i;
    for (i = 0; i < 10 && fgets(line, sizeof line, fp); i++)
        fputs(line, stdout);
    if (!i)
        puts("[File is empty or unreadable]");
    puts("--- End of preview ---\n");
    fclose(fp);
}

/* Retrieve file owner (if accessible) */
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize) {
    DWORD dwSize = 0;
    GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, NULL, 0, &dwSize);
    if (dwSize == 0)
        return 0;
    PSECURITY_DESCRIPTOR psd = malloc(dwSize);
    if (!psd)
        return 0;
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
    char name[256] = "", domain[256] = "";
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

/* Clear the console screen using WinAPI */
void clearScreen(HANDLE hConsole, WORD defaultAttr) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return;
    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y, count;
    COORD homeCoords = {0, 0};
    FillConsoleOutputCharacterA(hConsole, ' ', cellCount, homeCoords, &count);
    FillConsoleOutputAttribute(hConsole, defaultAttr, cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hConsole, homeCoords);
}

void interactiveMode(char *currentPath, HANDLE hConsole, WORD defaultAttr) {
    char input[512];

    while (1) {
        clearScreen(hConsole, defaultAttr);
        if (g_options.treeView)
            treeDirectory(currentPath, hConsole, defaultAttr, 0);
        else if (g_options.listDirs)
            listDirectorySelf(currentPath, hConsole, defaultAttr);
        else
            listDirectory(currentPath, hConsole, defaultAttr);

        printf("\nlk> ");
        if (!fgets(input, sizeof(input), stdin))
            break;
        input[strcspn(input, "\n")] = '\0';
        if (!*input)
            continue;
        if (!strcmp(input, "q") || !strcmp(input, ":q"))
            break;

        if (!strcmp(input, "help")) {
            printf("Interactive commands:\n"
                   "  cd <path>         Change directory\n"
                   "  preview <file>    Preview first 10 lines of a file\n"
                   "  filter <pattern>  Set file name filter (or 'filter' to clear)\n"
                   "  summary           Show summary\n"
                   "  open <file>       Open file with associated application\n"
                   "  rename <old> <new>  Rename a file/directory\n"
                   "  delete <file>     Delete a file or directory\n"
                   "  mkdir <dir>       Create a directory\n"
                   "  touch <file>      Create an empty file\n"
                   "  copy <src> <dst>  Copy file\n"
                   "  move <src> <dst>  Move (rename) file\n"
                   "  exec <command>    Execute system command\n"
                   "  info <file>       Display detailed file info\n"
                   "  :<flags>         Toggle options (flags)\n"
                   "  q or :q           Quit interactive mode\n");
            WAIT_ENTER();
            continue;
        }

        if (input[0] == ':') {
            for (char *p = input + 1; *p; p++) {
                switch (*p) {
                    case 'a': g_options.showAll = !g_options.showAll; break;
                    case 'l': g_options.longFormat = !g_options.longFormat; break;
                    case 'R': g_options.recursive = !g_options.recursive; break;
                    case 'S': g_options.sortBySize = !g_options.sortBySize; break;
                    case 't': g_options.sortByTime = !g_options.sortByTime; break;
                    case 'X': g_options.sortByExtension = !g_options.sortByExtension; break;
                    case 'r': g_options.reverseSort = !g_options.reverseSort; break;
                    case 'H': case 'h': g_options.humanSize = !g_options.humanSize; break;
                    case 'F': g_options.fileTypeIndicator = !g_options.fileTypeIndicator; break;
                    case 'd': g_options.listDirs = !g_options.listDirs; break;
                    case 'G': g_options.groupDirs = !g_options.groupDirs; break;
                    case 'E': g_options.showCreationTime = !g_options.showCreationTime; break;
                    case 'T': g_options.treeView = !g_options.treeView; break;
                    case 'N': g_options.naturalSort = !g_options.naturalSort; break;
                    case 'P': g_options.showFullPath = !g_options.showFullPath; break;
                    case 'O': g_options.showOwner = !g_options.showOwner; break;
                    case 'M': case 'm': g_options.showSummary = !g_options.showSummary; break;
                    default: printf("Unknown flag: %c\n", *p); break;
                }
            }
            continue;
        }
        if (!strncmp(input, "cd ", 3)) {
            char *newPath = input + 3;
            if (SetCurrentDirectoryA(newPath)) {
                if (!GetCurrentDirectoryA(MAX_PATH, currentPath))
                    fprintf(stderr, "Error retrieving new directory.\n");
            } else {
                printf("Failed to change directory to: %s\n", newPath);
                Sleep(800);
            }
            continue;
        }
        if (!strncmp(input, "preview ", 8)) {
            previewFile(input + 8);
            WAIT_ENTER();
            continue;
        }
        if (!strncmp(input, "filter ", 7)) {
            strncpy(g_options.filterPattern, input + 7, sizeof(g_options.filterPattern) - 1);
            g_options.filterPattern[sizeof(g_options.filterPattern) - 1] = '\0';
            printf("Filter set to: %s\n", g_options.filterPattern);
            Sleep(800);
            continue;
        }
        if (!strcmp(input, "filter")) {
            g_options.filterPattern[0] = '\0';
            printf("Filter cleared.\n");
            Sleep(800);
            continue;
        }
        if (!strcmp(input, "summary")) {
            FileList fileList;
            initFileList(&fileList);
            char searchPath[MAX_PATH];
            sprintf(searchPath, "%s\\*", currentPath);
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileExA(searchPath, FindExInfoStandard, &findData,
                                            FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!strcmp(findData.cFileName, ".") || !strcmp(findData.cFileName, ".."))
                        continue;
                    if (!g_options.showAll && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
                        continue;
                    addFileEntry(&fileList, &findData);
                } while (FindNextFileA(hFind, &findData));
                FindClose(hFind);
            }
            printSummary(&fileList);
            freeFileList(&fileList);
            WAIT_ENTER();
            continue;
        }
        if (!strncmp(input, "open ", 5)) {
            char fullPath[MAX_PATH];
            joinPath(currentPath, input + 5, fullPath, MAX_PATH);
            ShellExecuteA(NULL, "open", fullPath, NULL, NULL, SW_SHOWNORMAL);
            continue;
        }
        if (!strncmp(input, "rename ", 7)) {
            char *args = input + 7;
            char *oldName = strtok(args, " ");
            char *newName = strtok(NULL, " ");
            if (!oldName || !newName) {
                printf("Usage: rename <oldname> <newname>\n");
                Sleep(800);
                continue;
            }
            char oldPath[MAX_PATH], newPath[MAX_PATH];
            joinPath(currentPath, oldName, oldPath, MAX_PATH);
            joinPath(currentPath, newName, newPath, MAX_PATH);
            if (MoveFileA(oldPath, newPath))
                printf("Renamed successfully.\n");
            else
                printf("Failed to rename (Error code: %lu).\n", GetLastError());
            Sleep(800);
            continue;
        }
        if (!strncmp(input, "delete ", 7)) {
            char fullPath[MAX_PATH];
            joinPath(currentPath, input + 7, fullPath, MAX_PATH);
            DWORD attr = GetFileAttributesA(fullPath);
            if (attr == INVALID_FILE_ATTRIBUTES)
                printf("File not found: %s\n", fullPath);
            else {
                BOOL success = (attr & FILE_ATTRIBUTE_DIRECTORY) ?
                               RemoveDirectoryA(fullPath) : DeleteFileA(fullPath);
                printf(success ? "Deleted successfully.\n" :
                                 "Failed to delete (Error code: %lu).\n", GetLastError());
            }
            Sleep(800);
            continue;
        }
        if (!strncmp(input, "mkdir ", 6)) {
            char fullPath[MAX_PATH];
            joinPath(currentPath, input + 6, fullPath, MAX_PATH);
            if (CreateDirectoryA(fullPath, NULL))
                printf("Directory created successfully.\n");
            else
                printf("Failed to create directory (Error code: %lu).\n", GetLastError());
            Sleep(800);
            continue;
        }
        if (!strncmp(input, "touch ", 6)) {
            char fullPath[MAX_PATH];
            joinPath(currentPath, input + 6, fullPath, MAX_PATH);
            FILE *fp = fopen(fullPath, "a");
            if (fp) { fclose(fp); printf("File created successfully.\n"); }
            else printf("Failed to create file: %s\n", fullPath);
            Sleep(800);
            continue;
        }
        if (!strncmp(input, "copy ", 5)) {
            char *args = input + 5;
            char *src = strtok(args, " ");
            char *dst = strtok(NULL, " ");
            if (!src || !dst) {
                printf("Usage: copy <source> <destination>\n");
                Sleep(800);
                continue;
            }
            char srcPath[MAX_PATH], dstPath[MAX_PATH];
            joinPath(currentPath, src, srcPath, MAX_PATH);
            joinPath(currentPath, dst, dstPath, MAX_PATH);
            if (CopyFileA(srcPath, dstPath, FALSE))
                printf("Copied successfully.\n");
            else
                printf("Failed to copy (Error code: %lu).\n", GetLastError());
            Sleep(800);
            continue;
        }
        if (!strncmp(input, "move ", 5)) {
            char *args = input + 5;
            char *src = strtok(args, " ");
            char *dst = strtok(NULL, " ");
            if (!src || !dst) {
                printf("Usage: move <source> <destination>\n");
                Sleep(800);
                continue;
            }
            char srcPath[MAX_PATH], dstPath[MAX_PATH];
            joinPath(currentPath, src, srcPath, MAX_PATH);
            joinPath(currentPath, dst, dstPath, MAX_PATH);
            if (MoveFileA(srcPath, dstPath))
                printf("Moved successfully.\n");
            else
                printf("Failed to move (Error code: %lu).\n", GetLastError());
            Sleep(800);
            continue;
        }
        if (!strncmp(input, "exec ", 5)) {
            system(input + 5);
            WAIT_ENTER();
            continue;
        }
        if (!strncmp(input, "info ", 5)) {
            char fullPath[MAX_PATH];
            joinPath(currentPath, input + 5, fullPath, MAX_PATH);
            WIN32_FIND_DATAA data;
            HANDLE hFind = FindFirstFileA(fullPath, &data);
            if (hFind == INVALID_HANDLE_VALUE)
                printf("File not found: %s\n", fullPath);
            else {
                FindClose(hFind);
                printf("\nFile: %s\nAttributes: 0x%08X\n", fullPath, data.dwFileAttributes);
                char modTime[32], createTime[32];
                fileTimeToString(&data.ftLastWriteTime, modTime, sizeof(modTime));
                fileTimeToString(&data.ftCreationTime, createTime, sizeof(createTime));
                printf("Modified: %s\nCreated:  %s\n", modTime, createTime);
                LARGE_INTEGER filesize = { .LowPart = data.nFileSizeLow, .HighPart = data.nFileSizeHigh };
                char sizeStr[32];
                formatSize(filesize.QuadPart, sizeStr, sizeof(sizeStr), g_options.humanSize);
                printf("Size: %s\n", sizeStr);
                if (g_options.showOwner) {
                    char owner[256] = "";
                    printf(getFileOwner(fullPath, owner, sizeof(owner)) ? "Owner: %s\n" : "Owner: Unknown\n", owner);
                }
            }
            WAIT_ENTER();
            continue;
        }
        printf("Unknown command. Type 'help' for instructions.\n");
        Sleep(800);
    }
}

/* Main function */
int main(int argc, char *argv[]) {
    int fileCount = 0, filesCapacity = 16;
    char **files = malloc(filesCapacity * sizeof(char *));
    if (!files) {
        fprintf(stderr, "Memory allocation error.\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
                printUsage(argv[0]);
                free(files);
                return 0;
            }
            if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
                printf("ik version 1.2\n");
                free(files);
                return 0;
            }
            size_t len = strlen(argv[i]);
            for (size_t j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'a': g_options.showAll = 1; break;
                    case 'l': g_options.longFormat = 1; break;
                    case 'R': g_options.recursive = 1; break;
                    case 'S': g_options.sortBySize = 1; break;
                    case 't': g_options.sortByTime = 1; break;
                    case 'X': g_options.sortByExtension = 1; break;
                    case 'r': g_options.reverseSort = 1; break;
                    case 'H': g_options.humanSize = 1; break;
                    case 'F': g_options.fileTypeIndicator = 1; break;
                    case 'd': g_options.listDirs = 1; break;
                    case 'G': g_options.groupDirs = 1; break;
                    case 'E': g_options.showCreationTime = 1; break;
                    case 'T': g_options.treeView = 1; break;
                    case 'N': g_options.naturalSort = 1; break;
                    case 'P': g_options.showFullPath = 1; break;
                    case 'O': g_options.showOwner = 1; break;
                    case 'M': g_options.showSummary = 1; break;
                    case 'I': g_interactive = 1; break;
                    default:
                        fprintf(stderr, "Unknown option: -%c\n", argv[i][j]);
                        printUsage(argv[0]);
                        free(files);
                        return 1;
                }
            }
        } else {
            if (fileCount >= filesCapacity) {
                filesCapacity *= 2;
                char **temp = realloc(files, filesCapacity * sizeof(char *));
                if (!temp) {
                    fprintf(stderr, "Memory reallocation error.\n");
                    free(files);
                    return 1;
                }
                files = temp;
            }
            files[fileCount++] = argv[i];
        }
    }
    if (fileCount == 0) { files[0] = "."; fileCount = 1; }
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        csbi.wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    WORD defaultAttr = csbi.wAttributes;
    if (g_interactive) {
        char currentPath[MAX_PATH];
        if (!GetCurrentDirectoryA(MAX_PATH, currentPath)) {
            fprintf(stderr, "Error retrieving current directory.\n");
            free(files);
            return 1;
        }
        if (fileCount >= 1 && SetCurrentDirectoryA(files[0]))
            GetCurrentDirectoryA(MAX_PATH, currentPath);
        interactiveMode(currentPath, hConsole, defaultAttr);
    } else {
        for (int i = 0; i < fileCount; i++) {
            if (fileCount > 1)
                printf("==> %s <==\n", files[i]);
            if (g_options.listDirs)
                listDirectorySelf(files[i], hConsole, defaultAttr);
            else if (g_options.treeView)
                treeDirectory(files[i], hConsole, defaultAttr, 0);
            else
                listDirectory(files[i], hConsole, defaultAttr);
            if (i < fileCount - 1)
                printf("\n");
        }
    }
    free(files);
    return 0;
}