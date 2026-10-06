# 用 .NET 的 AES 实现独立生成完美平台解密测试向量。
#
# 目的：验证 C++ 解密实现是否正确。
# 如果加密和解密都用同一份代码，测试就是自证循环；
# 这里用 .NET 的 System.Security.Cryptography 独立加密，
# 再交给 C++ 解密比对，才能真正验证算法一致性。

$ErrorActionPreference = 'Stop'

$prefix = 'G#r%*VCDYj6P5$mny0838MhH8d'   # 26 字符
$t      = "569525"                       # 6 位，使 key 恰好 32 字节（26 + 6 = 32）
$key    = $prefix + $t

if ($key.Length -ne 32) { throw "密钥长度应为 32，实际 $($key.Length)（前缀 $($prefix.Length) + t $($t.Length)）" }

# 待加密的明文（模拟 overview 响应解密后的内容）
$plaintext = '{"user":{"steamId":"76561199667272550","name":"测试玩家","zqId":"abc123"},"matchmaking":{"score":2380}}'

$keyBytes = [Text.Encoding]::UTF8.GetBytes($key)
$plainBytes = [Text.Encoding]::UTF8.GetBytes($plaintext)

$aes = [System.Security.Cryptography.Aes]::Create()
$aes.Mode = [System.Security.Cryptography.CipherMode]::ECB
$aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
$aes.Key = $keyBytes

$encryptor = $aes.CreateEncryptor()
$cipher = $encryptor.TransformFinalBlock($plainBytes, 0, $plainBytes.Length)
$encryptor.Dispose()
$aes.Dispose()

$base64 = [Convert]::ToBase64String($cipher)

# 组装成真实的外层结构
$envelope = [ordered]@{
    code = 0
    data = [ordered]@{ e = $base64; t = $t }
    msg  = ''
}
$envelopeJson = $envelope | ConvertTo-Json -Compress -Depth 5

$outDir = Join-Path $PSScriptRoot '..\..\tests\vectors'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$envelopePath = Join-Path $outDir 'perfect_envelope.json'
$expectedPath = Join-Path $outDir 'perfect_expected.json'

# 用无 BOM UTF-8 写出，避免解析端受影响
[IO.File]::WriteAllText($envelopePath, $envelopeJson, (New-Object Text.UTF8Encoding $false))
[IO.File]::WriteAllText($expectedPath, $plaintext, (New-Object Text.UTF8Encoding $false))

Write-Host "密钥       : $key ($($key.Length) 字符)"
Write-Host "密文(Base64): $base64"
Write-Host "envelope   -> $envelopePath"
Write-Host "expected   -> $expectedPath"
