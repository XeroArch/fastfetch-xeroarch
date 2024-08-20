#include "editor.h"
#include "common/processing.h"
#include "util/stringUtils.h"
#include "util/path.h"
#include "util/binary.h"

#include <stdlib.h>

#ifdef _WIN32
static inline char* realpath(const char* restrict file_name, char* restrict resolved_name)
{
    return _fullpath(resolved_name, file_name, _MAX_PATH);
}
#endif

#if __linux__ || __FreeBSD__
static bool extractNvimVersion(const char* str, uint32_t len, void* userdata)
{
    if (len < strlen("NVIM v0.0.0")) return true;
    if (!ffStrStartsWith(str, "NVIM v")) return true;
    ffStrbufSetS((FFstrbuf*) userdata, str + strlen("NVIM v"));
    return false;
}

static bool extractVimVersion(const char* str, uint32_t len, void* userdata)
{
    if (len < strlen("VIM - Vi IMproved 0.0")) return true;
    if (!ffStrStartsWith(str, "VIM - Vi IMproved ")) return true;
    ffStrbufSetS((FFstrbuf*) userdata, str + strlen("VIM - Vi IMproved "));
    ffStrbufSubstrBeforeFirstC(userdata, ' ');
    return false;
}

static bool extractNanoVersion(const char* str, uint32_t len, void* userdata)
{
    if (len < strlen("GNU nano 0.0")) return true;
    if (!ffStrStartsWith(str, "GNU nano ")) return true;
    ffStrbufSetS((FFstrbuf*) userdata, str + strlen("GNU nano "));
    return false;
}
#endif

const char* ffDetectEditor(FFEditorResult* result)
{
    ffStrbufSetS(&result->name, getenv("VISUAL"));
    if (result->name.length)
        result->type = "Visual";
    else
    {
        ffStrbufSetS(&result->name, getenv("EDITOR"));
        if (result->name.length)
            result->type = "Editor";
        else
            return "$VISUAL or $EDITOR not set";
    }

    if (ffIsAbsolutePath(result->name.chars))
        ffStrbufSet(&result->path, &result->name);
    else
    {
        const char* error = ffFindExecutableInPath(result->name.chars, &result->path);
        if (error) return error;
    }

    if (!instance.config.general.detectVersion) return NULL;

    char buf[PATH_MAX + 1];
    if (!realpath(result->path.chars, buf))
        return NULL;

    ffStrbufSetS(&result->path, buf);

    {
        uint32_t index = ffStrbufLastIndexC(&result->path,
            #ifndef _WIN32
            '/'
            #else
            '\\'
            #endif
        );
        if (index == result->path.length)
            return NULL;
        ffStrbufSetS(&result->exe, &result->path.chars[index + 1]);
        if (!result->exe.length)
            return NULL;

        #ifdef _WIN32
        if (ffStrbufEndsWithS(&result->exe, ".exe"))
            ffStrbufSubstrBefore(&result->exe, result->exe.length - 4);
        #endif
    }

    #if __linux__ || __FreeBSD__
    if (ffStrbufEqualS(&result->exe, "nvim"))
        ffBinaryExtractStrings(buf, extractNvimVersion, &result->version);
    else if (ffStrbufEqualS(&result->exe, "vim"))
        ffBinaryExtractStrings(buf, extractVimVersion, &result->version);
    else if (ffStrbufEqualS(&result->exe, "nano"))
        ffBinaryExtractStrings(buf, extractNanoVersion, &result->version);

    if (result->version.length > 0) return NULL;
    #endif

    const char* param = NULL;
    if (
        ffStrbufEqualS(&result->exe, "nano") ||
        ffStrbufEqualS(&result->exe, "vim") ||
        ffStrbufEqualS(&result->exe, "nvim") ||
        ffStrbufEqualS(&result->exe, "micro") ||
        ffStrbufEqualS(&result->exe, "emacs") ||
        ffStrbufStartsWithS(&result->exe, "emacs-") || // emacs-29.3
        ffStrbufEqualS(&result->exe, "hx") ||
        ffStrbufEqualS(&result->exe, "code") ||
        ffStrbufEqualS(&result->exe, "pluma") ||
        ffStrbufEqualS(&result->exe, "sublime_text")
    ) param = "--version";
    else if (
        ffStrbufEqualS(&result->exe, "kak") ||
        ffStrbufEqualS(&result->exe, "pico")
    ) param = "-version";
    else if (
        ffStrbufEqualS(&result->exe, "ne")
    ) param = "-h";
    else return NULL;

    ffProcessAppendStdOut(&result->version, (char* const[]){
        result->path.chars,
        (char*) param,
        NULL,
    });

    if (result->version.length == 0)
        return NULL;

    ffStrbufSubstrBeforeFirstC(&result->version, '\n');
    for (uint32_t iStart = 0; iStart < result->version.length; ++iStart)
    {
        char c = result->version.chars[iStart];
        if (ffCharIsDigit(c))
        {
            for (uint32_t iEnd = iStart + 1; iEnd < result->version.length; ++iEnd)
            {
                char c = result->version.chars[iEnd];
                if (isspace(c))
                {
                    ffStrbufSubstrBefore(&result->version, iEnd);
                    break;
                }
            }
            if (iStart > 0)
                ffStrbufSubstrAfter(&result->version, iStart - 1);
            break;
        }
    }

    return NULL;
}
