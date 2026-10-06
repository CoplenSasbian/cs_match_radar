# CS 对局雷达 —— 参考项目调研笔记

调研对象：[qianjiachun/cs-match-helper](https://github.com/qianjiachun/cs-match-helper)（MIT）
本地路径：`D:\cs_match_radar\reference\cs-match-helper`（浅克隆，仅参考）

## 核心结论

**历史对局接口拿不到"当前正在进行的对局"。** 这正是你的判断，参考项目的做法也证实了这点：

- 5E 的 `/crane/http/api/data/match/list` 只返回**已结束**的对局；
  正在进行的对局在结束前不会出现在列表里。
- 因此参考项目**完全不走历史接口来发现当前对局**，而是从
  **客户端本地的实时数据源**里抓。

## 两个平台的数据来源完全不同

### 5E：CDP（Chrome DevTools Protocol）抓包

`5EClient.exe` 是 Electron 应用，支持 `--remote-debugging-port`。

```
1. 结束已有 5EClient.exe 进程
2. 以 5EClient.exe --remote-debugging-port=9222 启动（需管理员权限，失败则 runas 提权）
3. 通过 CDP 连上该端口
4. 订阅 Network 事件，被动抓取：
   - HTTP 响应：user/info、elo/info（含 specialData.match_data → match_code）、地图扩展
   - WebSocket 帧：匹配/准备阶段推送的对局事件
5. 从抓到的 match_code 再调 fetch5eMatchDetail 补地图等详情
```

关键代码位置（参考项目）：
- `src-tauri/src/platform/platform_5e_launch.rs` —— 启动与提权（`--remote-debugging-port`）
- `src-tauri/src/platform/platform_5e_cdp.rs` —— CDP 采集（34 KB）
- `src-tauri/src/platform/platform_5e_ws.rs` —— WebSocket 帧解析
- `src/platforms/5e/events.ts` —— URL 分类、UUID 提取、NDJSON 解析
- `src/platforms/5e/match-api.ts` —— 用 match_code 补详情 + 校验（UUID 集合必须完全一致）
- `src/platforms/5e/field-mapper.ts` —— 字段映射（14 KB）

注意：`specialData` 是**嵌套的 JSON 字符串**，需要二次解析才能拿到 `match_data[].match_id`。

启动参数（实测逻辑）：
- 创建标志：`CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS | CREATE_NO_WINDOW`
- 端口：默认 9222，占用时递增扫描
- 提权：`ShellExecuteExW` + `runas`；用户取消返回 1223

### 完美世界：本地日志文件

```
日志目录：%APPDATA%\Wmpvp\Log
文件名：  pvpClient.YYYY-MM-DD.log
```

- `src-tauri/src/log_watcher.rs` —— 目录轮询 + 增量 tail
- `src/platforms/perfect/log-parser.ts` —— 解析日志行提取对局/玩家
- `src/platforms/perfect/match-parser.ts`（14 KB）
- `src/platforms/perfect/player-api.ts`（18 KB）—— 玩家资料查询
- `src/platforms/perfect/auth.ts` + `perfect_auth.rs` —— 账号认证

完美平台的认证方式（两种）：
- 二维码：`passport.pwesports.cn/qrAuth/applyToken` + `/qrAuth/check`
- Steam 登录：`passport.pwesports.cn/steam/login` → 回调 `pvp.wanmei.com/csgo/pwaSteam`
- 凭据持久化，并有加解密（`perfect_crypto.rs`、`perfect_credentials.rs`）

## 与"对局雷达"业务目标的对应关系

你要的功能 → 参考项目里对应的机制：

| 需求 | 机制 |
|---|---|
| 监控当前对局 | 5E：CDP 抓 WS/HTTP；完美：tail 日志 |
| 获取 10 名玩家 | CDP 抓到的对局详情 / 日志里解析出的玩家 ID |
| 各平台段位 | 5E `level_info`；完美 `player-api`（需登录态） |
| 是否有封禁 | 未在参考项目里发现明确实现，需要另找数据源 |
| 各平台历史记录 | 5E `match/list`（已结束的）；完美 `player-api` |
| 玩家评论 | 走第三方服务 `fkbuff.com`（见下） |

## 第三方评论服务（可选，但很有用）

`docs/cs-match-helper-api.md` 定义了一套公开评论 API：

- 域名：`https://fkbuff.com`，前缀 `/api/v1/cs-match-helper/`
- 全部 POST，JSON
- 认证：除 `comment/batch` 外，需 Header `x-cs-client-key` = **sha256(clientId)**，64 位小写十六进制
- SteamID 格式：`^7656\d{13}$`
- 关键接口：
  - `comment/batch` —— 批量查多个玩家的评论数（**无需鉴权**，适合对局准备页）
  - `comment/list` —— 某玩家的顶级评论（支持 `time` / `hot` 排序，游标分页）
  - `comment/reply/list`、`comment/add`、`comment/update`、`comment/like`、`comment/history`

**这个可以直接复用**：对局雷达里"这个队友有没有被吐槽过"是一条很有价值的信息。

## 其他可复用的模块

| 参考项目文件 | 价值 |
|---|---|
| `docs/response_5e.md`（51 KB） | 5E 真实响应样本，字段比我自己抓的更全 |
| `docs/response_wm.md`（12 KB） | 完美世界响应样本 |
| `src/core/match/history/normalize.ts`（15 KB） | 把两平台数据归一化成统一模型 |
| `src/mainview/components/*` | 雷达图、队伍对比的界面参考 |

## 技术栈差异（需要注意）

参考项目：**Tauri + Vue + Rust**（Web 前端 + Rust 后端）
当前项目：**C++/WinRT + WinUI3**

CDP 抓包、日志 tail 这些在 C++ 里都能做，但工作量大：
- CDP 需要 WebSocket 客户端 + JSON 处理（WinRT 有 `Windows.Networking.Sockets`）
- 提权启动需要 `ShellExecuteExW` + `runas`
- 日志 tail 用 `ReadDirectoryChangesW` 或轮询

## 5E 实时对局的精确机制（最重要的一节）

来源：`docs/response_5e.md` §1.0 "Comet WebSocket 定锚（生产路径）"，含 2026-07-10 实机样本。

### 数据链路

```
5EClient.exe --remote-debugging-port=9222
        ↓ CDP 订阅 Network.webSocket*
wss://comet-client-arena.5eplay.com/     ← 只采集这个域名
        ↓ 解帧
game_ctx 帧 → 定锚 gameId + 10 个 UUID
        ↓
P5eMatchSession 与 HTTP batch 合并 → MatchRecord
```

### 帧编码（`platform_5e_ws.rs`）

| opcode | 载荷 | 处理 |
|---|---|---|
| 1 | 文本 | 直接解析；若整段是 Base64 再解一层 |
| 2 | Base64 二进制 | 解码后通常是 **约 12 字节协议头 + JSON** |
| 8 / 9 | 控制帧 | 跳过 |

**关键陷阱**：有效 JSON **不一定从偏移 0 开始**。必须实现括号/字符串感知的
`find_json_slice`，从首个 `{` 或 `[` 开始截取。

### `game_ctx` 帧结构（定锚依据）

```jsonc
{
  "game_ctx": {
    "id": "g161-n-20260710122439267141317",   // ← gameId，会话主键
    "status": 0,
    "gmi": {
      "map": 0,
      "selected_map": 0,                       // 数字地图 ID，接受前常为 0
      "t1": { "rooms": [ { "id": "...", "members": ["uuid", ...] } ] },  // 展平后须 5 人
      "t2": { "rooms": [ { "id": "...", "members": ["uuid", ...] } ] }   // 展平后须 5 人
    }
  },
  "ready_members": ["uuid", ...]               // 仅状态，不能用来确定参赛者
}
```

**定锚条件**（全部满足才算一局开始）：
1. `gameId` 非空
2. 两队各 5 人
3. 10 个 UUID 唯一且符合 GUID 格式

### 帧类型（`eventHint`，由顶层 key 推断）

| eventHint | 含义 | 能否定 10 人局 |
|---|---|---|
| `game_ctx` | 对局定锚（匹配成功） | **能**（权威） |
| `game_ctx_update` | 同局增量（接受人数、地图候选） | 更新已有局 |
| `room_ctx` | 当前开黑房间 | **不能**（只是开黑房成员） |

### 补全与校验

- 主键 `gameId`；HTTP batch 保留最近约 **15 秒**环形缓冲，定锚后回放按 UUID 合并
- 三类数据（user / elo / map-ext）各需 **10/10** 覆盖才算完成；超时 30s 标记 `incomplete`
- 数据源优先级：**game_ctx（权威）> matching/batch 请求体 > selected_map 表 > 未知**
- `GET match/{gameId}` 补详情时，**响应内 10 个 UUID 必须与 WS 定锚集合完全一致**，否则丢弃
- 地图冲突时优先 Gate 的 `main.map`，并记入 `parseWarnings`

### 已知运维问题

5E 客户端**自更新后会自行重启且不带调试参数**，表现为进程还在但 CDP 不可达。
collector 45 秒后上报 `needsRelaunch`，自动执行一次 `stop → launch → start`。

### HTTP batch 接口（与 WS 配合）

- map-ext/batch、elo/info/batch、user/info/batch
- 关联键是**玩家 UUID**（`76af4325-dd04-11ed-9ce2-ec0d9a495494`），不是数字 uid
- `specialData` 是**嵌套 JSON 字符串**，需二次解析
- `match/matching/batch` 请求体里带 `game_map` + `t1_uuids`/`t2_uuids`，可作地图回退来源

## 待确认

1. **是否只做 5E**，还是完美世界也要？（完美需要登录态 + 解密，复杂度高很多）
2. **"是否有封禁"的数据源**在哪？参考项目里没找到明确实现。
3. **是否复用 fkbuff 评论接口**（第三方服务，依赖其可用性）。
4. **本机未安装 5E / 完美客户端**，因此 CDP 与日志方案我都**无法实机验证**，
   只能依据参考项目文档复刻。
