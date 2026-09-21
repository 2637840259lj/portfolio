import 'package:flutter/material.dart';

import '../theme.dart';
import '../widgets/k230_rtsp_view.dart';

class K230RtspFullscreenPage extends StatelessWidget {
  const K230RtspFullscreenPage({required this.streamUrl, super.key});

  final String streamUrl;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: SafeArea(
        child: Stack(
          fit: StackFit.expand,
          children: [
            InteractiveViewer(
              minScale: 1,
              maxScale: 4,
              child: K230RtspView(streamUrl: streamUrl),
            ),
            Positioned(
              top: 12,
              left: 12,
              child: IconButton.filledTonal(
                tooltip: '退出全屏',
                onPressed: () => Navigator.of(context).pop(),
                icon: const Icon(Icons.fullscreen_exit_rounded),
              ),
            ),
            const Positioned(
              left: 12,
              bottom: 12,
              child: Text(
                'K230 H.264 · RTSP · 硬件解码',
                style: TextStyle(
                  color: CyberpunkTheme.text,
                  fontFamily: 'monospace',
                  fontSize: 11,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
