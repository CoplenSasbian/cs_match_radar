# 5E 鉴权机制实测报告

实测时间：2026-10-06，本机（未安装 5E 客户端、未登录）

## 结论速览

| 端点 | 方法 | 是否需要鉴权 | 实测结果 |
|---|---|---|---|
| `gate.5eplay.com/crane/http/api/data/match/{code}` | GET | **否** | ✅ 200，可匿名获取 |
| `gate.5eplay.com/crane/http/api/data/match/list` | GET | 否 | ✅ 200（但只有已结束对局） |
| `platform-api.5eplay.com/api/user/info` | POST | **是（登录态）** | ❌ `code:10000 请登录后进行此操作` |
| `gate.5eplay.com/cranenew/.../player/elo/info/batch` | POST | **是（需要签名+上下文）** | ❌ 401 / Invalid Signature |
| `gate.5eplay.com/cranenew/.../player/map-ext/batch` | POST | **是** | ❌ 401 |
| `gate.5eplay.com/cranenew/.../v3/player/home?uuid=` | GET | **是（HMAC 签名）** | ✅ 200，签名验证通过 |

## HMAC 签名算法（已验证可用）

密钥（硬编码在 5E 前端 `view-arena` chunk 43711 的 `production.HMAC_SECRET`）：

```
28137ed61f21705e2cddf498e22fae8f
```

### Canonical String 构造

```
METHOD \n
Accept \n
Content-MD5 \n
Content-Type \n
Date \n
[签名头块] \n
PathAndParameters
```

要点：
- 第 2 行固定 `*/*`
- 第 3~5 行（Content-MD5 / Content-Type / Date）**留空**
- 签名头块 = 按**小写字母序**排列的 `name:value`，`\n` 连接
  实际只签两个头：`accept-language:zh-cn` 和 `authorization:`（值为空）
- 最后一行为 path，query 需**按 key 排序**后用 `&` 拼接

### 计算

```
signature = Base64(HMAC-SHA256(secret, canonical))
```

### 必需请求头

```http
x-ca-key: 5eplay
x-ca-signature-method: HmacSHA256
x-ca-signature-headers: Accept-Language,Authorization
x-ca-signature: <base64>
Accept: */*
Accept-Language: zh-cn
Authorization: <空>
Referer: https://view-arena.5eplay.com/
Origin: https://view-arena.5eplay.com
```

### 已验证的测试向量

```
GET /cranenew/http/api/data/v3/player/home?uuid=ed954be0-7222-11ee-9ce2-ec0d9a495494
→ V+81D/A51CzX6Azk0HbElYwxZfLiquWykkaT6k+Z8Io=
```

本机复现该签名，且实际请求返回 **HTTP 200**，返回体含：
`career`（elo/level_id/rank/match_total/match_win）、`elo_info.modes["9"]`、
`disguise`（头像）等。

## C++ 实现的两个关键坑（已解决）

### 坑 1：`Authorization` 头不能真的发送

WinRT 的 `HttpRequestHeaderCollection` **拒绝写入空值**（E_INVALIDARG）。

实测结论：
- canonical string 里 `authorization:` 必须按**空值**参与签名
- 但该头**不要真的发出去** —— 完全不发送时服务器返回 **200**
- 所以 WinRT 的拒绝正好与正确行为一致，**直接不发送即可**

### 坑 2：必须用 `HttpRequestMessage`，不能用 `client.DefaultRequestHeaders()`

这是最初签名被服务器拒绝（`400 Invalid Signature`）的真正原因。

```cpp
// ✗ 错误：签名会被服务器拒绝
auto headers = client.DefaultRequestHeaders();
headers.Append(L"x-ca-signature", sig);

// ✓ 正确：每个请求独立构造
HttpRequestMessage request(HttpMethod::Get(), Uri(fullUrl));
auto rh = request.Headers();
rh.Append(L"x-ca-signature", sig);
const auto response = co_await client.SendRequestAsync(request);
```

推测原因：`DefaultRequestHeaders` 会被 WinRT 追加/规范化部分头，
或与其他默认头组合后改变了服务器看到的请求形态。

### 实测通过的请求头集合

```http
Referer: https://view-arena.5eplay.com/
Origin: https://view-arena.5eplay.com
x-ca-signature: <base64>
x-ca-signature-method: HmacSHA256
x-ca-signature-headers: Accept-Language,Authorization
x-ca-key: 5eplay
Accept: */*
Accept-Language: zh-cn
```

（`Authorization` 按上文**不发送**；`User-Agent` 可省略。）

## 已验证可用的接口

| 端点 | 方法 | 鉴权 | 返回内容 |
|---|---|---|---|
| `/cranenew/http/api/data/v3/player/home?uuid=` | GET | HMAC 签名 | 玩家全量资料（见下） |

`player/home` 的 `data` 顶层字段：

| 字段 | 内容 |
|---|---|
| `uinfo` | 昵称、头像、domain、**credit 信用分**、trusted_score/status、注册时间 |
| `career` | 生涯总览：elo、level_id、rank、match_total/win/loss/tie |
| `elo_info.modes["9"]` | 当前赛季：elo、level_id、rank、season、star_num |
| `season_data` | 赛季统计：avg_rating、avg_adr、avg_rws、kast、per_headshot、mvp_total、多杀/残局明细等 70+ 字段 |
| `disguise` | 头像（含伪装头像） |
| `plusinfo` | Plus 会员信息 |

**这已经覆盖了对局雷达所需的核心玩家信息。**

## POST batch 端点的困境

`elo/info/batch`、`map-ext/batch`、`user/info` 三个**玩家批量接口**：

- 尝试 1：不带签名 → **401 Unauthorized**
- 尝试 2：用同一套 HMAC 签名（Content-MD5 留空） → **400 Invalid Signature**
- 尝试 3：把 `Content-MD5` 纳入签名 → **400 Invalid Signature**

**推测原因**：这些接口的签名规则与 GET 不同（可能需要对 body 做不同处理，
或需要额外的签名头），且**很可能还需要登录态 Cookie/Token**。

## 参考项目是怎么做的

关键发现：参考项目**并不自己调用这些 POST 接口**，而是：

1. `platform_5e_launch.rs`：以 `--remote-debugging-port=9222` 启动 5EClient
2. `platform_5e_cdp.rs`：通过 CDP 订阅网络事件，**被动读取**
   5E 客户端自己发出的 batch 请求的 `postData` 和响应体
3. 因此**不需要自己构造签名**——签名由 5E 客户端完成

代码证据：
```rust
// platform_5e_cdp.rs:807
let post = req.and_then(|r| r.get("postData"))
```
`build_gate_signed_headers` 只被 `platform_5e_player_home.rs` 使用（GET 那个端点）。

## 对本项目的影响

| 方案 | 可行性 | 说明 |
|---|---|---|
| **A. 纯 HTTP + 自签名** | 部分可行 | 只有 `player/home`(GET) 能用；三个 batch 接口不行 |
| **B. CDP 抓包（参考项目做法）** | 可行，但重 | 需要启动客户端 + WebSocket + CDP 协议实现 |
| **C. 启动 5E 后借其登录态** | 需进一步验证 | 若客户端已登录，其 Cookie 可能可用于直连 batch |

**待验证**：方案 C —— 安装并登录 5E 客户端后，能否用其 Cookie/Token
直接调用 batch 接口。本机未安装客户端，无法验证。

## 完美世界

参考项目文档（`docs/response_wm.md`）提到响应是**加密**的
（"## 2. 加密响应"），且需要账号登录（二维码 / Steam）。
Rust 侧有 `perfect_crypto.rs`（9.8 KB）负责解密。
同样需要实机验证，本机未安装。
