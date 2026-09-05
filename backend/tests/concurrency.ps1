param(
  [Parameter(Mandatory = $true)] [string]$DatabaseUrl,
  [string]$BaseUrl = 'http://127.0.0.1:8080/api',
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
)

$ErrorActionPreference = 'Stop'
$databaseName = ([Uri]$DatabaseUrl).AbsolutePath.Trim('/')
if ($databaseName -notmatch '(?i)(test|ci)') {
  throw "Concurrency test requires a disposable database whose name contains test or ci."
}

function Invoke-JsonApi {
  param([string]$Method, [string]$Path, [object]$Body, [string]$Token)
  $arguments = @{ Method = $Method; Uri = "$BaseUrl$Path"; TimeoutSec = 60 }
  if ($Token) { $arguments.Headers = @{ Authorization = "Bearer $Token" } }
  if ($null -ne $Body) {
    $arguments.ContentType = 'application/json'
    $arguments.Body = $Body | ConvertTo-Json -Compress
  }
  (Invoke-RestMethod @arguments).data
}

$login = Invoke-JsonApi POST '/auth/wechat' @{ code = 'concurrency-test' } ''
$token = $login.accessToken
$scenarios = Invoke-JsonApi GET '/scenarios' $null $token
$scenario = $scenarios.items | Where-Object { $null -eq $_.activeSession } | Select-Object -First 1
if (-not $scenario) { throw 'No idle scenario is available for the concurrency test.' }
$created = Invoke-JsonApi POST '/sessions' @{ scenarioId = $scenario.id } $token
$sessionId = $created.session.id
$clientMessageId = "concurrent-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
$content = '我理解您的担忧，想先了解您目前最关注疼痛、时间还是费用？'

try {
  $jobs = 1..20 | ForEach-Object {
    Start-Job -ScriptBlock {
      param($Url, $AccessToken, $MessageId, $MessageContent)
      Add-Type -AssemblyName System.Net.Http
      $client = [System.Net.Http.HttpClient]::new()
      $client.DefaultRequestHeaders.Authorization =
        [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $AccessToken)
      $payload = @{ clientMessageId = $MessageId; content = $MessageContent } |
        ConvertTo-Json -Compress
      $body = [System.Net.Http.StringContent]::new(
        $payload, [System.Text.Encoding]::UTF8, 'application/json')
      $response = $client.PostAsync($Url, $body).GetAwaiter().GetResult()
      $responseBody = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
      $client.Dispose()
      [pscustomobject]@{ Status = [int]$response.StatusCode; Body = $responseBody }
    } -ArgumentList "$BaseUrl/sessions/$sessionId/messages", $token, $clientMessageId, $content
  }
  $results = $jobs | Wait-Job | Receive-Job
  $jobs | Remove-Job -Force
  $unexpected = @($results | Where-Object { $_.Status -notin @(200, 409) })
  if ($unexpected.Count -gt 0) { throw "Unexpected concurrent HTTP status: $($unexpected.Status -join ',')" }
  if (@($results | Where-Object { $_.Status -eq 200 }).Count -lt 1) {
    throw 'No concurrent request completed successfully.'
  }
  foreach ($pending in @($results | Where-Object { $_.Status -eq 409 })) {
    $payload = $pending.Body | ConvertFrom-Json
    if ($payload.code -ne 'SESSION_RESPONSE_PENDING') {
      throw "Unexpected 409 code: $($payload.code)"
    }
  }

  $detail = $null
  for ($attempt = 0; $attempt -lt 40; $attempt++) {
    $detail = Invoke-JsonApi GET "/sessions/$sessionId" $null $token
    if (@($detail.messages | Where-Object { $_.role -eq 'patient' -and $_.round -eq 1 }).Count -eq 1) { break }
    Start-Sleep -Milliseconds 500
  }
  if (@($detail.messages | Where-Object { $_.role -eq 'user' -and $_.round -eq 1 }).Count -ne 1 -or
      @($detail.messages | Where-Object { $_.role -eq 'patient' -and $_.round -eq 1 }).Count -ne 1) {
    throw 'Concurrent requests did not converge to exactly one message pair.'
  }

  try {
    Invoke-JsonApi POST "/sessions/$sessionId/messages" @{
      clientMessageId = $clientMessageId
      content = '不同的内容'
    } $token | Out-Null
    throw 'Different content reused the same clientMessageId without a conflict.'
  } catch {
    if ($_.Exception.Response.StatusCode.value__ -ne 409) { throw }
  }

  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c @"
DO `$`$ BEGIN
  IF (SELECT COUNT(*) FROM messages WHERE session_id = '$sessionId' AND role = 'user' AND round = 1) <> 1 OR
     (SELECT COUNT(*) FROM messages WHERE session_id = '$sessionId' AND role = 'patient' AND round = 1) <> 1 THEN
    RAISE EXCEPTION 'database contains duplicate concurrent messages';
  END IF;
END `$`$;
"@
  if ($LASTEXITCODE -ne 0) { throw 'Database concurrency assertion failed.' }
  [pscustomobject]@{ Result = 'passed'; Requests = 20; SessionId = $sessionId } |
    ConvertTo-Json -Compress
} finally {
  if ($jobs) { $jobs | Remove-Job -Force -ErrorAction SilentlyContinue }
  & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "DELETE FROM sessions WHERE id = '$sessionId';"
}
