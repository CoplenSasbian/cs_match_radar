#pragma once

// ============================================================================
// 5eApi —— 5E 对战平台对局信息获取组件（Win32 DLL）
// ============================================================================
//
// 本文件是**公开的 C 接口**。设计要点：
//
//   1. 跨边界只传 C 类型（const wchar_t*、int32_t、int64_t、void* 句柄），
//      不传 std::wstring / std::optional / std::vector —— 那些类型
//      在不同 CRT / 编译器版本间不保证兼容。
//   2. 复杂结果用 **JSON 文本**返回，调用方按需解析。
//   3. 所有返回的字符串都由 DLL 分配，必须用 FiveEFreeString 释放。
//   4. 同步接口内部等待异步完成；C++ 调用方建议改用头文件里的
//      轻封装 FiveEApi.hpp，它把结果还原成强类型模型。
//
// 能力：
//   - 用户 ID -> uuid
//   - 对局列表（可自动翻页）
//   - 单场对局详情（队伍、玩家、分段统计）
//   - 玩家资料（ELO / 段位 / 排名 / 信用分 / 赛季统计）
//
// 网络要求：调用方应用清单需要 internetClient 能力。

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MY5EAPI_EXPORTS) || defined(FIVEEAPI_EXPORTS)
#define FIVEE_API __declspec(dllexport)
#else
#define FIVEE_API __declspec(dllimport)
#endif

#define FIVEE_CALL __stdcall

// ---- 错误码 ----
typedef enum FiveEStatus
{
    FiveEStatus_Ok = 0,
    FiveEStatus_InvalidArgument = 1,   // 参数为空 / 非法
    FiveEStatus_Network = 2,           // 连接失败或超时
    FiveEStatus_Http = 3,              // 非 2xx
    FiveEStatus_Parse = 4,             // JSON 解析失败
    FiveEStatus_Business = 5,          // 业务返回码非成功
    FiveEStatus_NotFound = 6,          // 数据为空（如玩家不存在）
    FiveEStatus_Aborted = 7,           // 调用方取消
    FiveEStatus_Internal = 8,          // 其他内部错误
} FiveEStatus;

// ---- 生命周期 ----

// 创建客户端。返回的句柄由 FiveEClientDestroy 释放。
// minRequestIntervalMs <= 0 时使用默认值（250ms）。
FIVEE_API void* FIVEE_CALL FiveEClientCreate(int32_t minRequestIntervalMs);

// 销毁客户端。传 nullptr 安全。
FIVEE_API void FIVEE_CALL FiveEClientDestroy(void* client);

// 设置可选的 Bearer 令牌。传 nullptr / 空串表示清除。
FIVEE_API void FIVEE_CALL FiveEClientSetBearerToken(void* client, const wchar_t* token);

// ---- 业务接口 ----
// 这些函数会阻塞调用线程直到完成。UI 线程请放到工作线程调用，
// 或使用头文件里的异步封装。
// 成功时 *outJson 收到新分配的字符串，需用 FiveEFreeString 释放。

// 用户 ID（domain，如 "11814738gjdwn7"）-> uuid。
FIVEE_API FiveEStatus FIVEE_CALL FiveEGetUuid(
    void* client,
    const wchar_t* userId,
    wchar_t** outUuid);

// 对局列表，返回 JSON 数组文本（已解析的摘要字段 + raw 原始字段）。
//
// queryJson 可为 nullptr 表示用默认值。字段：
//   { "uuid": "...", "page": 1, "limit": 30,
//     "startTime": 0, "endTime": 0, "matchType": -1, "csType": 0,
//     "maxPages": 5 }
// maxPages > 1 时自动翻页并合并。
FIVEE_API FiveEStatus FIVEE_CALL FiveEGetMatchList(
    void* client,
    const wchar_t* queryJson,
    wchar_t** outJson);

// 单场对局详情，返回 JSON 对象文本。
FIVEE_API FiveEStatus FIVEE_CALL FiveEGetMatchDetail(
    void* client,
    const wchar_t* matchId,
    wchar_t** outJson);

// 玩家资料，返回 JSON 对象文本（uinfo / career / elo_info / season_data）。
FIVEE_API FiveEStatus FIVEE_CALL FiveEGetPlayerProfile(
    void* client,
    const wchar_t* uuid,
    wchar_t** outJson);

// 原始 HTTP：取任意 5E 接口的响应文本，便于排查字段变更。
FIVEE_API FiveEStatus FIVEE_CALL FiveEFetchRaw(
    void* client,
    const wchar_t* url,
    wchar_t** outJson);

// ---- 字符串与错误 ----

// 按昵称 / 5E ID 搜索玩家。
// 返回 JSON：{ "list": [ { "username": "...", "domain": "...", "uuid": "..." } ] }
// domain 即 5E 用户 ID；uuid 已通过 idTransfer 补齐，可直接用于查对局。
FIVEE_API FiveEStatus FIVEE_CALL FiveESearchPlayer(
    const wchar_t* keyword,
    wchar_t** outJson);
// 释放 DLL 返回的字符串。传 nullptr 安全。
FIVEE_API void FIVEE_CALL FiveEFreeString(wchar_t* text);

// 最近一次失败的可读描述。属于 DLL 内部缓冲，**不要释放**。
FIVEE_API const wchar_t* FIVEE_CALL FiveEGetLastError(void);

// 组件版本（形如 L"1.1.0"）。静态字符串，不要释放。
FIVEE_API const wchar_t* FIVEE_CALL FiveEGetVersion(void);

#ifdef __cplusplus
} // extern "C"
#endif
