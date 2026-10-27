-- 037_personal_gateway_tuned_slot.sql
-- 为「个人 LiteLLM 网关配置」追加一个可选的「微调槽位」。
--
-- 背景：035 把模型配置从进程级环境变量改为按账号保存的单个网关（base_url + model + key），
-- 于是「按功能分派到不同模型实例」的能力消失——所有功能只能走同一个模型。
-- 本迁移把该能力以**可选第二槽位**的形式加回来：三个字段全部默认空串。
--
-- 语义：
--   * 微调槽位留空  → tuned_configured() 为 false，所有功能走主槽，行为与 035 完全一致；
--   * 微调槽位填齐  → 被显式标记为「话术类」的功能走微调槽，其余仍走主槽。
-- 这样既不破坏既有单槽位设计，也保留随时退回单实例的能力。
--
-- 密钥仍只存 Windows DPAPI 密文（与主槽同一套 protect/unprotect），本表不存明文。
-- 审计表同步留痕，但只记地址与模型名，不记密钥。
--
-- 幂等：全部 ADD COLUMN IF NOT EXISTS，可重复执行；不改动任何既有列与约束。

BEGIN;

ALTER TABLE user_model_gateway_settings
  ADD COLUMN IF NOT EXISTS tuned_base_url TEXT NOT NULL DEFAULT '';
ALTER TABLE user_model_gateway_settings
  ADD COLUMN IF NOT EXISTS tuned_model TEXT NOT NULL DEFAULT '';
ALTER TABLE user_model_gateway_settings
  ADD COLUMN IF NOT EXISTS tuned_api_key_encrypted TEXT NOT NULL DEFAULT '';

ALTER TABLE user_model_gateway_settings_audit
  ADD COLUMN IF NOT EXISTS tuned_base_url TEXT NOT NULL DEFAULT '';
ALTER TABLE user_model_gateway_settings_audit
  ADD COLUMN IF NOT EXISTS tuned_model TEXT NOT NULL DEFAULT '';

COMMENT ON COLUMN user_model_gateway_settings.tuned_base_url IS
  '可选的微调实例网关地址；为空表示不启用微调槽位。';
COMMENT ON COLUMN user_model_gateway_settings.tuned_model IS
  '可选的微调实例模型名；与 tuned_base_url 同时非空且密钥可解密时才生效。';

COMMIT;
