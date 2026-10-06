#pragma once

// PerfectWorldApi 的预编译头入口。
//
// 与 5eApi 用同样策略：使用独一无二的文件名（而非 pch.h），
// 因为宿主应用 CsMatchRadar 会把本目录加入 include 路径以直接编译
// 本模块源码，若仍叫 pch.h，宿主的同名 pch.h 会抢先命中导致缺符号。
#include "framework.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
