/* AviUtl2 を起動し、前回開いていたプロジェクト(.aup2)を一緒に開くランチャ。
 *
 * 前回のパスは AviUtl2 自身が書いている history.ini の [project] から読む。
 * 新しい順に 1,2,3... と並んでいるので、実在する最初のものを開く。
 * 見つからなければ引数なしで普通に起動する。
 *
 * 引数:
 *   (なし)                   AviUtl2 を起動して前回のプロジェクトを開く
 *   --set-app "<フォルダ>"   aviutl2.exe のあるフォルダを記録し、
 *                            スタートメニューにショートカットを作る
 *                            (AviUtl2 カタログのインストール手順から呼ばれる)
 *   --uninstall              記録とショートカットを消す
 *
 * 終了コード(--set-app / --uninstall):
 *   0 成功 / 1 引数が不正 / 2 aviutl2.exe が無い / 3 記録に失敗 / 4 ショートカット作成に失敗
 *
 * ビルド:
 *   gcc -O2 -s -municode -mwindows -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE
 *       -o aviutl2-open-last.exe aviutl2-open-last.c -lshlwapi -lole32 -luuid
 * 動作確認用(コンソール版。AviUtl2 は起動せず、読み取り結果だけ表示する):
 *   gcc -O2 -s -municode -DTESTMAIN ... -o test.exe aviutl2-open-last.c -lshlwapi -lole32 -luuid
 *   test.exe "<aviutl2.exe のあるフォルダ>"
 *
 * SPDX-License-Identifier: MIT
 */

#define COBJMACROS
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>

#define MAXP 1024

static const wchar_t *APP_TITLE = L"AviUtl2 ランチャ";
static const wchar_t *LNK_NAME  = L"AviUtl2（前回のプロジェクトを開く）.lnk";
static const wchar_t *CFG_NAME  = L"aviutl2-open-last.ini";
static const wchar_t *CFG_SEC   = L"aviutl2-open-last";

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

/* 自分(このexe)のあるフォルダ */
static void self_dir(wchar_t *out) {
    GetModuleFileNameW(NULL, out, MAXP);
    PathRemoveFileSpecW(out);
}

static void cfg_path(wchar_t *out) {
    self_dir(out);
    PathAppendW(out, CFG_NAME);
}

/* スタートメニュー(現在のユーザー)のショートカットの場所 */
static int lnk_path(wchar_t *out) {
    wchar_t dir[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, dir))) return 0;
    lstrcpynW(out, dir, MAXP);
    PathAppendW(out, LNK_NAME);
    return 1;
}

/* 記録してある aviutl2.exe のフォルダを読む */
static int read_cfg_app(wchar_t *out, int outmax) {
    wchar_t cfg[MAXP];
    cfg_path(cfg);
    if (GetFileAttributesW(cfg) == INVALID_FILE_ATTRIBUTES) return 0;
    DWORD n = GetPrivateProfileStringW(CFG_SEC, L"app", L"", out, outmax, cfg);
    return n > 0;
}

/* aviutl2.exe のフォルダを記録する。
 * 先に UTF-16LE の BOM だけのファイルを作っておくと、
 * WritePrivateProfileStringW が Unicode のまま書く(ANSI に落ちない)。 */
static int write_cfg_app(const wchar_t *app) {
    wchar_t cfg[MAXP];
    cfg_path(cfg);
    HANDLE h = CreateFileW(cfg, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    static const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD w = 0;
    BOOL ok = WriteFile(h, bom, 2, &w, NULL);
    CloseHandle(h);
    if (!ok) return 0;
    return WritePrivateProfileStringW(CFG_SEC, L"app", app, cfg) != 0;
}

/* スタートメニューにショートカットを作る */
static int make_shortcut(const wchar_t *target, const wchar_t *workdir, const wchar_t *icon) {
    wchar_t lnk[MAXP];
    if (!lnk_path(lnk)) return 0;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int ok = 0;
    IShellLinkW *sl = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IShellLinkW, (void **)&sl))) {
        IShellLinkW_SetPath(sl, target);
        IShellLinkW_SetWorkingDirectory(sl, workdir);
        IShellLinkW_SetIconLocation(sl, icon, 0);
        IShellLinkW_SetDescription(sl, L"AviUtl2 を起動して前回のプロジェクトを開く");
        IPersistFile *pf = NULL;
        if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
            if (SUCCEEDED(IPersistFile_Save(pf, lnk, TRUE))) ok = 1;
            IPersistFile_Release(pf);
        }
        IShellLinkW_Release(sl);
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

/* 引数の値を整える: 前後の空白と引用符、末尾の \ を落とす。
 * "C:\Program Files\aviutl2\" のように末尾が \" だと
 * CommandLineToArgvW が引用符を文字として残すので、その対策も兼ねる。 */
static void trim_path(wchar_t *s) {
    int n = lstrlenW(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'"' || s[n - 1] == L'\\')) s[--n] = 0;
    int i = 0;
    while (s[i] == L' ' || s[i] == L'"') i++;
    if (i) memmove(s, s + i, (n - i + 1) * sizeof(wchar_t));
}

/* --set-app: argv[from..] を空白で繋いで 1 つのパスとして扱う
 * (引用符なしで Program Files が分割されて渡されても復元できる) */
static int cmd_set_app(int argc, wchar_t **argv, int from) {
    if (from >= argc) return 1;
    wchar_t app[MAXP] = L"";
    for (int i = from; i < argc; i++) {
        if (i > from && lstrlenW(app) + 1 < MAXP) lstrcatW(app, L" ");
        if (lstrlenW(app) + lstrlenW(argv[i]) >= MAXP) return 1;
        lstrcatW(app, argv[i]);
    }
    trim_path(app);
    if (!app[0]) return 1;

    wchar_t exe[MAXP];
    if (!try_exe(app, L"aviutl2.exe", exe)) return 2;
    if (!write_cfg_app(app)) return 3;

    wchar_t self[MAXP];
    GetModuleFileNameW(NULL, self, MAXP);
    wchar_t icon[MAXP];
    lstrcpynW(icon, exe, MAXP);
    if (!make_shortcut(self, app, icon)) return 4;
    return 0;
}

/* --uninstall: 記録とショートカットを消す。無くても成功扱い */
static int cmd_uninstall(void) {
    wchar_t p[MAXP];
    if (lnk_path(p)) DeleteFileW(p);
    cfg_path(p);
    DeleteFileW(p);
    return 0;
}

/* aviutl2.exe を探す: 記録 → 同じフォルダ → ..\AviUtl2 → .. → ..\..\AviUtl2 */
static int find_aviutl2(wchar_t *exe) {
    wchar_t app[MAXP];
    if (read_cfg_app(app, MAXP) && try_exe(app, L"aviutl2.exe", exe)) return 1;
    wchar_t dir[MAXP];
    self_dir(dir);
    return try_exe(dir, L"aviutl2.exe", exe)
        || try_exe(dir, L"..\\AviUtl2\\aviutl2.exe", exe)
        || try_exe(dir, L"..\\aviutl2.exe", exe)
        || try_exe(dir, L"..\\..\\AviUtl2\\aviutl2.exe", exe);
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

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        int rc = -1;
        if (lstrcmpiW(argv[1], L"--set-app") == 0) rc = cmd_set_app(argc, argv, 2);
        else if (lstrcmpiW(argv[1], L"--uninstall") == 0) rc = cmd_uninstall();
        if (rc >= 0) { LocalFree(argv); return rc; }
    }
    if (argv) LocalFree(argv);

    wchar_t exe[MAXP] = L"";
    if (!find_aviutl2(exe)) {
        MessageBoxW(NULL,
            L"aviutl2.exe が見つかりません。\n"
            L"AviUtl2 カタログから入れ直すか、このランチャを aviutl2.exe と同じフォルダに置いてください。",
            APP_TITLE, MB_ICONERROR);
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

    /* コマンドラインを組む: "exe" "proj" */
    wchar_t cl[MAXP * 2 + 8];
    lstrcpyW(cl, L"\"");
    lstrcatW(cl, exe);
    lstrcatW(cl, L"\"");
    if (proj[0]) {
        lstrcatW(cl, L" \"");
        lstrcatW(cl, proj);
        lstrcatW(cl, L"\"");
    }

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(exe, cl, NULL, NULL, FALSE, 0, NULL, exedir, &si, &pi)) {
        MessageBoxW(NULL, L"AviUtl2 の起動に失敗しました。", APP_TITLE, MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
#endif
