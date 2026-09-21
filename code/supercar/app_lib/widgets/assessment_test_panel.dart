import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/bench_result.dart';
import '../models/bench_session.dart';
import '../services/pid_schedule_service.dart';
import '../theme.dart';

/// 已固化 PID 的只读测评入口。
/// 它和自动调参会话完全隔离：只运行、记录和评分，绝不生成候选 PID 或写入 MCU。
class AssessmentTestPanel extends StatelessWidget {
  const AssessmentTestPanel({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<PidScheduleService>(
      builder: (context, service, _) => Container(
        width: double.infinity,
        padding: const EdgeInsets.all(13),
        decoration: BoxDecoration(
          color: CyberpunkTheme.raisedSurface,
          borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
          border: Border.all(color: CyberpunkTheme.edgeHighlight),
          boxShadow: CyberpunkTheme.raisedShadows,
        ),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Row(children: [
            Icon(Icons.fact_check_outlined, size: 20, color: CyberpunkTheme.cyan),
            SizedBox(width: 8),
            Text('已固化 PID · 只读测评', style: TextStyle(color: CyberpunkTheme.text, fontSize: 12, fontWeight: FontWeight.w700)),
          ]),
          const SizedBox(height: 6),
          const Text('从 MCU 读取当前已固化 PID 后，可按闭环类型和速度独立跑 3 次并评分。本区域不会修改参数、生成候选值、导出调参 CSV 或提交 MCU。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.4)),
          const SizedBox(height: 9),
          Row(children: [
            Expanded(
              child: OutlinedButton.icon(
                onPressed: service.busy ? null : service.loadAssessment,
                icon: const Icon(Icons.download_rounded, size: 15),
                label: const Text('读取 MCU 当前 PID'),
                style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.cyan),
              ),
            ),
            if (service.assessmentBusy) ...[
              const SizedBox(width: 7),
              IconButton(
                tooltip: '停止当前只读测评',
                onPressed: service.abortAssessment,
                icon: const Icon(Icons.stop_circle_outlined, color: CyberpunkTheme.red, size: 20),
                visualDensity: VisualDensity.compact,
              ),
            ],
          ]),
          const SizedBox(height: 6),
          Text(service.status, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10)),
          if (service.assessmentNodes.isEmpty) ...[
            const SizedBox(height: 8),
            const Text('请先读取 MCU 当前 PID，测评将固定使用读回的速度节点。', style: TextStyle(color: CyberpunkTheme.amber, fontSize: 10)),
          ] else ...[
            const SizedBox(height: 10),
            _testRow(context, service, BenchMode.lineStraight, Icons.straighten_rounded, '直线循迹', '线段终点以检测到出线并自动停车作为正常完成。'),
            const SizedBox(height: 8),
            _testRow(context, service, BenchMode.lineCurve, Icons.timeline_rounded, '曲线循迹', '允许侧面探头压线；评分不以侧向质心绝对位置判零。'),
            const SizedBox(height: 8),
            _testRow(context, service, BenchMode.yaw90, Icons.turn_right_rounded, '相对 90° 航向', '保存时直接采用 MCU 回传的最终相对航向；误差自动按“最终角度 - 90°”计算。'),
            const SizedBox(height: 8),
            _testRow(context, service, BenchMode.distance1m, Icons.straighten_rounded, '1 m 距离停车', '每次结束后填写卷尺实测距离，方向偏差可选。'),
          ],
          if (service.lastAssessmentResult != null) ...[
            const SizedBox(height: 11),
            _result(context, service, service.lastAssessmentResult!),
          ],
        ]),
      ),
    );
  }

  Widget _testRow(BuildContext context, PidScheduleService service, BenchMode mode, IconData icon, String title, String hint) => Container(
    padding: const EdgeInsets.all(9),
    decoration: BoxDecoration(
      color: CyberpunkTheme.insetDeep,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
      border: Border.all(color: CyberpunkTheme.edgeHighlight),
      boxShadow: CyberpunkTheme.insetShadows,
    ),
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        Icon(icon, size: 16, color: CyberpunkTheme.primary),
        const SizedBox(width: 6),
        Expanded(child: Text(title, style: const TextStyle(color: CyberpunkTheme.text, fontSize: 11, fontWeight: FontWeight.w700))),
      ]),
      const SizedBox(height: 3),
      Text(hint, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9, height: 1.3)),
      const SizedBox(height: 7),
      Wrap(spacing: 6, runSpacing: 5, children: service.assessmentNodes.map((node) {
        final session = service.assessmentSessionFor(mode, node.index);
        final progress = session?.samples.length ?? 0;
        final complete = session?.isReady ?? false;
        return Row(mainAxisSize: MainAxisSize.min, children: [
          OutlinedButton(
            onPressed: service.busy || complete || !service.canStartBench(mode) ? null : () => _confirmRun(context, service, mode, node.index, node.speedCmS),
            style: OutlinedButton.styleFrom(
              foregroundColor: complete ? CyberpunkTheme.green : CyberpunkTheme.cyan,
              visualDensity: VisualDensity.compact,
              textStyle: const TextStyle(fontSize: 10),
            ),
            child: Text('${node.speedCmS.toStringAsFixed(0)} cm/s  $progress/${BenchSession.requiredSamples}'),
          ),
          if (session != null)
            IconButton(
              tooltip: '清空本节点只读测评记录',
              onPressed: service.busy ? null : () => service.clearAssessmentSession(mode, node.index),
              icon: const Icon(Icons.refresh_rounded, color: CyberpunkTheme.dim, size: 17),
              visualDensity: VisualDensity.compact,
            ),
          if (complete) const Icon(Icons.verified_rounded, size: 16, color: CyberpunkTheme.green),
        ]);
      }).toList()),
    ]),
  );

  Widget _result(BuildContext context, PidScheduleService service, BenchResult result) {
    final saved = service.isAssessmentResultRecorded(result);
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: CyberpunkTheme.insetDeep,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
        border: Border.all(color: result.reason == 2 ? CyberpunkTheme.amber : CyberpunkTheme.green),
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('${result.mode.label}测评结果 · ${result.reasonLabel}', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 11, fontWeight: FontWeight.w700)),
        const SizedBox(height: 5),
        Text('耗时 ${(result.elapsed.inMilliseconds / 1000).toStringAsFixed(2)} s  |  距离 ${result.distanceCm.toStringAsFixed(1)} cm  |  最终航向 ${result.finalYawDeg.toStringAsFixed(2)}°', style: const TextStyle(color: CyberpunkTheme.dim, fontFamily: 'monospace', fontSize: 9, height: 1.5)),
        Text('平均误差 ${result.meanAbsError.toStringAsFixed(3)}  |  平均差速 ${result.meanAbsSteer.toStringAsFixed(2)}  |  控制样本 ${result.samples}', style: const TextStyle(color: CyberpunkTheme.dim, fontFamily: 'monospace', fontSize: 9)),
        const SizedBox(height: 7),
        Align(
          alignment: Alignment.centerRight,
          child: OutlinedButton.icon(
            onPressed: service.busy || saved ? null : () => _recordSample(context, service, result),
            icon: Icon(saved ? Icons.check_circle_rounded : Icons.add_task_rounded, size: 15),
            label: Text(saved ? '已保存为测评样本' : _recordLabel(result.mode)),
            style: OutlinedButton.styleFrom(
              foregroundColor: saved ? CyberpunkTheme.text : CyberpunkTheme.amber,
              backgroundColor: saved ? CyberpunkTheme.darkGray : null,
              disabledForegroundColor: CyberpunkTheme.text,
              disabledBackgroundColor: CyberpunkTheme.darkGray,
              side: BorderSide(color: saved ? CyberpunkTheme.darkBorder : CyberpunkTheme.amber),
              visualDensity: VisualDensity.compact,
              textStyle: const TextStyle(fontSize: 10),
            ),
          ),
        ),
      ]),
    );
  }

  String _recordLabel(BenchMode mode) => switch (mode) {
    BenchMode.yaw90 => '按 MCU 角度保存测评',
    BenchMode.distance1m => '录入实际距离并保存测评',
    _ => '保存为本轮测评样本',
  };

  Future<void> _confirmRun(BuildContext context, PidScheduleService service, BenchMode mode, int index, double speed) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        backgroundColor: CyberpunkTheme.surface,
        title: Text('测评 ${mode.label}？', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 16)),
        content: Text('将使用 MCU 当前已固化的 ${speed.toStringAsFixed(0)} cm/s PID 运行。结果只进入只读测评记录，不会调整、候选或提交任何 PID。请确认周围无人、路线无障碍。', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 13, height: 1.4)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('取消')),
          ElevatedButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('确认测评')),
        ],
      ),
    );
    if (confirmed == true) await service.runAssessment(mode, index);
  }

  double? _parseMeasurement(String text) {
    final normalized = text.trim().replaceAll('－', '-').replaceAll('＋', '+');
    final match = RegExp(r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)').firstMatch(normalized);
    return match == null ? null : double.tryParse(match.group(0)!);
  }

  Future<void> _recordSample(BuildContext context, PidScheduleService service, BenchResult result) async {
    final distance = TextEditingController(text: result.distanceCm.toStringAsFixed(1));
    final heading = TextEditingController();
    final accepted = await showDialog<bool>(
      context: context,
      builder: (dialogContext) {
        final media = MediaQuery.of(dialogContext);
        // 不再使用 AlertDialog 的内部固定内容/按钮间距；键盘弹出时由整个 Dialog
        // 在“屏幕高度 - 键盘高度”内重新分配标题、可滚动表单和操作区。
        final maxHeight = media.size.height - media.viewInsets.bottom - 32.0;
        return Dialog(
          backgroundColor: CyberpunkTheme.surface,
          insetPadding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
          child: ConstrainedBox(
            constraints: BoxConstraints(maxWidth: 420, maxHeight: maxHeight),
            child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(24, 20, 24, 10),
                child: Text('保存 ${result.mode.label}测评样本', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 16)),
              ),
              Flexible(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(24, 4, 24, 12),
                  child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                    if (result.mode == BenchMode.yaw90) ...[
                      Text('MCU 回传最终相对航向：${result.finalYawDeg.toStringAsFixed(2)}°', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 13, fontWeight: FontWeight.w700)),
                      const SizedBox(height: 6),
                      Text('本轮误差自动计算为 ${(result.finalYawDeg - 90.0).toStringAsFixed(2)}°（目标 90°），确认保存即可。', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 12, height: 1.4)),
                    ] else if (result.mode == BenchMode.distance1m) ...[
                      const Text('请使用卷尺填写实际距离；方向偏差可选。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 12, height: 1.4)),
                      const SizedBox(height: 10),
                      TextField(controller: distance, keyboardType: const TextInputType.numberWithOptions(decimal: true), decoration: const InputDecoration(labelText: '实际距离 (cm，必填)')),
                      const SizedBox(height: 8),
                      TextField(controller: heading, keyboardType: const TextInputType.numberWithOptions(decimal: true, signed: true), decoration: const InputDecoration(labelText: '实际方向偏差 (°，选填)')),
                    ] else
                      const Text('确认将当前 MCU 实测结果保存为只读测评样本；三次完成后显示评分，不会改 PID。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 12, height: 1.4)),
                  ]),
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                child: OverflowBar(
                  alignment: MainAxisAlignment.end,
                  spacing: 8,
                  overflowSpacing: 4,
                  children: [
                    TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('取消')),
                    ElevatedButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('保存测评样本')),
                  ],
                ),
              ),
            ]),
          ),
        );
      },
    );
    if (accepted == true) {
      final saved = await service.recordAssessmentSample(
        result: result,
        manualTurnErrorDeg: null,
        actualDistanceCm: result.mode == BenchMode.distance1m ? double.tryParse(distance.text.trim()) : null,
        manualHeadingErrorDeg: result.mode == BenchMode.distance1m && heading.text.trim().isNotEmpty ? _parseMeasurement(heading.text) : null,
      );
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text(saved ? '本轮测评样本已保存；PID 未改动' : service.status),
          backgroundColor: saved ? CyberpunkTheme.success : CyberpunkTheme.danger,
        ));
      }
    }
    // Dialog 的退出动画仍可能持有 TextField；延后越过路由退出动画再释放控制器，
    // 避免关闭对话框后 Animated TextField 继续访问已 dispose 的 controller。
    Future<void>.delayed(const Duration(milliseconds: 300), () {
      distance.dispose();
      heading.dispose();
    });
  }
}
