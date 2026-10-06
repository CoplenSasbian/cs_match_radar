#pragma once

// 5eApi 的预编译头入口。
//
// 使用独一无二的文件名（而非 pch.h），原因：宿主应用 CsMatchRadar
// 会把 ..\5eApi 加入 include 路径以直接编译本模块源码，若这里仍叫
// pch.h，宿主的同名 pch.h 会抢先命中，导致本模块缺符号。
#include "framework.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>