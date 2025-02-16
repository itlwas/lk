#include <windows.h>
#include <shellapi.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Branch prediction macros for performance */
#if defined(__GNUC__)
#define LIKELY(x)   (__builtin_expect(!!(x), 1))
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

/* Console color definitions */
#define DEFAULT_COLOR     (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)   // Standard white
#define BINARY_COLOR      (FOREGROUND_GREEN | FOREGROUND_INTENSITY)                // Executable: bright green
#define FOLDER_COLOR      (FOREGROUND_BLUE | FOREGROUND_INTENSITY)                 // Directory: bright blue
#define SYMLINK_COLOR     (FOREGROUND_RED | FOREGROUND_INTENSITY)                  // Symlink: bright red
#define HIDDEN_COLOR      (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)      // Hidden: gray
#define GRAY_TEXT         (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

/* Detailed output field colors */
#define COLOR_ATTR        (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)   // File attributes: yellow
#define COLOR_SIZE        (FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)    // File size: cyan
#define COLOR_TIME        (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)      // File time: magenta
#define COLOR_OWNER       (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY) // Owner: bright white
#define COLOR_FULLPATH    (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)          // Full path: gray

/* Precomputed indent strings (levels 0 to 31, 2 spaces per level) */
static const char* getIndentString(int indent) {
    static int initialized = 0;
    static char indentCache[32][65]; /* 64 chars max + null */
    if (UNLIKELY(!initialized)) {
        for (int i = 0; i < 32; i++) {
            int len = i * 2;
            if (len > 64) len = 64;
            memset(indentCache[i], ' ', len);
            indentCache[i][len] = '\0';
        }
        initialized = 1;
    }
    if (indent < 0)
        indent = 0;
    else if (indent > 31)
        indent = 31;
    return indentCache[indent];
}

#define INITIAL_CAPACITY 128

/* Options structure for application settings */
typedef struct Options {
    int showAll;           // Show hidden files.
    int longFormat;        // Detailed listing.
    int recursive;         // Recursive directory listing.
    int sortBySize;        // Sort by file size.
    int sortByTime;        // Sort by modification time.
    int sortByExtension;   // Sort by file extension.
    int reverseSort;       // Reverse sort order.
    int humanSize;         // Use human-readable sizes.
    int fileTypeIndicator; // Append file type indicator.
    int listDirs;          // List directory entry itself.
    int groupDirs;         // Group directories first.
    int showCreationTime;  // Display file creation time.
    int treeView;          // Tree view of directory.
    int naturalSort;       // Use natural sorting.
    int showFullPath;      // Show full file path.
    int showOwner;         // Display file owner.
    int showSummary;       // Show summary info.
    char filterPattern[256]; // Filename filter (empty = no filter).
} Options;

/* Global options with default values */
static Options g_options = {
    .showAll = 0, .longFormat = 1, .recursive = 0, .sortBySize = 0,
    .sortByTime = 0, .sortByExtension = 0, .reverseSort = 0, .humanSize = 1,
    .fileTypeIndicator = 1, .listDirs = 0, .groupDirs = 1, .showCreationTime = 0,
    .treeView = 0, .naturalSort = 1, .showFullPath = 0, .showOwner = 0,
    .showSummary = 1, .filterPattern = ""
};

/* Wraps WIN32_FIND_DATAA for file/directory entry */
typedef struct {
    WIN32_FIND_DATAA findData;
} FileEntry;

/* Dynamic array for file entries */
typedef struct {
    FileEntry *entries;
    size_t count;
    size_t capacity;
} FileList;

/* Function prototypes */
static void fatalError(const char *msg);
static void initFileList(FileList *list);
static void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data);
static void freeFileList(FileList *list);
static inline void joinPath(const char *restrict base, const char *restrict child, char *restrict result, size_t size);
static inline int isBinaryFile(const char *restrict filename);
static inline void formatAttributes(DWORD attr, int isDir, char *restrict outStr, size_t size);
static void fileTimeToString(const FILETIME *ft, char *restrict buffer, size_t size);
static void formatSize(ULONGLONG size, char *restrict buffer, size_t bufferSize, int humanReadable);
static int naturalCompare(const char *restrict a, const char *restrict b);
static int compareEntries(const void *a, const void *b);
static void clearLineToEnd(HANDLE hConsole, WORD attr);
static void printFileEntry(const char *restrict directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr);
static void readDirectory(const char *restrict path, FileList *list);
static inline void printHeader(const char *restrict path);
static void listDirectory(const char *restrict path, HANDLE hConsole, WORD defaultAttr);
static void listDirectorySelf(const char *restrict path, HANDLE hConsole, WORD defaultAttr);
static void treeDirectory(const char *restrict path, HANDLE hConsole, WORD defaultAttr, int indent);
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize);

/* Print fatal error message and exit; includes Windows error code if relevant */
static void fatalError(const char *msg) {
    DWORD err = GetLastError();
    fprintf(stderr, "Fatal error: %s (Error code: %lu)\n", msg, err);
    exit(EXIT_FAILURE);
}

/* Initialize FileList with INITIAL_CAPACITY */
static void initFileList(FileList *list) {
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    list->entries = (FileEntry *)malloc(list->capacity * sizeof(FileEntry));
    if (UNLIKELY(!list->entries))
        fatalError("Memory allocation failed for FileList.");
}

/* Add a file entry; reallocates if needed */
static void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data) {
    if (UNLIKELY(list->count >= list->capacity)) {
        list->capacity *= 2;
        FileEntry *temp = (FileEntry *)realloc(list->entries, list->capacity * sizeof(FileEntry));
        if (UNLIKELY(!temp))
            fatalError("Memory reallocation failed for FileList.");
        list->entries = temp;
    }
    list->entries[list->count++].findData = *data;
}

/* Free the FileList memory */
static void freeFileList(FileList *list) {
    free(list->entries);
    list->entries = NULL;
    list->count = list->capacity = 0;
}

/* Fast ASCII lowercase conversion */
static inline int fast_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c | 0x20) : c;
}

/* Join base and child paths; fatal if invalid args */
static inline void joinPath(const char *restrict base, const char *restrict child, char *restrict result, size_t size) {
    if (UNLIKELY(!base || !child || !result || size == 0))
        fatalError("Invalid arguments to joinPath.");
    size_t baseLen = strlen(base);
    int needsSlash = (baseLen && (base[baseLen - 1] != '\\' && base[baseLen - 1] != '/'));
    if (needsSlash)
        snprintf(result, size, "%s\\%s", base, child);
    else
        snprintf(result, size, "%s%s", base, child);
}

/* Check if filename indicates a binary file by extension */
static inline int isBinaryFile(const char *restrict filename) {
    static const char *extensions[] = {
        ".exe", ".dll", ".bin", ".com", ".bat", ".cmd",
        ".msi", ".sys", ".drv", ".cpl", ".ocx", ".scr", ".vxd", NULL
    };
    const char *dot = strrchr(filename, '.');
    if (!dot)
        return 0;
    for (int i = 0; extensions[i]; i++) {
        if (_stricmp(dot, extensions[i]) == 0)
            return 1;
    }
    return 0;
}

/* Format file attributes into a short string (e.g., dRHS+A) */
static inline void formatAttributes(DWORD attr, int isDir, char *restrict outStr, size_t size) {
    if (size < 6) return;
    outStr[0] = isDir ? 'd' : '-';
    outStr[1] = (attr & FILE_ATTRIBUTE_READONLY) ? 'R' : '-';
    outStr[2] = (attr & FILE_ATTRIBUTE_HIDDEN)   ? 'H' : '-';
    outStr[3] = (attr & FILE_ATTRIBUTE_SYSTEM)     ? 'S' : '-';
    outStr[4] = (attr & FILE_ATTRIBUTE_ARCHIVE)    ? 'A' : '-';
    outStr[5] = '\0';
}

/* Convert FILETIME to "YYYY-MM-DD HH:MM:SS" format */
static void fileTimeToString(const FILETIME *ft, char *restrict buffer, size_t size) {
    SYSTEMTIME stUTC, stLocal;
    if (!FileTimeToSystemTime(ft, &stUTC))
        fatalError("FileTimeToSystemTime failed.");
    if (!SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal))
        fatalError("SystemTimeToTzSpecificLocalTime failed.");
    if (size >= 20)
        snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
                 stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                 stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
}

/* Format file size; scale to human-readable units if requested */
static void formatSize(ULONGLONG size, char *restrict buffer, size_t bufferSize, int humanReadable) {
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

/* Natural (human-friendly) string comparison */
static int naturalCompare(const char *restrict a, const char *restrict b) {
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

/* Compare two FileEntry items with support for various sort options */
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

/* Clear from current console cursor to end of line */
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

/* Print a single file entry with formatting and color */
static void printFileEntry(const char *restrict directory, int index, const FileEntry *entry, HANDLE hConsole, WORD defaultAttr) {
    const WIN32_FIND_DATAA *data = &entry->findData;
    const int isDir = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const WORD baseBG = defaultAttr & 0xF0;
    const WORD rowBG = (index & 1) ? (baseBG | BACKGROUND_INTENSITY) : baseBG;
    if (!SetConsoleTextAttribute(hConsole, DEFAULT_COLOR | rowBG))
        fprintf(stderr, "Warning: SetConsoleTextAttribute failed.\n");
    printf("%3d. ", index);

    if (g_options.longFormat) {
        char attrStr[6];
        formatAttributes(data->dwFileAttributes, isDir, attrStr, sizeof(attrStr));
        if (!SetConsoleTextAttribute(hConsole, COLOR_ATTR | rowBG))
            fprintf(stderr, "Warning: SetConsoleTextAttribute failed for attributes.\n");
        printf("%-6s ", attrStr);

        char sizeStr[32];
        if (isDir) {
            strncpy(sizeStr, "<DIR>", sizeof(sizeStr) - 1);
            sizeStr[sizeof(sizeStr) - 1] = '\0';
        } else {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = data->nFileSizeLow;
            fileSize.HighPart = data->nFileSizeHigh;
            formatSize(fileSize.QuadPart, sizeStr, sizeof(sizeStr), g_options.humanSize);
        }
        if (!SetConsoleTextAttribute(hConsole, COLOR_SIZE | rowBG))
            fprintf(stderr, "Warning: SetConsoleTextAttribute failed for size.\n");
        printf("%12s ", sizeStr);

        if (g_options.showCreationTime) {
            char createTimeStr[32];
            fileTimeToString(&data->ftCreationTime, createTimeStr, sizeof(createTimeStr));
            if (!SetConsoleTextAttribute(hConsole, COLOR_TIME | rowBG))
                fprintf(stderr, "Warning: SetConsoleTextAttribute failed for creation time.\n");
            printf("%20s ", createTimeStr);
        }
        char modTimeStr[32];
        fileTimeToString(&data->ftLastWriteTime, modTimeStr, sizeof(modTimeStr));
        if (!SetConsoleTextAttribute(hConsole, COLOR_TIME | rowBG))
            fprintf(stderr, "Warning: SetConsoleTextAttribute failed for modification time.\n");
        printf("%20s ", modTimeStr);

        if (g_options.showOwner) {
            char fullPath[MAX_PATH];
            joinPath(directory, data->cFileName, fullPath, MAX_PATH);
            char owner[256] = "Unknown";
            if (!getFileOwner(fullPath, owner, sizeof(owner)))
                strncpy(owner, "Unknown", sizeof(owner) - 1);
            if (!SetConsoleTextAttribute(hConsole, COLOR_OWNER | rowBG))
                fprintf(stderr, "Warning: SetConsoleTextAttribute failed for owner.\n");
            printf("%-20s ", owner);
        }
    }
    if (g_options.fileTypeIndicator) {
        if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            putchar('/');
        else if (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            putchar('@');
        else
            putchar(' ');
    }
    WORD fileColor = (data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? SYMLINK_COLOR :
                     (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FOLDER_COLOR :
                     (isBinaryFile(data->cFileName) ? BINARY_COLOR : DEFAULT_COLOR);
    if (!SetConsoleTextAttribute(hConsole, (fileColor & 0x0F) | rowBG))
        fprintf(stderr, "Warning: SetConsoleTextAttribute failed for file color.\n");
    printf("%s", data->cFileName);
    if (g_options.showFullPath) {
        char fullPath[MAX_PATH];
        joinPath(directory, data->cFileName, fullPath, MAX_PATH);
        if (!SetConsoleTextAttribute(hConsole, COLOR_FULLPATH | rowBG))
            fprintf(stderr, "Warning: SetConsoleTextAttribute failed for full path.\n");
        printf(" (%s)", fullPath);
    }

    clearLineToEnd(hConsole, defaultAttr);
    putchar('\n');
    SetConsoleTextAttribute(hConsole, defaultAttr);
}

/* Simple wildcard matching with support for '?' and '*' */
static int wildcardMatch(const char *restrict pattern, const char *restrict str) {
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

/* Read directory entries matching the filter pattern into FileList */
static void readDirectory(const char *restrict path, FileList *list) {
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
        fprintf(stderr, "Error: Unable to open directory '%s' (Error code: %lu)\n", directory, GetLastError());
        return;
    }

    const size_t wildcardLen = strlen(wildcard);
    do {
        /* Skip "." and ".." */
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

/* Print header with full (absolute) path */
static inline void printHeader(const char *restrict path) {
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

/* List directory entries with sorting and optional recursion */
static void listDirectory(const char *restrict path, HANDLE hConsole, WORD defaultAttr) {
    FileList list;
    initFileList(&list);
    readDirectory(path, &list);

    if (list.count > 0)
        qsort(list.entries, list.count, sizeof(FileEntry), compareEntries);

    printHeader(path);
    for (size_t i = 0; i < list.count; ++i)
        printFileEntry(path, (int)(i + 1), &list.entries[i], hConsole, defaultAttr);

    if (g_options.showSummary) {
        int dirCount = 0, fileCount = 0;
        ULONGLONG totalSize = 0;
        for (size_t i = 0; i < list.count; ++i) {
            const WIN32_FIND_DATAA *data = &list.entries[i].findData;
            if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                ++dirCount;
            else {
                ++fileCount;
                ULARGE_INTEGER fileSize;
                fileSize.LowPart = data->nFileSizeLow;
                fileSize.HighPart = data->nFileSizeHigh;
                totalSize += fileSize.QuadPart;
            }
        }
        char sizeStr[32] = {0};
        formatSize(totalSize, sizeStr, sizeof(sizeStr), g_options.humanSize);
        printf("\nSummary: %d directories, %d files, total size: %s\n", 
               dirCount, fileCount, sizeStr);
    }
    if (g_options.recursive && !g_options.treeView) {
        for (size_t i = 0; i < list.count; ++i) {
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

/* List a single directory entry (not its contents) */
static void listDirectorySelf(const char *restrict path, HANDLE hConsole, WORD defaultAttr) {
    WIN32_FIND_DATAA data;
    HANDLE hFind = FindFirstFileA(path, &data);
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Unable to retrieve info for '%s' (Error code: %lu)\n", path, GetLastError());
        return;
    }
    FindClose(hFind);
    printHeader(path);
    FileEntry entry;
    entry.findData = data;
    printFileEntry(path, 1, &entry, hConsole, defaultAttr);
}

/* Recursively print directory structure in tree view */
static void treeDirectory(const char *restrict path, HANDLE hConsole, WORD defaultAttr, int indent) {
    FileList list;
    initFileList(&list);
    readDirectory(path, &list);
    
    if (list.count > 1)
        qsort(list.entries, list.count, sizeof(FileEntry), compareEntries);
    
    const char *indentBuf = getIndentString(indent);
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
                char newPath[MAX_PATH];
                joinPath(path, data->cFileName, newPath, MAX_PATH);
                printf("%s|\n", indentBuf);
                treeDirectory(newPath, hConsole, defaultAttr, indent + 1);
            }
        }
    }
    freeFileList(&list);
}

/* Retrieve file owner as "DOMAIN\\Name"; returns 1 on success */
int getFileOwner(const char *filePath, char *owner, DWORD ownerSize) {
    char stackBuffer[1024];
    DWORD dwSize = sizeof(stackBuffer);
    PSECURITY_DESCRIPTOR psd = (PSECURITY_DESCRIPTOR)stackBuffer;
    int allocated = 0;
    if (!GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, psd, dwSize, &dwSize)) {
         if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
              fprintf(stderr, "Error: Unable to retrieve security info for '%s' (Error code: %lu)\n", filePath, GetLastError());
              return 0;
         }
         psd = (PSECURITY_DESCRIPTOR)malloc(dwSize);
         if (!psd) {
              fprintf(stderr, "Error: Memory allocation failed for security descriptor for '%s'.\n", filePath);
              return 0;
         }
         allocated = 1;
         if (!GetFileSecurityA(filePath, OWNER_SECURITY_INFORMATION, psd, dwSize, &dwSize)) {
              fprintf(stderr, "Error: Failed to get security descriptor for '%s' (Error code: %lu)\n", filePath, GetLastError());
              free(psd);
              return 0;
         }
    }
    PSID pSid = NULL;
    BOOL ownerDefaulted = FALSE;
    if (!GetSecurityDescriptorOwner(psd, &pSid, &ownerDefaulted)) {
         fprintf(stderr, "Error: Failed to retrieve owner from security descriptor for '%s'.\n", filePath);
         if (allocated) free(psd);
         return 0;
    }
    char name[256] = {0}, domain[256] = {0};
    DWORD nameSize = sizeof(name), domainSize = sizeof(domain);
    SID_NAME_USE sidType;
    if (!LookupAccountSidA(NULL, pSid, name, &nameSize, domain, &domainSize, &sidType)) {
         fprintf(stderr, "Error: LookupAccountSid failed for '%s' (Error code: %lu)\n", filePath, GetLastError());
         if (allocated) free(psd);
         return 0;
    }
    snprintf(owner, ownerSize, "%s\\%s", domain, name);
    if (allocated) free(psd);
    return 1;
}

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
    char **files = (char **)malloc(filesCapacity * sizeof(*files));
    if (!files)
        fatalError("Memory allocation failed for files array.");
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
                } else if (!strcmp(argv[i], "--version")) {
                    printf("lk version 1.5\n");
                    free(files);
                    return EXIT_SUCCESS;
                } else {
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
                            printf("lk version 1.5\n");
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
                char **temp = (char **)realloc(files, filesCapacity * sizeof(*files));
                if (!temp)
                    fatalError("Memory reallocation failed for files array.");
                files = temp;
            }
            files[fileCount++] = argv[i];
        }
    }

    /* Use current directory if no paths provided */
    if (fileCount == 0) {
        files[0] = ".";
        fileCount = 1;
    }
    char *absPathsBlock = (char *)malloc(fileCount * MAX_PATH);
    if (!absPathsBlock) {
        free(files);
        fatalError("Memory allocation failed for absolute paths block.");
    }

    for (int i = 0; i < fileCount; i++) {
        char *absPath = absPathsBlock + i * MAX_PATH;
        if (!GetFullPathNameA(files[i], MAX_PATH, absPath, NULL)) {
            strncpy(absPath, files[i], MAX_PATH - 1);
            absPath[MAX_PATH - 1] = '\0';
        }
    }
    free(files);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        csbi.wAttributes = GRAY_TEXT;
    const WORD defaultAttr = csbi.wAttributes;

    for (int i = 0; i < fileCount; i++) {
        char *currentPath = absPathsBlock + i * MAX_PATH;
        if (fileCount > 1)
            printf("==> %s <==\n", currentPath);

        if (g_options.listDirs)
            listDirectorySelf(currentPath, hConsole, defaultAttr);
        else if (g_options.treeView)
            treeDirectory(currentPath, hConsole, defaultAttr, 0);
        else
            listDirectory(currentPath, hConsole, defaultAttr);

        if (i < fileCount - 1)
            printf("\n");
    }
    free(absPathsBlock);
    return EXIT_SUCCESS;
}
