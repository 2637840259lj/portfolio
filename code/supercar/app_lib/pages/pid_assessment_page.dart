import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/bench_result.dart';
import '../models/pid_assessment.dart';
import '../services/app_state.dart';
import '../services/pid_schedule_service.dart';
import '../theme.dart';
import '../widgets/assessment_test_panel.dart';
import '../widgets/bench_tuning_panel.dart';
import '../widgets/pid_schedule_panel.dart';

/// 已下发 PID 的独立质量测评页。
/// 参数编辑、三次采样和评分均保持在通用台架闭环范围内，不接入竞赛状态机。
class PidAssessmentPage extends StatelessWidget {
  const PidAssessmentPage({super.key});

  static const _modes = [
    BenchMode.lineStraight,
    BenchMode.lineCurve,
    BenchMode.yaw90,
    BenchMode.distance1m,
  ];

  @override
  Widget build(BuildContext context) {
    final app = context.watch<AppState>();
    final service = context.watch<PidScheduleService>();
    final linkOk = app.bleConn.isConnected && app.handshake.linkAlive;
    return Container(
      color: CyberpunkTheme.background,
      child: Column(children: [
        _header(linkOk),
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(12, 10, 12, 18),
            child: Column(children: [
              _calibrationGate(context, app, linkOk),
              const SizedBox(height: 10),
              const PidSchedulePanel(),
              const SizedBox(height: 10),
              const BenchTuningPanel(),
              const SizedBox(height: 10),
              const AssessmentTestPanel(),
              const SizedBox(height: 10),
              _assessmentSummary(service),
            ]),
          ),
        ),
      ]),
    );
  }

  Widget _header(bool linkOk) => Container(
    width: double.infinity,
    margin: const EdgeInsets.fromLTRB(8, 8, 8, 4),
    padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      border: Border.all(color: (linkOk ? CyberpunkTheme.cyan : CyberpunkTheme.red).withAlpha(160)),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: Row(children: [
      Icon(Icons.fact_check_rounded, color: linkOk ? CyberpunkTheme.cyan : CyberpunkTheme.dim, size: 20),
      const SizedBox(width: 8),
      const Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('PID 测评', style: TextStyle(color: CyberpunkTheme.text, fontSize: 13, fontWeight: FontWeight.w700)),
        SizedBox(height: 2),
        Text('先校准，再完成三次独立测试；评分不直接修改 PID。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10)),
      ])),
      _statusChip(linkOk ? '链路在线' : '未连接', linkOk ? CyberpunkTheme.green : CyberpunkTheme.red),
    ]),
  );

  Widget _calibrationGate(BuildContext context, AppState app, bool linkOk) {
    final imuOk = app.imuCalibrationValid;
    final grayOk = app.grayCalibrationValid;
    return _card(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Row(children: [
          Icon(Icons.verified_user_outlined, size: 19, color: CyberpunkTheme.amber),
          SizedBox(width: 7),
          Text('测评前置校准', style: TextStyle(color: CyberpunkTheme.text, fontSize: 12, fontWeight: FontWeight.w700)),
        ]),
        const SizedBox(height: 7),
        const Text('循迹测试需要白底灰度标定；航向和距离停车需要陀螺仪静止校准。校准确认仅对当前连接设备和本次连接有效。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.35)),
        const SizedBox(height: 10),
        Wrap(spacing: 8, runSpacing: 8, children: [
          _calibrationChip('白底灰度标定', grayOk, grayOk ? _timeText(app.grayCalibrationAt) : '未确认'),
          _calibrationChip('陀螺仪静止校准', imuOk, imuOk ? _timeText(app.imuCalibrationAt) : '未确认'),
        ]),
        if (!linkOk) ...[
          const SizedBox(height: 8),
          const Text('请先建立握手；离线时不会把旧校准记录当作有效。', style: TextStyle(color: CyberpunkTheme.red, fontSize: 10)),
        ] else if (!grayOk || !imuOk) ...[
          const SizedBox(height: 8),
          const Text('未满足对应测试的前置项时，开始按钮会被拦截并说明原因。', style: TextStyle(color: CyberpunkTheme.amber, fontSize: 10)),
        ],
      ]),
    );
  }

  Widget _assessmentSummary(PidScheduleService service) => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      const Row(children: [
        Icon(Icons.analytics_outlined, size: 19, color: CyberpunkTheme.primary),
        SizedBox(width: 7),
        Text('当前 PID 测评结论', style: TextStyle(color: CyberpunkTheme.text, fontSize: 12, fontWeight: FontWeight.w700)),
      ]),
      const SizedBox(height: 6),
      const Text('评分顺序：安全 40 分、重复稳定性 30 分、精度 30 分。曲线允许由侧面探头压线，精度以曲线覆盖与转向余量判断，不使用质心绝对位置。缺少三次样本时不出合格结论。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.35)),
      const SizedBox(height: 10),
      if (service.assessmentNodes.isEmpty)
        const Text('请先读取 MCU 当前 PID，才能按速度节点显示只读测评。', style: TextStyle(color: CyberpunkTheme.amber, fontSize: 10))
      else
        ..._modes.expand((mode) => [
          Text(mode.label, style: const TextStyle(color: CyberpunkTheme.cyan, fontSize: 11, fontWeight: FontWeight.w700)),
          const SizedBox(height: 5),
          Wrap(spacing: 7, runSpacing: 7, children: service.assessmentNodes.map((node) {
            final score = PidAssessment.fromSession(mode, node.index, service.assessmentSessionFor(mode, node.index));
            return _scoreCard(score, node.speedCmS);
          }).toList()),
          const SizedBox(height: 10),
        ]),
    ]),
  );

  Widget _scoreCard(PidAssessment score, double speed) {
    final color = switch (score.level) {
      PidAssessmentLevel.qualified => CyberpunkTheme.green,
      PidAssessmentLevel.needsTuning => CyberpunkTheme.amber,
      PidAssessmentLevel.failed => CyberpunkTheme.red,
      PidAssessmentLevel.insufficient => CyberpunkTheme.dim,
    };
    return Container(
      width: 166,
      padding: const EdgeInsets.all(9),
      decoration: BoxDecoration(
        color: CyberpunkTheme.insetDeep,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
        border: Border.all(color: color.withAlpha(160)),
        boxShadow: CyberpunkTheme.insetShadows,
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Text('${speed.toStringAsFixed(0)} cm/s', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 10, fontWeight: FontWeight.w700)),
          const Spacer(),
          Text(score.level.label, style: TextStyle(color: color, fontSize: 10, fontWeight: FontWeight.w700)),
        ]),
        const SizedBox(height: 5),
        Text(score.level == PidAssessmentLevel.insufficient ? '${score.sampleCount}/3 样本' : '${score.totalScore}/100 分', style: TextStyle(color: color, fontSize: 17, fontWeight: FontWeight.w700)),
        if (score.level != PidAssessmentLevel.insufficient)
          Text('安全 ${score.safetyScore}  稳定 ${score.stabilityScore}  精度 ${score.accuracyScore}', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 8)),
        const SizedBox(height: 5),
        Text(score.reasons.first, maxLines: 2, overflow: TextOverflow.ellipsis, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9, height: 1.25)),
        if (score.suggestions.isNotEmpty) ...[
          const SizedBox(height: 4),
          Text(score.suggestions.first, maxLines: 2, overflow: TextOverflow.ellipsis, style: TextStyle(color: color.withAlpha(230), fontSize: 8, height: 1.25)),
        ],
      ]),
    );
  }

  Widget _card({required Widget child}) => Container(
    width: double.infinity,
    padding: const EdgeInsets.all(13),
    decoration: BoxDecoration(
      color: CyberpunkTheme.raisedSurface,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
      border: Border.all(color: CyberpunkTheme.edgeHighlight),
      boxShadow: CyberpunkTheme.raisedShadows,
    ),
    child: child,
  );

  Widget _calibrationChip(String label, bool ok, String value) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 7),
    decoration: BoxDecoration(color: CyberpunkTheme.insetDeep, borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl), border: Border.all(color: (ok ? CyberpunkTheme.green : CyberpunkTheme.amber).withAlpha(160))),
    child: Row(mainAxisSize: MainAxisSize.min, children: [
      Icon(ok ? Icons.check_circle : Icons.warning_amber_rounded, size: 14, color: ok ? CyberpunkTheme.green : CyberpunkTheme.amber),
      const SizedBox(width: 5),
      Text('$label · $value', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 10)),
    ]),
  );

  Widget _statusChip(String text, Color color) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
    decoration: BoxDecoration(color: color.withAlpha(25), borderRadius: BorderRadius.circular(20)),
    child: Text(text, style: TextStyle(color: color, fontSize: 10, fontWeight: FontWeight.w700)),
  );

  String _timeText(DateTime? time) => time == null ? '未确认' : '${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}';
}
