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

/* Corrected addFileEntry: Added overflow check before doubling capacity */
static void addFileEntry(FileList *list, const WIN32_FIND_DATAA *data) {
    if (UNLIKELY(list->count >= list->capacity)) {
        /* Prevent integer overflow when doubling capacity */
        if (list->capacity > SIZE_MAX / 2)
            fatalError("Maximum FileList capacity reached; potential integer overflow.");
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

/*
 * joinPath: Safely concatenates the base and child paths into the result buffer.
 * This revised version captures the return value of snprintf to ensure that the resulting
 * string fully fits within the provided buffer. If truncation is detected, a fatal error
 * is raised to prevent subtle path issues that could compromise system integrity.
 */
static inline void joinPath(const char *restrict base, const char *restrict child, char *restrict result, size_t size) {
    if (UNLIKELY(!base || !child || !result || size == 0))
        fatalError("Invalid arguments to joinPath.");
    size_t baseLen = strlen(base);
    int needsSlash = (baseLen && (base[baseLen - 1] != '\\' && base[baseLen - 1] != '/'));
    int written;
    if (needsSlash)
        written = snprintf(result, size, "%s\\%s", base, child);
    else
        written = snprintf(result, size, "%s%s", base, child);
    if (written < 0 || (size_t)written >= size)
        fatalError("joinPath: Resulting path was truncated.");
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

/* Corrected formatAttributes: Now verifies buffer size and aborts if insufficient */
static inline void formatAttributes(DWORD attr, int isDir, char *restrict outStr, size_t size) {
    /* Enforce a minimum buffer size of 6 to safely store attributes */
    if (size < 6)
        fatalError("Buffer size too small in formatAttributes; expected at least 6 characters.");

    static const char flags[5] = { 'd', 'R', 'H', 'S', 'A' };
    static const DWORD masks[5] = { 
        0,  /* Directory is handled separately */
        FILE_ATTRIBUTE_READONLY,
        FILE_ATTRIBUTE_HIDDEN,
        FILE_ATTRIBUTE_SYSTEM,
        FILE_ATTRIBUTE_ARCHIVE
    };

    outStr[0] = isDir ? flags[0] : '-';
    outStr[1] = (attr & masks[1]) ? flags[1] : '-';
    outStr[2] = (attr & masks[2]) ? flags[2] : '-';
    outStr[3] = (attr & masks[3]) ? flags[3] : '-';
    outStr[4] = (attr & masks[4]) ? flags[4] : '-';
    outStr[5] = '\0';
}

/*
 * fileTimeToString: Converts a FILETIME structure to a human-readable string in "YYYY-MM-DD HH:MM:SS" format.
 * This updated version first verifies that the provided buffer is large enough (at least 20 characters)
 * to hold the formatted string. It then checks the snprintf return value to ensure no truncation occurs,
 * thereby safeguarding against potential data corruption under edge-case conditions.
 */
static void fileTimeToString(const FILETIME *ft, char *restrict buffer, size_t size) {
    if (size < 20)
        fatalError("Buffer size too small in fileTimeToString; expected at least 20 characters.");
    SYSTEMTIME stUTC, stLocal;
    if (!FileTimeToSystemTime(ft, &stUTC))
        fatalError("FileTimeToSystemTime failed.");
    if (!SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal))
        fatalError("SystemTimeToTzSpecificLocalTime failed.");
    int written = snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
                 stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                 stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
    if (written < 0 || (size_t)written >= size)
        fatalError("fileTimeToString: Resulting string was truncated.");
}

/* Format file size; scale to human-readable units if requested */
static void formatSize(ULONGLONG size, char *restrict buffer, size_t bufferSize, int humanReadable) {
    if (!humanReadable) {
        snprintf(buffer, bufferSize, "%llu", size);
        return;
    }
    
    /* Use a compile-time array of suffix strings */
    static const char *suffixes[6] = { "B", "K", "M", "G", "T", "P" };
    
    /* Use integer scaling for values under threshold to avoid floating point */
    if (size < 1024) {
        snprintf(buffer, bufferSize, "%llu%s", size, suffixes[0]);
        return;
    }
    
    /* Find appropriate unit with binary division */
    int i = 0;
    double s = (double)size;
    
    /* Unroll this loop for common cases */
    if (s >= 1024) { s /= 1024; i++; }  /* KB */
    if (s >= 1024) { s /= 1024; i++; }  /* MB */
    if (s >= 1024) { s /= 1024; i++; }  /* GB */
    if (s >= 1024 && i < 4) { s /= 1024; i++; }  /* TB */
    if (s >= 1024 && i < 5) { s /= 1024; i++; }  /* PB */
    
    /* Format with one decimal place */
    snprintf(buffer, bufferSize, "%.1f%s", s, suffixes[i]);
}

/* Natural (human-friendly) string comparison */
static int naturalCompare(const char *restrict a, const char *restrict b) {
    const unsigned char *ua = (const unsigned char *)a;
    const unsigned char *ub = (const unsigned char *)b;
    
    while (*ua && *ub) {
        if (isdigit(*ua) && isdigit(*ub)) {
            /* Fast path for common case of single-digit numbers */
            if (!isdigit(ua[1]) && !isdigit(ub[1])) {
                if (*ua != *ub)
                    return *ua - *ub;
                ua++; ub++;
                continue;
            }
            
            /* Parse multiple-digit numbers efficiently */
            unsigned long numA = 0, numB = 0;
            do { numA = numA * 10 + (*ua++ - '0'); } while (isdigit(*ua));
            do { numB = numB * 10 + (*ub++ - '0'); } while (isdigit(*ub));
            
            if (numA != numB)
                return (numA < numB) ? -1 : 1;
        } else {
            /* Use direct lowercase table lookup instead of function call */
            unsigned char ca = (*ua >= 'A' && *ua <= 'Z') ? (*ua | 0x20) : *ua;
            unsigned char cb = (*ub >= 'A' && *ub <= 'Z') ? (*ub | 0x20) : *ub;
            
            if (ca != cb)
                return ca - cb;
            ua++; ub++;
        }
    }
    
    return (*ua) ? 1 : ((*ub) ? -1 : 0);
}

/* Compare two FileEntry items with support for various sort options */
static int compareEntries(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    const DWORD attrA = fa->findData.dwFileAttributes;
    const DWORD attrB = fb->findData.dwFileAttributes;
    const int aIsDir = (attrA & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const int bIsDir = (attrB & FILE_ATTRIBUTE_DIRECTORY) != 0;

    /* Fast path for directory grouping */
    if (g_options.groupDirs && aIsDir != bIsDir)
        return aIsDir ? -1 : 1;

    /* Time-based sorting */
    if (g_options.sortByTime) {
        /* Direct 64-bit composition instead of bit shifting */
        ULONGLONG ta = (((ULONGLONG)fa->findData.ftLastWriteTime.dwHighDateTime) << 32) | 
                         fa->findData.ftLastWriteTime.dwLowDateTime;
        ULONGLONG tb = (((ULONGLONG)fb->findData.ftLastWriteTime.dwHighDateTime) << 32) | 
                         fb->findData.ftLastWriteTime.dwLowDateTime;
        
        if (ta != tb) {
            /* Combine conditional with return to eliminate branch */
            int result = (ta < tb) ? -1 : 1;
            return g_options.reverseSort ? -result : result;
        }
    }
    
    /* Size-based sorting */
    if (g_options.sortBySize) {
        ULONGLONG sa = (((ULONGLONG)fa->findData.nFileSizeHigh) << 32) | fa->findData.nFileSizeLow;
        ULONGLONG sb = (((ULONGLONG)fb->findData.nFileSizeHigh) << 32) | fb->findData.nFileSizeLow;
        
        if (sa != sb) {
            int result = (sa < sb) ? -1 : 1;
            return g_options.reverseSort ? -result : result;
        }
    }
    
    /* Extension-based sorting - only do string operations if needed */
    if (g_options.sortByExtension) {
        const char *extA = strrchr(fa->findData.cFileName, '.');
        const char *extB = strrchr(fb->findData.cFileName, '.');
        
        /* Handle various extension cases efficiently */
        if (!extA && !extB) {
            /* No extensions, fall through to name comparison */
        } else if (extA && !extB) {
            return g_options.reverseSort ? -1 : 1;
        } else if (!extA && extB) {
            return g_options.reverseSort ? 1 : -1;
        } else {
            int result = _stricmp(extA, extB);
            if (result) {
                return g_options.reverseSort ? -result : result;
            }
        }
    }
    
    /* Name-based sorting as fallback */
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
    /* Fast path for exact matching or empty pattern */
    if (!pattern[0]) return !str[0];
    if (!str[0]) return pattern[0] == '*' && !pattern[1];
    
    /* Use more efficient pointer tracking for pattern matching */
    const char *s = str;
    const char *p = pattern;
    const char *star_p = NULL;
    const char *star_s = NULL;
    
    while (*s) {
        /* Direct char match or single wildcard */
        if (*p == '?' || ((*p | 0x20) == (*s | 0x20))) {
            p++;
            s++;
        }
        /* Star wildcard handling with backtracking information */
        else if (*p == '*') {
            star_p = p++;
            star_s = s;
            /* Skip consecutive stars - they're redundant */
            while (*p == '*') p++;
            if (!*p) return 1; /* Trailing star matches everything */
        }
        /* Backtrack to last star if available */
        else if (star_p) {
            p = star_p + 1;
            s = ++star_s;
        }
        else {
            return 0;
        }
    }
    
    /* Skip any trailing stars */
    while (*p == '*') p++;
    
    return !*p;
}

/* Corrected readDirectory: Uses safe string copies and bounds checks for wildcard and directory names */
static void readDirectory(const char *restrict path, FileList *list) {
    char directory[MAX_PATH] = {0};
    char wildcard[256] = {0};
    int hasWildcard = (strchr(path, '*') || strchr(path, '?'));

    if (hasWildcard) {
        const char *sep = strrchr(path, '\\');
        if (!sep) sep = strrchr(path, '/');
        if (sep) {
            size_t dirLen = sep - path;
            if (dirLen >= MAX_PATH) {
                dirLen = MAX_PATH - 1; /* Truncate to avoid overflow */
                fprintf(stderr, "Warning: Directory path truncated to %d characters: '%s'\n", MAX_PATH - 1, path);
            }
            memcpy(directory, path, dirLen);
            directory[dirLen] = '\0';
            /* Safely copy the wildcard pattern */
            strncpy(wildcard, sep + 1, sizeof(wildcard) - 1);
            wildcard[sizeof(wildcard) - 1] = '\0';
        } else {
            strncpy(directory, ".", MAX_PATH - 1);
            directory[MAX_PATH - 1] = '\0';
            strncpy(wildcard, path, sizeof(wildcard) - 1);
            wildcard[sizeof(wildcard) - 1] = '\0';
        }
    } else {
        size_t pathLen = strlen(path);
        if (pathLen >= MAX_PATH) {
            pathLen = MAX_PATH - 1; /* Truncate to avoid overflow */
            fprintf(stderr, "Warning: Path truncated to %d characters: '%s'\n", MAX_PATH - 1, path);
        }
        strncpy(directory, path, pathLen);
        directory[pathLen] = '\0';
        /* Safely apply the global filter pattern if specified */
        if (g_options.filterPattern[0]) {
            strncpy(wildcard, g_options.filterPattern, sizeof(wildcard) - 1);
            wildcard[sizeof(wildcard) - 1] = '\0';
        }
    }

    char searchPath[MAX_PATH];
    size_t dirLen = strlen(directory);
    if (dirLen > 0 && directory[dirLen - 1] != '\\')
        snprintf(searchPath, sizeof(searchPath), "%s\\*", directory);
    else
        snprintf(searchPath, sizeof(searchPath), "%s*", directory);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileExA(
        searchPath,
        FindExInfoBasic,         /* Use basic info for performance */
        &findData,
        FindExSearchNameMatch,
        NULL,
        FIND_FIRST_EX_LARGE_FETCH /* Optimize for large directories */
    );
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Unable to open directory '%s' (Error code: %lu)\n", directory, GetLastError());
        return;
    }

    const size_t wildcardLen = strlen(wildcard);
    do {
        /* Skip current and parent directory entries */
        if (findData.cFileName[0] == '.' &&
            (findData.cFileName[1] == '\0' ||
             (findData.cFileName[1] == '.' && findData.cFileName[2] == '\0')))
            continue;

        /* Filter out hidden files unless showAll is enabled */
        if (!g_options.showAll && (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
            continue;

        /* Apply wildcard filter if present */
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

/* Corrected listDirectory: Skips recursing into reparse points to prevent cyclic loops */
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
            /* Only recurse into directories that are not reparse points to avoid cycles */
            if ((data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
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

/* Corrected treeDirectory: Adds recursion depth limit and skips reparse points to ensure system resilience */
static void treeDirectory(const char *restrict path, HANDLE hConsole, WORD defaultAttr, int indent) {
    /* Limit recursion depth to avoid potential stack overflow */
    if (indent >= 31)
        return;

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
            /* Prevent recursion into reparse points to avoid cyclic directory traversal */
            if ((data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
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

/* Main entry point for the directory listing utility */
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

    /* Parse command-line arguments to set options and collect paths */
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
                /* Check for potential overflow before doubling capacity */
                if (filesCapacity > SIZE_MAX / 2) {
                    fprintf(stderr, "Error: Maximum file capacity reached.\n");
                    free(files);
                    return EXIT_FAILURE;
                }
                filesCapacity *= 2;
                char **temp = (char **)realloc(files, filesCapacity * sizeof(*files));
                if (!temp) {
                    fprintf(stderr, "Error: Memory reallocation failed.\n");
                    free(files);
                    return EXIT_FAILURE;
                }
                files = temp;
            }
            files[fileCount++] = argv[i];
        }
    }

    /* Default to current directory if no paths specified */
    if (fileCount == 0) {
        files[0] = ".";
        fileCount = 1;
    }

    /* Allocate block for absolute paths to improve memory locality */
    char *absPathsBlock = (char *)malloc(fileCount * MAX_PATH);
    if (!absPathsBlock) {
        free(files);
        fatalError("Memory allocation failed for absolute paths block.");
    }

    /* Convert all paths to absolute paths */
    for (int i = 0; i < fileCount; i++) {
        char *absPath = absPathsBlock + i * MAX_PATH;
        if (!GetFullPathNameA(files[i], MAX_PATH, absPath, NULL)) {
            strncpy(absPath, files[i], MAX_PATH - 1);
            absPath[MAX_PATH - 1] = '\0';
        }
    }
    free(files);

    /* Initialize console handle and default attributes */
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    WORD defaultAttr;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        defaultAttr = GRAY_TEXT; /* Fallback to gray if console info unavailable */
        fprintf(stderr, "Warning: Failed to get console buffer info (Error code: %lu)\n", GetLastError());
    } else {
        defaultAttr = csbi.wAttributes;
    }

    /* Process each path according to options */
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
