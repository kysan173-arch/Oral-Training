/* 患者画像派生唯一来源：场景默认画像与学员自定义画像的「合并覆盖」语义。
   training（对练舱）与 mistake-retrain（错题重练）共用，页面不得各写一份。 */

const PROFILE_FIELDS = ['age', 'gender', 'description', 'emotion'];
const PATIENT_GENDERS = ['男', '女'];

const normalizeScenario = (item, customProfile) => {
  // 自定义画像按「合并覆盖」处理：只覆盖学员真正填过的字段，其余仍用场景默认。
  // 整段替换会让只填了性别的画像把年龄和描述一起清空，摘要条变成「年龄待填写 / 描述待填写」。
  const fallback = (item && item.patientProfile) || {};
  const custom = {};
  PROFILE_FIELDS.forEach(field => {
    const value = customProfile && customProfile[field] ? String(customProfile[field]).trim() : '';
    if (value) custom[field] = value;
  });
  const profile = Object.assign({}, fallback, custom);
  // 场景默认 gender 是 unknown，不能直接透出来给学员看
  const gender = String(profile.gender || '');
  return Object.assign({}, item, {
    patientAge: profile.age ? `${profile.age}岁` : '年龄待填写',
    patientGender: PATIENT_GENDERS.indexOf(gender) >= 0 ? gender : '',
    patientConcern: profile.description || '描述待填写',
    patientEmotion: profile.emotion || ''
  });
};

module.exports = { PROFILE_FIELDS, PATIENT_GENDERS, normalizeScenario };
