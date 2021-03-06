#include <stdio.h>
#include <stdint.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

#ifdef _MSC_VER
#define STATIC_TLS __declspec(thread) static
#elif defined(__GNUC__)
#define STATIC_TLS static __thread
#endif

void usage(const wchar_t*);
DWORD recursive_desparse(const wchar_t *base, int streams);
void desparse_streams(const wchar_t *f);
DWORD desparse(const wchar_t *f);

const wchar_t *w32strerror(DWORD err) {
    STATIC_TLS wchar_t buf[4096];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buf, sizeof(buf), NULL);
    return buf;
}

int wmain(int argc, const wchar_t **argv) {
    int recursive = 0, parse_opts = 1, streams = 0;
    DWORD err;
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc == 1)
    {
        usage(argv[0]);
        exit(0);
    }

    for (int i = 1; i < argc; i++) {
        if (wcsncmp(argv[i], L"--", 3) == 0) {
            parse_opts = 0;
            continue;
        }
        if (parse_opts && *argv[i] == L'-') {
            const wchar_t *optchr = &argv[i][1];
            while (*optchr) {
                switch (*optchr) {
                    case L'h':
                        usage(argv[0]);
                        exit(0);
                        break;
                    case L'r':
                        recursive = 1;
                        break;
                    case L's':
                        streams = 1;
                        break;
                    default:
                        fwprintf(stderr, L"unknown option: -%c\n", *optchr);
                        exit(1);
                        break;
                }
                optchr++;
            }
            continue;
        }
        if (recursive) {
            err = recursive_desparse(argv[i], streams);
            if (err != ERROR_DIRECTORY) continue;
        }
        if (streams)
            desparse_streams(argv[i]);
        else
            desparse(argv[i]);        
    }
    return 0;
}

void usage(const wchar_t *argv0) {
    wprintf(L"usage: %s [-rsh] files ...\n", argv0);
    fputws( L"opts:\n", stdout);
    fputws( L"  -r\trecursively desparse on directories\n", stdout);
    fputws( L"  -s\tdesparse all alternate streams\n", stdout);
    fputws( L"  -h\tthis message\n", stdout);
    fputws( L"  --\tstop option parsing\n", stdout);
}

DWORD recursive_desparse(const wchar_t *base, int streams) {
    HANDLE hSearch;
    WIN32_FIND_DATAW find;
    BOOL ret;
    DWORD err;
    wchar_t *name = malloc(65536);
    swprintf_s(name, 32768, L"%s\\*", base);
    hSearch = FindFirstFileW(name, &find);
    err = GetLastError();
    if (err == ERROR_DIRECTORY) {
        return err;
    }
    if (hSearch == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"FindFirstFile on %s failed: %s\n", base, w32strerror(err));
        return err;
    }
    do {
        if (wcscmp(find.cFileName, L".") == 0 || wcscmp(find.cFileName, L"..") == 0)
            goto skip;
        swprintf(name, 32768, L"%s\\%s", base, find.cFileName);
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            recursive_desparse(name, streams);
        } else if (find.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) {
            if (streams)
                desparse_streams(name);
            else
                desparse(name);
        } else {
            wprintf(L"%s is not a sparse file\n", name);
        }
skip:
        ret = FindNextFileW(hSearch, &find);
        err = GetLastError();
        if (!ret && err != ERROR_NO_MORE_FILES) {
            fwprintf(stderr, L"FindNextFile on %s failed: %s\n", base, w32strerror(err));
        }
    } while (ret);
    FindClose(hSearch);
    free(name);
    return err;
}

void desparse_streams(const wchar_t *f) {
    BOOL ret;
    DWORD err;
    HANDLE hSearchStream;
    WIN32_FIND_STREAM_DATA stream_find;
    wchar_t *stream_name = malloc(65536);
    hSearchStream = FindFirstStreamW(f, FindStreamInfoStandard, &stream_find, 0);
    err = GetLastError();
    if (err == ERROR_HANDLE_EOF) return;
    if (hSearchStream == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"FindFirstStreamW on %s failed: %s\n", f, w32strerror(err));    
        return;    
    }
    do {
        swprintf(stream_name, 32768, L"%s%s", f, stream_find.cStreamName);
        err = desparse(stream_name);
        ret = FindNextStreamW(hSearchStream, &stream_find);
        err = GetLastError();
        if (!ret && err != ERROR_HANDLE_EOF) {
            fwprintf(stderr, L"FindNextStreamW on %s failed: %s\n", f, w32strerror(err));
        }
    } while(ret);
    free(stream_name);
}

DWORD desparse(const wchar_t *f){
    HANDLE hFile;
    LARGE_INTEGER fsize;
    LARGE_INTEGER sparse_size;
    BOOL ret;
    DWORD attr, err, sizeh, iocret;
    FILE_SET_SPARSE_BUFFER setsparse = {FALSE};
    fputws(f, stdout);
    attr = GetFileAttributesW(f);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        err = GetLastError();
        wprintf(L" can't get attributes: %s\n", w32strerror(err));
        goto end;
    }
    if ((attr & FILE_ATTRIBUTE_SPARSE_FILE) == 0) {
        wprintf(L" not a sparse file\n");
        err = ERROR_FILE_EXISTS;
        goto end;
    }
    hFile = CreateFileW(f, (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        wprintf(L" CreateFile failed: %s\n", w32strerror(err));
        goto end;
    }
    if(!GetFileSizeEx(hFile, &fsize)) {
        err = GetLastError();
        wprintf(L" can't get size: %s\n", w32strerror(err));
        goto end_close;
    }
    sparse_size.LowPart = GetCompressedFileSizeW(f, &sizeh);
    sparse_size.HighPart = (LONG)sizeh;
    err = GetLastError();
    if (sparse_size.LowPart == INVALID_FILE_SIZE && err != NO_ERROR) {
        wprintf(L" can't get size: %s\n", w32strerror(err));
        goto end_close;
    }
    if (fsize.QuadPart != sparse_size.QuadPart) {
        wprintf(L" not fully filled\n");
        err = ERROR_FILE_EXISTS;
        goto end_close;
    }
    ret = DeviceIoControl(hFile, FSCTL_SET_SPARSE, &setsparse, sizeof(setsparse), NULL, 0, &iocret, NULL);
    err = GetLastError();
    if (ret) {
        wprintf(L" removed sparse flag\n");
    } else {
        wprintf(L" DeviceIoControl failed: %s\n", w32strerror(err));
    }
end_close:
    CloseHandle(hFile);
end:
    return err;
}
