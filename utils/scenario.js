/* 场景分类/难度的前端展示映射（唯一来源，勿在页面里另写副本）。
   后端口径见 reliable_store.h sceneCategories()。 */
const CATEGORY_CONFIG = [
  { id: 'consultation', name: '咨询解答', icon: '咨', description: '先了解患者关切，再清楚说明服务边界' },
  { id: 'price_negotiation', name: '价格异议', icon: '价', description: '客观说明费用构成，不承诺固定价格' },
  { id: 'complaint_handling', name: '投诉安抚', icon: '诉', description: '先回应情绪，及时引导联系医生或复诊' },
  { id: 'recommendation', name: '项目推荐', icon: '推', description: '从真实需求出发，不替代医生判断' }
];

const DIFFICULTY_MAP = {
  beginner: { level: 'beginner', label: '初级' },
  intermediate: { level: 'intermediate', label: '中级' },
  advanced: { level: 'advanced', label: '高级' },
  basic: { level: 'beginner', label: '初级' }
};

const categoryName = id => {
  const matched = CATEGORY_CONFIG.find(category => category.id === id);
  return matched ? matched.name : id;
};

const difficultyLabel = difficulty => {
  const mapped = DIFFICULTY_MAP[difficulty] || DIFFICULTY_MAP.basic;
  return mapped.label;
};

module.exports = { CATEGORY_CONFIG, DIFFICULTY_MAP, categoryName, difficultyLabel };
