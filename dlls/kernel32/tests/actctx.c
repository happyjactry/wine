/*
 * Copyright 2007 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "wine/test.h"
#include <winbase.h>
#include <windef.h>
#include <winnt.h>
#include <winternl.h>
#include <winnls.h>
#include <stdio.h>

#include "initguid.h"

static BOOL   (WINAPI *pActivateActCtx)(HANDLE,ULONG_PTR*);
static HANDLE (WINAPI *pCreateActCtxW)(PCACTCTXW);
static BOOL   (WINAPI *pDeactivateActCtx)(DWORD,ULONG_PTR);
static BOOL   (WINAPI *pFindActCtxSectionStringW)(DWORD,const GUID *,ULONG,LPCWSTR,PACTCTX_SECTION_KEYED_DATA);
static BOOL   (WINAPI *pGetCurrentActCtx)(HANDLE *);
static BOOL   (WINAPI *pIsDebuggerPresent)(void);
static BOOL   (WINAPI *pQueryActCtxW)(DWORD,HANDLE,PVOID,ULONG,PVOID,SIZE_T,SIZE_T*);
static VOID   (WINAPI *pReleaseActCtx)(HANDLE);
static BOOL   (WINAPI *pFindActCtxSectionGuid)(DWORD,const GUID*,ULONG,const GUID*,PACTCTX_SECTION_KEYED_DATA);

static const char* strw(LPCWSTR x)
{
    static char buffer[1024];
    char*       p = buffer;

    if (!x) return "(nil)";
    else while ((*p++ = *x++));
    return buffer;
}

static const char *debugstr_guid(REFIID riid)
{
    static char buf[50];

    sprintf(buf, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            riid->Data1, riid->Data2, riid->Data3, riid->Data4[0],
            riid->Data4[1], riid->Data4[2], riid->Data4[3], riid->Data4[4],
            riid->Data4[5], riid->Data4[6], riid->Data4[7]);

    return buf;
}

#ifdef __i386__
#define ARCH "x86"
#elif defined __x86_64__
#define ARCH "amd64"
#else
#define ARCH "none"
#endif

static const char manifest1[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"</assembly>";

static const char manifest2[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.2.3.4\" name=\"Wine.Test\" type=\"win32\">"
"</assemblyIdentity>"
"<dependency>"
"<dependentAssembly>"
"<assemblyIdentity type=\"win32\" name=\"testdep\" version=\"6.5.4.3\" processorArchitecture=\"" ARCH "\">"
"</assemblyIdentity>"
"</dependentAssembly>"
"</dependency>"
"</assembly>";

DEFINE_GUID(IID_CoTest, 0x12345678, 0x1234, 0x5678, 0x12, 0x34, 0x11, 0x11, 0x22, 0x22, 0x33, 0x33);
DEFINE_GUID(IID_TlibTest, 0x99999999, 0x8888, 0x7777, 0x66, 0x66, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55);

static const char manifest3[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.2.3.4\"  name=\"Wine.Test\" type=\"win32\""
" publicKeyToken=\"6595b6414666f1df\" />"
"<file name=\"testlib.dll\">"
"<windowClass>wndClass</windowClass>"
"    <comClass description=\"Test com class\""
"              clsid=\"{12345678-1234-5678-1234-111122223333}\""
"              tlbid=\"{99999999-8888-7777-6666-555555555555}\""
"              threadingModel=\"Neutral\""
"              progid=\"ProgId.ProgId\""
"    />"
"</file>"
"</assembly>";

static const char manifest_wndcls1[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.2.3.4\"  name=\"testdep1\" type=\"win32\" processorArchitecture=\"" ARCH "\"/>"
"<file name=\"testlib1.dll\">"
"<windowClass>wndClass1</windowClass>"
"<windowClass>wndClass2</windowClass>"
"</file>"
"</assembly>";

static const char manifest_wndcls2[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"4.3.2.1\"  name=\"testdep2\" type=\"win32\" processorArchitecture=\"" ARCH "\" />"
"<file name=\"testlib2.dll\">"
"<windowClass>wndClass3</windowClass>"
"<windowClass>wndClass4</windowClass>"
"</file>"
"</assembly>";

static const char manifest_wndcls_main[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.2.3.4\" name=\"Wine.Test\" type=\"win32\" />"
"<dependency>"
" <dependentAssembly>"
"  <assemblyIdentity type=\"win32\" name=\"testdep1\" version=\"1.2.3.4\" processorArchitecture=\"" ARCH "\" />"
" </dependentAssembly>"
"</dependency>"
"<dependency>"
" <dependentAssembly>"
"  <assemblyIdentity type=\"win32\" name=\"testdep2\" version=\"4.3.2.1\" processorArchitecture=\"" ARCH "\" />"
" </dependentAssembly>"
"</dependency>"
"</assembly>";

static const char manifest4[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.2.3.4\" name=\"Wine.Test\" type=\"win32\">"
"</assemblyIdentity>"
"<dependency>"
"<dependentAssembly>"
"<assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
    "version=\"6.0.1.0\" processorArchitecture=\"" ARCH "\" publicKeyToken=\"6595b64144ccf1df\">"
"</assemblyIdentity>"
"</dependentAssembly>"
"</dependency>"
"</assembly>";

static const char testdep_manifest1[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity type=\"win32\" name=\"testdep\" version=\"6.5.4.3\" processorArchitecture=\"" ARCH "\"/>"
"</assembly>";

static const char testdep_manifest2[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity type=\"win32\" name=\"testdep\" version=\"6.5.4.3\" processorArchitecture=\"" ARCH "\" />"
"<file name=\"testlib.dll\"></file>"
"<file name=\"testlib2.dll\" hash=\"63c978c2b53d6cf72b42fb7308f9af12ab19ec53\" hashalg=\"SHA1\" />"
"</assembly>";

static const char testdep_manifest3[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"> "
"<assemblyIdentity type=\"win32\" name=\"testdep\" version=\"6.5.4.3\" processorArchitecture=\"" ARCH "\"/>"
"<file name=\"testlib.dll\"/>"
"<file name=\"testlib2.dll\" hash=\"63c978c2b53d6cf72b42fb7308f9af12ab19ec53\" hashalg=\"SHA1\">"
"<windowClass>wndClass</windowClass>"
"<windowClass>wndClass2</windowClass>"
"</file>"
"</assembly>";

static const char wrong_manifest1[] =
"<assembly manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"</assembly>";

static const char wrong_manifest2[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"</assembly>";

static const char wrong_manifest3[] =
"<assembly test=\"test\" xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"</assembly>";

static const char wrong_manifest4[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"<test></test>"
"</assembly>";

static const char wrong_manifest5[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"</assembly>"
"<test></test>";

static const char wrong_manifest6[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v5\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.0.0.0\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"</assembly>";

static const char wrong_manifest7[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity type=\"win32\" name=\"testdep\" version=\"6.5.4.3\" processorArchitecture=\"" ARCH "\" />"
"<file name=\"testlib.dll\" hash=\"63c978c2b53d6cf72b42fb7308f9af12ab19ec5\" hashalg=\"SHA1\" />"
"</assembly>";

static const char wrong_manifest8[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity version=\"1.2.3.4\"  name=\"Wine.Test\" type=\"win32\"></assemblyIdentity>"
"<file></file>"
"</assembly>";

static const char wrong_depmanifest1[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity type=\"win32\" name=\"testdep\" version=\"6.5.4.4\" processorArchitecture=\"" ARCH "\" />"
"</assembly>";

static const WCHAR testlib_dll[] =
    {'t','e','s','t','l','i','b','.','d','l','l',0};
static const WCHAR testlib2_dll[] =
    {'t','e','s','t','l','i','b','2','.','d','l','l',0};
static const WCHAR wndClassW[] =
    {'w','n','d','C','l','a','s','s',0};
static const WCHAR wndClass1W[] =
    {'w','n','d','C','l','a','s','s','1',0};
static const WCHAR wndClass2W[] =
    {'w','n','d','C','l','a','s','s','2',0};
static const WCHAR wndClass3W[] =
    {'w','n','d','C','l','a','s','s','3',0};
static const WCHAR acr_manifest[] =
    {'a','c','r','.','m','a','n','i','f','e','s','t',0};

static WCHAR app_dir[MAX_PATH], exe_path[MAX_PATH], work_dir[MAX_PATH], work_dir_subdir[MAX_PATH];
static WCHAR app_manifest_path[MAX_PATH], manifest_path[MAX_PATH], depmanifest_path[MAX_PATH];

static int strcmp_aw(LPCWSTR strw, const char *stra)
{
    WCHAR buf[1024];

    if (!stra) return 1;
    MultiByteToWideChar(CP_ACP, 0, stra, -1, buf, sizeof(buf)/sizeof(WCHAR));
    return lstrcmpW(strw, buf);
}

static DWORD strlen_aw(const char *str)
{
    return MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0) - 1;
}

static BOOL create_manifest_file(const char *filename, const char *manifest, int manifest_len,
                                 const char *depfile, const char *depmanifest)
{
    DWORD size;
    HANDLE file;
    WCHAR path[MAX_PATH];

    MultiByteToWideChar( CP_ACP, 0, filename, -1, path, MAX_PATH );
    GetFullPathNameW(path, sizeof(manifest_path)/sizeof(WCHAR), manifest_path, NULL);

    if (manifest_len == -1)
        manifest_len = strlen(manifest);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile failed: %u\n", GetLastError());
    if(file == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(file, manifest, manifest_len, &size, NULL);
    CloseHandle(file);

    if (depmanifest)
    {
        MultiByteToWideChar( CP_ACP, 0, depfile, -1, path, MAX_PATH );
        GetFullPathNameW(path, sizeof(depmanifest_path)/sizeof(WCHAR), depmanifest_path, NULL);
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        ok(file != INVALID_HANDLE_VALUE, "CreateFile failed: %u\n", GetLastError());
        if(file == INVALID_HANDLE_VALUE)
            return FALSE;
        WriteFile(file, depmanifest, strlen(depmanifest), &size, NULL);
        CloseHandle(file);
    }
    return TRUE;
}

static BOOL create_wide_manifest(const char *filename, const char *manifest, BOOL fBOM, BOOL fReverse)
{
    WCHAR *wmanifest = HeapAlloc(GetProcessHeap(), 0, (strlen(manifest)+2) * sizeof(WCHAR));
    BOOL ret;
    int offset = (fBOM ? 0 : 1);

    MultiByteToWideChar(CP_ACP, 0, manifest, -1, &wmanifest[1], (strlen(manifest)+1));
    wmanifest[0] = 0xfeff;
    if (fReverse)
    {
        size_t i;
        for (i = 0; i < strlen(manifest)+1; i++)
            wmanifest[i] = (wmanifest[i] << 8) | ((wmanifest[i] >> 8) & 0xff);
    }
    ret = create_manifest_file(filename, (char *)&wmanifest[offset], (strlen(manifest)+1-offset) * sizeof(WCHAR), NULL, NULL);
    HeapFree(GetProcessHeap(), 0, wmanifest);
    return ret;
}

typedef struct {
    ULONG format_version;
    ULONG assembly_cnt_min;
    ULONG assembly_cnt_max;
    ULONG root_manifest_type;
    LPWSTR root_manifest_path;
    ULONG root_config_type;
    ULONG app_dir_type;
    LPCWSTR app_dir;
} detailed_info_t;

static const detailed_info_t detailed_info0 = {
    0, 0, 0, 0, NULL, 0, 0, NULL
};

static const detailed_info_t detailed_info1 = {
    1, 1, 1, ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE, manifest_path,
    ACTIVATION_CONTEXT_PATH_TYPE_NONE, ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE,
    work_dir,
};

static const detailed_info_t detailed_info1_child = {
    1, 1, 1, ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE, app_manifest_path,
    ACTIVATION_CONTEXT_PATH_TYPE_NONE, ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE,
    app_dir,
};

/* On Vista+, there's an extra assembly for Microsoft.Windows.Common-Controls.Resources */
static const detailed_info_t detailed_info2 = {
    1, 2, 3, ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE, manifest_path,
    ACTIVATION_CONTEXT_PATH_TYPE_NONE, ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE,
    work_dir,
};

static void test_detailed_info(HANDLE handle, const detailed_info_t *exinfo, int line)
{
    ACTIVATION_CONTEXT_DETAILED_INFORMATION detailed_info_tmp, *detailed_info;
    SIZE_T size, exsize, retsize;
    BOOL b;

    exsize = sizeof(ACTIVATION_CONTEXT_DETAILED_INFORMATION)
        + (exinfo->root_manifest_path ? (lstrlenW(exinfo->root_manifest_path)+1)*sizeof(WCHAR):0)
        + (exinfo->app_dir ? (lstrlenW(exinfo->app_dir)+1)*sizeof(WCHAR) : 0);

    if(exsize != sizeof(ACTIVATION_CONTEXT_DETAILED_INFORMATION)) {
        size = 0xdeadbeef;
        b = pQueryActCtxW(0, handle, NULL,
                          ActivationContextDetailedInformation, &detailed_info_tmp,
                          sizeof(detailed_info_tmp), &size);
        ok_(__FILE__, line)(!b, "QueryActCtx succeeded\n");
        ok_(__FILE__, line)(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetLastError() = %u\n", GetLastError());
        ok_(__FILE__, line)(size == exsize, "size=%ld, expected %ld\n", size, exsize);
    }else {
        size = sizeof(ACTIVATION_CONTEXT_DETAILED_INFORMATION);
    }

    detailed_info = HeapAlloc(GetProcessHeap(), 0, size);
    memset(detailed_info, 0xfe, size);
    b = pQueryActCtxW(0, handle, NULL,
                      ActivationContextDetailedInformation, detailed_info,
                      size, &retsize);
    ok_(__FILE__, line)(b, "QueryActCtx failed: %u\n", GetLastError());
    ok_(__FILE__, line)(retsize == exsize, "size=%ld, expected %ld\n", retsize, exsize);

    ok_(__FILE__, line)(detailed_info->dwFlags == 0, "detailed_info->dwFlags=%x\n", detailed_info->dwFlags);
    ok_(__FILE__, line)(detailed_info->ulFormatVersion == exinfo->format_version,
       "detailed_info->ulFormatVersion=%u, expected %u\n", detailed_info->ulFormatVersion,
       exinfo->format_version);
    ok_(__FILE__, line)(exinfo->assembly_cnt_min <= detailed_info->ulAssemblyCount &&
       detailed_info->ulAssemblyCount <= exinfo->assembly_cnt_max,
       "detailed_info->ulAssemblyCount=%u, expected between %u and %u\n", detailed_info->ulAssemblyCount,
       exinfo->assembly_cnt_min, exinfo->assembly_cnt_max);
    ok_(__FILE__, line)(detailed_info->ulRootManifestPathType == exinfo->root_manifest_type,
       "detailed_info->ulRootManifestPathType=%u, expected %u\n",
       detailed_info->ulRootManifestPathType, exinfo->root_manifest_type);
    ok_(__FILE__, line)(detailed_info->ulRootManifestPathChars ==
       (exinfo->root_manifest_path ? lstrlenW(exinfo->root_manifest_path) : 0),
       "detailed_info->ulRootManifestPathChars=%u, expected %u\n",
       detailed_info->ulRootManifestPathChars,
       exinfo->root_manifest_path ?lstrlenW(exinfo->root_manifest_path) : 0);
    ok_(__FILE__, line)(detailed_info->ulRootConfigurationPathType == exinfo->root_config_type,
       "detailed_info->ulRootConfigurationPathType=%u, expected %u\n",
       detailed_info->ulRootConfigurationPathType, exinfo->root_config_type);
    ok_(__FILE__, line)(detailed_info->ulRootConfigurationPathChars == 0,
       "detailed_info->ulRootConfigurationPathChars=%d\n", detailed_info->ulRootConfigurationPathChars);
    ok_(__FILE__, line)(detailed_info->ulAppDirPathType == exinfo->app_dir_type,
       "detailed_info->ulAppDirPathType=%u, expected %u\n", detailed_info->ulAppDirPathType,
       exinfo->app_dir_type);
    ok_(__FILE__, line)(detailed_info->ulAppDirPathChars == (exinfo->app_dir ? lstrlenW(exinfo->app_dir) : 0),
       "detailed_info->ulAppDirPathChars=%u, expected %u\n",
       detailed_info->ulAppDirPathChars, exinfo->app_dir ? lstrlenW(exinfo->app_dir) : 0);
    if(exinfo->root_manifest_path) {
        ok_(__FILE__, line)(detailed_info->lpRootManifestPath != NULL, "detailed_info->lpRootManifestPath == NULL\n");
        if(detailed_info->lpRootManifestPath)
            ok_(__FILE__, line)(!lstrcmpiW(detailed_info->lpRootManifestPath, exinfo->root_manifest_path),
               "unexpected detailed_info->lpRootManifestPath\n");
    }else {
        ok_(__FILE__, line)(detailed_info->lpRootManifestPath == NULL, "detailed_info->lpRootManifestPath != NULL\n");
    }
    ok_(__FILE__, line)(detailed_info->lpRootConfigurationPath == NULL,
       "detailed_info->lpRootConfigurationPath=%p\n", detailed_info->lpRootConfigurationPath);
    if(exinfo->app_dir) {
        ok_(__FILE__, line)(detailed_info->lpAppDirPath != NULL, "detailed_info->lpAppDirPath == NULL\n");
        if(detailed_info->lpAppDirPath)
            ok_(__FILE__, line)(!lstrcmpiW(exinfo->app_dir, detailed_info->lpAppDirPath),
               "unexpected detailed_info->lpAppDirPath\n%s\n",strw(detailed_info->lpAppDirPath));
    }else {
        ok_(__FILE__, line)(detailed_info->lpAppDirPath == NULL, "detailed_info->lpAppDirPath != NULL\n");
    }

    HeapFree(GetProcessHeap(), 0, detailed_info);
}

typedef struct {
    ULONG flags;
/*    ULONG manifest_path_type; FIXME */
    LPCWSTR manifest_path;
    LPCSTR encoded_assembly_id;
    BOOL has_assembly_dir;
} info_in_assembly;

static const info_in_assembly manifest1_info = {
    1, manifest_path,
    "Wine.Test,type=\"win32\",version=\"1.0.0.0\"",
    FALSE
};

static const info_in_assembly manifest1_child_info = {
    1, app_manifest_path,
    "Wine.Test,type=\"win32\",version=\"1.0.0.0\"",
    FALSE
};

static const info_in_assembly manifest2_info = {
    1, manifest_path,
    "Wine.Test,type=\"win32\",version=\"1.2.3.4\"",
    FALSE
};

static const info_in_assembly manifest3_info = {
    1, manifest_path,
    "Wine.Test,publicKeyToken=\"6595b6414666f1df\",type=\"win32\",version=\"1.2.3.4\"",
    FALSE
};

static const info_in_assembly manifest4_info = {
    1, manifest_path,
    "Wine.Test,type=\"win32\",version=\"1.2.3.4\"",
    FALSE
};

static const info_in_assembly depmanifest1_info = {
    0x10, depmanifest_path,
    "testdep,processorArchitecture=\"" ARCH "\","
    "type=\"win32\",version=\"6.5.4.3\"",
    TRUE
};

static const info_in_assembly depmanifest2_info = {
    0x10, depmanifest_path,
    "testdep,processorArchitecture=\"" ARCH "\","
    "type=\"win32\",version=\"6.5.4.3\"",
    TRUE
};

static const info_in_assembly depmanifest3_info = {
    0x10, depmanifest_path,
    "testdep,processorArchitecture=\"" ARCH "\",type=\"win32\",version=\"6.5.4.3\"",
    TRUE
};

static const info_in_assembly manifest_comctrl_info = {
    0, NULL, NULL, TRUE /* These values may differ between Windows installations */
};

static void test_info_in_assembly(HANDLE handle, DWORD id, const info_in_assembly *exinfo, int line)
{
    ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION *info, info_tmp;
    SIZE_T size, exsize;
    ULONG len;
    BOOL b;

    exsize = sizeof(ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION);
    if (exinfo->manifest_path) exsize += (lstrlenW(exinfo->manifest_path)+1) * sizeof(WCHAR);
    if (exinfo->encoded_assembly_id) exsize += (strlen_aw(exinfo->encoded_assembly_id) + 1) * sizeof(WCHAR);

    size = 0xdeadbeef;
    b = pQueryActCtxW(0, handle, &id,
                      AssemblyDetailedInformationInActivationContext, &info_tmp,
                      sizeof(info_tmp), &size);
    ok_(__FILE__, line)(!b, "QueryActCtx succeeded\n");
    ok_(__FILE__, line)(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetLastError() = %u\n", GetLastError());

    ok_(__FILE__, line)(size >= exsize, "size=%lu, expected %lu\n", size, exsize);

    if (size == 0xdeadbeef)
    {
        skip("bad size\n");
        return;
    }

    info = HeapAlloc(GetProcessHeap(), 0, size);
    memset(info, 0xfe, size);

    size = 0xdeadbeef;
    b = pQueryActCtxW(0, handle, &id,
                      AssemblyDetailedInformationInActivationContext, info, size, &size);
    ok_(__FILE__, line)(b, "QueryActCtx failed: %u\n", GetLastError());
    if (!exinfo->manifest_path)
        exsize += info->ulManifestPathLength + sizeof(WCHAR);
    if (!exinfo->encoded_assembly_id)
        exsize += info->ulEncodedAssemblyIdentityLength + sizeof(WCHAR);
    if (exinfo->has_assembly_dir)
        exsize += info->ulAssemblyDirectoryNameLength + sizeof(WCHAR);
    ok_(__FILE__, line)(size == exsize, "size=%lu, expected %lu\n", size, exsize);

    if (0)  /* FIXME: flags meaning unknown */
    {
        ok_(__FILE__, line)((info->ulFlags) == exinfo->flags, "info->ulFlags = %x, expected %x\n",
           info->ulFlags, exinfo->flags);
    }
    if(exinfo->encoded_assembly_id) {
        len = strlen_aw(exinfo->encoded_assembly_id)*sizeof(WCHAR);
        ok_(__FILE__, line)(info->ulEncodedAssemblyIdentityLength == len,
           "info->ulEncodedAssemblyIdentityLength = %u, expected %u\n",
           info->ulEncodedAssemblyIdentityLength, len);
    } else {
        ok_(__FILE__, line)(info->ulEncodedAssemblyIdentityLength != 0,
           "info->ulEncodedAssemblyIdentityLength == 0\n");
    }
    ok_(__FILE__, line)(info->ulManifestPathType == ACTIVATION_CONTEXT_PATH_TYPE_WIN32_FILE,
       "info->ulManifestPathType = %x\n", info->ulManifestPathType);
    if(exinfo->manifest_path) {
        len = lstrlenW(exinfo->manifest_path)*sizeof(WCHAR);
        ok_(__FILE__, line)(info->ulManifestPathLength == len, "info->ulManifestPathLength = %u, expected %u\n",
           info->ulManifestPathLength, len);
    } else {
        ok_(__FILE__, line)(info->ulManifestPathLength != 0, "info->ulManifestPathLength == 0\n");
    }

    ok_(__FILE__, line)(info->ulPolicyPathType == ACTIVATION_CONTEXT_PATH_TYPE_NONE,
       "info->ulPolicyPathType = %x\n", info->ulPolicyPathType);
    ok_(__FILE__, line)(info->ulPolicyPathLength == 0,
       "info->ulPolicyPathLength = %u, expected 0\n", info->ulPolicyPathLength);
    ok_(__FILE__, line)(info->ulMetadataSatelliteRosterIndex == 0, "info->ulMetadataSatelliteRosterIndex = %x\n",
       info->ulMetadataSatelliteRosterIndex);
    ok_(__FILE__, line)(info->ulManifestVersionMajor == 1,"info->ulManifestVersionMajor = %x\n",
       info->ulManifestVersionMajor);
    ok_(__FILE__, line)(info->ulManifestVersionMinor == 0, "info->ulManifestVersionMinor = %x\n",
       info->ulManifestVersionMinor);
    ok_(__FILE__, line)(info->ulPolicyVersionMajor == 0, "info->ulPolicyVersionMajor = %x\n",
       info->ulPolicyVersionMajor);
    ok_(__FILE__, line)(info->ulPolicyVersionMinor == 0, "info->ulPolicyVersionMinor = %x\n",
       info->ulPolicyVersionMinor);
    if(exinfo->has_assembly_dir)
        ok_(__FILE__, line)(info->ulAssemblyDirectoryNameLength != 0,
           "info->ulAssemblyDirectoryNameLength == 0\n");
    else
        ok_(__FILE__, line)(info->ulAssemblyDirectoryNameLength == 0,
           "info->ulAssemblyDirectoryNameLength != 0\n");

    ok_(__FILE__, line)(info->lpAssemblyEncodedAssemblyIdentity != NULL,
       "info->lpAssemblyEncodedAssemblyIdentity == NULL\n");
    if(info->lpAssemblyEncodedAssemblyIdentity && exinfo->encoded_assembly_id) {
        ok_(__FILE__, line)(!strcmp_aw(info->lpAssemblyEncodedAssemblyIdentity, exinfo->encoded_assembly_id),
           "unexpected info->lpAssemblyEncodedAssemblyIdentity %s / %s\n",
           strw(info->lpAssemblyEncodedAssemblyIdentity), exinfo->encoded_assembly_id);
    }
    if(exinfo->manifest_path) {
        ok_(__FILE__, line)(info->lpAssemblyManifestPath != NULL, "info->lpAssemblyManifestPath == NULL\n");
        if(info->lpAssemblyManifestPath)
            ok_(__FILE__, line)(!lstrcmpiW(info->lpAssemblyManifestPath, exinfo->manifest_path),
               "unexpected info->lpAssemblyManifestPath\n");
    }else {
        ok_(__FILE__, line)(info->lpAssemblyManifestPath != NULL, "info->lpAssemblyManifestPath == NULL\n");
    }

    ok_(__FILE__, line)(info->lpAssemblyPolicyPath == NULL, "info->lpAssemblyPolicyPath != NULL\n");
    if(info->lpAssemblyPolicyPath)
        ok_(__FILE__, line)(*(WORD*)info->lpAssemblyPolicyPath == 0, "info->lpAssemblyPolicyPath is not empty\n");
    if(exinfo->has_assembly_dir)
        ok_(__FILE__, line)(info->lpAssemblyDirectoryName != NULL, "info->lpAssemblyDirectoryName == NULL\n");
    else
        ok_(__FILE__, line)(info->lpAssemblyDirectoryName == NULL, "info->lpAssemblyDirectoryName = %s\n",
           strw(info->lpAssemblyDirectoryName));
    HeapFree(GetProcessHeap(), 0, info);
}

static void test_file_info(HANDLE handle, ULONG assid, ULONG fileid, LPCWSTR filename, int line)
{
    ASSEMBLY_FILE_DETAILED_INFORMATION *info, info_tmp;
    ACTIVATION_CONTEXT_QUERY_INDEX index = {assid, fileid};
    SIZE_T size, exsize;
    BOOL b;

    exsize = sizeof(ASSEMBLY_FILE_DETAILED_INFORMATION)
        +(lstrlenW(filename)+1)*sizeof(WCHAR);

    size = 0xdeadbeef;
    b = pQueryActCtxW(0, handle, &index,
                      FileInformationInAssemblyOfAssemblyInActivationContext, &info_tmp,
                      sizeof(info_tmp), &size);
    ok_(__FILE__, line)(!b, "QueryActCtx succeeded\n");
    ok_(__FILE__, line)(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetLastError() = %u\n", GetLastError());
    ok_(__FILE__, line)(size == exsize, "size=%lu, expected %lu\n", size, exsize);

    if(size == 0xdeadbeef)
    {
        skip("bad size\n");
        return;
    }

    info = HeapAlloc(GetProcessHeap(), 0, size);
    memset(info, 0xfe, size);

    b = pQueryActCtxW(0, handle, &index,
                      FileInformationInAssemblyOfAssemblyInActivationContext, info, size, &size);
    ok_(__FILE__, line)(b, "QueryActCtx failed: %u\n", GetLastError());
    ok_(__FILE__, line)(!size, "size=%lu, expected 0\n", size);

    ok_(__FILE__, line)(info->ulFlags == 2, "info->ulFlags=%x, expected 2\n", info->ulFlags);
    ok_(__FILE__, line)(info->ulFilenameLength == lstrlenW(filename)*sizeof(WCHAR),
       "info->ulFilenameLength=%u, expected %u*sizeof(WCHAR)\n",
       info->ulFilenameLength, lstrlenW(filename));
    ok_(__FILE__, line)(info->ulPathLength == 0, "info->ulPathLength=%u\n", info->ulPathLength);
    ok_(__FILE__, line)(info->lpFileName != NULL, "info->lpFileName == NULL\n");
    if(info->lpFileName)
        ok_(__FILE__, line)(!lstrcmpiW(info->lpFileName, filename), "unexpected info->lpFileName\n");
    ok_(__FILE__, line)(info->lpFilePath == NULL, "info->lpFilePath != NULL\n");
    HeapFree(GetProcessHeap(), 0, info);
}

static HANDLE test_create(const char *file)
{
    ACTCTXW actctx;
    HANDLE handle;
    WCHAR path[MAX_PATH];

    MultiByteToWideChar( CP_ACP, 0, file, -1, path, MAX_PATH );
    memset(&actctx, 0, sizeof(ACTCTXW));
    actctx.cbSize = sizeof(ACTCTXW);
    actctx.lpSource = path;

    handle = pCreateActCtxW(&actctx);
    ok(handle != INVALID_HANDLE_VALUE, "handle == INVALID_HANDLE_VALUE, error %u\n", GetLastError());

    ok(actctx.cbSize == sizeof(actctx), "actctx.cbSize=%d\n", actctx.cbSize);
    ok(actctx.dwFlags == 0, "actctx.dwFlags=%d\n", actctx.dwFlags);
    ok(actctx.lpSource == path, "actctx.lpSource=%p\n", actctx.lpSource);
    ok(actctx.wProcessorArchitecture == 0,
       "actctx.wProcessorArchitecture=%d\n", actctx.wProcessorArchitecture);
    ok(actctx.wLangId == 0, "actctx.wLangId=%d\n", actctx.wLangId);
    ok(actctx.lpAssemblyDirectory == NULL,
       "actctx.lpAssemblyDirectory=%p\n", actctx.lpAssemblyDirectory);
    ok(actctx.lpResourceName == NULL, "actctx.lpResourceName=%p\n", actctx.lpResourceName);
    ok(actctx.lpApplicationName == NULL, "actctx.lpApplicationName=%p\n",
       actctx.lpApplicationName);
    ok(actctx.hModule == NULL, "actctx.hModule=%p\n", actctx.hModule);

    return handle;
}

static void test_create_and_fail(const char *manifest, const char *depmanifest, int todo)
{
    ACTCTXW actctx;
    HANDLE handle;
    WCHAR path[MAX_PATH];

    MultiByteToWideChar( CP_ACP, 0, "bad.manifest", -1, path, MAX_PATH );
    memset(&actctx, 0, sizeof(ACTCTXW));
    actctx.cbSize = sizeof(ACTCTXW);
    actctx.lpSource = path;

    create_manifest_file("bad.manifest", manifest, -1, "testdep.manifest", depmanifest);
    handle = pCreateActCtxW(&actctx);
    if (todo) todo_wine
    {
        ok(handle == INVALID_HANDLE_VALUE, "handle != INVALID_HANDLE_VALUE\n");
        ok(GetLastError() == ERROR_SXS_CANT_GEN_ACTCTX, "GetLastError == %u\n", GetLastError());
    }
    else
    {
        ok(handle == INVALID_HANDLE_VALUE, "handle != INVALID_HANDLE_VALUE\n");
        ok(GetLastError() == ERROR_SXS_CANT_GEN_ACTCTX, "GetLastError == %u\n", GetLastError());
    }
    if (handle != INVALID_HANDLE_VALUE) pReleaseActCtx( handle );
    DeleteFileA("bad.manifest");
    DeleteFileA("testdep.manifest");
}

static void test_create_wide_and_fail(const char *manifest, BOOL fBOM)
{
    ACTCTXW actctx;
    HANDLE handle;
    WCHAR path[MAX_PATH];

    MultiByteToWideChar( CP_ACP, 0, "bad.manifest", -1, path, MAX_PATH );
    memset(&actctx, 0, sizeof(ACTCTXW));
    actctx.cbSize = sizeof(ACTCTXW);
    actctx.lpSource = path;

    create_wide_manifest("bad.manifest", manifest, fBOM, FALSE);
    handle = pCreateActCtxW(&actctx);
    ok(handle == INVALID_HANDLE_VALUE, "handle != INVALID_HANDLE_VALUE\n");
    ok(GetLastError() == ERROR_SXS_CANT_GEN_ACTCTX, "GetLastError == %u\n", GetLastError());

    if (handle != INVALID_HANDLE_VALUE) pReleaseActCtx( handle );
    DeleteFileA("bad.manifest");
}

static void test_create_fail(void)
{
    ACTCTXW actctx;
    HANDLE handle;
    WCHAR path[MAX_PATH];

    MultiByteToWideChar( CP_ACP, 0, "nonexistent.manifest", -1, path, MAX_PATH );
    memset(&actctx, 0, sizeof(ACTCTXW));
    actctx.cbSize = sizeof(ACTCTXW);
    actctx.lpSource = path;

    handle = pCreateActCtxW(&actctx);
    ok(handle == INVALID_HANDLE_VALUE, "handle != INVALID_HANDLE_VALUE\n");
    ok(GetLastError() == ERROR_FILE_NOT_FOUND, "GetLastError == %u\n", GetLastError());

    trace("wrong_manifest1\n");
    test_create_and_fail(wrong_manifest1, NULL, 0 );
    trace("wrong_manifest2\n");
    test_create_and_fail(wrong_manifest2, NULL, 0 );
    trace("wrong_manifest3\n");
    test_create_and_fail(wrong_manifest3, NULL, 1 );
    trace("wrong_manifest4\n");
    test_create_and_fail(wrong_manifest4, NULL, 1 );
    trace("wrong_manifest5\n");
    test_create_and_fail(wrong_manifest5, NULL, 0 );
    trace("wrong_manifest6\n");
    test_create_and_fail(wrong_manifest6, NULL, 0 );
    trace("wrong_manifest7\n");
    test_create_and_fail(wrong_manifest7, NULL, 1 );
    trace("wrong_manifest8\n");
    test_create_and_fail(wrong_manifest8, NULL, 0 );
    trace("UTF-16 manifest1 without BOM\n");
    test_create_wide_and_fail(manifest1, FALSE );
    trace("manifest2\n");
    test_create_and_fail(manifest2, NULL, 0 );
    trace("manifest2+depmanifest1\n");
    test_create_and_fail(manifest2, wrong_depmanifest1, 0 );
}

struct dllredirect_keyed_data
{
    ULONG size;
    ULONG unk;
    DWORD res[3];
};

static void test_find_dll_redirection(HANDLE handle, LPCWSTR libname, ULONG exid, int line)
{
    ACTCTX_SECTION_KEYED_DATA data;
    BOOL ret;

    memset(&data, 0xfe, sizeof(data));
    data.cbSize = sizeof(data);

    ret = pFindActCtxSectionStringW(0, NULL,
                                    ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    libname, &data);
    ok_(__FILE__, line)(ret, "FindActCtxSectionStringW failed: %u\n", GetLastError());
    if(!ret)
    {
        skip("couldn't find %s\n",strw(libname));
        return;
    }

    ok_(__FILE__, line)(data.cbSize == sizeof(data), "data.cbSize=%u\n", data.cbSize);
    ok_(__FILE__, line)(data.ulDataFormatVersion == 1, "data.ulDataFormatVersion=%u\n", data.ulDataFormatVersion);
    ok_(__FILE__, line)(data.lpData != NULL, "data.lpData == NULL\n");
    ok_(__FILE__, line)(data.ulLength == 20, "data.ulLength=%u\n", data.ulLength);

    if (data.lpData)
    {
        struct dllredirect_keyed_data *dlldata = (struct dllredirect_keyed_data*)data.lpData;
todo_wine
        ok_(__FILE__, line)(dlldata->size == data.ulLength, "got wrong size %d\n", dlldata->size);

if (dlldata->size == data.ulLength)
{
        ok_(__FILE__, line)(dlldata->unk == 2, "got wrong field value %d\n", dlldata->unk);
        ok_(__FILE__, line)(dlldata->res[0] == 0, "got wrong res[0] value %d\n", dlldata->res[0]);
        ok_(__FILE__, line)(dlldata->res[1] == 0, "got wrong res[1] value %d\n", dlldata->res[1]);
        ok_(__FILE__, line)(dlldata->res[2] == 0, "got wrong res[2] value %d\n", dlldata->res[2]);
}
    }

    ok_(__FILE__, line)(data.lpSectionGlobalData == NULL, "data.lpSectionGlobalData != NULL\n");
    ok_(__FILE__, line)(data.ulSectionGlobalDataLength == 0, "data.ulSectionGlobalDataLength=%u\n",
       data.ulSectionGlobalDataLength);
    ok_(__FILE__, line)(data.lpSectionBase != NULL, "data.lpSectionBase == NULL\n");
    /* ok_(__FILE__, line)(data.ulSectionTotalLength == ??, "data.ulSectionTotalLength=%u\n",
       data.ulSectionTotalLength); */
    ok_(__FILE__, line)(data.hActCtx == NULL, "data.hActCtx=%p\n", data.hActCtx);
    ok_(__FILE__, line)(data.ulAssemblyRosterIndex == exid, "data.ulAssemblyRosterIndex=%u, expected %u\n",
       data.ulAssemblyRosterIndex, exid);

    memset(&data, 0xfe, sizeof(data));
    data.cbSize = sizeof(data);

    ret = pFindActCtxSectionStringW(FIND_ACTCTX_SECTION_KEY_RETURN_HACTCTX, NULL,
                                    ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    libname, &data);
    ok_(__FILE__, line)(ret, "FindActCtxSectionStringW failed: %u\n", GetLastError());
    if(!ret)
    {
        skip("couldn't find\n");
        return;
    }

    ok_(__FILE__, line)(data.cbSize == sizeof(data), "data.cbSize=%u\n", data.cbSize);
    ok_(__FILE__, line)(data.ulDataFormatVersion == 1, "data.ulDataFormatVersion=%u\n", data.ulDataFormatVersion);
    ok_(__FILE__, line)(data.lpData != NULL, "data.lpData == NULL\n");
    ok_(__FILE__, line)(data.ulLength == 20, "data.ulLength=%u\n", data.ulLength);
    ok_(__FILE__, line)(data.lpSectionGlobalData == NULL, "data.lpSectionGlobalData != NULL\n");
    ok_(__FILE__, line)(data.ulSectionGlobalDataLength == 0, "data.ulSectionGlobalDataLength=%u\n",
       data.ulSectionGlobalDataLength);
    ok_(__FILE__, line)(data.lpSectionBase != NULL, "data.lpSectionBase == NULL\n");
    /* ok_(__FILE__, line)(data.ulSectionTotalLength == ?? , "data.ulSectionTotalLength=%u\n",
       data.ulSectionTotalLength); */
    ok_(__FILE__, line)(data.hActCtx == handle, "data.hActCtx=%p\n", data.hActCtx);
    ok_(__FILE__, line)(data.ulAssemblyRosterIndex == exid, "data.ulAssemblyRosterIndex=%u, expected %u\n",
       data.ulAssemblyRosterIndex, exid);

    pReleaseActCtx(handle);
}

struct wndclass_keyed_data
{
    DWORD size;
    DWORD reserved;
    DWORD classname_len;
    DWORD classname_offset;
    DWORD modulename_len;
    DWORD modulename_offset; /* offset relative to section base */
    WCHAR strdata[1];
};

static void test_find_window_class(HANDLE handle, LPCWSTR clsname, ULONG exid, int line)
{
    struct wndclass_keyed_data *wnddata;
    ACTCTX_SECTION_KEYED_DATA data;
    BOOL ret;

    memset(&data, 0xfe, sizeof(data));
    data.cbSize = sizeof(data);

    ret = pFindActCtxSectionStringW(0, NULL,
                                    ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                    clsname, &data);
    ok_(__FILE__, line)(ret, "FindActCtxSectionStringW failed: %u\n", GetLastError());
    if(!ret)
    {
        skip("couldn't find\n");
        return;
    }

    wnddata = (struct wndclass_keyed_data*)data.lpData;

    ok_(__FILE__, line)(data.cbSize == sizeof(data), "data.cbSize=%u\n", data.cbSize);
    ok_(__FILE__, line)(data.ulDataFormatVersion == 1, "data.ulDataFormatVersion=%u\n", data.ulDataFormatVersion);
    ok_(__FILE__, line)(data.lpData != NULL, "data.lpData == NULL\n");
todo_wine
    ok_(__FILE__, line)(wnddata->size == FIELD_OFFSET(struct wndclass_keyed_data, strdata), "got %d for header size\n", wnddata->size);
    if (data.lpData && wnddata->size == FIELD_OFFSET(struct wndclass_keyed_data, strdata))
    {
        static const WCHAR verW[] = {'6','.','5','.','4','.','3','!',0};
        WCHAR buff[50];
        WCHAR *ptr;
        ULONG len;

        ok_(__FILE__, line)(wnddata->reserved == 0, "got reserved as %d\n", wnddata->reserved);
        /* redirect class name (versioned or not) is stored just after header data */
        ok_(__FILE__, line)(wnddata->classname_offset == wnddata->size, "got name offset as %d\n", wnddata->classname_offset);
        ok_(__FILE__, line)(wnddata->modulename_len > 0, "got module name length as %d\n", wnddata->modulename_len);

        /* expected versioned name */
        lstrcpyW(buff, verW);
        lstrcatW(buff, clsname);
        ptr = (WCHAR*)((BYTE*)wnddata + wnddata->size);
        ok_(__FILE__, line)(!lstrcmpW(ptr, buff), "got wrong class name %s, expected %s\n", wine_dbgstr_w(ptr), wine_dbgstr_w(buff));
        ok_(__FILE__, line)(lstrlenW(ptr)*sizeof(WCHAR) == wnddata->classname_len,
            "got wrong class name length %d, expected %d\n", wnddata->classname_len, lstrlenW(ptr));

        /* data length is simply header length + string data length including nulls */
        len = wnddata->size + wnddata->classname_len + wnddata->modulename_len + 2*sizeof(WCHAR);
        ok_(__FILE__, line)(data.ulLength == len, "got wrong data length %d, expected %d\n", data.ulLength, len);

        if (data.ulSectionTotalLength > wnddata->modulename_offset)
        {
            WCHAR *modulename, *sectionptr;

            /* just compare pointers */
            modulename = (WCHAR*)((BYTE*)wnddata + wnddata->size + wnddata->classname_len + sizeof(WCHAR));
            sectionptr = (WCHAR*)((BYTE*)data.lpSectionBase + wnddata->modulename_offset);
            ok_(__FILE__, line)(modulename == sectionptr, "got wrong name offset %p, expected %p\n", sectionptr, modulename);
        }
    }

    ok_(__FILE__, line)(data.lpSectionGlobalData == NULL, "data.lpSectionGlobalData != NULL\n");
    ok_(__FILE__, line)(data.ulSectionGlobalDataLength == 0, "data.ulSectionGlobalDataLength=%u\n",
       data.ulSectionGlobalDataLength);
    ok_(__FILE__, line)(data.lpSectionBase != NULL, "data.lpSectionBase == NULL\n");
todo_wine
    ok_(__FILE__, line)(data.ulSectionTotalLength > 0, "data.ulSectionTotalLength=%u\n",
       data.ulSectionTotalLength);
    ok_(__FILE__, line)(data.hActCtx == NULL, "data.hActCtx=%p\n", data.hActCtx);
    ok_(__FILE__, line)(data.ulAssemblyRosterIndex == exid, "data.ulAssemblyRosterIndex=%u, expected %u\n",
       data.ulAssemblyRosterIndex, exid);

    memset(&data, 0xfe, sizeof(data));
    data.cbSize = sizeof(data);

    ret = pFindActCtxSectionStringW(FIND_ACTCTX_SECTION_KEY_RETURN_HACTCTX, NULL,
                                    ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                    clsname, &data);
    ok_(__FILE__, line)(ret, "FindActCtxSectionStringW failed: %u\n", GetLastError());
    if(!ret)
    {
        skip("couldn't find\n");
        return;
    }

    ok_(__FILE__, line)(data.cbSize == sizeof(data), "data.cbSize=%u\n", data.cbSize);
    ok_(__FILE__, line)(data.ulDataFormatVersion == 1, "data.ulDataFormatVersion=%u\n", data.ulDataFormatVersion);
    ok_(__FILE__, line)(data.lpData != NULL, "data.lpData == NULL\n");
    /* ok_(__FILE__, line)(data.ulLength == ??, "data.ulLength=%u\n", data.ulLength); FIXME */
    ok_(__FILE__, line)(data.lpSectionGlobalData == NULL, "data.lpSectionGlobalData != NULL\n");
    ok_(__FILE__, line)(data.ulSectionGlobalDataLength == 0, "data.ulSectionGlobalDataLength=%u\n",
       data.ulSectionGlobalDataLength);
    ok_(__FILE__, line)(data.lpSectionBase != NULL, "data.lpSectionBase == NULL\n");
    /* ok_(__FILE__, line)(data.ulSectionTotalLength == 0, "data.ulSectionTotalLength=%u\n",
       data.ulSectionTotalLength); FIXME */
    ok_(__FILE__, line)(data.hActCtx == handle, "data.hActCtx=%p\n", data.hActCtx);
    ok_(__FILE__, line)(data.ulAssemblyRosterIndex == exid, "data.ulAssemblyRosterIndex=%u, expected %u\n",
       data.ulAssemblyRosterIndex, exid);

    pReleaseActCtx(handle);
}

static void test_find_string_fail(void)
{
    ACTCTX_SECTION_KEYED_DATA data = {sizeof(data)};
    BOOL ret;

    ret = pFindActCtxSectionStringW(0, NULL, 100, testlib_dll, &data);
    ok(!ret, "FindActCtxSectionStringW succeeded\n");
    ok(GetLastError() == ERROR_SXS_SECTION_NOT_FOUND, "GetLastError()=%u\n", GetLastError());

    ret = pFindActCtxSectionStringW(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    testlib2_dll, &data);
    ok(!ret, "FindActCtxSectionStringW succeeded\n");
    ok(GetLastError() == ERROR_SXS_KEY_NOT_FOUND, "GetLastError()=%u\n", GetLastError());

    ret = pFindActCtxSectionStringW(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    testlib_dll, NULL);
    ok(!ret, "FindActCtxSectionStringW succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetLastError()=%u\n", GetLastError());

    ret = pFindActCtxSectionStringW(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    NULL, &data);
    ok(!ret, "FindActCtxSectionStringW succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetLastError()=%u\n", GetLastError());

    data.cbSize = 0;
    ret = pFindActCtxSectionStringW(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    testlib_dll, &data);
    ok(!ret, "FindActCtxSectionStringW succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetLastError()=%u\n", GetLastError());

    data.cbSize = 35;
    ret = pFindActCtxSectionStringW(0, NULL, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION,
                                    testlib_dll, &data);
    ok(!ret, "FindActCtxSectionStringW succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "GetLastError()=%u\n", GetLastError());
}


static void test_basic_info(HANDLE handle, int line)
{
    ACTIVATION_CONTEXT_BASIC_INFORMATION basic;
    SIZE_T size;
    BOOL b;

    b = pQueryActCtxW(QUERY_ACTCTX_FLAG_NO_ADDREF, handle, NULL,
                          ActivationContextBasicInformation, &basic,
                          sizeof(basic), &size);

    ok_(__FILE__, line) (b,"ActivationContextBasicInformation failed\n");
    ok_(__FILE__, line) (size == sizeof(ACTIVATION_CONTEXT_BASIC_INFORMATION),"size mismatch\n");
    ok_(__FILE__, line) (basic.dwFlags == 0, "unexpected flags %x\n",basic.dwFlags);
    ok_(__FILE__, line) (basic.hActCtx == handle, "unexpected handle\n");

    b = pQueryActCtxW(QUERY_ACTCTX_FLAG_USE_ACTIVE_ACTCTX |
                      QUERY_ACTCTX_FLAG_NO_ADDREF, handle, NULL,
                          ActivationContextBasicInformation, &basic,
                          sizeof(basic), &size);
    if (handle)
    {
        ok_(__FILE__, line) (!b,"ActivationContextBasicInformation succeeded\n");
        ok_(__FILE__, line) (size == 0,"size mismatch\n");
        ok_(__FILE__, line) (GetLastError() == ERROR_INVALID_PARAMETER, "Wrong last error\n");
        ok_(__FILE__, line) (basic.dwFlags == 0, "unexpected flags %x\n",basic.dwFlags);
        ok_(__FILE__, line) (basic.hActCtx == handle, "unexpected handle\n");
    }
    else
    {
        ok_(__FILE__, line) (b,"ActivationContextBasicInformation failed\n");
        ok_(__FILE__, line) (size == sizeof(ACTIVATION_CONTEXT_BASIC_INFORMATION),"size mismatch\n");
        ok_(__FILE__, line) (basic.dwFlags == 0, "unexpected flags %x\n",basic.dwFlags);
        ok_(__FILE__, line) (basic.hActCtx == handle, "unexpected handle\n");
    }
}

enum comclass_threadingmodel {
    ThreadingModel_Apartment = 1,
    ThreadingModel_Free      = 2,
    ThreadingModel_Both      = 4,
    ThreadingModel_Neutral   = 5
};

enum comclass_miscfields {
    MiscStatus          = 0x1,
    MiscStatusIcon      = 0x2,
    MiscStatusContent   = 0x4,
    MiscStatusThumbnail = 0x8
};

struct comclass_keyed_data {
    DWORD size;
    BYTE reserved;
    BYTE miscmask;
    BYTE unk[2];
    DWORD model;
    GUID clsid;
    GUID unkguid;
    GUID clsid2;
    GUID tlid;
    DWORD modulename_len;
    DWORD modulename_offset;
    DWORD progid_len;
    DWORD progid_offset;
    DWORD res2[7];
    WCHAR strdata[1];
};

static void test_find_com_redirection(HANDLE handle, const GUID *clsid, const GUID *tlid, ULONG exid, int line)
{
    struct comclass_keyed_data *comclass;
    ACTCTX_SECTION_KEYED_DATA data;
    BOOL ret;

    memset(&data, 0xfe, sizeof(data));
    data.cbSize = sizeof(data);

    ret = pFindActCtxSectionGuid(0, NULL,
                                    ACTIVATION_CONTEXT_SECTION_COM_SERVER_REDIRECTION,
                                    clsid, &data);
todo_wine
    ok_(__FILE__, line)(ret, "FindActCtxSectionGuid failed: %u\n", GetLastError());
    if(!ret)
    {
        skip("couldn't find guid %s\n", debugstr_guid(clsid));
        return;
    }

    comclass = (struct comclass_keyed_data*)data.lpData;

    ok_(__FILE__, line)(data.cbSize == sizeof(data), "data.cbSize=%u\n", data.cbSize);
    ok_(__FILE__, line)(data.ulDataFormatVersion == 1, "data.ulDataFormatVersion=%u\n", data.ulDataFormatVersion);
    ok_(__FILE__, line)(data.lpData != NULL, "data.lpData == NULL\n");
    ok_(__FILE__, line)(comclass->size == FIELD_OFFSET(struct comclass_keyed_data, strdata), "got %d for header size\n", comclass->size);
    if (data.lpData && comclass->size == FIELD_OFFSET(struct comclass_keyed_data, strdata))
    {
        static const WCHAR progid[] = {'P','r','o','g','I','d','.','P','r','o','g','I','d',0};
        WCHAR *ptr;
        ULONG len;

        ok_(__FILE__, line)(comclass->reserved == 0, "got reserved as %d\n", comclass->reserved);
        ok_(__FILE__, line)(comclass->miscmask == 0, "got miscmask as %02x\n", comclass->miscmask);
        ok_(__FILE__, line)(comclass->unk[0] == 0, "got unk[0] as %02x\n", comclass->unk[0]);
        ok_(__FILE__, line)(comclass->unk[1] == 0, "got unk[1] as %02x\n", comclass->unk[1]);
        ok_(__FILE__, line)(comclass->model == ThreadingModel_Neutral, "got model %d\n", comclass->model);
        ok_(__FILE__, line)(IsEqualGUID(&comclass->clsid, clsid), "got wrong clsid %s\n", debugstr_guid(&comclass->clsid));
        ok_(__FILE__, line)(IsEqualGUID(&comclass->clsid2, clsid), "got wrong clsid2 %s\n", debugstr_guid(&comclass->clsid2));
        ok_(__FILE__, line)(IsEqualGUID(&comclass->tlid, tlid), "got wrong tlid %s\n", debugstr_guid(&comclass->tlid));
        ok_(__FILE__, line)(comclass->modulename_len > 0, "got modulename len %d\n", comclass->modulename_len);
        ok_(__FILE__, line)(comclass->progid_offset == comclass->size, "got progid offset %d\n", comclass->progid_offset);

        ptr = (WCHAR*)((BYTE*)comclass + comclass->size);
        ok_(__FILE__, line)(!lstrcmpW(ptr, progid), "got wrong progid %s, expected %s\n", wine_dbgstr_w(ptr), wine_dbgstr_w(progid));
        ok_(__FILE__, line)(lstrlenW(ptr)*sizeof(WCHAR) == comclass->progid_len,
            "got progid name length %d, expected %d\n", comclass->progid_len, lstrlenW(ptr));

        /* data length is simply header length + string data length including nulls */
        len = comclass->size + comclass->progid_len + sizeof(WCHAR);
        ok_(__FILE__, line)(data.ulLength == len, "got wrong data length %d, expected %d\n", data.ulLength, len);

        /* keyed data structure doesn't include module name, it's available from section data */
        ok_(__FILE__, line)(data.ulSectionTotalLength > comclass->modulename_offset, "got wrong offset %d\n", comclass->modulename_offset);
    }

    ok_(__FILE__, line)(data.lpSectionGlobalData != NULL, "data.lpSectionGlobalData == NULL\n");
    ok_(__FILE__, line)(data.ulSectionGlobalDataLength > 0, "data.ulSectionGlobalDataLength=%u\n",
       data.ulSectionGlobalDataLength);
    ok_(__FILE__, line)(data.lpSectionBase != NULL, "data.lpSectionBase == NULL\n");
    ok_(__FILE__, line)(data.ulSectionTotalLength > 0, "data.ulSectionTotalLength=%u\n",
       data.ulSectionTotalLength);
    ok_(__FILE__, line)(data.hActCtx == NULL, "data.hActCtx=%p\n", data.hActCtx);
    ok_(__FILE__, line)(data.ulAssemblyRosterIndex == exid, "data.ulAssemblyRosterIndex=%u, expected %u\n",
       data.ulAssemblyRosterIndex, exid);
}

static void test_wndclass_section(void)
{
    ACTCTX_SECTION_KEYED_DATA data, data2;
    ULONG_PTR cookie;
    HANDLE handle;
    BOOL ret;

    /* use two dependent manifests, each defines 2 window class redirects */
    create_manifest_file("testdep1.manifest", manifest_wndcls1, -1, NULL, NULL);
    create_manifest_file("testdep2.manifest", manifest_wndcls2, -1, NULL, NULL);
    create_manifest_file("main_wndcls.manifest", manifest_wndcls_main, -1, NULL, NULL);

    handle = test_create("main_wndcls.manifest");
    DeleteFileA("testdep1.manifest");
    DeleteFileA("testdep2.manifest");
    DeleteFileA("main_wndcls.manifest");

    ret = pActivateActCtx(handle, &cookie);
    ok(ret, "ActivateActCtx failed: %u\n", GetLastError());

    memset(&data, 0, sizeof(data));
    memset(&data2, 0, sizeof(data2));
    data.cbSize = sizeof(data);
    data2.cbSize = sizeof(data2);

    /* get data for two classes from different assemblies */
    ret = pFindActCtxSectionStringW(0, NULL,
                                    ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                    wndClass1W, &data);
    ok(ret, "got %d\n", ret);
    ret = pFindActCtxSectionStringW(0, NULL,
                                    ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION,
                                    wndClass3W, &data2);
    ok(ret, "got %d\n", ret);

    /* For both string same section is returned, meaning it's one wndclass section per context */
todo_wine
    ok(data.lpSectionBase == data2.lpSectionBase, "got %p, %p\n", data.lpSectionBase, data2.lpSectionBase);
    ok(data.ulSectionTotalLength == data2.ulSectionTotalLength, "got %u, %u\n", data.ulSectionTotalLength,
        data2.ulSectionTotalLength);

    ret = pDeactivateActCtx(0, cookie);
    ok(ret, "DeactivateActCtx failed: %u\n", GetLastError());

    pReleaseActCtx(handle);
}

static void test_actctx(void)
{
    ULONG_PTR cookie;
    HANDLE handle;
    BOOL b;

    test_create_fail();

    trace("default actctx\n");

    b = pGetCurrentActCtx(&handle);
    ok(handle == NULL, "handle = %p, expected NULL\n", handle);
    ok(b, "GetCurrentActCtx failed: %u\n", GetLastError());
    if(b) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info0, __LINE__);
        pReleaseActCtx(handle);
    }

    if(!create_manifest_file("test1.manifest", manifest1, -1, NULL, NULL)) {
        skip("Could not create manifest file\n");
        return;
    }

    trace("manifest1\n");

    handle = test_create("test1.manifest");
    DeleteFileA("test1.manifest");
    if(handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info1, __LINE__);
        test_info_in_assembly(handle, 1, &manifest1_info, __LINE__);

        if (pIsDebuggerPresent && !pIsDebuggerPresent())
        {
            /* CloseHandle will generate an exception if a debugger is present */
            b = CloseHandle(handle);
            ok(!b, "CloseHandle succeeded\n");
            ok(GetLastError() == ERROR_INVALID_HANDLE, "GetLastError() == %u\n", GetLastError());
        }

        pReleaseActCtx(handle);
    }

    if(!create_manifest_file("test2.manifest", manifest2, -1, "testdep.manifest", testdep_manifest1)) {
        skip("Could not create manifest file\n");
        return;
    }

    trace("manifest2 depmanifest1\n");

    handle = test_create("test2.manifest");
    DeleteFileA("test2.manifest");
    DeleteFileA("testdep.manifest");
    if(handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info2, __LINE__);
        test_info_in_assembly(handle, 1, &manifest2_info, __LINE__);
        test_info_in_assembly(handle, 2, &depmanifest1_info, __LINE__);
        pReleaseActCtx(handle);
    }

    if(!create_manifest_file("test2-2.manifest", manifest2, -1, "testdep.manifest", testdep_manifest2)) {
        skip("Could not create manifest file\n");
        return;
    }

    trace("manifest2 depmanifest2\n");

    handle = test_create("test2-2.manifest");
    DeleteFileA("test2-2.manifest");
    DeleteFileA("testdep.manifest");
    if(handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info2, __LINE__);
        test_info_in_assembly(handle, 1, &manifest2_info, __LINE__);
        test_info_in_assembly(handle, 2, &depmanifest2_info, __LINE__);
        test_file_info(handle, 1, 0, testlib_dll, __LINE__);
        test_file_info(handle, 1, 1, testlib2_dll, __LINE__);

        b = pActivateActCtx(handle, &cookie);
        ok(b, "ActivateActCtx failed: %u\n", GetLastError());
        test_find_dll_redirection(handle, testlib_dll, 2, __LINE__);
        test_find_dll_redirection(handle, testlib2_dll, 2, __LINE__);
        b = pDeactivateActCtx(0, cookie);
        ok(b, "DeactivateActCtx failed: %u\n", GetLastError());

        pReleaseActCtx(handle);
    }

    trace("manifest2 depmanifest3\n");

    if(!create_manifest_file("test2-3.manifest", manifest2, -1, "testdep.manifest", testdep_manifest3)) {
        skip("Could not create manifest file\n");
        return;
    }

    handle = test_create("test2-3.manifest");
    DeleteFileA("test2-3.manifest");
    DeleteFileA("testdep.manifest");
    if(handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info2, __LINE__);
        test_info_in_assembly(handle, 1, &manifest2_info, __LINE__);
        test_info_in_assembly(handle, 2, &depmanifest3_info, __LINE__);
        test_file_info(handle, 1, 0, testlib_dll, __LINE__);
        test_file_info(handle, 1, 1, testlib2_dll, __LINE__);

        b = pActivateActCtx(handle, &cookie);
        ok(b, "ActivateActCtx failed: %u\n", GetLastError());
        test_find_dll_redirection(handle, testlib_dll, 2, __LINE__);
        test_find_dll_redirection(handle, testlib2_dll, 2, __LINE__);
        test_find_window_class(handle, wndClassW, 2, __LINE__);
        test_find_window_class(handle, wndClass2W, 2, __LINE__);
        b = pDeactivateActCtx(0, cookie);
        ok(b, "DeactivateActCtx failed: %u\n", GetLastError());

        pReleaseActCtx(handle);
    }

    trace("manifest3\n");

    if(!create_manifest_file("test3.manifest", manifest3, -1, NULL, NULL)) {
        skip("Could not create manifest file\n");
        return;
    }

    handle = test_create("test3.manifest");
    DeleteFileA("test3.manifest");
    if(handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info1, __LINE__);
        test_info_in_assembly(handle, 1, &manifest3_info, __LINE__);
        test_file_info(handle, 0, 0, testlib_dll, __LINE__);

        b = pActivateActCtx(handle, &cookie);
        ok(b, "ActivateActCtx failed: %u\n", GetLastError());
        test_find_dll_redirection(handle, testlib_dll, 1, __LINE__);
        test_find_dll_redirection(handle, testlib_dll, 1, __LINE__);
        test_find_com_redirection(handle, &IID_CoTest, &IID_TlibTest, 1, __LINE__);
        test_find_string_fail();
        b = pDeactivateActCtx(0, cookie);
        ok(b, "DeactivateActCtx failed: %u\n", GetLastError());

        pReleaseActCtx(handle);
    }

    trace("manifest4\n");

    if(!create_manifest_file("test4.manifest", manifest4, -1, NULL, NULL)) {
        skip("Could not create manifest file\n");
        return;
    }

    handle = test_create("test4.manifest");
    DeleteFileA("test4.manifest");
    DeleteFileA("testdep.manifest");
    if(handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info2, __LINE__);
        test_info_in_assembly(handle, 1, &manifest4_info, __LINE__);
        test_info_in_assembly(handle, 2, &manifest_comctrl_info, __LINE__);
        pReleaseActCtx(handle);
    }

    trace("manifest1 in subdir\n");

    CreateDirectoryW(work_dir_subdir, NULL);
    if (SetCurrentDirectoryW(work_dir_subdir))
    {
        if(!create_manifest_file("..\\test1.manifest", manifest1, -1, NULL, NULL)) {
            skip("Could not create manifest file\n");
            return;
        }
        handle = test_create("..\\test1.manifest");
        DeleteFileA("..\\test1.manifest");
        if(handle != INVALID_HANDLE_VALUE) {
            test_basic_info(handle, __LINE__);
            test_detailed_info(handle, &detailed_info1, __LINE__);
            test_info_in_assembly(handle, 1, &manifest1_info, __LINE__);
            pReleaseActCtx(handle);
        }
        SetCurrentDirectoryW(work_dir);
    }
    else
        skip("Couldn't change directory\n");
    RemoveDirectoryW(work_dir_subdir);

    trace("UTF-16 manifest1, with BOM\n");
    if(!create_wide_manifest("test1.manifest", manifest1, TRUE, FALSE)) {
        skip("Could not create manifest file\n");
        return;
    }

    handle = test_create("test1.manifest");
    DeleteFileA("test1.manifest");
    if (handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info1, __LINE__);
        test_info_in_assembly(handle, 1, &manifest1_info, __LINE__);
        pReleaseActCtx(handle);
    }

    trace("UTF-16 manifest1, reverse endian, with BOM\n");
    if(!create_wide_manifest("test1.manifest", manifest1, TRUE, TRUE)) {
        skip("Could not create manifest file\n");
        return;
    }

    handle = test_create("test1.manifest");
    DeleteFileA("test1.manifest");
    if (handle != INVALID_HANDLE_VALUE) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info1, __LINE__);
        test_info_in_assembly(handle, 1, &manifest1_info, __LINE__);
        pReleaseActCtx(handle);
    }

    test_wndclass_section();
}

static void test_app_manifest(void)
{
    HANDLE handle;
    BOOL b;

    trace("child process manifest1\n");

    b = pGetCurrentActCtx(&handle);
    ok(handle == NULL, "handle != NULL\n");
    ok(b, "GetCurrentActCtx failed: %u\n", GetLastError());
    if(b) {
        test_basic_info(handle, __LINE__);
        test_detailed_info(handle, &detailed_info1_child, __LINE__);
        test_info_in_assembly(handle, 1, &manifest1_child_info, __LINE__);
        pReleaseActCtx(handle);
    }
}

static void run_child_process(void)
{
    char cmdline[MAX_PATH];
    char path[MAX_PATH];
    char **argv;
    PROCESS_INFORMATION pi;
    STARTUPINFO si = { 0 };
    HANDLE file;
    FILETIME now;
    BOOL ret;

    GetModuleFileNameA(NULL, path, MAX_PATH);
    strcat(path, ".manifest");
    if(!create_manifest_file(path, manifest1, -1, NULL, NULL)) {
        skip("Could not create manifest file\n");
        return;
    }

    si.cb = sizeof(si);
    winetest_get_mainargs( &argv );
    /* Vista+ seems to cache presence of .manifest files. Change last modified
       date to defeat the cache */
    file = CreateFileA(argv[0], FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        GetSystemTimeAsFileTime(&now);
        SetFileTime(file, NULL, NULL, &now);
        CloseHandle(file);
    }
    sprintf(cmdline, "\"%s\" %s manifest1", argv[0], argv[1]);
    ret = CreateProcess(argv[0], cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    ok(ret, "Could not create process: %u\n", GetLastError());
    winetest_wait_child_process( pi.hProcess );
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileA(path);
}

static void init_paths(void)
{
    LPWSTR ptr;
    WCHAR last;

    static const WCHAR dot_manifest[] = {'.','M','a','n','i','f','e','s','t',0};
    static const WCHAR backslash[] = {'\\',0};
    static const WCHAR subdir[] = {'T','e','s','t','S','u','b','d','i','r','\\',0};

    GetModuleFileNameW(NULL, exe_path, sizeof(exe_path)/sizeof(WCHAR));
    lstrcpyW(app_dir, exe_path);
    for(ptr=app_dir+lstrlenW(app_dir); *ptr != '\\' && *ptr != '/'; ptr--);
    ptr[1] = 0;

    GetCurrentDirectoryW(MAX_PATH, work_dir);
    last = work_dir[lstrlenW(work_dir) - 1];
    if (last != '\\' && last != '/')
        lstrcatW(work_dir, backslash);
    lstrcpyW(work_dir_subdir, work_dir);
    lstrcatW(work_dir_subdir, subdir);

    GetModuleFileNameW(NULL, app_manifest_path, sizeof(app_manifest_path)/sizeof(WCHAR));
    lstrcpyW(app_manifest_path+lstrlenW(app_manifest_path), dot_manifest);
}

static BOOL init_funcs(void)
{
    HMODULE hKernel32 = GetModuleHandle("kernel32");

#define X(f) if (!(p##f = (void*)GetProcAddress(hKernel32, #f))) return FALSE;
    X(ActivateActCtx);
    X(CreateActCtxW);
    X(DeactivateActCtx);
    X(FindActCtxSectionStringW);
    X(GetCurrentActCtx);
    X(IsDebuggerPresent);
    X(QueryActCtxW);
    X(ReleaseActCtx);
    X(FindActCtxSectionGuid);
#undef X

    return TRUE;
}

START_TEST(actctx)
{
    int argc;
    char **argv;

    argc = winetest_get_mainargs(&argv);

    if (!init_funcs())
    {
        win_skip("Needed functions are not available\n");
        return;
    }
    init_paths();

    if(argc > 2 && !strcmp(argv[2], "manifest1")) {
        test_app_manifest();
        return;
    }

    test_actctx();
    run_child_process();
}
