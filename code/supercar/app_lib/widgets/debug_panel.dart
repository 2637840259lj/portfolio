import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/app_state.dart';
import '../services/log_file_service.dart';
import '../theme.dart';
import 'press_scale.dart';

/// 调试面板 — 协议数据 + App 日志
class DebugPanel extends StatefulWidget {
  const DebugPanel({super.key});

  static void show(BuildContext context) {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => const DebugPanel(),
    );
  }

  @override
  State<DebugPanel> createState() => _DebugPanelState();
}

class _DebugPanelState extends State<DebugPanel> {
  final ScrollController _protoSC = ScrollController();
  final ScrollController _appSC = ScrollController();
  bool _autoScroll = true;
  DateTime? _lastSave;

  @override
  void dispose() {
    _protoSC.dispose();
    _appSC.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final app = context.watch<AppState>();
    final protoReversed = app.protoLogs.reversed.toList();
    final appReversed = app.logs.reversed.toList();

    return DraggableScrollableSheet(
      initialChildSize: 0.55, minChildSize: 0.25, maxChildSize: 0.9, expand: false,
      builder: (ctx, scrollCtrl) => Container(
        decoration: const BoxDecoration(
          color: CyberpunkTheme.background,
          borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
          border: Border(top: BorderSide(color: CyberpunkTheme.edgeHighlight, width: 1)),
        ),
        child: Column(children: [
        Container(margin: const EdgeInsets.only(top: 10), width: 42, height: 4,
          decoration: BoxDecoration(color: CyberpunkTheme.dim, borderRadius: BorderRadius.circular(2))),
        const SizedBox(height: 6),
        Row(children: [
          const SizedBox(width: 14),
          const Icon(Icons.terminal, size: 15, color: CyberpunkTheme.primary),
          const SizedBox(width: 7),
          Text('协议 ${app.protoLogs.length} / App ${app.logs.length}',
            style: const TextStyle(fontSize: 11, color: CyberpunkTheme.dim, fontFamily: 'monospace')),
          const Spacer(),
          _btn(Icons.vertical_align_bottom, _autoScroll ? CyberpunkTheme.success : CyberpunkTheme.dim,
            () => setState(() => _autoScroll = !_autoScroll)),
          _btn(Icons.delete_sweep, CyberpunkTheme.dim, () {
            app.clearLogs();
            app.protoLogs.clear();
            setState(() {});
          }),
          PopupMenuButton<String>(
            icon: const Icon(Icons.save_alt, size: 15, color: CyberpunkTheme.success),
            color: CyberpunkTheme.raisedSurface,
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            onSelected: (v) => _exportLogs(v, app),
            itemBuilder: (_) => [
              const PopupMenuItem(value: 'proto', child: Text('协议.csv', style: TextStyle(fontSize: 12, color: Color(0xFF6EE7B7)))),
              const PopupMenuItem(value: 'app', child: Text('App日志.csv', style: TextStyle(fontSize: 12, color: Color(0xFFE2E8F0)))),
              const PopupMenuItem(value: 'both', child: Text('全部', style: TextStyle(fontSize: 12, color: Color(0xFF94A3B8)))),
            ],
          ),
          const SizedBox(width: 6),
        ]),
        const Divider(height: 1, color: Color(0xFF27273B)),
        Expanded(child: Row(children: [
          Expanded(child: protoReversed.isEmpty
            ? const Center(child: Text('协议数据…', style: TextStyle(color: Color(0xFF64748B), fontSize: 11)))
            : ListView.builder(controller: _protoSC, itemCount: protoReversed.length,
                itemBuilder: (_, i) => Text(protoReversed[i],
                  style: const TextStyle(fontSize: 9.5, color: Color(0xFF6EE7B7), fontFamily: 'monospace', height: 1.4)))),
          const VerticalDivider(width: 1, color: Color(0xFF27273B)),
          Expanded(child: appReversed.isEmpty
            ? const Center(child: Text('App 日志…', style: TextStyle(color: Color(0xFF64748B), fontSize: 11)))
            : ListView.builder(controller: _appSC, itemCount: appReversed.length,
                itemBuilder: (_, i) => Text(appReversed[i],
                  style: const TextStyle(fontSize: 9.5, color: Color(0xFFE2E8F0), fontFamily: 'monospace', height: 1.4)))),
        ])),
        Container(
          margin: const EdgeInsets.fromLTRB(12, 6, 12, 10),
          child: PressScale(
            pressedScale: 0.97,
            onTap: () => Navigator.pop(context),
            child: Container(
              width: double.infinity,
              padding: const EdgeInsets.symmetric(vertical: 10),
              decoration: BoxDecoration(
                color: CyberpunkTheme.raisedSurface,
                borderRadius: BorderRadius.circular(CyberpunkTheme.radiusControl),
                border: Border.all(color: CyberpunkTheme.edgeHighlight),
                boxShadow: CyberpunkTheme.raisedShadows,
              ),
              child: const Center(
                child: Text(
                  '关闭',
                  style: TextStyle(fontSize: 12, color: CyberpunkTheme.dim, fontWeight: FontWeight.w600),
                ),
              ),
            ),
          ),
        ),
        ]),
      ),
    );
  }

  Widget _btn(IconData icon, Color color, VoidCallback onTap) {
    return PressScale(
      pressedScale: 0.9,
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 6),
        child: Icon(icon, size: 15, color: color),
      ),
    );
  }

  Future<void> _exportLogs(String which, AppState app) async {
    final now = DateTime.now();
    if (_lastSave != null && now.difference(_lastSave!) < const Duration(seconds: 2)) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('请稍后再保存')));
      return;
    }
    _lastSave = now;

    try {
      final content = switch (which) {
        'proto' => app.protoLogs.join('\n'),
        'app' => app.logs.join('\n'),
        _ => <String>[
            '=== MCU 协议日志 ===',
            ...app.protoLogs,
            '',
            '=== App 日志 ===',
            ...app.logs,
          ].join('\n'),
      };
      final fileName = switch (which) {
        'proto' => 'mcu_protocol_log.csv',
        'app' => 'app_log.csv',
        _ => 'aicar_log.csv',
      };
      // Android 端会保留 Download/Aicar 中的历史日志，便于对比不同测试会话。
      final uri = await LogFileService.saveLatest(fileName: fileName, content: content);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('已保存到 下载/Aicar/$fileName', style: const TextStyle(fontSize: 11)),
        action: SnackBarAction(label: '打开', onPressed: () => _openFile(uri)),
        duration: const Duration(seconds: 5),
      ));
    } catch (e) {
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('保存失败: $e')));
    }
  }

  Future<void> _openFile(String uri) async {
    try {
      final opened = await LogFileService.open(uri);
      if (!opened && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('未找到可打开 CSV 的应用')),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('打开失败: $e')));
      }
    }
  }
}
