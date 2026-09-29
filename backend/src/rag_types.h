#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oral_training::rag {

using json = nlohmann::json;

inline constexpr int kContextVersion = 2;
inline constexpr int kReportSchemaVersion = 2;
inline constexpr std::string_view kTokenizerVersion = "zh-bigram-v1";
inline constexpr std::string_view kKnowledgeRubricVersion = "knowledge-rubric-v1";

namespace prompt_version {
inline constexpr std::string_view kServiceDraft = "service-draft-v1";
inline constexpr std::string_view kKnowledgeDraft = "knowledge-draft-v1";
inline constexpr std::string_view kPatientInitialization = "patient-init-v1";
inline constexpr std::string_view kPatientReply = "patient-rag-v1";
inline constexpr std::string_view kServiceReply = "service-reply-rag-v3";
inline constexpr std::string_view kClaimExtraction = "claim-extract-v1";
inline constexpr std::string_view kScoring = "score-rag-v1";
inline constexpr std::string_view kRoleplaySummary = "roleplay-summary-rag-v1";
}  // namespace prompt_version

namespace error_code {
inline constexpr std::string_view kServiceNotAvailable = "SERVICE_NOT_AVAILABLE";
inline constexpr std::string_view kServiceScenarioMismatch = "SERVICE_SCENARIO_MISMATCH";
inline constexpr std::string_view kDraftVersionConflict = "DRAFT_VERSION_CONFLICT";
inline constexpr std::string_view kKnowledgeNotReady = "KNOWLEDGE_NOT_READY";
inline constexpr std::string_view kPatientInitializationPending = "PATIENT_INITIALIZATION_PENDING";
inline constexpr std::string_view kPatientInitializationFailed = "PATIENT_INITIALIZATION_FAILED";
inline constexpr std::string_view kRagUnavailable = "RAG_UNAVAILABLE";
inline constexpr std::string_view kEvidenceValidationFailed = "EVIDENCE_VALIDATION_FAILED";
}  // namespace error_code

enum class RetrievalPurpose {
  CustomerReply,
  PatientReply,
  PatientInitialization,
  ClaimVerification,
  Scoring,
  RoleplaySummary,
  Preview,
};

enum class RetrievalStatus { Ok, NoHit, Conflicted, Unavailable };
enum class AnswerStatus { Answered, Partial, Unknown, Conflicted };
enum class InitializationStatus { Pending, Generating, Ready, Failed };
enum class KnowledgeVerdict {
  Supported,
  Contradicted,
  Incomplete,
  EvidenceMissing,
  Conflicted,
  NotApplicable,
};

inline std::string_view toString(RetrievalPurpose value) {
  switch (value) {
    case RetrievalPurpose::CustomerReply: return "customer_reply";
    case RetrievalPurpose::PatientReply: return "patient_reply";
    case RetrievalPurpose::PatientInitialization: return "patient_initialization";
    case RetrievalPurpose::ClaimVerification: return "claim_verification";
    case RetrievalPurpose::Scoring: return "scoring";
    case RetrievalPurpose::RoleplaySummary: return "roleplay_summary";
    case RetrievalPurpose::Preview: return "preview";
  }
  return "customer_reply";
}

inline std::string_view toString(RetrievalStatus value) {
  switch (value) {
    case RetrievalStatus::Ok: return "ok";
    case RetrievalStatus::NoHit: return "no_hit";
    case RetrievalStatus::Conflicted: return "conflicted";
    case RetrievalStatus::Unavailable: return "unavailable";
  }
  return "unavailable";
}

inline std::string_view toString(AnswerStatus value) {
  switch (value) {
    case AnswerStatus::Answered: return "answered";
    case AnswerStatus::Partial: return "partial";
    case AnswerStatus::Unknown: return "unknown";
    case AnswerStatus::Conflicted: return "conflicted";
  }
  return "unknown";
}

inline std::string_view toString(InitializationStatus value) {
  switch (value) {
    case InitializationStatus::Pending: return "pending";
    case InitializationStatus::Generating: return "generating";
    case InitializationStatus::Ready: return "ready";
    case InitializationStatus::Failed: return "failed";
  }
  return "failed";
}

inline std::string_view toString(KnowledgeVerdict value) {
  switch (value) {
    case KnowledgeVerdict::Supported: return "supported";
    case KnowledgeVerdict::Contradicted: return "contradicted";
    case KnowledgeVerdict::Incomplete: return "incomplete";
    case KnowledgeVerdict::EvidenceMissing: return "evidence_missing";
    case KnowledgeVerdict::Conflicted: return "conflicted";
    case KnowledgeVerdict::NotApplicable: return "not_applicable";
  }
  return "not_applicable";
}

struct Initialization {
  InitializationStatus status = InitializationStatus::Pending;
  std::optional<std::string> error_code;
  bool retryable = false;
};

struct TrainingContext {
  std::string context_id;
  int context_version = kContextVersion;
  std::string service_revision_id;
  std::string knowledge_as_of;
  json manifest = json::array();
  std::string manifest_hash;
  json patient_profile = nullptr;
  Initialization initialization;
};

struct RetrievalRequest {
  std::string context_id;
  RetrievalPurpose purpose = RetrievalPurpose::CustomerReply;
  std::string current_question;
  json recent_question_answers = json::array();
  std::optional<std::string> topic;
  std::optional<std::string> field;
};

struct FactEvidence {
  std::string evidence_id;
  std::string field;
  json value;
  std::string display_text;
  std::string revision_id;
  std::string origin;
};

struct PassageEvidence {
  std::string evidence_id;
  std::string chunk_id;
  std::string revision_id;
  std::string title;
  std::string body;
  std::string applicability;
  std::string source_title;
  std::optional<std::string> source_url;
  std::optional<std::string> source_locator;
  std::string scope;
  std::string service_id;
  std::string training_scope;
};

struct EvidenceConflict {
  std::string field_or_topic;
  std::vector<std::string> evidence_ids;
  std::string reason;
};

struct EvidenceBundle {
  std::string service_id;
  std::string training_scope;
  std::string context_id;
  std::string service_revision_id;
  std::string knowledge_as_of;
  RetrievalPurpose purpose = RetrievalPurpose::CustomerReply;
  std::string manifest_hash;
  std::string tokenizer_version = std::string(kTokenizerVersion);
  std::vector<FactEvidence> facts;
  std::vector<PassageEvidence> passages;
  std::vector<std::string> missing_fields;
  std::vector<EvidenceConflict> conflicts;
  RetrievalStatus retrieval_status = RetrievalStatus::NoHit;
};

struct CitationRef {
  std::string trace_id;
  std::string evidence_id;
};

struct GroundedResult {
  std::string reply;
  AnswerStatus answer_status = AnswerStatus::Unknown;
  std::vector<CitationRef> citations;
  json learning_points = json::array();
  std::string compliance_boundary;
  bool should_end = false;
};

struct KnowledgeCheck {
  std::string check_id;
  int round = 0;
  std::string original_quote;
  std::string topic;
  KnowledgeVerdict verdict = KnowledgeVerdict::NotApplicable;
  std::vector<CitationRef> evidence_refs;
  std::string reason;
  json expected_fact = nullptr;
  std::string severity;
  std::optional<double> score_impact;
  bool corrected_in_later_round = false;
};

struct KnowledgeAssessment {
  std::string status = "insufficient_evidence";
  std::optional<int> knowledge_accuracy;
  int assessable_count = 0;
  int unassessable_count = 0;
  std::optional<double> coverage;
  std::string rubric_version = std::string(kKnowledgeRubricVersion);
};

struct ReportV2 {
  int schema_version = kReportSchemaVersion;
  json dimension_scores = json::object();
  std::optional<int> total_score;
  std::optional<bool> passed;
  std::string summary;
  json strengths = json::array();
  json improvements = json::array();
  json violations = json::array();
  json round_comments = json::array();
  KnowledgeAssessment knowledge_assessment;
  std::vector<KnowledgeCheck> knowledge_checks;
  std::string knowledge_manifest_hash;
};

struct IdempotencyRequest {
  std::string key;
  std::string normalized_request_digest;
};

template <typename T>
json optionalJson(const std::optional<T>& value) {
  return value.has_value() ? json(*value) : json(nullptr);
}

inline void to_json(json& output, const Initialization& value) {
  output = {{"status", toString(value.status)}, {"errorCode", optionalJson(value.error_code)},
            {"retryable", value.retryable}};
}

inline void to_json(json& output, const TrainingContext& value) {
  output = {{"contextId", value.context_id}, {"contextVersion", value.context_version},
            {"serviceRevisionId", value.service_revision_id},
            {"knowledgeAsOf", value.knowledge_as_of}, {"manifest", value.manifest},
            {"manifestHash", value.manifest_hash}, {"patientProfile", value.patient_profile},
            {"initialization", value.initialization}};
}

inline void to_json(json& output, const RetrievalRequest& value) {
  output = {{"contextId", value.context_id}, {"purpose", toString(value.purpose)},
            {"currentQuestion", value.current_question},
            {"recentQuestionAnswers", value.recent_question_answers},
            {"topic", optionalJson(value.topic)}, {"field", optionalJson(value.field)}};
}

inline void to_json(json& output, const FactEvidence& value) {
  output = {{"evidenceId", value.evidence_id}, {"field", value.field}, {"value", value.value},
            {"displayText", value.display_text}, {"revisionId", value.revision_id},
            {"origin", value.origin}};
}

inline void to_json(json& output, const PassageEvidence& value) {
  output = {{"evidenceId", value.evidence_id}, {"chunkId", value.chunk_id},
            {"revisionId", value.revision_id}, {"title", value.title}, {"body", value.body},
            {"applicability", value.applicability}, {"sourceTitle", value.source_title},
            {"sourceUrl", optionalJson(value.source_url)},
            {"sourceLocator", optionalJson(value.source_locator)}, {"scope", value.scope},
            {"serviceId", value.service_id}, {"trainingScope", value.training_scope}};
}

inline void to_json(json& output, const EvidenceConflict& value) {
  output = {{"fieldOrTopic", value.field_or_topic}, {"evidenceIds", value.evidence_ids},
            {"reason", value.reason}};
}

inline void to_json(json& output, const EvidenceBundle& value) {
  output = {{"serviceId", value.service_id}, {"trainingScope", value.training_scope}, {"contextId", value.context_id}, {"serviceRevisionId", value.service_revision_id},
            {"knowledgeAsOf", value.knowledge_as_of}, {"purpose", toString(value.purpose)},
            {"manifestHash", value.manifest_hash}, {"tokenizerVersion", value.tokenizer_version},
            {"facts", value.facts}, {"passages", value.passages},
            {"missingFields", value.missing_fields}, {"conflicts", value.conflicts},
            {"retrievalStatus", toString(value.retrieval_status)}};
}

inline void to_json(json& output, const CitationRef& value) {
  output = {{"traceId", value.trace_id}, {"evidenceId", value.evidence_id}};
}

inline void to_json(json& output, const GroundedResult& value) {
  output = {{"reply", value.reply}, {"answerStatus", toString(value.answer_status)},
            {"citations", value.citations}, {"learningPoints", value.learning_points},
            {"complianceBoundary", value.compliance_boundary}, {"shouldEnd", value.should_end}};
}

inline void to_json(json& output, const KnowledgeCheck& value) {
  output = {{"checkId", value.check_id}, {"round", value.round},
            {"originalQuote", value.original_quote}, {"topic", value.topic},
            {"verdict", toString(value.verdict)}, {"evidenceRefs", value.evidence_refs},
            {"reason", value.reason}, {"expectedFact", value.expected_fact},
            {"severity", value.severity}, {"scoreImpact", optionalJson(value.score_impact)},
            {"correctedInLaterRound", value.corrected_in_later_round}};
}

inline void to_json(json& output, const KnowledgeAssessment& value) {
  output = {{"status", value.status},
            {"knowledgeAccuracy", optionalJson(value.knowledge_accuracy)},
            {"assessableCount", value.assessable_count},
            {"unassessableCount", value.unassessable_count},
            {"coverage", optionalJson(value.coverage)},
            {"rubricVersion", value.rubric_version}};
}

inline void to_json(json& output, const ReportV2& value) {
  output = {{"schemaVersion", value.schema_version}, {"dimensionScores", value.dimension_scores},
            {"totalScore", optionalJson(value.total_score)},
            {"passed", optionalJson(value.passed)},
            {"summary", value.summary}, {"strengths", value.strengths},
            {"improvements", value.improvements}, {"violations", value.violations},
            {"roundComments", value.round_comments},
            {"knowledgeAssessment", value.knowledge_assessment},
            {"knowledgeChecks", value.knowledge_checks},
            {"knowledgeManifestHash", value.knowledge_manifest_hash}};
}

inline void to_json(json& output, const IdempotencyRequest& value) {
  output = {{"key", value.key}, {"normalizedRequestDigest", value.normalized_request_digest}};
}

}  // namespace oral_training::rag
