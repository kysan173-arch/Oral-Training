/* ============================================================
   backend/tests/datetime_check.js —— utils/datetime.js 的离线断言

   utils/datetime.js 不依赖任何 wx API，可直接用 node 跑：
     node backend/tests/datetime_check.js
   覆盖：完整 ISO / 纯日期 / 空值 / null / undefined / 非法串 /
         合成小时前导零 / 跨天区间 / toMs 时区反算。

   失败用例以 ASCII 输出，避免 Windows 控制台编码干扰排查。
   ============================================================ */

const assert = require('assert');
const dt = require('../../utils/datetime.js');

const cases = [];
const check = (name, actual, expected) => {
  cases.push({ name, actual, expected });
};

/* ---- formatClock：消息气泡时间 ---- */
check('clock/full iso', dt.formatClock('2026-09-12T21:28:55+08:00'), '21:28');
check('clock/leading zero', dt.formatClock('2026-09-12T09:05:00+08:00'), '09:05');
check('clock/no seconds', dt.formatClock('2026-09-12T21:28+08:00'), '21:28');
check('clock/midnight', dt.formatClock('2026-09-12T00:00:00+08:00'), '00:00');
check('clock/date only', dt.formatClock('2026-09-12'), '');
check('clock/empty', dt.formatClock(''), '');
check('clock/null', dt.formatClock(null), '');
check('clock/undefined', dt.formatClock(undefined), '');
check('clock/garbage', dt.formatClock('not-a-date'), '');
check('clock/number', dt.formatClock(1757688535000), '');

/* ---- formatMonthDay / formatDate：图表标签与日期 ---- */
check('monthday/date only', dt.formatMonthDay('2026-09-12'), '09-12');
check('monthday/full iso', dt.formatMonthDay('2026-09-12T21:28:55+08:00'), '09-12');
check('monthday/empty', dt.formatMonthDay(null), '');
check('date/full iso', dt.formatDate('2026-09-12T21:28:55+08:00'), '2026-09-12');
check('date/date only', dt.formatDate('2026-09-12'), '2026-09-12');

/* ---- formatDateTime / formatFull：列表与详情 ---- */
check('datetime/full iso', dt.formatDateTime('2026-09-12T21:28:55+08:00'), '09-12 21:28');
check('datetime/date only', dt.formatDateTime('2026-09-12'), '09-12');
check('datetime/empty', dt.formatDateTime(''), '');
check('full/full iso', dt.formatFull('2026-09-12T21:28:55+08:00'), '2026-09-12 21:28');
check('full/date only', dt.formatFull('2026-09-12'), '2026-09-12');
check('full/garbage', dt.formatFull('x'), '');

/* ---- formatRange：结果页起止区间 ---- */
check(
  'range/same day collapse',
  dt.formatRange('2026-09-12T21:28:55+08:00', '2026-09-12T21:40:10+08:00'),
  '09-12 21:28 — 21:40'
);
check(
  'range/cross day keeps date',
  dt.formatRange('2026-09-12T21:28:55+08:00', '2026-09-13T00:05:10+08:00'),
  '09-12 21:28 — 09-13 00:05'
);
check(
  'range/missing end',
  dt.formatRange('2026-09-12T21:28:55+08:00', null),
  '09-12 21:28'
);
check('range/missing start', dt.formatRange(null, '2026-09-12T21:40:10+08:00'), '');
check(
  'range/cross month',
  dt.formatRange('2026-09-30T23:50:00+08:00', '2026-10-01T00:10:00+08:00'),
  '09-30 23:50 — 10-01 00:10'
);

/* ---- toMs：北京时间墙上时钟 → UTC 时间轴 ---- */
/* 21:28:55 +08:00 即 UTC 13:28:55 */
check('toMs/offset applied', dt.toMs('2026-09-12T21:28:55+08:00'), Date.UTC(2026, 8, 12, 13, 28, 55));
check('toMs/seconds default', dt.toMs('2026-09-12T21:28+08:00'), Date.UTC(2026, 8, 12, 13, 28, 0));
/* 纯日期按北京时间当天 00:00 处理（last_training_date 就是纯日期） */
check('toMs/date only is midnight', dt.toMs('2026-09-12'), Date.UTC(2026, 8, 11, 16, 0, 0));
check('toMs/garbage is NaN', Number.isNaN(dt.toMs('x')), true);

/* ---- 排序兜底：非法值必须能被 isFinite 挡掉 ---- */
check('toMs/null is NaN', Number.isNaN(dt.toMs(null)), true);

let failed = 0;
for (const item of cases) {
  try {
    assert.deepStrictEqual(item.actual, item.expected);
  } catch (error) {
    failed += 1;
    console.error(`FAIL ${item.name}: expected ${JSON.stringify(item.expected)}, got ${JSON.stringify(item.actual)}`);
  }
}

if (failed > 0) {
  console.error(`\n${failed}/${cases.length} case(s) failed.`);
  process.exit(1);
}

console.log(JSON.stringify({ result: 'passed', cases: cases.length }));
