import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_vlc_player/flutter_vlc_player.dart';

/// H.264/RTSP preview backed by Android's VLC playback stack.
///
/// K230 encodes on its VENC hardware path. This widget receives already
/// compressed H.264 and asks the Android media stack to decode it, avoiding
/// Dart-side JPEG frame parsing and image copies.
class K230RtspView extends StatefulWidget {
  const K230RtspView({
    required this.streamUrl,
    this.fit = BoxFit.contain,
    this.onReadyChanged,
    this.onError,
    this.onControllerCreated,
    this.onControllerDisposed,
    super.key,
  });

  final String streamUrl;
  final BoxFit fit;
  final ValueChanged<bool>? onReadyChanged;
  final ValueChanged<String>? onError;

  /// Exposes the live player only to the owning preview page so it can start
  /// and stop libVLC's direct H.264 recording without decoding frames in Dart.
  final ValueChanged<VlcPlayerController>? onControllerCreated;
  final ValueChanged<VlcPlayerController>? onControllerDisposed;

  @override
  State<K230RtspView> createState() => _K230RtspViewState();
}

class _K230RtspViewState extends State<K230RtspView> {
  late final VlcPlayerController _controller;
  bool _ready = false;
  String? _error;
  Timer? _connectTimeout;

  @override
  void initState() {
    super.initState();
    _controller = VlcPlayerController.network(
      widget.streamUrl,
      hwAcc: HwAcc.full,
      autoPlay: true,
      // Use libVLC's default RTSP transport negotiation. CanMV's built-in
      // RTSP service is verified with VLC and may expose media as RTP/UDP
      // rather than RTP interleaved over the RTSP TCP socket.
      options: VlcPlayerOptions(
        advanced: VlcAdvancedOptions([
          VlcAdvancedOptions.networkCaching(150),
          VlcAdvancedOptions.liveCaching(150),
        ]),
      ),
    );
    _controller.addListener(_observePlayer);
    widget.onControllerCreated?.call(_controller);
    _connectTimeout = Timer(const Duration(seconds: 8), () {
      if (!mounted || _ready || _controller.value.hasError) return;
      setState(() {
        _error =
            '8 秒内未收到 RTSP 视频首帧。K230 服务已验证可用时，'
            '请退出并重新进入预览，以重新协商 RTP 视频传输。';
      });
      widget.onError?.call(_error!);
    });
  }

  void _observePlayer() {
    if (!mounted) return;
    final value = _controller.value;
    final ready = value.isInitialized && value.isPlaying && !value.hasError;
    if (ready != _ready || value.hasError) {
      setState(() {
        _ready = ready;
        _error = value.hasError ? value.errorDescription : null;
      });
      if (ready) _connectTimeout?.cancel();
      widget.onReadyChanged?.call(ready);
      if (value.hasError) {
        widget.onError?.call(_error ?? 'RTSP 播放器发生未知错误');
      }
    }
  }

  @override
  void dispose() {
    _connectTimeout?.cancel();
    widget.onControllerDisposed?.call(_controller);
    _controller.removeListener(_observePlayer);
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Text(
            '无法播放 K230 H.264 视频\n$_error',
            textAlign: TextAlign.center,
            style: const TextStyle(color: Color(0xFFFF6B6B)),
          ),
        ),
      );
    }

    return Stack(
      fit: StackFit.expand,
      children: [
        VlcPlayer(
          controller: _controller,
          aspectRatio: 2.0,
          placeholder: const Center(child: CircularProgressIndicator()),
        ),
        if (!_ready)
          const Align(
            alignment: Alignment.bottomCenter,
            child: Padding(
              padding: EdgeInsets.all(12),
              child: Text(
                '正在连接 K230 H.264 / RTSP…',
                style: TextStyle(color: Color(0xFFB8C7D9), fontSize: 12),
              ),
            ),
          ),
      ],
    );
  }
}
