#pragma once

// ============================================================================
// 对局雷达：实时对局的共享模型 + 状态/封禁判定
// ============================================================================
//
// 这一层是"实时对局"的中枢：5E（CDP 抓 comet WS）与完美（tail 日志）两种
// 采集源都把自己解析出的对局归一化成这里的结构，雷达界面只认这套模型。
//
// 设计要点：
//   - 跨 DLL / 跨线程只传 C++ 值类型（std::wstring / optional / vector），
//     不传 5E 或完美各自的模型定义，避免编译期耦合。
//   - 10 名玩家按 team 1 / team 2 分组；每名玩家聚合两平台的
//     段位、游玩总时间、以及"状态/封禁"提示。
//   - 封禁数据来源：5E 用 credit / trusted_status 代理；完美用账号状态。
//     参考项目确认两平台都没有真正的"封禁历史"接口，因此这里统一成
//     一个"风险等级"提示，而不是断言某人是封号。

#include <optional>
#include <string>
#include <vector>

namespace LiveMatch
{
    // 平台标识（与 5E / 完美区分开，雷达内部自洽）。
    enum class Platform
    {
        Unknown = 0,
        FiveE,
        Perfect,
    };

    // 单个平台暴露给雷达的"该玩家在此平台的状态"。
    struct PlatformStanding
    {
        bool available = false;                 // 是否查到该平台资料
        std::wstring rankText;                  // 段位展示文本（如 "2500" / "钻石 S 32★"）
        std::optional<int32_t> rankScore;       // 数值（5E elo / 完美 score），用于排序或对比
        std::optional<int64_t> playtimeSeconds; // 游玩总时间（秒），缺失则为 nullopt
        // 账号层面状态：Banned（异常/封禁），Caution（可疑/低信用），Normal（正常）。
        enum class Risk
        {
            Unknown = 0,
            Normal,
            Caution,
            Banned,
        };
        Risk risk = Risk::Unknown;
        std::wstring riskText;                  // 状态文案（"正常" / "低信用" / "账号异常"）
    };

    // 雷达上的一名玩家：聚合两平台信息。
    struct RadarPlayer
    {
        std::wstring name;                      // 展示名（优先昵称）
        int32_t team = 1;                       // 1 或 2
        bool isMe = false;                      // 是否为本机登录账号（自己）

        // 平台定位信息：点开"5E 页面" / "完美页面"需要知道查谁。
        std::wstring fiveEUuid;                 // 5E 玩家 UUID（查 player/home 用）
        std::wstring fiveEUserId;               // 5E 数字/域名 ID（查对局用）
        std::wstring perfectSteamId;            // 完美 SteamID64（查资料/对局用）

        PlatformStanding fiveE;
        PlatformStanding perfect;
    };

    // 一局正在进行的对局。
    struct LiveMatch
    {
        Platform source = Platform::Unknown;    // 这一局来自哪个平台
        std::wstring gameId;                    // 5E game_ctx.id / 完美场次标识
        std::wstring mapName;                   // 地图（接受阶段可能为"Arena"或空）
        std::wstring startedAt;                 // 人类可读的开始时间
        std::vector<RadarPlayer> team1;         // 5 人
        std::vector<RadarPlayer> team2;         // 5 人

        bool IsValid() const { return team1.size() == 5 && team2.size() == 5; }
    };

    // ---- 风险等级汇总（界面用）----

    enum class RiskLevel
    {
        Unknown = 0,
        Normal,
        Caution,
        Banned,
    };

    // 把两平台的 Risk 收敛成一个"该玩家总体风险"。
    // 任一平台为 Banned 则整体 Banned；否则任一 Caution 则 Caution；
    // 都 Normal 才 Normal；都没数据则 Unknown。
    inline RiskLevel CombineRisk(
        PlatformStanding::Risk a, PlatformStanding::Risk b)
    {
        if (a == PlatformStanding::Risk::Banned ||
            b == PlatformStanding::Risk::Banned)
        {
            return RiskLevel::Banned;
        }
        if (a == PlatformStanding::Risk::Caution ||
            b == PlatformStanding::Risk::Caution)
        {
            return RiskLevel::Caution;
        }
        if (a == PlatformStanding::Risk::Normal ||
            b == PlatformStanding::Risk::Normal)
        {
            return RiskLevel::Normal;
        }
        return RiskLevel::Unknown;
    }

    inline std::wstring RiskBadgeText(RiskLevel level)
    {
        switch (level)
        {
        case RiskLevel::Normal:   return L"正常";
        case RiskLevel::Caution:  return L"注意";
        case RiskLevel::Banned:   return L"异常";
        default:                  return L"—";
        }
    }

    // 风险对应的主题画刷键（自动适配浅色/深色）。
    inline std::wstring RiskBrushKey(RiskLevel level)
    {
        switch (level)
        {
        case RiskLevel::Normal:   return L"SystemFillColorSuccessBrush";
        case RiskLevel::Caution:  return L"SystemFillColorCautionBrush";
        case RiskLevel::Banned:   return L"SystemFillColorCriticalBrush";
        default:                  return L"SystemFillColorNeutralBrush";
        }
    }

    // 把秒数格式化为"1,234 小时" / "320 分钟"。
    inline std::wstring FormatPlaytime(std::optional<int64_t> const& seconds)
    {
        if (!seconds.has_value() || *seconds <= 0)
        {
            return L"—";
        }
        const int64_t totalMinutes = *seconds / 60;
        if (totalMinutes < 60)
        {
            return std::to_wstring(totalMinutes) + L" 分钟";
        }
        const int64_t hours = totalMinutes / 60;
        const int64_t minutes = totalMinutes % 60;
        if (minutes == 0)
        {
            return std::to_wstring(hours) + L" 小时";
        }
        return std::to_wstring(hours) + L" 小时 " + std::to_wstring(minutes) + L" 分";
    }

    // 把 5E 的 credit / trustedStatus 收敛成单平台风险。
    // credit 满分 100000，阈值可调；trustedStatus 非 1 视为异常。
    inline PlatformStanding::Risk FiveECreditToRisk(
        std::optional<int32_t> const& credit,
        std::optional<int32_t> const& trustedStatus,
        int32_t creditThreshold = 80000)
    {
        if (!credit.has_value() && !trustedStatus.has_value())
        {
            return PlatformStanding::Risk::Unknown;
        }
        if (trustedStatus.has_value() && trustedStatus != 1)
        {
            return PlatformStanding::Risk::Banned;
        }
        if (credit.has_value() && *credit < creditThreshold)
        {
            return PlatformStanding::Risk::Caution;
        }
        return PlatformStanding::Risk::Normal;
    }

    // 把完美账号状态收敛成单平台风险。
    // 0 通常表示正常；非 0（如封禁/冻结）视为异常。
    inline PlatformStanding::Risk PerfectStatusToRisk(
        std::optional<int32_t> const& accountStatus)
    {
        if (!accountStatus.has_value())
        {
            return PlatformStanding::Risk::Unknown;
        }
        if (*accountStatus == 0)
        {
            return PlatformStanding::Risk::Normal;
        }
        return PlatformStanding::Risk::Banned;
    }
}
