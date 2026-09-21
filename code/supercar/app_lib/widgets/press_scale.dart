import 'package:flutter/material.dart';
import '../theme.dart';

/// 为点击控件提供轻量按压反馈；不改变任何业务回调或事件时机。
class PressScale extends StatefulWidget {
  final Widget child;
  final GestureTapDownCallback? onTapDown;
  final GestureTapUpCallback? onTapUp;
  final GestureTapCancelCallback? onTapCancel;
  final Future<void> Function()? onTapAsync;
  final VoidCallback? onTap;
  final bool enabled;
  final double pressedScale;

  const PressScale({
    super.key,
    required this.child,
    this.onTapDown,
    this.onTapUp,
    this.onTapCancel,
    this.onTapAsync,
    this.onTap,
    this.enabled = true,
    this.pressedScale = 0.96,
  });

  @override
  State<PressScale> createState() => _PressScaleState();
}

class _PressScaleState extends State<PressScale> {
  bool _pressed = false;

  void _setPressed(bool value) {
    if (_pressed != value) setState(() => _pressed = value);
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTapDown: widget.enabled
          ? (details) {
              _setPressed(true);
              widget.onTapDown?.call(details);
            }
          : null,
      onTapUp: widget.enabled
          ? (details) {
              _setPressed(false);
              widget.onTapUp?.call(details);
            }
          : null,
      onTapCancel: widget.enabled
          ? () {
              _setPressed(false);
              widget.onTapCancel?.call();
            }
          : null,
      onTap: widget.enabled
          ? () {
              final async = widget.onTapAsync;
              if (async != null) {
                async();
              } else {
                widget.onTap?.call();
              }
            }
          : null,
      child: AnimatedScale(
        scale: _pressed ? widget.pressedScale : 1,
        duration: CyberpunkTheme.durPress,
        curve: CyberpunkTheme.easeOut,
        child: widget.child,
      ),
    );
  }
}
