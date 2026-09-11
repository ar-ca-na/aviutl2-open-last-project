/* AviUtl2 を起動し、前回開いていたプロジェクト(.aup2)を一緒に開くランチャ。
 *
 * 前回のパスは AviUtl2 自身が書いている history.ini の [project] から読む。
 * 新しい順に 1,2,3... と並んでいるので、実在する最初のものを開く。
 * 見つからなければ引数なしで普通に起動する。
 *
 * ビルド:
 *   gcc -O2 -s -municode -mwindows -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE
 *       -o aviutl2-open-last.exe aviutl2-open-last.c -lshlwapi
 * 動作確認用(コンソール版。AviUtl2 は起動せず、読み取り結果だけ表示する):
 *   gcc -O2 -s -municode -DTESTMAIN ... -o test.exe aviutl2-open-last.c -lshlwapi
 *   test.exe "<aviutl2.exe のあるフォルダ>"
 *
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <shlwapi.h>

#define MAXP 1024

/* UTF-8 のバイト列 s から len バイトを wide に。失敗時 0 */
static int u8towide(const char *s, int len, wchar_t *out, int outmax) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, out, outmax - 1);
    if (n <= 0) return 0;
    out[n] = 0;
    return 1;
}

/* ファイルを丸ごと読む。呼び出し側が free する */
static char *readfile(const wchar_t *path, DWORD *size) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 1024 * 1024) { CloseHandle(h); return NULL; }
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD got = 0;
    if (!ReadFile(h, buf, sz, &got, NULL)) { free(buf); CloseHandle(h); return NULL; }
    CloseHandle(h);
    buf[got] = 0;
    *size = got;
    /* UTF-8 BOM を飛ばす */
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB
        && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, got - 3 + 1);
        *size = got - 3;
    }
    return buf;
}

/* [project] セクションの中で実在する最初のパスを out に入れる */
static int find_project(const char *ini, wchar_t *out, int outmax) {
    const char *p = strstr(ini, "[project]");
    if (!p) return 0;
    p += 9;
    while (*p) {
        while (*p == '\r' || *p == '\n') p++;   /* 行頭へ */
        if (*p == '[') break;                   /* 次のセクションに入った */
        if (!*p) break;
        const char *eol = p;
        while (*eol && *eol != '\r' && *eol != '\n') eol++;
        const char *eq = p;
        while (eq < eol && *eq != '=') eq++;
        if (eq < eol) {
            int len = (int)(eol - (eq + 1));
            wchar_t path[MAXP];
            if (len > 0 && len < MAXP - 1 && u8towide(eq + 1, len, path, MAXP)) {
                if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
                    lstrcpynW(out, path, outmax);
                    return 1;
                }
            }
        }
        p = eol;
    }
    return 0;
}

/* dir に sub を足したものが存在すれば out に入れる */
static int try_exe(const wchar_t *dir, const wchar_t *sub, wchar_t *out) {
    wchar_t t[MAXP];
    lstrcpynW(t, dir, MAXP);
    PathAppendW(t, sub);
    if (GetFileAttributesW(t) == INVALID_FILE_ATTRIBUTES) return 0;
    lstrcpynW(out, t, MAXP);
    return 1;
}

/* aviutl2.exe の隣の Data、なければ %ProgramData%\aviutl2 から history.ini を探す */
static int find_ini(const wchar_t *exedir, wchar_t *ini) {
    if (try_exe(exedir, L"Data\\history.ini", ini)) return 1;
    wchar_t pd[MAXP];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", pd, MAXP);
    if (n > 0 && n < MAXP) {
        PathAppendW(pd, L"aviutl2");
        if (try_exe(pd, L"history.ini", ini)) return 1;
    }
    return 0;
}

#ifdef TESTMAIN
int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(65001);
    wchar_t exedir[MAXP];
    lstrcpynW(exedir, argc > 1 ? argv[1] : L"C:\\nonexistent", MAXP);
    wchar_t ini[MAXP];
    if (!find_ini(exedir, ini)) { wprintf(L"ini not found\n"); return 1; }
    DWORD sz = 0;
    char *buf = readfile(ini, &sz);
    if (!buf) { wprintf(L"read failed\n"); return 1; }
    wprintf(L"ini=%s size=%lu\n", ini, (unsigned long)sz);
    wchar_t proj[MAXP] = L"";
    int ok = find_project(buf, proj, MAXP);
    wprintf(L"found=%d proj=%s\n", ok, proj);
    free(buf);
    return 0;
}
#else
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show) {
    (void)hi; (void)hp; (void)cmd; (void)show;

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef HANDLE (WINAPI *SPDAC)(HANDLE);
        SPDAC f = (SPDAC)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (f) f((HANDLE)(INT_PTR)-4);   /* PER_MONITOR_AWARE_V2 */
    }

    /* 自分の居場所 */
    wchar_t self[MAXP];
    GetModuleFileNameW(NULL, self, MAXP);
    wchar_t selfdir[MAXP];
    lstrcpynW(selfdir, self, MAXP);
    PathRemoveFileSpecW(selfdir);

    /* aviutl2.exe を探す: 同じフォルダ → ..\AviUtl2 → .. → ..\..\AviUtl2 */
    wchar_t exe[MAXP] = L"";
    if (!try_exe(selfdir, L"aviutl2.exe", exe)
        && !try_exe(selfdir, L"..\\AviUtl2\\aviutl2.exe", exe)
        && !try_exe(selfdir, L"..\\aviutl2.exe", exe)
        && !try_exe(selfdir, L"..\\..\\AviUtl2\\aviutl2.exe", exe)) {
        MessageBoxW(NULL,
            L"aviutl2.exe が見つかりません。\n"
            L"このランチャを AviUtl2 のフォルダか、その隣に置いてください。",
            L"AviUtl2 ランチャ", MB_ICONERROR);
        return 1;
    }

    wchar_t exedir[MAXP];
    lstrcpynW(exedir, exe, MAXP);
    PathRemoveFileSpecW(exedir);

    wchar_t ini[MAXP];
    wchar_t proj[MAXP] = L"";
    if (find_ini(exedir, ini)) {
        DWORD sz = 0;
        char *buf = readfile(ini, &sz);
        if (buf) {
            find_project(buf, proj, MAXP);
            free(buf);
        }
    }

    /* コマンドラインを組む */
    wchar_t cl[MAXP * 2 + 8];
    if (proj[0])
        wsprintfW(cl, L"\"%s\" \"%s\"", exe, proj);
    else
        wsprintfW(cl, L"\"%s\"", exe);

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(exe, cl, NULL, NULL, FALSE, 0, NULL, exedir, &si, &pi)) {
        MessageBoxW(NULL, L"AviUtl2 の起動に失敗しました。",
                    L"AviUtl2 ランチャ", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
#endif
