#pragma once

// 唯一版本来源；tools/Set-Version.ps1 显式修改，构建固定版本时不改源码。
#define YUVRAW_VERSION_MAJOR 1
#define YUVRAW_VERSION_MINOR 0
#define YUVRAW_VERSION_PATCH 1
#define YUVRAW_STRINGIZE_INNER(Value) #Value
#define YUVRAW_STRINGIZE(Value) YUVRAW_STRINGIZE_INNER(Value)
#define YUVRAW_VERSION_STRING YUVRAW_STRINGIZE(YUVRAW_VERSION_MAJOR) "." YUVRAW_STRINGIZE(YUVRAW_VERSION_MINOR) "." YUVRAW_STRINGIZE(YUVRAW_VERSION_PATCH)

#ifndef RC_INVOKED
#include <cstdint>
namespace FAppVersion
{
    constexpr int32_t Major = YUVRAW_VERSION_MAJOR;
    constexpr int32_t Minor = YUVRAW_VERSION_MINOR;
    constexpr int32_t Patch = YUVRAW_VERSION_PATCH;
    constexpr const char* String =
        YUVRAW_STRINGIZE(YUVRAW_VERSION_MAJOR) "."
        YUVRAW_STRINGIZE(YUVRAW_VERSION_MINOR) "."
        YUVRAW_STRINGIZE(YUVRAW_VERSION_PATCH);
}

#endif
