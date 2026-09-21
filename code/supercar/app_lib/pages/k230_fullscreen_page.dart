import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

/// 使用主预览传入的最新帧，不额外创建 K230 MJPEG 网络连接。
class K230FullscreenPage extends StatelessWidget {
  const K230FullscreenPage({
    super.key,
    required this.frame,
    required this.fps,
    required this.recording,
    required this.elapsed,
  });

  final ValueListenable<Uint8List?> frame;
  final ValueListenable<double> fps;
  final ValueListenable<bool> recording;
  final ValueListenable<Duration> elapsed;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(
        fit: StackFit.expand,
        children: [
          ValueListenableBuilder<Uint8List?>(
            valueListenable: frame,
            builder: (context, jpeg, child) => jpeg == null
                ? const Center(child: CircularProgressIndicator())
                : InteractiveViewer(
                    minScale: 1,
                    maxScale: 4,
                    child: Center(
                      child: Image.memory(jpeg, fit: BoxFit.contain),
                    ),
                  ),
          ),
          Positioned(
            left: 16,
            top: 16,
            child: ValueListenableBuilder<double>(
              valueListenable: fps,
              builder: (context, value, child) =>
                  _tag('LIVE · ${value.toStringAsFixed(1)} FPS'),
            ),
          ),
          Positioned(
            right: 16,
            top: 12,
            child: IconButton.filledTonal(
              tooltip: '退出全屏',
              onPressed: () => Navigator.of(context).pop(),
              icon: const Icon(Icons.fullscreen_exit_rounded),
            ),
          ),
          Positioned(
            left: 16,
            bottom: 16,
            child: ValueListenableBuilder<bool>(
              valueListenable: recording,
              builder: (context, active, child) {
                if (!active) return const SizedBox.shrink();
                return ValueListenableBuilder<Duration>(
                  valueListenable: elapsed,
                  builder: (context, duration, child) => _tag(
                    'REC  ${_formatDuration(duration)}',
                    color: const Color(0xFFE5484D),
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }

  static Widget _tag(String text, {Color color = const Color(0xFF4ADE80)}) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: const Color(0xD9000000),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: color.withAlpha(180)),
      ),
      child: Text(
        text,
        style: TextStyle(color: color, fontFamily: 'monospace', fontSize: 12),
      ),
    );
  }

  static String _formatDuration(Duration value) {
    String two(int n) => n.toString().padLeft(2, '0');
    return '${two(value.inMinutes)}:${two(value.inSeconds.remainder(60))}';
  }
}
