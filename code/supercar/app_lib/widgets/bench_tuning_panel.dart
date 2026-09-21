import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/bench_result.dart';
import '../models/bench_session.dart';
import '../services/pid_schedule_service.dart';
import '../theme.dart';

/// 独立的直线、曲线、航向和距离闭环测试入口。
/// 每一类/速度节点各自累计至少三次样本，再由 App 自动生成最终 PID。
class BenchTuningPanel extends StatelessWidget {
  const BenchTuningPanel({super.key});

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
            Icon(Icons.route_rounded, size: 20, color: CyberpunkTheme.cyan),
            SizedBox(width: 8),
            Text('闭环自动调参会话', style: TextStyle(color: CyberpunkTheme.text, fontSize: 12, fontWeight: FontWeight.w700)),
          ]),
          const SizedBox(height: 6),
          const Text('直线循迹、曲线循迹、90°航向与 1 m 停车均为独立会话。每一类在每个速度节点保存满 3 次有效样本后，App 会自动汇总并一次性提交最终 PID，同时覆盖写入该节点的 CSV；若自动提交失败可再点琥珀色汇总按钮重试。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.4)),
          const SizedBox(height: 10),
          if (service.nodes.isEmpty)
            const Text('请先在上方读取 PID 曲线，才能按速度节点启动测试。', style: TextStyle(color: CyberpunkTheme.amber, fontSize: 10))
          else ...[
            _testRow(context, service, BenchMode.lineStraight, Icons.straighten_rounded, '直线循迹 / 独立直线 PID', '将车放在直线黑线上；仅使用 line_kp / line_kd / line_max_steer。出线 250ms 自动停车。'),
            const SizedBox(height: 8),
            _testRow(context, service, BenchMode.lineCurve, Icons.timeline_rounded, '曲线循迹 / 独立曲线 PID', '将车放在连续弯道黑线上；仅使用 curve_kp / curve_kd / curve_max_steer，不会混入直线 PID。'),
            const SizedBox(height: 8),
            _testRow(context, service, BenchMode.yaw90, Icons.turn_right_rounded, '航向 / 相对 90° 原地转', 'MCU 用 MPU6050 陀螺仪积分得到相对 yaw，再经 pidAngle 闭环：目标 90°，输出差速 set_speed_targets(-steer,steer)。不是示教开环转。结束后直接用 MCU 回传 finalYaw 算误差（finalYaw−90°），无需人工读角度。'),
            const SizedBox(height: 8),
            _testRow(context, service, BenchMode.distance1m, Icons.straighten_rounded, '距离 / 1 m 停车', '每次结束须填写实际量尺距离；方向偏差可不填，未填写时不会因主观估计调整航向 PID。'),
          ],
          if (service.lastBenchResult != null) ...[
            const SizedBox(height: 11),
            _result(context, service, service.lastBenchResult!),
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
        if (service.busy) IconButton(tooltip: '停止测试', onPressed: service.abortBench, icon: const Icon(Icons.stop_circle_outlined, color: CyberpunkTheme.red, size: 19), visualDensity: VisualDensity.compact),
      ]),
      const SizedBox(height: 3),
      Text(hint, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9, height: 1.3)),
      const SizedBox(height: 7),
      Wrap(spacing: 6, runSpacing: 5, children: service.nodes.map((node) {
        final session = service.sessionFor(mode, node.index);
        final progress = session?.samples.length ?? 0;
        return Row(mainAxisSize: MainAxisSize.min, children: [
          OutlinedButton(
            onPressed: service.busy || (session?.isFinalized ?? false) || !service.canStartBench(mode)
                ? null
                : () => _confirmRun(context, service, mode, node.index, node.speedCmS),
            style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.cyan, visualDensity: VisualDensity.compact, textStyle: const TextStyle(fontSize: 10)),
            child: Text('${node.speedCmS.toStringAsFixed(0)} cm/s  $progress/${BenchSession.requiredSamples}'),
          ),
          if (session?.isReady == true && !session!.isFinalized)
            IconButton(tooltip: '汇总并提交最终 PID', onPressed: service.busy ? null : () => service.finalizeSession(mode, node.index), icon: const Icon(Icons.auto_fix_high_rounded, color: CyberpunkTheme.amber, size: 18), visualDensity: VisualDensity.compact),
          if (session != null && !session.isFinalized)
            IconButton(tooltip: '丢弃此会话', onPressed: service.busy ? null : () => service.discardSession(mode, node.index), icon: const Icon(Icons.delete_outline, color: CyberpunkTheme.dim, size: 17), visualDensity: VisualDensity.compact),
          if (session?.isFinalized == true)
            const Icon(Icons.check_circle, size: 16, color: CyberpunkTheme.green),
        ]);
      }).toList()),
    ]),
  );

  Widget _result(BuildContext context, PidScheduleService service, BenchResult result) => Container(
    width: double.infinity,
    padding: const EdgeInsets.all(10),
    decoration: BoxDecoration(color: CyberpunkTheme.insetDeep, borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl), border: Border.all(color: result.reason == 2 ? CyberpunkTheme.amber : CyberpunkTheme.green)),
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Text('${result.mode.label}结果 · ${result.reasonLabel}', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 11, fontWeight: FontWeight.w700)),
      const SizedBox(height: 5),
      Text('耗时 ${(result.elapsed.inMilliseconds / 1000).toStringAsFixed(2)} s  |  距离 ${result.distanceCm.toStringAsFixed(1)} cm  |  相对最终航向 ${result.finalYawDeg.toStringAsFixed(2)}°  |  峰值 ${result.peakYawDeg.toStringAsFixed(2)}°', style: const TextStyle(color: CyberpunkTheme.dim, fontFamily: 'monospace', fontSize: 9, height: 1.5)),
      Text('平均误差 ${result.meanAbsError.toStringAsFixed(3)}  |  平均差速 ${result.meanAbsSteer.toStringAsFixed(2)}  |  控制样本 ${result.samples}', style: const TextStyle(color: CyberpunkTheme.dim, fontFamily: 'monospace', fontSize: 9)),
      const SizedBox(height: 7),
      Builder(builder: (context) {
        final saved = service.isBenchResultRecorded(result);
        return Align(
          alignment: Alignment.centerRight,
          child: OutlinedButton.icon(
            onPressed: service.busy || saved ? null : () => _recordSample(context, service, result),
            icon: Icon(saved ? Icons.check_circle_rounded : Icons.add_task_rounded, size: 15),
            label: Text(saved ? '已保存为本轮样本' : _recordLabel(result.mode)),
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
        );
      }),
    ]),
  );

  String _recordLabel(BenchMode mode) => switch (mode) {
    BenchMode.yaw90 => '确认 MCU 角度并保存样本',
    BenchMode.distance1m => '录入实际距离并保存样本',
    _ => '保存为本轮样本',
  };

  Future<void> _confirmRun(BuildContext context, PidScheduleService service, BenchMode mode, int index, double speed) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        backgroundColor: CyberpunkTheme.surface,
        title: Text('开始 ${mode.label}？', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 16)),
        content: Text('将以 ${speed.toStringAsFixed(0)} cm/s 运行。本轮只采集样本，不会立即改 PID；完成至少 3 次后才自动汇总。请确认周围无人、路线无障碍。', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 13, height: 1.4)),
        actions: [TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('取消')), ElevatedButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('确认开始'))],
      ),
    );
    if (confirmed == true) {
      final blockReason = service.calibrationBlockReason(mode);
      if (blockReason != null) {
        if (context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('无法开始：$blockReason')));
        }
        return;
      }
      await service.runBench(mode, index);
    }
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
      builder: (dialogContext) => AlertDialog(
        backgroundColor: CyberpunkTheme.surface,
        title: Text('保存 ${result.mode.label}样本', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 16)),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          if (result.mode == BenchMode.yaw90) ...[
            Text('MCU 回传最终相对航向：${result.finalYawDeg.toStringAsFixed(2)}°', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 13, fontWeight: FontWeight.w700)),
            const SizedBox(height: 6),
            Text('本轮误差自动计算为 ${(result.finalYawDeg - 90.0).toStringAsFixed(2)}°（目标 90°），确认保存即可。', style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 12, height: 1.4)),
          ] else if (result.mode == BenchMode.distance1m) ...[
            const Text('实际距离必须用卷尺填写；方向偏差为选填，留空时仅按距离数据调整。左偏填负数，右偏填正数。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 12, height: 1.4)),
            const SizedBox(height: 10),
            TextField(controller: distance, keyboardType: const TextInputType.numberWithOptions(decimal: true), decoration: const InputDecoration(labelText: '实际距离 (cm，必填)')),
            const SizedBox(height: 8),
            TextField(controller: heading, keyboardType: const TextInputType.numberWithOptions(decimal: true, signed: true), decoration: const InputDecoration(labelText: '实际方向偏差 (°，选填)')),
          ] else
            const Text('确认将当前 MCU 实测结果保存为独立会话样本。完成三次后可自动汇总并生成最终 PID。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 12, height: 1.4)),
        ]),
        actions: [TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('取消')), ElevatedButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('保存样本'))],
      ),
    );
    if (accepted == true) {
      final saved = await service.recordBenchSample(
        result: result,
        manualTurnErrorDeg: null,
        actualDistanceCm: result.mode == BenchMode.distance1m ? double.tryParse(distance.text.trim()) : null,
        manualHeadingErrorDeg: result.mode == BenchMode.distance1m && heading.text.trim().isNotEmpty ? double.tryParse(heading.text.trim()) : null,
      );
      if (context.mounted) {
        final session = service.sessionFor(result.mode, result.nodeIndex);
        final autoDone = saved && (session?.isFinalized ?? false);
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text(autoDone
              ? '3 次样本已汇总：最终 PID 已提交，CSV 已保存（${session?.csvUri ?? '本地下载目录'}）'
              : (saved ? service.status : service.status)),
          backgroundColor: saved ? CyberpunkTheme.success : CyberpunkTheme.danger,
        ));
      }
    }
    Future<void>.delayed(const Duration(milliseconds: 300), () {
      distance.dispose();
      heading.dispose();
    });
  }
}
