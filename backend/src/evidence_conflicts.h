#pragma once

#include "rag_types.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace oral_training::rag {

struct EvidenceClaim {
  std::string key;
  std::string numbers;
  bool negative = false;
};

// Deterministic contradiction checks preserve the original evidence text.
// Scope/topic must match before comparing statements. Complementary sentences
// and identical duplicates are not conflicts. No model text can resolve a conflict.
inline std::vector<EvidenceClaim> evidenceClaims(const std::string& text) {
  std::string separated = text;
  for (const std::string delimiter : {"。", "！", "？", "；", "\n", ";"}) {
    std::size_t pos = 0;
    while ((pos = separated.find(delimiter, pos)) != std::string::npos) {
      separated.replace(pos, delimiter.size(), "\n");
      ++pos;
    }
  }
  std::vector<EvidenceClaim> claims;
  std::size_t begin = 0;
  while (begin < separated.size()) {
    const auto end = separated.find('\n', begin);
    auto sentence = separated.substr(begin, end == std::string::npos ? end : end - begin);
    begin = end == std::string::npos ? separated.size() : end + 1;
    sentence.erase(std::remove_if(sentence.begin(), sentence.end(), [](unsigned char c) {
      return c < 128 && std::isspace(c);
    }), sentence.end());
    EvidenceClaim claim;
    // Longest forms first; each negation reverses polarity, including double negation.
    for (const auto& negation : std::vector<std::pair<std::string, std::string>>{
        {"并非", "是"}, {"不是", "是"}, {"没有包含", "包含"}, {"没有", "有"},
        {"不能", "能"}, {"不会", "会"}, {"无需", "需要"}, {"不", ""}, {"无", "有"}, {"未", ""}}) {
      std::size_t pos = 0;
      while ((pos = sentence.find(negation.first, pos)) != std::string::npos) {
        sentence.replace(pos, negation.first.size(), negation.second);
        pos += negation.second.size();
        claim.negative = !claim.negative;
      }
    }
    for (std::size_t i = 0; i < sentence.size();) {
      if (sentence[i] >= '0' && sentence[i] <= '9') {
        const auto start = i++;
        while (i < sentence.size() && ((sentence[i] >= '0' && sentence[i] <= '9') || sentence[i] == '.')) ++i;
        claim.numbers += sentence.substr(start, i - start) + ";";
        claim.key += "#";
      } else claim.key += sentence[i++];
    }
    if (!claim.key.empty()) claims.push_back(std::move(claim));
  }
  return claims;
}

inline bool contradictoryClaims(const std::vector<EvidenceClaim>& a, const std::vector<EvidenceClaim>& b) {
  for (const auto& left : a) for (const auto& right : b) {
    if (left.key == right.key &&
        ((left.negative != right.negative && left.numbers == right.numbers) ||
         (!left.negative && !right.negative && left.numbers != right.numbers))) return true;
  }
  return false;
}

inline bool contradictoryClaims(const std::string& a, const std::string& b) {
  return contradictoryClaims(evidenceClaims(a), evidenceClaims(b));
}

inline void detectEvidenceConflicts(EvidenceBundle& bundle,
                                    const std::map<std::string, std::string>& topics) {
  std::vector<std::vector<EvidenceClaim>> claims;
  for (const auto& passage : bundle.passages) claims.push_back(evidenceClaims(passage.body));
  for (std::size_t i = 0; i < bundle.passages.size(); ++i) {
    const auto& a = bundle.passages[i];
    for (std::size_t j = i + 1; j < bundle.passages.size(); ++j) {
      const auto& b = bundle.passages[j];
      if (a.scope != b.scope || a.service_id != b.service_id || a.applicability != b.applicability ||
          topics.at(a.evidence_id) != topics.at(b.evidence_id)) continue;
      if (contradictoryClaims(claims[i], claims[j]))
        bundle.conflicts.push_back({topics.at(a.evidence_id), {a.evidence_id, b.evidence_id},
                                   "同适用范围的资料包含相反陈述或不同数值，请人工核对"});
    }
  }
  // Detect before top-K truncation: a lower-ranked contradiction must not disappear.
  std::set<std::string> selected;
  for (const auto& conflict : bundle.conflicts) {
    auto candidate = selected;
    candidate.insert(conflict.evidence_ids.begin(), conflict.evidence_ids.end());
    if (candidate.size() <= 6) selected = std::move(candidate);
  }
  for (const auto& passage : bundle.passages) {
    if (selected.size() == 6) break;
    selected.insert(passage.evidence_id);
  }
  bundle.passages.erase(std::remove_if(bundle.passages.begin(), bundle.passages.end(),
      [&](const auto& p) { return !selected.count(p.evidence_id); }), bundle.passages.end());
  bundle.conflicts.erase(std::remove_if(bundle.conflicts.begin(), bundle.conflicts.end(),
      [&](const auto& c) { return !std::all_of(c.evidence_ids.begin(), c.evidence_ids.end(),
          [&](const auto& id) { return selected.count(id) != 0; }); }), bundle.conflicts.end());
}

}  // namespace oral_training::rag
