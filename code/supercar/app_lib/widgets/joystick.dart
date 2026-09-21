import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/physics.dart';
import '../theme.dart';

/// 水平转向摇杆
///
/// 左右拖动控制转向角度, throttle 由外部传入。
/// 50ms 一次 onChange(throttle, steer), 松开时 onRelease()。
class Joystick extends StatefulWidget {
  final double size;
  final Color? activeColor;
  final int baseThrottle;
  final void Function(int throttle, int steer)? onChange;
  final void Function()? onRelease;

  const Joystick({
    super.key,
    this.size = 200,
    this.activeColor,
    this.baseThrottle = 30,
    this.onChange,
    this.onRelease,
  });

  @override
  State<Joystick> createState() => _JoystickState();
}

class _JoystickState extends State<Joystick>
    with SingleTickerProviderStateMixin {
  double _thumbX = 0;
  bool _isDragging = false;
  Timer? _sendTimer;
  late final AnimationController _returnCtrl;

  @override
  void initState() {
    super.initState();
    _returnCtrl = AnimationController.unbounded(vsync: this)
      ..addListener(() {
        if (mounted) setState(() => _thumbX = _returnCtrl.value);
      });
  }

  int get steer => (-_thumbX * 127).round().clamp(-127, 127);

  void _emit() => widget.onChange?.call(widget.baseThrottle, steer);

  void _onDragStart(DragStartDetails d) {
    _returnCtrl.stop();
    _isDragging = true;
    _sendTimer?.cancel();
    _sendTimer = Timer.periodic(const Duration(milliseconds: 50), (_) => _emit());
    _moveThumb(d.localPosition.dx);
  }

  void _onDragUpdate(DragUpdateDetails d) => _moveThumb(d.localPosition.dx);

  void _onDragEnd(DragEndDetails details) {
    _isDragging = false;
    _sendTimer?.cancel();
    final velocity = (details.primaryVelocity ?? 0) / (widget.size * 1.8);
    _returnCtrl.animateWith(
      SpringSimulation(
        const SpringDescription(mass: 1, stiffness: 300, damping: 24),
        _thumbX,
        0,
        velocity,
      ),
    );
    // 视觉回中不延迟原有控制回调：松手时仍立即执行停车/直行判定。
    widget.onRelease?.call();
  }

  void _moveThumb(double localX) {
    final w = widget.size * 1.8;
    final offset = localX - w / 2;
    setState(() => _thumbX = (offset / (w * 0.4)).clamp(-1.0, 1.0));
  }

  @override
  void dispose() {
    _sendTimer?.cancel();
    _returnCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final color = widget.activeColor ?? Theme.of(context).colorScheme.primary;
    final w = widget.size * 1.8;
    final h = widget.size * 0.35;
    final trackH = h * 0.3;
    final thumbR = h * 0.42;
    final thumbX = w / 2 + _thumbX * (w * 0.4);

    return SizedBox(width: w, height: h,
      child: GestureDetector(
        onPanStart: _onDragStart,
        onPanUpdate: _onDragUpdate,
        onPanEnd: _onDragEnd,
        child: CustomPaint(
          size: Size(w, h),
          painter: _Painter(thumbX: thumbX, centerY: h / 2, trackH: trackH, thumbR: thumbR, color: color, isDragging: _isDragging),
        ),
      ),
    );
  }
}

class _Painter extends CustomPainter {
  final double thumbX, centerY, trackH, thumbR;
  final Color color;
  final bool isDragging;
  _Painter({required this.thumbX, required this.centerY, required this.trackH, required this.thumbR, required this.color, required this.isDragging});

  @override
  void paint(Canvas canvas, Size size) {
    final trackRect = RRect.fromRectAndRadius(
      Rect.fromCenter(center: Offset(size.width / 2, centerY), width: size.width - thumbR, height: trackH),
      Radius.circular(trackH / 2),
    );
    canvas.drawRRect(
      trackRect,
      Paint()..color = CyberpunkTheme.insetDeep..style = PaintingStyle.fill,
    );
    canvas.drawRRect(
      trackRect,
      Paint()..color = Colors.black.withAlpha(125)..style = PaintingStyle.stroke..strokeWidth = 2.5,
    );
    canvas.drawRRect(
      trackRect.inflate(-1.25),
      Paint()..color = Colors.white.withAlpha(36)..style = PaintingStyle.stroke..strokeWidth = 1.25,
    );
    canvas.drawLine(Offset(size.width / 2, centerY - trackH), Offset(size.width / 2, centerY + trackH),
      Paint()..color = color.withAlpha(58)..strokeWidth = 1.5);
    for (final x in [size.width * 0.15, size.width * 0.85]) {
      canvas.drawLine(Offset(x, centerY - trackH * 0.5), Offset(x, centerY + trackH * 0.5),
        Paint()..color = color.withAlpha(30)..strokeWidth = 1);
    }
    final thumbCenter = Offset(thumbX, centerY);
    canvas.drawCircle(
      thumbCenter.translate(5, 6),
      thumbR + 1,
      Paint()..color = Colors.black.withAlpha(isDragging ? 155 : 120),
    );
    final thumbRect = Rect.fromCircle(center: thumbCenter, radius: thumbR);
    canvas.drawCircle(
      thumbCenter,
      thumbR,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [color.withAlpha(isDragging ? 255 : 230), color.withAlpha(isDragging ? 180 : 145)],
        ).createShader(thumbRect),
    );
    canvas.drawCircle(
      thumbCenter,
      thumbR,
      Paint()..color = Colors.white.withAlpha(130)..style = PaintingStyle.stroke..strokeWidth = 1.8,
    );
    canvas.drawCircle(
      Offset(thumbX - thumbR * 0.24, centerY - thumbR * 0.25),
      thumbR * 0.34,
      Paint()..color = Colors.white.withAlpha(92)..style = PaintingStyle.fill,
    );
  }

  @override
  bool shouldRepaint(covariant _Painter o) => o.thumbX != thumbX || o.isDragging != isDragging;
}
