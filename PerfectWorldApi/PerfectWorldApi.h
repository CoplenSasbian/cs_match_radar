#pragma once

// ============================================================================
// PerfectWorldApi —— 完美世界对战平台玩家查询组件（Win32 DLL）
// ============================================================================
//
// 公开的 C 接口。与 5eApi 同样的约定：
//   - 只传 C 类型，复杂结果用 JSON 文本
//   - 返回的字符串必须用 PerfectFreeString 释放
//   - 同步阻塞接口
//
// 与 5E 的关键差异：完美平台的接口**需要登录凭据**
// （access_token + 登录账号自身 SteamID），且响应是 AES 加密的
// （解密在 DLL 内部完成，调用方拿到的已经是明文 JSON）。
//
// 登录态失效时返回 PerfectStatus_AuthExpired。

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(PERFECTWORLDAPI_EXPORTS) || defined(PERFECTAPI_EXPORTS)
#define PERFECT_API __declspec(dllexport)
#else
#define PERFECT_API __declspec(dllimport)
#endif

#define PERFECT_CALL __stdcall

// ---- 错误码 ----
typedef enum PerfectStatus
{
    PerfectStatus_Ok = 0,
    PerfectStatus_InvalidArgument = 1,  // 参数为空 / 非法
    PerfectStatus_Network = 2,          // 连接失败或超时
    PerfectStatus_Http = 3,             // 非 2xx
    PerfectStatus_Decrypt = 4,          // AES 解密失败（算法或字段变更）
    PerfectStatus_Parse = 5,            // JSON 解析失败
    PerfectStatus_Business = 6,         // 业务返回码非成功
    PerfectStatus_AuthExpired = 7,      // 登录态失效（code 1002 / 1033）
    PerfectStatus_Internal = 8,         // 其他内部错误
} PerfectStatus;

// ---- 生命周期 ----

// 创建客户端。返回的句柄由 PerfectClientDestroy 释放。
PERFECT_API void* PERFECT_CALL PerfectClientCreate(void);

// 销毁客户端。传 nullptr 安全。
PERFECT_API void PERFECT_CALL PerfectClientDestroy(void* client);

// ---- 业务接口 ----
// 阻塞当前线程直到完成。成功时 *outJson 收到新分配的 JSON 文本。

// 查询玩家资料（overview + season-stats 合并结果）。
//
// 参数：
//   steamId        目标玩家 SteamID64
//   accessToken    登录凭据
//   selfSteamId    登录账号自身的 SteamID64（作为 PwaSteamId 请求头）
//
// 返回 JSON 结构：
//   { "identity": {...}, "currentRank": {...}, "peakRank": {...},
//     "ladder": {...}, "radar": [...], "maps": [...], "weapons": [...],
//     "seasonWe": 12.7,
//     "partialFailure": false, "failureReason": "" }
//
// 注意：season-stats 失败时仍返回 overview 数据，并把 partialFailure 置 true。
PERFECT_API PerfectStatus PERFECT_CALL PerfectGetPlayer(
    void* client,
    const wchar_t* steamId,
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    wchar_t** outJson);

// 只查 overview（较快的轻量查询，用于列表批量拉取）。
PERFECT_API PerfectStatus PERFECT_CALL PerfectGetOverview(
    void* client,
    const wchar_t* steamId,
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    wchar_t** outJson);

// 查询赛季号列表（不加密接口），返回 JSON 数组文本。
PERFECT_API PerfectStatus PERFECT_CALL PerfectGetSeasonList(
    void* client,
    const wchar_t* steamId,
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    wchar_t** outJson);

// ---- 工具 ----

// 申请扫码登录二维码。
//
// 成功后 *outJson 收到：
//   { "accessUrl": "https://news.wmpvp.com?...&accessToken=...&...",
//     "pollToken": "<轮询凭据，来自 accessUrl 的 accessToken 参数>" }
// accessUrl 用于生成二维码图片供用户扫描；pollToken 传给
// PerfectPollQrLogin 轮询状态。
//
// 二维码 5 分钟过期，过期后需重新调用本接口。
PERFECT_API PerfectStatus PERFECT_CALL PerfectApplyQrLogin(
    wchar_t** outJson);

// 轮询一次扫码状态。
//
// 返回 JSON：
//   { "phase": "WaitingScan" | "Scanned" | "Confirmed" | "Expired",
//     "accessToken": "...",    // 仅 Confirmed 时非空（40 位字母数字）
//     "selfSteamId": "...",    // 仅 Confirmed 时非空（17 位数字）
//     "message": "" }
//
// 调用方每 2 秒调用一次；返回 Confirmed 后即可用 accessToken /
// selfSteamId 调用其它业务接口。Expired 时需重新申请二维码。
PERFECT_API PerfectStatus PERFECT_CALL PerfectPollQrLogin(
    const wchar_t* pollToken,
    wchar_t** outJson);

// 校验既有凭据是否仍有效（用于启动时自动登录）。
// 有效时 *outValid 置 true；失效（1002 / 1033）置 false。
PERFECT_API PerfectStatus PERFECT_CALL PerfectValidateCredentials(
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    int32_t* outValid);

// ---- 比赛记录 ----
// 这两个接口用 token 请求头鉴权，并要求 mySteamId 与 token 属于同一账号；
// toSteamId 才是"要查谁"，因此可以用自己的登录态查询任意玩家。
// 响应为明文 JSON，无需解密。

// 查询指定玩家的最近对局列表。
//
// 参数：
//   toSteamId      要查询的玩家 SteamID64
//   accessToken    登录 token
//   selfSteamId    token 所属账号的 SteamID64
//   dataSource     1=国服竞技 2=国际服竞技 3=完美天梯
//   page/pageSize  分页（page=1,pageSize=5 表示取最近 5 场）
//   seasonId       "recent" 或 S25 等
//
// 返回 JSON：
//   { "total": 20, "list": [ { "matchId": "...", "mapName": "...",
//     "score1": 13, "score2": 7, "kill": 23, "rating": 1.54, ... } ] }
PERFECT_API PerfectStatus PERFECT_CALL PerfectGetMatchList(
    const wchar_t* toSteamId,
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    int32_t dataSource,
    int32_t page,
    int32_t pageSize,
    const wchar_t* seasonId,
    wchar_t** outJson);

// 查询单场比赛详情（含全部 10 名玩家）。
//
// 参数：
//   matchId     形如 PVP@9210321939064225676
//   dataSource  与列表查询保持一致
//
// 返回 JSON：
//   { "base": { "map": "...", "score1": 13, "score2": 7, ... },
//     "players": [ { "nickName": "...", "team": 1, "kill": 23, ... }, ... ] }
PERFECT_API PerfectStatus PERFECT_CALL PerfectGetMatchDetail(
    const wchar_t* matchId,
    int32_t dataSource,
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    wchar_t** outJson);

// 按关键字搜索玩家。keyword 可以是完美昵称、完美编号或完整 SteamID，
// 完全匹配的结果会排在第一个。
//
// 返回 JSON：
//   { "list": [ { "steamId": "7656...", "pvpNickName": "...",
//     "pvpAvatar": "...", "appNickName": "..." } ] }
PERFECT_API PerfectStatus PERFECT_CALL PerfectSearchUser(
    const wchar_t* keyword,
    const wchar_t* accessToken,
    wchar_t** outJson);
// 拉取玩家完整资料（段位、本季战绩、累计、常用武器、常用地图、趋势）。
// 返回 JSON：{ "profile": { "name": "...", "steamId": "...", "pvpScore": 2249, ... },
//            "weapons": [ ... ], "maps": [ ... ] }
PERFECT_API PerfectStatus PERFECT_CALL PerfectGetPlayerProfile(
    const wchar_t* steamId,
    const wchar_t* accessToken,
    const wchar_t* selfSteamId,
    wchar_t** outJson);
// 解密一段完美平台响应（便于离线排查）。
// 传入原始响应体，返回解密后的 JSON 文本；非加密的错误响应原样返回。
PERFECT_API PerfectStatus PERFECT_CALL PerfectDecryptResponse(
    const wchar_t* responseBody,
    wchar_t** outJson);

// 释放 DLL 返回的字符串。传 nullptr 安全。
PERFECT_API void PERFECT_CALL PerfectFreeString(wchar_t* text);

// 最近一次失败描述。属于 DLL 内部缓冲，**不要释放**。
PERFECT_API const wchar_t* PERFECT_CALL PerfectGetLastError(void);

// 组件版本。静态字符串，不要释放。
PERFECT_API const wchar_t* PERFECT_CALL PerfectGetVersion(void);

#ifdef __cplusplus
} // extern "C"
#endif
