#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <stdexcept>
#include <memory>

namespace oral_training {

class IModelGateway {
 public:
  using json = nlohmann::json;

  virtual ~IModelGateway() = default;

  // Production gateways bind an immutable, server-resolved user to each caller.
  // In-process deterministic test gateways may retain their injected instance.
  virtual std::shared_ptr<IModelGateway> forUser(const std::string&) const { return nullptr; }

  virtual bool configured() const = 0;
  virtual std::string modelVersion() const = 0;
  virtual int modelCallCount() const { return 0; }
  virtual void setRuntimeKey(const std::string& api_key) = 0;
  // N02 infrastructure seam. N03 implements grounded generation in the DeepSeek gateway.
  virtual bool supportsPatientInitialization() const { return false; }
  virtual json initializePatient(const json&, const json&, const json&) const {
    return json::object();
  }
  virtual json groundedPatientReply(const json&, const json&, const json&) const {
    return json::object();
  }
  virtual json extractKnowledgeClaims(const json&) const { return json::array(); }
  virtual json patientReply(const json& scenario, const json& patient_state,
                            const json& history) const = 0;
  virtual json evaluate(const json& scenario, const json& messages) const = 0;
  virtual json evaluateCommunication(const json&, const json&) const {
    throw std::runtime_error("grounded communication evaluator unavailable");
  }
  virtual json standardServiceReply(const json& scenario, const json& history) const = 0;
  virtual json groundedServiceReply(const json& scenario, const json& history,
                                    const json& evidence) const {
    return standardServiceReply(scenario, history);
  }
  virtual json roleplaySummary(const json& scenario, const json& history) const = 0;
  virtual json generateKnowledgeDraft(const std::string& kind,
                                      const json& input) const {
    return json::object();
  }

  // 实时训练提示：必须按「患者本轮说了什么、学员上一轮答了什么」现场生成，
  // 不能退化成按场景查模板，因此它是网关契约的一部分而不是某个实现的私有方法。
  virtual json trainingHint(const json& scenario, const json& patient_state,
                            const json& history,
                            const std::string& current_patient_message, int round,
                            int hint_number) const {
    return json::object();
  }

  // 错题「复现原回合」的单轮复评：只评这一轮新回答，不输出五维分数
  // （单回合覆盖不了五维，硬给会偏离整场权重口径）。
  virtual json evaluateSingleRound(const json& scenario_public,
                                   const std::string& patient_question,
                                   const std::string& mistake_reason,
                                   const std::string& new_answer) const {
    return json::object();
  }

  // 个性化训练计划草稿：按学员五维薄弱项，在候选场景池内挑场景并给出计划参数。
  // 模型只负责「选哪些场景、目标定多少」，最终数值边界仍由 store 层再 clamp 一次。
  virtual json trainingPlanDraft(const json& learner_profile,
                                 const json& scenario_candidates) const {
    return json::object();
  }
};

}  // namespace oral_training
