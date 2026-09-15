// Mock phrase vault data
const PHRASES = [
  {
    id: 1,
    category: 'consultation',
    categoryName: '咨询解答',
    scenario: '种植牙基础咨询',
    patientType: '谨慎型',
    patientLine: '种植牙要多久才能长好？我下个月有个重要场合，怕来不及。',
    response: '种植牙一般需要3-6个月的愈合期。如果您时间比较紧，可以帮您安排尽早检查，医生会给出最适合您的时间方案。',
    rating: 92,
    tip: '先确认时间边界，再给专业时间预估，最后引导行动预约检查。',
    collected: false
  },
  {
    id: 2,
    category: 'consultation',
    categoryName: '咨询解答',
    scenario: '正畸基础咨询',
    patientType: '焦虑型',
    patientLine: '我都30多了，现在做正畸还来得及吗？会不会很痛苦？',
    response: '30多岁完全可以做正畸。成人正畸虽然周期会比青少年稍长，但现在隐形矫正技术很成熟，美观舒适，很多人这个年龄开始矫正。方便约个检查，医生根据您的情况给出专业方案。',
    rating: 88,
    tip: '先消除年龄顾虑，再降低痛苦预期，用"隐形矫正"转移注意力。',
    collected: false
  },
  {
    id: 3,
    category: 'price_negotiation',
    categoryName: '价格异议',
    scenario: '与其他诊所比价',
    patientType: '比价型',
    patientLine: '隔壁诊所才8000一颗，你们要两万多，这差价也太大了吧？',
    response: '理解您会对比。种植体品牌、医生经验、质保年限、后续维护都是影响价格的因素。我们的种植体有10年质保，是由有15年经验的种植医生操作。您主要在意的是价格还是品质保障呢？',
    rating: 86,
    tip: '不贬低竞品，分解价值维度，最后转向提问挖掘真实顾虑。',
    collected: false
  },
  {
    id: 4,
    category: 'complaint_handling',
    categoryName: '投诉安抚',
    scenario: '术后不适咨询',
    patientType: '焦虑型',
    patientLine: '拔完牙三天了还疼，你们手术是不是有问题？！',
    response: '非常抱歉让您不舒服了。拔牙后3-4天有轻微疼痛是正常恢复过程，但如果疼痛加重或有其他不适，建议您尽快来复查，我们免费为您处理。方便现在描述一下疼痛的程度吗？',
    rating: 84,
    tip: '先道歉安抚情绪，再科普正常恢复过程，最后提供免费复查承诺。',
    collected: false
  },
  {
    id: 5,
    category: 'recommendation',
    categoryName: '项目推荐',
    scenario: '洁牙升单推荐',
    patientType: '谨慎型',
    patientLine: '我就是来洗个牙，不需要做别的治疗。',
    response: '好的，先帮您安排洁牙。洁牙过程中医生如果发现需要关注的牙齿问题，会记录下来给您参考，最终是否处理由您自己决定。这是对您健康负责的做法，不强制消费。',
    rating: 87,
    tip: '尊重选择，降低防御心理，用"发现-记录-您决定"三步骤建立信任。',
    collected: false
  },
  {
    id: 6,
    category: 'price_negotiation',
    categoryName: '价格异议',
    scenario: '正畸费用质疑',
    patientType: '比价型',
    patientLine: '隐形矫正太贵了，传统钢牙不也一样能矫正吗？',
    response: '两种方式都能矫正，效果类似。隐形矫正的优势是美观、吃饭刷牙方便、复诊频率低。很多人选择是因为不影响工作和社交。您可以先检查，看适合哪种方案，再根据预算做选择。',
    rating: 85,
    tip: '不否定传统方案，客观对比优劣，强调选择权在患者手中。',
    collected: false
  },
  {
    id: 7,
    category: 'complaint_handling',
    categoryName: '投诉安抚',
    scenario: '服务态度投诉',
    patientType: '愤怒型',
    patientLine: '你们前台态度太差了！我等了半天都没人理我！',
    response: '非常抱歉给您带来不好的体验。我们非常重视您的反馈，会马上核实情况并做出改进。现在有什么我可以立即帮您处理的吗？',
    rating: 90,
    tip: '80%的话术用在情绪安抚上，先承认问题并道歉，再给出行动承诺。',
    collected: false
  },
  {
    id: 8,
    category: 'recommendation',
    categoryName: '项目推荐',
    scenario: '9.9洗牙券升单',
    patientType: '犹豫型',
    patientLine: '你们的9.9洗牙是不是有什么陷阱？到时候会不会让我做一堆别的项目？',
    response: '9.9洗牙是真实的体验活动，没有任何强制消费。医生会做口腔检查并告知客观情况，是否做后续治疗完全由您决定。很多人通过这个活动才发现了一些早该处理的问题，反而是省钱了呢。',
    rating: 83,
    tip: '直接回应"陷阱"怀疑，用"客观告知+您决定"消除戒心。',
    collected: false
  }
];

const CATEGORIES = [
  { key: 'all', name: '全部' },
  { key: 'consultation', name: '咨询解答' },
  { key: 'price_negotiation', name: '价格异议' },
  { key: 'complaint_handling', name: '投诉安抚' },
  { key: 'recommendation', name: '项目推荐' }
];

Page({
  data: {
    phrases: PHRASES,
    categories: CATEGORIES,
    activeCategory: 'all',
    searchValue: ''
  },

  onLoad() {
    this.applyFilter();
  },

  loadCollected() {
    const collected = wx.getStorageSync('collectedPhrases') || [];
    this.data.phrases = PHRASES.map(p => ({
      ...p,
      collected: collected.includes(p.id)
    }));
  },

  onSearchInput(e) {
    this.setData({ searchValue: e.detail.value }, () => this.applyFilter());
  },

  changeCategory(e) {
    this.setData({ activeCategory: e.currentTarget.dataset.key }, () => this.applyFilter());
  },

  applyFilter() {
    const { activeCategory, searchValue } = this.data;
    const collected = wx.getStorageSync('collectedPhrases') || [];
    let phrases = PHRASES.map(p => ({
      ...p,
      collected: collected.includes(p.id)
    }));

    if (activeCategory !== 'all') {
      phrases = phrases.filter(p => p.category === activeCategory);
    }

    const keyword = searchValue.trim().toLowerCase();
    if (keyword) {
      phrases = phrases.filter(p =>
        p.scenario.toLowerCase().includes(keyword) ||
        p.categoryName.includes(keyword) ||
        p.patientLine.toLowerCase().includes(keyword) ||
        p.response.toLowerCase().includes(keyword) ||
        p.patientType.includes(keyword)
      );
    }

    this.setData({ phrases });
  },

  toggleCollect(e) {
    const id = e.currentTarget.dataset.id;
    const phrases = this.data.phrases.map(p => {
      if (p.id === id) {
        return { ...p, collected: !p.collected };
      }
      return p;
    });
    this.setData({ phrases });

    // Persist to storage
    const collected = phrases.filter(p => p.collected).map(p => p.id);
    wx.setStorageSync('collectedPhrases', collected);

    wx.showToast({
      title: phrases.find(p => p.id === id).collected ? '已收藏' : '已取消收藏',
      icon: 'none'
    });
  },

  copyPhrase(e) {
    const content = e.currentTarget.dataset.content;
    wx.setClipboardData({
      data: content,
      success: () => wx.showToast({ title: '已复制', icon: 'success' })
    });
  },

  goTraining() {
    wx.switchTab({ url: '/pages/index/index' });
  }
});
