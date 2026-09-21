import 'package:flutter/material.dart';

/// 全局可拖动急停悬浮按钮
///
/// 连接后覆盖在所有 Tab 页上层, 可长按拖动到任意位置。
/// onTap 发送 EMG 急停命令 (只停车, 不断蓝牙)。
class EmergencyFab extends StatefulWidget {
  final VoidCallback onTap;
  final bool enabled;

  const EmergencyFab({super.key, required this.onTap, this.enabled = true});

  @override
  State<EmergencyFab> createState() => _EmergencyFabState();
}

class _EmergencyFabState extends State<EmergencyFab>
    with SingleTickerProviderStateMixin {
  /// 拖动位置 (null = 默认右上角)
  Offset? _pos;
  late AnimationController _pulse;
  bool _pressed = false;

  static const _danger = Color(0xFFEF4444);
  static const _dim = Color(0xFF64748B);
  static const _size = 64.0;

  @override
  void initState() {
    super.initState();
    _pulse = AnimationController(
        vsync: this, duration: const Duration(seconds: 2))
      ..repeat(reverse: true);
  }

  @override
  void dispose() {
    _pulse.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final mq = MediaQuery.of(context);
    final screenW = mq.size.width;
    final screenH = mq.size.height;
    // 默认右上角 (避开 AppBar)
    final defaultPos = Offset(screenW - _size - 16, mq.padding.top + 48);
    final pos = _pos ?? defaultPos;
    // 边界约束
    final minX = 8.0;
    final maxX = screenW - _size - 8;
    final minY = mq.padding.top + 8;
    final maxY = screenH - _size - 8;

    return Positioned(
      left: pos.dx.clamp(minX, maxX),
      top: pos.dy.clamp(minY, maxY),
      child: GestureDetector(
        onPanUpdate: (d) {
          setState(() {
            final cur = _pos ?? defaultPos;
            _pos = Offset(
              (cur.dx + d.delta.dx).clamp(minX, maxX),
              (cur.dy + d.delta.dy).clamp(minY, maxY),
            );
          });
        },
        onTapDown: widget.enabled ? (_) => setState(() => _pressed = true) : null,
        onTapUp: widget.enabled ? (_) => setState(() => _pressed = false) : null,
        onTapCancel: widget.enabled ? () => setState(() => _pressed = false) : null,
        onTap: widget.enabled ? widget.onTap : null,
        child: AnimatedScale(
          scale: _pressed ? 0.93 : 1,
          duration: const Duration(milliseconds: 100),
          curve: Curves.easeOut,
          child: AnimatedBuilder(
            animation: _pulse,
            builder: (_, __) => Container(
            width: _size,
            height: _size,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              gradient: widget.enabled
                  ? const LinearGradient(
                      begin: Alignment.topLeft,
                      end: Alignment.bottomRight,
                      colors: [Color(0xFFF87171), Color(0xFFDC2626)],
                    )
                  : null,
              color: widget.enabled ? null : _dim,
              border: Border.all(
                color: widget.enabled
                    ? Colors.white.withAlpha(150)
                    : const Color(0xFF2D2D4A),
                width: 2.2,
              ),
              boxShadow: widget.enabled
                  ? [
                      BoxShadow(
                          color: _danger.withAlpha(90 + (25 * _pulse.value).round()),
                          blurRadius: 20,
                          spreadRadius: 4),
                      const BoxShadow(color: Color(0x99050812), offset: Offset(6, 8), blurRadius: 14),
                      const BoxShadow(color: Color(0x1AFFFFFF), offset: Offset(-2, -2), blurRadius: 6),
                    ]
                  : const [
                      BoxShadow(color: Color(0x66080816), offset: Offset(5, 5), blurRadius: 10),
                      BoxShadow(color: Color(0x1AFFFFFF), offset: Offset(-2, -2), blurRadius: 5),
                    ],
            ),
            child: Stack(
              alignment: Alignment.center,
              children: [
                Icon(Icons.emergency_rounded,
                    color: widget.enabled ? Colors.white : _dim, size: 28),
                Positioned(
                  bottom: 6,
                  child: Text('急停',
                      style: TextStyle(
                          color: widget.enabled ? Colors.white : _dim,
                          fontSize: 9,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 1)),
                ),
              ],
            ),
            ),
          ),
        ),
      ),
    );
  }
}
