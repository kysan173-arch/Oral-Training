Component({
  properties: {
    icon: { type: String, value: '' },
    title: { type: String, value: '暂无内容' },
    text: { type: String, value: '' },
    actionText: { type: String, value: '' }
  },
  methods: {
    onAction() {
      this.triggerEvent('action');
    }
  }
});
