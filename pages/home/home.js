const util = require('../../utils/util.js');

const TAG_MAP = {
  '价格异议': 'price',
  '投诉安抚': 'complaint',
  '咨询解答': 'consult',
  '项目推荐': 'recommend'
};

const HOT_PHRASES = [
  {
    id: 1,
    scenario: '价格异议',
    title: '患者说太贵了该怎么回？',
    patientLine: '你们这种植牙也太贵了，别家才几千块。',
    response: '理解您的顾虑。不同种植体在材料、医生技术和质保上有差异。方便了解您的预算吗？我帮您匹配适合的方案。',
    scenarioId: 'price-comparison'
  },
  {
    id: 2,
    scenario: '咨询解答',
    title: '患者担心种植牙手术疼痛',
    patientLine: '听说种植牙手术很疼，恢复期也长，我有点害怕。',
    response: '您的担心很正常。现在采用微创技术，术后大多数人表示疼痛感和拔牙差不多，会为您做好麻醉和术后护理指导。',
    scenarioId: 'implant-basic'
  },
  {
    id: 3,
    scenario: '投诉安抚',
    title: '患者对术后效果不满意',
    patientLine: '做完正畸怎么感觉牙齿还是不够整齐，我对效果不满意。',
    response: '非常感谢您的反馈。正畸结束后牙齿需要佩戴保持器一段时间才能稳定，方便来院复查一下吗？医生会详细解答。',
    scenarioId: 'post-treatment-discomfort'
  },
  {
    id: 4,
    scenario: '项目推荐',
    title: '患者只咨询基础项目如何升单？',
    patientLine: '我就想洗个牙，不需要那么复杂的治疗。',
    response: '好的，先帮您安排洁牙。医生检查时如果发现其他需要关注的问题，会客观告知您，由您决定是否进一步处理。',
    scenarioId: 'orthodontic-basic'
  }
].map(p => ({
  ...p,
  tagClass: TAG_MAP[p.scenario] || 'consult'
}));

Page({
  data: {
    searchValue: '',
    phrases: HOT_PHRASES,
    currentSwiper: 0
  },

  onSearchInput(e) {
    this.setData({ searchValue: e.detail.value });
  },

  onSearchConfirm() {
    const keyword = this.data.searchValue.trim().toLowerCase();
    if (!keyword) {
      this.setData({ phrases: HOT_PHRASES });
      return;
    }
    const filtered = HOT_PHRASES.filter(p =>
      p.title.toLowerCase().includes(keyword) ||
      p.scenario.includes(keyword) ||
      p.patientLine.toLowerCase().includes(keyword)
    );
    this.setData({ phrases: filtered, currentSwiper: 0 });
  },

  onClearSearch() {
    this.setData({ searchValue: '', phrases: HOT_PHRASES, currentSwiper: 0 });
  },

  onSwiperChange(e) {
    this.setData({ currentSwiper: e.detail.current });
  },

  goTraining(e) {
    const scenarioId = e.currentTarget.dataset.scenarioId;
    if (scenarioId) {
      wx.switchTab({ url: '/pages/index/index' });
    }
  },

  goPhraseVault() {
    wx.navigateTo({ url: '/pages/phrase-vault/phrase-vault' });
  },

  onPullDownRefresh() {
    this.setData({ searchValue: '', phrases: HOT_PHRASES, currentSwiper: 0 });
    wx.stopPullDownRefresh();
  }
});
