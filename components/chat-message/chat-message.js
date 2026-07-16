// components/chat-message/chat-message.js
// 聊天消息组件，统一处理 user / patient / system 三类消息展示
// Props:
//   message: { id, role, content, createdAt, emotionLabel? }

const EMOTION_CLASS_MAP = {
  '将信将疑': 'emotion-doubt',
  '开始接受': 'emotion-accept',
  '价格敏感': 'emotion-price',
  '焦虑不安': 'emotion-anxiety',
  '愤怒不满': 'emotion-angry',
  '积极配合': 'emotion-cooperate',
  '犹豫不决': 'emotion-hesitate'
};

Component({
  properties: {
    message: {
      type: Object,
      value: {},
      observer: '_onMessageChange'
    }
  },

  data: {
    roleClass: '',
    avatar: '',
    emotionLabel: '',
    emotionClass: ''
  },

  methods: {
    _onMessageChange(msg) {
      if (!msg || !msg.role) return;
      const role = msg.role;
      const roleClass = role === 'user' ? 'msg-user' : (role === 'system' ? 'msg-system' : 'msg-patient');
      const avatar = role === 'user' ? '我' : (role === 'system' ? '💡' : '患');
      const emotionLabel = msg.emotionLabel || '';
      const emotionClass = EMOTION_CLASS_MAP[emotionLabel] || '';
      this.setData({ roleClass, avatar, emotionLabel, emotionClass });
    }
  }
});
