param(
  [Parameter(Mandatory = $true)] [string]$DatabaseUrl,
  [string]$BaseUrl = 'http://127.0.0.1:8080/api',
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
)

$ErrorActionPreference = 'Stop'
$databaseName = ([Uri]$DatabaseUrl).AbsolutePath.Trim('/')
if ($databaseName -notmatch '(?i)(test|ci)') {
  throw 'State-machine test requires a disposable database whose name contains test or ci.'
}
Add-Type -AssemblyName System.Net.Http
$script:Token = ''
$createdSessionIds = [System.Collections.Generic.List[string]]::new()
$createdRoleplayIds = [System.Collections.Generic.List[string]]::new()

function Invoke-TestApi {
  param([string]$Method, [string]$Path, [object]$Body)
  $client = [System.Net.Http.HttpClient]::new()
  try {
    if ($script:Token) {
      $client.DefaultRequestHeaders.Authorization =
        [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $script:Token)
    }
    $request = [System.Net.Http.HttpRequestMessage]::new(
      [System.Net.Http.HttpMethod]::new($Method), "$BaseUrl$Path")
    if ($null -ne $Body) {
      $json = $Body | ConvertTo-Json -Compress -Depth 8
      $request.Content = [System.Net.Http.StringContent]::new(
        $json, [System.Text.Encoding]::UTF8, 'application/json')
    }
    $response = $client.SendAsync($request).GetAwaiter().GetResult()
    $content = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    [pscustomobject]@{
      Status = [int]$response.StatusCode
      Payload = if ($content) { $content | ConvertFrom-Json } else { $null }
    }
  } finally {
    $client.Dispose()
  }
}

function Assert-ApiCode {
  param($Response, [int]$Status, [object]$Code)
  if ($Response.Status -ne $Status -or $Response.Payload.code -ne $Code) {
    throw "Expected HTTP $Status / $Code, got $($Response.Status) / $($Response.Payload.code)"
  }
}

function Invoke-Sql {
  param([string]$Sql)
  $output = & $PsqlPath $DatabaseUrl -v ON_ERROR_STOP=1 -X -Atc $Sql
  if ($LASTEXITCODE -ne 0) { throw 'SQL assertion/setup failed.' }
  ($output -join "`n").Trim()
}

try {
  $health = Invoke-TestApi GET '/health' $null
  Assert-ApiCode $health 200 0
  if ($health.Payload.data.modelConfigured) {
    throw 'Refusing state-machine test while a model key is configured.'
  }
  $login = Invoke-TestApi POST '/auth/wechat' @{ code = 'state-machine-test' }
  Assert-ApiCode $login 200 0
  $script:Token = $login.Payload.data.accessToken
  $scenarios = Invoke-TestApi GET '/scenarios' $null
  Assert-ApiCode $scenarios 200 0
  $scenarioId = $scenarios.Payload.data.items[0].id

  $created = Invoke-TestApi POST '/sessions' @{ scenarioId = $scenarioId }
  Assert-ApiCode $created 201 0
  $sessionId = $created.Payload.data.session.id
  $createdSessionIds.Add($sessionId)
  $messageId = "state-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $content = '我理解您的担忧，想先了解您最关注的问题。'

  $failedReply = Invoke-TestApi POST "/sessions/$sessionId/messages" @{
    clientMessageId = $messageId; content = $content
  }
  Assert-ApiCode $failedReply 503 'MODEL_NOT_CONFIGURED'
  $conflict = Invoke-TestApi POST "/sessions/$sessionId/messages" @{
    clientMessageId = $messageId; content = '不同内容'
  }
  Assert-ApiCode $conflict 409 'IDEMPOTENCY_CONFLICT'
  $detail = Invoke-TestApi GET "/sessions/$sessionId" $null
  Assert-ApiCode $detail 200 0
  if ($detail.Payload.data.pendingMessage.replyStatus -ne 'failed') {
    throw 'Failed reply was not exposed as safely retryable input.'
  }

  Invoke-Sql "UPDATE messages SET reply_status='generating', reply_lease_until=NOW()+INTERVAL '180 seconds', reply_attempt_token='test-lease' WHERE session_id='$sessionId' AND role='user' AND round=1" | Out-Null
  $pending = Invoke-TestApi POST "/sessions/$sessionId/messages" @{
    clientMessageId = $messageId; content = $content
  }
  Assert-ApiCode $pending 409 'SESSION_RESPONSE_PENDING'

  $patientId = "state_patient_$([Guid]::NewGuid().ToString('N'))"
  Invoke-Sql "UPDATE messages SET reply_status='ready', reply_lease_until=NULL, reply_attempt_token=NULL WHERE session_id='$sessionId' AND role='user' AND round=1; INSERT INTO messages(id,session_id,role,content,round) VALUES('$patientId','$sessionId','patient','test patient reply',1); UPDATE sessions SET current_round=1 WHERE id='$sessionId';" | Out-Null
  $finished = Invoke-TestApi POST "/sessions/$sessionId/finish" @{ reason = 'manual' }
  Assert-ApiCode $finished 202 0
  $repeated = Invoke-TestApi POST "/sessions/$sessionId/finish" @{ reason = 'manual' }
  Assert-ApiCode $repeated 202 0
  Start-Sleep -Milliseconds 500
  $jobBeforeRetry = Invoke-Sql "SELECT COUNT(*)||':'||MAX(generation) FROM ai_jobs WHERE dedupe_key='evaluation:$sessionId'"
  if ($jobBeforeRetry -ne '1:1') { throw "Repeated finish changed task identity: $jobBeforeRetry" }

  $evaluation = $null
  for ($attempt = 0; $attempt -lt 20; $attempt++) {
    $evaluation = Invoke-TestApi GET "/sessions/$sessionId/evaluation" $null
    if ($evaluation.Payload.data.status -eq 'failed') { break }
    Start-Sleep -Milliseconds 100
  }
  if ($evaluation.Payload.data.status -ne 'failed') { throw 'Unconfigured model task did not fail immediately.' }
  $retry = Invoke-TestApi POST "/sessions/$sessionId/evaluation/retry" @{}
  Assert-ApiCode $retry 202 0
  Start-Sleep -Milliseconds 500
  $jobAfterRetry = Invoke-Sql "SELECT COUNT(*)||':'||MAX(generation) FROM ai_jobs WHERE dedupe_key='evaluation:$sessionId'"
  if ($jobAfterRetry -ne '1:2') { throw "Manual retry did not open a new task generation: $jobAfterRetry" }

  $active = Invoke-TestApi POST '/sessions' @{ scenarioId = $scenarioId }
  Assert-ApiCode $active 201 0
  $abandonedId = $active.Payload.data.session.id
  $createdSessionIds.Add($abandonedId)
  $restart = Invoke-TestApi POST "/sessions/$abandonedId/restart" @{}
  Assert-ApiCode $restart 201 0
  $createdSessionIds.Add($restart.Payload.data.session.id)
  $abandonedFinish = Invoke-TestApi POST "/sessions/$abandonedId/finish" @{}
  Assert-ApiCode $abandonedFinish 409 'SESSION_ABANDONED'

  $roleplayScenarios = Invoke-TestApi GET '/roleplay/scenarios' $null
  Assert-ApiCode $roleplayScenarios 200 0
  $roleplay = Invoke-TestApi POST '/roleplay/sessions' @{
    scenarioId = $roleplayScenarios.Payload.data.items[0].id
  }
  Assert-ApiCode $roleplay 201 0
  $roleplayAbandonedId = $roleplay.Payload.data.session.id
  $createdRoleplayIds.Add($roleplayAbandonedId)
  $roleplayRestart = Invoke-TestApi POST "/roleplay/sessions/$roleplayAbandonedId/restart" @{}
  Assert-ApiCode $roleplayRestart 201 0
  $createdRoleplayIds.Add($roleplayRestart.Payload.data.session.id)
  $roleplayAbandonedFinish = Invoke-TestApi POST "/roleplay/sessions/$roleplayAbandonedId/finish" @{}
  Assert-ApiCode $roleplayAbandonedFinish 409 'ROLEPLAY_SESSION_ABANDONED'

  [pscustomobject]@{
    Result = 'passed'
    IdempotencyConflict = $conflict.Payload.code
    PendingCode = $pending.Payload.code
    EvaluationGeneration = 2
    TrainingAbandonedCode = $abandonedFinish.Payload.code
    RoleplayAbandonedCode = $roleplayAbandonedFinish.Payload.code
  } | ConvertTo-Json -Compress
} finally {
  if ($createdSessionIds.Count -gt 0) {
    $ids = ($createdSessionIds | ForEach-Object { "'$_'" }) -join ','
    Invoke-Sql "DELETE FROM ai_jobs WHERE target_id IN ($ids); DELETE FROM sessions WHERE id IN ($ids);" | Out-Null
  }
  if ($createdRoleplayIds.Count -gt 0) {
    $ids = ($createdRoleplayIds | ForEach-Object { "'$_'" }) -join ','
    Invoke-Sql "DELETE FROM ai_jobs WHERE target_id IN ($ids); DELETE FROM roleplay_sessions WHERE id IN ($ids);" | Out-Null
  }
}
