import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../models/pid_schedule_node.dart';
import '../services/pid_schedule_service.dart';
import '../theme.dart';

/// 速度自适应 PID 调试面板：App 只做慢速测试和节点配置，MCU 保持本地实时闭环。
class PidSchedulePanel extends StatelessWidget {
  const PidSchedulePanel({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<PidScheduleService>(
      builder: (context, service, _) {
        return Container(
          width: double.infinity,
          padding: const EdgeInsets.all(13),
          decoration: BoxDecoration(
            color: CyberpunkTheme.raisedSurface,
            borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
            border: Border.all(color: CyberpunkTheme.edgeHighlight),
            boxShadow: CyberpunkTheme.raisedShadows,
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Row(children: [
                Icon(Icons.tune_rounded, color: CyberpunkTheme.primary, size: 20),
                SizedBox(width: 8),
                Text('速度自适应 PID', style: TextStyle(color: CyberpunkTheme.text, fontSize: 12, fontWeight: FontWeight.w700)),
              ]),
              const SizedBox(height: 6),
              const Text('可编辑速度、循迹、航向与位置节点参数；位置 P、减速脉冲、末速和距离比例用于 1 m 距离台架。仅停车状态可提交，MCU 运行中禁止写参。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 10, height: 1.35)),
              const SizedBox(height: 9),
              _actions(context, service),
              const SizedBox(height: 8),
              Text(service.status, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 10)),
              if (service.nodes.isNotEmpty) ...[
                const SizedBox(height: 10),
                for (final node in service.nodes) _node(context, service, node),
              ],
            ],
          ),
        );
      },
    );
  }

  Widget _actions(BuildContext context, PidScheduleService service) => Row(
    children: [
      Expanded(
        child: OutlinedButton.icon(
          onPressed: service.busy ? null : service.load,
          icon: const Icon(Icons.download_rounded, size: 15),
          label: const Text('读取曲线'),
          style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.cyan),
        ),
      ),
      const SizedBox(width: 8),
      Expanded(
        child: OutlinedButton.icon(
          onPressed: service.busy || service.nodes.length != 3 ? null : service.saveAll,
          icon: const Icon(Icons.save_rounded, size: 15),
          label: const Text('提交 MCU'),
          style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.amber),
        ),
      ),
      const SizedBox(width: 8),
      Expanded(
        child: OutlinedButton.icon(
          onPressed: service.busy || service.nodes.isEmpty ? null : () => _copy(context, service.exportCConfig()),
          icon: const Icon(Icons.content_copy_rounded, size: 15),
          label: const Text('复制 C 快照'),
          style: OutlinedButton.styleFrom(foregroundColor: CyberpunkTheme.green),
        ),
      ),
    ],
  );

  Widget _node(BuildContext context, PidScheduleService service, PidScheduleNode node) => Container(
    margin: const EdgeInsets.only(bottom: 7),
    padding: const EdgeInsets.all(10),
    decoration: BoxDecoration(
      color: CyberpunkTheme.insetDeep,
      borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
      border: Border.all(color: CyberpunkTheme.edgeHighlight),
      boxShadow: CyberpunkTheme.insetShadows,
    ),
    child: Column(
      children: [
        Row(children: [
          Text('${node.speedCmS.toStringAsFixed(0)} cm/s 节点', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 11, fontWeight: FontWeight.w700)),
          const Spacer(),
          Text('Kp ${node.speedKp.toStringAsFixed(1)}  Kd ${node.speedKd.toStringAsFixed(2)}  Kf ${node.speedKf.toStringAsFixed(1)}', style: const TextStyle(color: CyberpunkTheme.dim, fontFamily: 'monospace', fontSize: 9)),
          IconButton(
            tooltip: '编辑节点',
            onPressed: service.busy ? null : () => _editNode(context, service, node),
            icon: const Icon(Icons.edit_rounded, size: 16, color: CyberpunkTheme.cyan),
            visualDensity: VisualDensity.compact,
          ),
        ]),
        const SizedBox(height: 7),
        Row(children: [
          Expanded(child: _metric('直线', 'P ${node.lineKp.toStringAsExponential(2)} / D ${node.lineKd.toStringAsExponential(2)}')),
          Expanded(child: _metric('曲线', 'P ${node.curveKp.toStringAsExponential(2)} / D ${node.curveKd.toStringAsExponential(2)}')),
          Expanded(child: _metric('航向', 'P ${node.yawKp.toStringAsFixed(2)} / D ${node.yawKd.toStringAsFixed(2)}')),
          Expanded(child: _metric('位置', 'P ${node.posKp.toStringAsFixed(5)}')),
          Expanded(child: _metric('减速/末速', '${node.decelPulses.round()} / ${node.endSpeed.toStringAsFixed(1)}')),
          Expanded(child: _metric('距离标定', node.distanceScale.toStringAsFixed(4))),
          const SizedBox(width: 6),
          SizedBox(
            height: 33,
            child: ElevatedButton.icon(
              onPressed: service.busy ? null : () => _confirmTune(context, service, node),
              icon: const Icon(Icons.play_arrow_rounded, size: 15),
              label: const Text('自动整定'),
              style: ElevatedButton.styleFrom(backgroundColor: CyberpunkTheme.primary, foregroundColor: Colors.white, textStyle: const TextStyle(fontSize: 10)),
            ),
          ),
        ]),
      ],
    ),
  );

  Widget _metric(String title, String value) => Padding(
    padding: const EdgeInsets.only(right: 6),
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Text(title, style: const TextStyle(color: CyberpunkTheme.dim, fontSize: 9)),
      const SizedBox(height: 2),
      Text(value, overflow: TextOverflow.ellipsis, style: const TextStyle(color: CyberpunkTheme.text, fontSize: 9, fontFamily: 'monospace')),
    ]),
  );

  Future<void> _editNode(BuildContext context, PidScheduleService service, PidScheduleNode source) async {
    final node = source.copy();
    final fields = <String, TextEditingController>{
      '速度 cm/s': TextEditingController(text: node.speedCmS.toStringAsFixed(1)),
      '速度 Kp': TextEditingController(text: node.speedKp.toStringAsFixed(4)),
      '速度 Ki': TextEditingController(text: node.speedKi.toStringAsFixed(4)),
      '速度 Kd': TextEditingController(text: node.speedKd.toStringAsFixed(4)),
      '速度 Kf': TextEditingController(text: node.speedKf.toStringAsFixed(4)),
      '直线 Kp': TextEditingController(text: node.lineKp.toStringAsExponential(6)),
      '直线 Kd': TextEditingController(text: node.lineKd.toStringAsExponential(6)),
      '直线最大差速': TextEditingController(text: node.lineMaxSteer.toStringAsFixed(2)),
      '曲线 Kp': TextEditingController(text: node.curveKp.toStringAsExponential(6)),
      '曲线 Kd': TextEditingController(text: node.curveKd.toStringAsExponential(6)),
      '曲线最大差速': TextEditingController(text: node.curveMaxSteer.toStringAsFixed(2)),
      '曲线误差阈值': TextEditingController(text: node.curveErrorTrigger.toStringAsFixed(1)),
      '航向 Kp': TextEditingController(text: node.yawKp.toStringAsFixed(4)),
      '航向 Kd': TextEditingController(text: node.yawKd.toStringAsFixed(4)),
      '航向最大差速': TextEditingController(text: node.yawMaxSteer.toStringAsFixed(2)),
      '位置 Kp': TextEditingController(text: node.posKp.toStringAsFixed(5)),
      '减速距离 脉冲': TextEditingController(text: node.decelPulses.toStringAsFixed(1)),
      '末速 cm/s': TextEditingController(text: node.endSpeed.toStringAsFixed(2)),
      '距离比例': TextEditingController(text: node.distanceScale.toStringAsFixed(4)),
    };
    final saved = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        backgroundColor: CyberpunkTheme.surface,
        title: Text('编辑 ${source.speedCmS.toStringAsFixed(0)} cm/s 节点', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 16)),
        content: SizedBox(
          width: 460,
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: fields.entries.map((entry) => Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: TextField(
                  controller: entry.value,
                  keyboardType: const TextInputType.numberWithOptions(decimal: true, signed: false),
                  style: const TextStyle(color: CyberpunkTheme.text, fontSize: 13),
                  decoration: InputDecoration(labelText: entry.key, isDense: true),
                ),
              )).toList(),
            ),
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('取消')),
          ElevatedButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('保存候选值')),
        ],
      ),
    );
    if (saved == true) {
      double value(String key, double fallback) => double.tryParse(fields[key]!.text.trim()) ?? fallback;
      node.speedCmS = value('速度 cm/s', node.speedCmS);
      node.speedKp = value('速度 Kp', node.speedKp);
      node.speedKi = value('速度 Ki', node.speedKi);
      node.speedKd = value('速度 Kd', node.speedKd);
      node.speedKf = value('速度 Kf', node.speedKf);
      node.lineKp = value('直线 Kp', node.lineKp);
      node.lineKd = value('直线 Kd', node.lineKd);
      node.lineMaxSteer = value('直线最大差速', node.lineMaxSteer);
      node.curveKp = value('曲线 Kp', node.curveKp);
      node.curveKd = value('曲线 Kd', node.curveKd);
      node.curveMaxSteer = value('曲线最大差速', node.curveMaxSteer);
      node.curveErrorTrigger = value('曲线误差阈值', node.curveErrorTrigger);
      node.yawKp = value('航向 Kp', node.yawKp);
      node.yawKd = value('航向 Kd', node.yawKd);
      node.yawMaxSteer = value('航向最大差速', node.yawMaxSteer);
      node.posKp = value('位置 Kp', node.posKp);
      node.decelPulses = value('减速距离 脉冲', node.decelPulses);
      node.endSpeed = value('末速 cm/s', node.endSpeed);
      node.distanceScale = value('距离比例', node.distanceScale);
      service.replaceNode(node);
    }
    // Dialog 路由退出动画期间 TextField 可能仍会读取 controller；延后释放，
    // 与测评保存弹窗保持一致，避免关闭编辑窗口时出现 disposed controller 红屏。
    Future<void>.delayed(const Duration(milliseconds: 300), () {
      for (final controller in fields.values) {
        controller.dispose();
      }
    });
  }

  Future<void> _confirmTune(BuildContext context, PidScheduleService service, PidScheduleNode node) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        backgroundColor: CyberpunkTheme.surface,
        title: Text('整定 ${node.speedCmS.toStringAsFixed(0)} cm/s 节点？', style: const TextStyle(color: CyberpunkTheme.text, fontSize: 16)),
        content: const Text('小车必须架空、静止，且未处于循迹或遥控运行状态。MCU 将执行约 4.5 秒阶跃测试并回传均速、峰值和波动；App 会按实测误差比例计算当前速度节点的候选值。', style: TextStyle(color: CyberpunkTheme.dim, fontSize: 13, height: 1.4)),
        actions: [
          TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('取消')),
          ElevatedButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('已架空，开始')),
        ],
      ),
    );
    if (confirmed == true) await service.autoTuneSpeedNode(node.index);
  }

  void _copy(BuildContext context, String text) {
    Clipboard.setData(ClipboardData(text: text));
    ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('已复制 MCU C 配置快照')));
  }
}
