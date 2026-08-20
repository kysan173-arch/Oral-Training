param(
  [string]$BaseUrl = 'http://127.0.0.1:8080/api',
  [string]$WechatCode = 'controlled-smoke-code',
  [switch]$WithModel
)

$ErrorActionPreference = 'Stop'

function Invoke-Api {
  param(
    [ValidateSet('GET', 'POST')] [string]$Method,
    [string]$Path,
    [object]$Body
  )

  $args = @{ Method = $Method; Uri = "$BaseUrl$Path"; TimeoutSec = 45 }
  if ($script:AccessToken) { $args.Headers = @{ Authorization = "Bearer $script:AccessToken" } }
  if ($null -ne $Body) {
    $args.ContentType = 'application/json'
    $args.Body = $Body | ConvertTo-Json -Compress -Depth 8
  }
  $response = Invoke-RestMethod @args
  if ($response.code -ne 0) { throw "API failed: $($response.code) $($response.message)" }
  return $response.data
}

$health = Invoke-Api GET '/health' $null
if (-not $health.database) { throw 'Database health check failed.' }
if (-not $health.workerRunning) { throw 'AI worker is not running.' }
if ($null -eq $health.pendingJobs -or $null -eq $health.deadJobs) { throw 'Worker health counters are missing.' }

$login = Invoke-Api POST '/auth/wechat' @{ code = $WechatCode }
if ([string]::IsNullOrWhiteSpace($login.accessToken)) { throw 'Login did not return an access token.' }
$script:AccessToken = $login.accessToken

$scenarioData = Invoke-Api GET '/scenarios' $null
if ($scenarioData.items.Count -ne 4) { throw "Expected 4 scenarios, got $($scenarioData.items.Count)." }
if ($scenarioData.items[0].PSObject.Properties.Name -contains 'hidden') { throw 'Scenario API leaked hidden configuration.' }
$roleplayScenarioData = Invoke-Api GET '/roleplay/scenarios' $null
if ($roleplayScenarioData.items.Count -ne 4) { throw "Expected 4 roleplay scenarios, got $($roleplayScenarioData.items.Count)." }
if ($roleplayScenarioData.items[0].PSObject.Properties.Name -contains 'serviceGuidance') { throw 'Roleplay scenario API leaked service guidance.' }
if ($roleplayScenarioData.items[0].suggestedQuestions.Count -lt 3) { throw 'Roleplay scenario suggestions are missing.' }
$dashboard = Invoke-Api GET '/dashboard/summary' $null
if ($null -eq $dashboard.scenarioStats -or $dashboard.scenarioStats.Count -ne 4) { throw 'Dashboard scenario statistics are invalid.' }
if ($null -eq $dashboard.dimensionAverages) { throw 'Dashboard dimension averages are missing.' }

if (-not $WithModel) {
  [pscustomobject]@{
    Result = 'passed'
    Database = $health.database
    ModelConfigured = $health.modelConfigured
    ScenarioCount = $scenarioData.items.Count
    RoleplayScenarioCount = $roleplayScenarioData.items.Count
    ModelTest = 'skipped'
  } | ConvertTo-Json -Compress
  exit 0
}

if (-not $health.modelConfigured) { throw 'DEEPSEEK_API_KEY is not configured in the backend process.' }
$scenario = $scenarioData.items | Where-Object { $null -eq $_.activeSession } | Select-Object -First 1
if ($null -eq $scenario) { throw 'No idle scenario is available. Finish or restart an active session before model smoke testing.' }
$activeRoleplayScenarios = @($roleplayScenarioData.items | Where-Object { $null -ne $_.activeSession })
if ($activeRoleplayScenarios.Count -gt 0) { throw 'Finish or restart all active roleplay sessions before the four-scenario model smoke test.' }

$created = Invoke-Api POST '/sessions' @{ scenarioId = $scenario.id }
$sessionId = $created.session.id
$message = Invoke-Api POST "/sessions/$sessionId/messages" @{
  clientMessageId = "smoke-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  content = 'I understand your concern. Which matters most to you: pain, time, or cost?'
}
if ([string]::IsNullOrWhiteSpace($message.patientMessage.content)) { throw 'Patient model returned an empty reply.' }

$null = Invoke-Api POST "/sessions/$sessionId/finish" @{ reason = 'manual' }
$evaluation = $null
for ($attempt = 0; $attempt -lt 20; $attempt++) {
  Start-Sleep -Seconds 2
  $evaluation = Invoke-Api GET "/sessions/$sessionId/evaluation" $null
  if ($evaluation.status -eq 'ready') { break }
  if ($evaluation.status -eq 'failed') { throw 'Evaluation generation failed.' }
}
if ($null -eq $evaluation -or $evaluation.status -ne 'ready') { throw 'Evaluation did not become ready within 40 seconds.' }
if (@($evaluation.evaluation.dimensionScores.PSObject.Properties).Count -ne 5) { throw 'Evaluation does not contain five dimensions.' }

$roleplayRuns = @()
foreach ($roleplayScenario in $roleplayScenarioData.items) {
  $roleplayCreated = Invoke-Api POST '/roleplay/sessions' @{ scenarioId = $roleplayScenario.id }
  $roleplaySessionId = $roleplayCreated.session.id
  $questions = @($roleplayScenario.suggestedQuestions)
  $firstQuestion = if ($questions.Count -gt 0) { $questions[0] } else { '我想了解一下这个场景的服务流程。' }
  $secondQuestion = if ($questions.Count -gt 1) { $questions[1] } else { '如果需要进一步确认，我应该如何安排？' }
  $roleplayClientMessageId = "roleplay-smoke-$($roleplayScenario.id)-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
  $roleplayMessage = Invoke-Api POST "/roleplay/sessions/$roleplaySessionId/messages" @{
    clientMessageId = $roleplayClientMessageId
    content = $firstQuestion
  }
  if ([string]::IsNullOrWhiteSpace($roleplayMessage.standardCustomerMessage.content)) { throw "Standard customer model returned an empty reply for $($roleplayScenario.id)." }
  if ($roleplayMessage.standardCustomerMessage.learningPoints.Count -lt 2 -or
      [string]::IsNullOrWhiteSpace($roleplayMessage.standardCustomerMessage.complianceBoundary)) {
    throw "Standard customer response is missing learning points or compliance boundary for $($roleplayScenario.id)."
  }
  $roleplayRetry = Invoke-Api POST "/roleplay/sessions/$roleplaySessionId/messages" @{
    clientMessageId = $roleplayClientMessageId
    content = $firstQuestion
  }
  if ($roleplayRetry.standardCustomerMessage.id -ne $roleplayMessage.standardCustomerMessage.id) {
    throw "Roleplay message retry created a duplicate customer response for $($roleplayScenario.id)."
  }
  $roleplaySecond = Invoke-Api POST "/roleplay/sessions/$roleplaySessionId/messages" @{
    clientMessageId = "roleplay-smoke-2-$($roleplayScenario.id)-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
    content = $secondQuestion
  }
  if ($roleplaySecond.session.currentRound -ne 2) { throw "Roleplay did not advance to a second round for $($roleplayScenario.id)." }
  $roleplayRuns += [pscustomobject]@{
    SessionId = $roleplaySessionId
    ScenarioId = $roleplayScenario.id
    ReplyLength = $roleplayMessage.standardCustomerMessage.content.Length
    Summary = $null
  }
}

foreach ($roleplayRun in $roleplayRuns) {
  $null = Invoke-Api POST "/roleplay/sessions/$($roleplayRun.SessionId)/finish" @{ reason = 'manual' }
}
for ($attempt = 0; $attempt -lt 20; $attempt++) {
  $readyCount = 0
  foreach ($roleplayRun in $roleplayRuns) {
    $summary = Invoke-Api GET "/roleplay/sessions/$($roleplayRun.SessionId)/summary" $null
    if ($summary.status -eq 'failed') { throw "Roleplay summary generation failed for $($roleplayRun.ScenarioId)." }
    if ($summary.status -eq 'ready') {
      if ($summary.summary.keyPrinciples.Count -lt 2 -or $summary.summary.nextPracticeSuggestions.Count -lt 1) {
        throw "Roleplay summary structure is incomplete for $($roleplayRun.ScenarioId)."
      }
      $roleplayRun.Summary = $summary.summary
      $readyCount++
    }
  }
  if ($readyCount -eq $roleplayRuns.Count) { break }
  Start-Sleep -Seconds 2
}
if (@($roleplayRuns | Where-Object { $null -eq $_.Summary }).Count -gt 0) {
  throw 'One or more roleplay summaries did not become ready within 40 seconds.'
}

[pscustomobject]@{
  Result = 'passed'
  Database = $health.database
  ScenarioCount = $scenarioData.items.Count
  SessionId = $sessionId
  PatientReplyLength = $message.patientMessage.content.Length
  TotalScore = $evaluation.evaluation.totalScore
  RoleplayScenarioCount = $roleplayRuns.Count
  RoleplaySessionIds = ($roleplayRuns.SessionId -join ',')
  StandardCustomerReplyLengths = ($roleplayRuns.ReplyLength -join ',')
  RoleplaySummaryTopicCounts = (($roleplayRuns | ForEach-Object { $_.Summary.coveredTopics.Count }) -join ',')
  ModelTest = 'passed'
} | ConvertTo-Json -Compress
