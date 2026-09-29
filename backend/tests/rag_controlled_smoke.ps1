param(
  [string]$BaseUrl = 'http://127.0.0.1:8080/api',
  [Parameter(Mandatory)][string]$ServiceId,
  [Parameter(Mandatory)][string]$ScenarioId,
  [Parameter(Mandatory)][string]$ServiceDraftId,
  [Parameter(Mandatory)][string]$KnowledgeDraftId,
  [Parameter(Mandatory)][string]$LearnerAnswer,
  [Parameter(Mandatory)][ValidateRange(1,100)][int]$MaxHttpCalls,
  [Parameter(Mandatory)][string]$OutputPath,
  [switch]$ReviewedInputs
)
$ErrorActionPreference = 'Stop'
# Intentionally never run by CI. Tokens come from this process's environment, not command arguments.
if (-not $ReviewedInputs) { throw 'Reviewed synthetic inputs and completed N06 manual gates are required.' }
if (-not $env:RAG_SMOKE_ADMIN_TOKEN -or -not $env:RAG_SMOKE_LEARNER_TOKEN) { throw 'Set RAG_SMOKE_ADMIN_TOKEN and RAG_SMOKE_LEARNER_TOKEN locally.' }
if (Test-Path -LiteralPath $OutputPath) { throw 'Use a new output path for this single batch.' }
$outputDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) { throw 'Create the output directory before running the batch.' }
$batch = [guid]::NewGuid().ToString('N')
$record = [ordered]@{ batchId=$batch; status='running'; maxHttpCalls=$MaxHttpCalls; serviceId=$ServiceId; scenarioId=$ScenarioId; steps=@() }
function Invoke-BatchApi([string]$Method, [string]$Path, $Body=$null, [switch]$Admin) {
  $token = if ($Admin) { $env:RAG_SMOKE_ADMIN_TOKEN } else { $env:RAG_SMOKE_LEARNER_TOKEN }
  $args = @{ Method=$Method; Uri="$BaseUrl$Path"; TimeoutSec=90; Headers=@{ Authorization="Bearer $token"; 'Idempotency-Key'="$batch-$($record.steps.Count)" } }
  if ($null -ne $Body) { $args.ContentType='application/json; charset=utf-8'; $args.Body=$Body | ConvertTo-Json -Depth 30 -Compress }
  $response = Invoke-RestMethod @args
  if ($response.code -ne 0) { throw 'API returned a failure; inspect request IDs in server logs.' }
  return $response.data
}
function Wait-Batch([string]$Path,[string]$Ready,[switch]$Admin) {
  for ($i=0; $i -lt 90; $i++) {
    $value = Invoke-BatchApi GET $Path -Admin:$Admin
    if ($value.status -eq $Ready) { return $value }
    if ($value.status -in @('failed','dead')) { throw "Task failed at $Path; this batch does not request retries." }
    Start-Sleep -Seconds 2
  }
  throw "Polling deadline exceeded at $Path; do not blindly repeat the batch."
}
try {
  $health = Invoke-BatchApi GET '/health'
  if (-not $health.ready -or -not $health.rag.roleplayNewSessions -or -not $health.rag.patientNewSessions) { throw 'Ready backend and both v2 creation gates required.' }
  if ($health.modelCallLimit -ne $MaxHttpCalls -or $health.modelCallCount -ne 0) { throw 'Use a fresh isolated backend with matching MODEL_CALL_LIMIT and zero calls.' }
  if ($health.pendingJobs -ne 0 -or $health.knowledgePendingJobs -ne 0) { throw 'The controlled backend must have no outstanding jobs.' }
  foreach ($draft in @(@{kind='service_draft';id=$ServiceDraftId},@{kind='knowledge_draft';id=$KnowledgeDraftId})) {
    $job = Invoke-BatchApi POST '/admin/knowledge/generation-jobs' @{
      kind=$draft.kind; draftId=$draft.id; count=1; brief='仅生成模拟演示候选草稿，未知信息保持 unknown，不编造真实诊所或医学来源。'
    } -Admin
    $step = @{stage=$draft.kind;jobId=$job.jobId;status='pending'}
    $record.steps += $step
    $result = Wait-Batch "/admin/knowledge/generation-jobs/$($job.jobId)" 'succeeded' -Admin
    $step.status=$result.status; $step.modelVersion=$result.modelVersion
    $step.promptVersion=$result.promptVersion; $step.attempts=$result.attempts
  }
  # Generated drafts are never published. Training uses already reviewed, published synthetic versions.
  $training = Invoke-BatchApi POST '/sessions' @{scenarioId=$ScenarioId;serviceId=$ServiceId;clientSessionId="$batch-training"}
  $trainingId = $training.session.id
  $record.trainingSessionId=$trainingId
  $record.trainingRevisionId=$training.session.serviceRevisionId
  $init = Wait-Batch "/sessions/$trainingId/initialization" 'ready'
  $record.manifestHash=$init.manifestHash
  Invoke-BatchApi POST "/sessions/$trainingId/messages" @{clientMessageId="$batch-training-msg";content=$LearnerAnswer} | Out-Null
  Invoke-BatchApi POST "/sessions/$trainingId/finish" @{} | Out-Null
  $evaluation = Wait-Batch "/sessions/$trainingId/evaluation" 'ready'
  if ($evaluation.evaluation.schemaVersion -ne 2 -or $evaluation.evaluation.knowledgeManifestHash -ne $init.manifestHash) { throw 'Evaluation did not retain the initialized snapshot.' }
  foreach ($check in $evaluation.evaluation.knowledgeChecks) {
    foreach ($ref in $check.evidenceRefs) {
      $evidence = Invoke-BatchApi GET "/sessions/$trainingId/evidence/$($ref.traceId)"
      if ($evidence.manifestHash -ne $init.manifestHash) { throw 'Report evidence manifest drift.' }
    }
  }
  $record.steps += @{stage='training';schemaVersion=2;modelVersion=$evaluation.evaluation.modelVersion;promptVersion=$evaluation.evaluation.promptVersion;totalScore=$evaluation.evaluation.totalScore}
  $roleplay = Invoke-BatchApi POST '/roleplay/sessions' @{scenarioId=$ScenarioId;serviceId=$ServiceId;clientSessionId="$batch-roleplay"}
  $roleplayId=$roleplay.session.id
  $record.roleplaySessionId=$roleplayId
  $record.roleplayRevisionId=$roleplay.session.serviceRevisionId
  $reply = Invoke-BatchApi POST "/roleplay/sessions/$roleplayId/messages" @{clientMessageId="$batch-roleplay-msg";content='请介绍已发布资料中的价格、适用条件和需要进一步确认的事项。'}
  foreach ($ref in $reply.standardCustomerMessage.citations) { Invoke-BatchApi GET "/roleplay/sessions/$roleplayId/evidence/$($ref.traceId)" | Out-Null }
  Invoke-BatchApi POST "/roleplay/sessions/$roleplayId/finish" @{} | Out-Null
  $summary = Wait-Batch "/roleplay/sessions/$roleplayId/summary" 'ready'
  $record.steps += @{stage='roleplay';summaryStatus=$summary.status;summary=$summary.summary}
  $record.modelCallCount=(Invoke-BatchApi GET '/health').modelCallCount
  $record.status='passed'
} catch {
  $record.status='failed'
  # Avoid persisting request headers, tokens or full exception responses.
  $record.failureType=$_.Exception.GetType().FullName
  throw
} finally {
  $record.finishedAt=[DateTimeOffset]::UtcNow.ToString('o')
  $record | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}
