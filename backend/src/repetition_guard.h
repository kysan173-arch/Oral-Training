#pragma once
// 复读守卫（P0）：CJK 归一化 + LCS + 标点结构指纹。
//
// 设计取舍与主环境 backend/src/main.cpp:919-1170 保持一致：
//   - 只内置口腔客服场景的高频繁简字，不引入 OpenCC；
//   - 先按同角色历史做 LCS，再用“长度差<=2、前3字相同、标点序列相同”兜底换皮复读；
//   - 只与同角色历史比对：学员的话被模型引用是正常的，模型复述自己的话才是复读。
//
// 这个头文件不依赖 main.cpp 的 jsonString/trim，方便单测直接 include。

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace oral_training::guard {

using json = nlohmann::json;

inline std::string trimCopy(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos
             ? ""
             : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

inline std::string jsonStringField(const json& object, const char* key) {
  if (!object.is_object() || !object.contains(key) || !object[key].is_string()) return "";
  return object[key].get<std::string>();
}

inline std::vector<std::string> splitUtf8Chars(const std::string& text) {
  std::vector<std::string> chars;
  chars.reserve(text.size() / 2);
  size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    size_t width = 1;
    if (lead >= 0xF0) width = 4;
    else if (lead >= 0xE0) width = 3;
    else if (lead >= 0xC0) width = 2;
    if (index + width > text.size()) width = 1;
    chars.push_back(text.substr(index, width));
    index += width;
  }
  return chars;
}

inline std::string firstUtf8Chars(const std::string& text, size_t count) {
  const auto chars = splitUtf8Chars(text);
  std::string result;
  for (size_t i = 0; i < chars.size() && i < count; ++i) result += chars[i];
  return result;
}

// 最长公共子串占较短串的比例。逐字复述时接近 1。
inline double longestCommonSubstringRatio(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return 0.0;
  std::vector<size_t> previous(b.size() + 1, 0);
  std::vector<size_t> current(b.size() + 1, 0);
  size_t best = 0;
  for (size_t i = 1; i <= a.size(); ++i) {
    for (size_t j = 1; j <= b.size(); ++j) {
      current[j] = (a[i - 1] == b[j - 1]) ? previous[j - 1] + 1 : 0;
      if (current[j] > best) best = current[j];
    }
    std::swap(previous, current);
    std::fill(current.begin(), current.end(), 0);
  }
  return static_cast<double>(best) / static_cast<double>(std::min(a.size(), b.size()));
}

// 同一条回复内部是否有非重叠的重复片段（车轱辘话）。
// 注意按 UTF-8 字符切分，而不是按字节：min_run=6 应表示 6 个汉字。
// 按字节会把“我先看看，然后再看看”这类正常重复（2 个汉字×2）误判成车轱辘话。
inline bool hasInternalDuplication(const std::string& reply, size_t min_run) {
  const auto chars = splitUtf8Chars(reply);
  if (chars.size() < min_run * 2) return false;
  std::unordered_map<std::string, size_t> first_seen;
  for (size_t i = 0; i + min_run <= chars.size(); ++i) {
    std::string piece;
    piece.reserve(min_run * 3);
    for (size_t j = 0; j < min_run; ++j) piece += chars[i + j];
    const auto found = first_seen.find(piece);
    if (found == first_seen.end()) {
      first_seen.emplace(piece, i);
      continue;
    }
    if (i - found->second >= min_run) return true;
  }
  return false;
}

// 繁体 -> 简体高频字。覆盖边界：不是完整繁简表，未覆盖的字形由结构指纹兜底。
inline std::string normalizeCjkVariants(const std::string& text) {
  static const std::unordered_map<std::string, std::string> table = {
      {"費", "费"}, {"療", "疗"}, {"慮", "虑"}, {"響", "响"}, {"復", "复"},
      {"調", "调"}, {"問", "问"}, {"題", "题"}, {"檢", "检"}, {"驗", "验"},
      {"點", "点"}, {"處", "处"}, {"後", "后"}, {"術", "术"}, {"過", "过"},
      {"麼", "么"}, {"嗎", "吗"}, {"對", "对"}, {"應", "应"}, {"該", "该"},
      {"讓", "让"}, {"說", "说"}, {"話", "话"}, {"這", "这"}, {"們", "们"},
      {"個", "个"}, {"兒", "儿"}, {"幾", "几"}, {"關", "关"}, {"於", "于"},
      {"係", "系"}, {"繫", "系"}, {"覺", "觉"}, {"擔", "担"}, {"開", "开"},
      {"業", "业"}, {"務", "务"}, {"醫", "医"}, {"診", "诊"}, {"斷", "断"},
      {"藥", "药"}, {"預", "预"}, {"約", "约"}, {"時", "时"}, {"間", "间"},
      {"種", "种"}, {"齒", "齿"}, {"齦", "龈"}, {"緩", "缓"}, {"衝", "冲"},
      {"價", "价"}, {"錢", "钱"}, {"貴", "贵"}, {"優", "优"}, {"繼", "继"},
      {"續", "续"}, {"複", "复"}, {"雜", "杂"}, {"願", "愿"}, {"專", "专"},
      {"標", "标"}, {"準", "准"}, {"項", "项"}, {"總", "总"}, {"結", "结"},
      {"構", "构"}, {"與", "与"}, {"並", "并"}, {"為", "为"}, {"無", "无"},
      {"沒", "没"}, {"還", "还"}, {"會", "会"}, {"買", "买"}, {"賣", "卖"},
      {"購", "购"}, {"選", "选"}, {"擇", "择"}, {"適", "适"}, {"轉", "转"},
      {"換", "换"}, {"記", "记"}, {"錄", "录"}, {"資", "资"}, {"訊", "讯"},
      {"網", "网"}, {"絡", "络"}, {"確", "确"}, {"認", "认"}, {"護", "护"},
      {"執", "执"}, {"觀", "观"}, {"詢", "询"}, {"掛", "挂"}, {"號", "号"},
      {"補", "补"}, {"償", "偿"}, {"損", "损"}, {"傷", "伤"}, {"滿", "满"},
      {"條", "条"}, {"報", "报"}, {"銷", "销"}, {"額", "额"}, {"況", "况"},
      {"狀", "状"}, {"態", "态"}, {"學", "学"}, {"員", "员"}, {"練", "练"},
      {"習", "习"}, {"訓", "训"}, {"課", "课"}, {"漸", "渐"}, {"發", "发"},
      {"長", "长"}, {"給", "给"}, {"幫", "帮"}, {"辦", "办"}, {"評", "评"},
      {"內", "内"}, {"腫", "肿"}, {"脹", "胀"}, {"縫", "缝"}, {"線", "线"},
      {"塗", "涂"}, {"劑", "剂"}, {"臟", "脏"}, {"彆", "别"}, {"尷", "尴"},
      {"癒", "愈"}, {"釋", "释"}, {"瞭", "了"}, {"髒", "脏"}, {"體", "体"},
      {"畢", "毕"}, {"週", "周"}, {"灣", "湾"}, {"島", "岛"}, {"腦", "脑"},
      {"衛", "卫"}, {"歸", "归"}, {"圍", "围"}, {"權", "权"}, {"責", "责"},
      {"誤", "误"}, {"議", "议"}, {"簽", "签"}, {"簡", "简"}, {"單", "单"},
      {"雙", "双"}, {"離", "离"}, {"難", "难"}, {"雖", "虽"}, {"隨", "随"},
      {"險", "险"}, {"際", "际"}, {"隊", "队"}, {"階", "阶"}, {"陳", "陈"},
      {"陽", "阳"}, {"順", "顺"}, {"領", "领"}, {"顏", "颜"}, {"類", "类"},
      {"風", "风"}, {"飯", "饭"}, {"飲", "饮"}, {"黃", "黄"}, {"齊", "齐"},
      {"緻", "致"}, {"靜", "静"}, {"緒", "绪"}, {"幹", "干"},
  };

  std::string output;
  output.reserve(text.size());
  size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead >= 0xE0 && index + 3 <= text.size()) {
      const auto key = text.substr(index, 3);
      const auto found = table.find(key);
      output += (found == table.end()) ? key : found->second;
      index += 3;
      continue;
    }
    output.push_back(text[index]);
    ++index;
  }
  return output;
}

// 标点序列指纹：全角映射到半角后抽取，避免「？」与「?」被判成不同序列。
inline std::string punctuationSequence(const std::string& text) {
  static const std::map<std::string, std::string> fullwidth_to_halfwidth = {
      {"。", "."}, {"，", ","}, {"、", ","}, {"；", ";"}, {"：", ":"},
      {"！", "!"}, {"？", "?"}, {"—", "-"}, {"～", "~"}, {"　", " "},
      {"“", "\""}, {"”", "\""}, {"‘", "'"}, {"’", "'"},
      {"（", "("}, {"）", ")"}, {"…", "..."},
      // 实测的换皮样本会混入阿拉伯标点：\xD8\x8C（U+060C）、\xD8\x9F（U+061F）、\xD8\x9B（U+061B）。
      // 不归一的话标点序列与原文不同，结构指纹兜底会漏判。
      {"\xD8\x8C", ","}, {"\xD8\x9F", "?"}, {"\xD8\x9B", ";"},
  };
  static const std::set<std::string> marks = {
      "。", "，", "、", "；", "：", "！", "？", "…", "—", "～", "·",
      "“", "”", "‘", "’", "（", "）", "《", "》", "【", "】", "　",
      "\xD8\x8C", "\xD8\x9F", "\xD8\x9B",
      ".", ",", ";", ":", "!", "?", "-", "(", ")", "\"", "'", "\n"};
  std::string sequence;
  for (const auto& character : splitUtf8Chars(text)) {
    if (marks.count(character) == 0) continue;
    const auto mapped = fullwidth_to_halfwidth.find(character);
    sequence += mapped == fullwidth_to_halfwidth.end() ? character : mapped->second;
  }
  return sequence;
}

inline bool looksLikeVariantRewrite(const std::string& candidate, const std::string& previous) {
  const auto candidate_chars = splitUtf8Chars(candidate);
  const auto previous_chars = splitUtf8Chars(previous);
  if (candidate_chars.size() < 6 || previous_chars.size() < 6) return false;
  const auto difference = candidate_chars.size() > previous_chars.size()
                              ? candidate_chars.size() - previous_chars.size()
                              : previous_chars.size() - candidate_chars.size();
  if (difference > 2) return false;
  if (firstUtf8Chars(candidate, 3) != firstUtf8Chars(previous, 3)) return false;
  const auto candidate_marks = punctuationSequence(candidate);
  if (candidate_marks.empty()) return false;
  return candidate_marks == punctuationSequence(previous);
}

struct RepetitionFinding {
  bool repeated = false;
  bool internal = false;
  bool variant_rewrite = false;
  double ratio = 0.0;
  std::string source;
};

inline RepetitionFinding detectRepetition(const std::string& reply, const json& history,
                                          const std::string& self_role, double threshold,
                                          size_t min_run) {
  RepetitionFinding finding;
  const auto cleaned = trimCopy(reply);
  if (cleaned.empty()) return finding;

  if (hasInternalDuplication(cleaned, min_run)) {
    finding.repeated = true;
    finding.internal = true;
    finding.ratio = 1.0;
    return finding;
  }

  const auto normalized = normalizeCjkVariants(cleaned);
  for (const auto& message : history) {
    if (!message.is_object()) continue;
    if (jsonStringField(message, "role") != self_role) continue;
    const auto previous = trimCopy(jsonStringField(message, "content"));
    if (splitUtf8Chars(previous).size() < 6) continue;
    const auto ratio = longestCommonSubstringRatio(normalized, normalizeCjkVariants(previous));
    if (ratio > finding.ratio) {
      finding.ratio = ratio;
      finding.source = previous;
    }
  }

  if (finding.ratio < threshold) {
    for (const auto& message : history) {
      if (!message.is_object()) continue;
      if (jsonStringField(message, "role") != self_role) continue;
      const auto previous = trimCopy(jsonStringField(message, "content"));
      if (!looksLikeVariantRewrite(cleaned, previous)) continue;
      finding.variant_rewrite = true;
      finding.ratio = std::max(finding.ratio, threshold);
      finding.source = previous;
      break;
    }
  }

  finding.repeated = finding.ratio >= threshold;
  return finding;
}

// 报告 quote 校验用：去掉空白/标点、繁简归一后比较。
// 这样「是否需要拔牙，要由医生评估」和「是否需要拔牙 要由医生评估」可以匹配，
// 但不会把完全不同的句子误判为同一条。
inline std::string normalizeForQuoteMatch(const std::string& text) {
  const auto normalized = normalizeCjkVariants(text);
  static const std::set<std::string> marks = {
      "。", "，", "、", "；", "：", "！", "？", "…", "—", "～", "·",
      "“", "”", "‘", "’", "（", "）", "《", "》", "【", "】", "　",
      "\xD8\x8C", "\xD8\x9F", "\xD8\x9B",
      ".", ",", ";", ":", "!", "?", "-", "(", ")", "\"", "'", "\n", "\r", "\t", " "};
  std::string output;
  output.reserve(normalized.size());
  for (const auto& character : splitUtf8Chars(normalized)) {
    if (character.size() == 1) {
      const auto byte = static_cast<unsigned char>(character[0]);
      if (std::isalnum(byte)) output.push_back(static_cast<char>(std::tolower(byte)));
      continue;
    }
    if (marks.count(character) == 0) output += character;
  }
  return output;
}

}  // namespace oral_training::guard
