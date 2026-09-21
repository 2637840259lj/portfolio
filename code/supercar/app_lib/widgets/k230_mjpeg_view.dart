import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';

/// K230 `/stream` 的轻量 MJPEG 播放器。
///
/// 服务端以 multipart/x-mixed-replace 输出 JPEG 帧。组件只保留最新完整帧，
/// 避免在手机端堆积网络数据或视频帧。
class K230MjpegView extends StatefulWidget {
  const K230MjpegView({
    super.key,
    required this.streamUrl,
    this.fit = BoxFit.contain,
    this.errorBuilder,
    this.onFpsChanged,
    this.onJpegFrame,
  });

  final String streamUrl;
  final BoxFit fit;
  final Widget Function(BuildContext context, Object error)? errorBuilder;
  final ValueChanged<double>? onFpsChanged;
  final ValueChanged<Uint8List>? onJpegFrame;

  @override
  State<K230MjpegView> createState() => _K230MjpegViewState();
}

class _K230MjpegViewState extends State<K230MjpegView> {
  HttpClient? _client;
  StreamSubscription<List<int>>? _subscription;
  Uint8List? _frame;
  Object? _error;
  List<int> _buffer = <int>[];
  bool _closed = false;
  int _fpsFrameCount = 0;
  Stopwatch _fpsStopwatch = Stopwatch();

  static const List<int> _jpegStart = <int>[0xff, 0xd8];
  static const List<int> _jpegEnd = <int>[0xff, 0xd9];

  @override
  void initState() {
    super.initState();
    _connect();
  }

  @override
  void didUpdateWidget(covariant K230MjpegView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.streamUrl != widget.streamUrl) {
      _disconnect();
      _frame = null;
      _error = null;
      _fpsFrameCount = 0;
      _fpsStopwatch = Stopwatch();
      _connect();
    }
  }

  Future<void> _connect() async {
    try {
      final uri = Uri.parse(widget.streamUrl);
      final client = HttpClient();
      _client = client;
      final request = await client.getUrl(uri);
      request.headers.set(HttpHeaders.cacheControlHeader, 'no-cache');
      final response = await request.close();
      if (response.statusCode != HttpStatus.ok) {
        throw HttpException('K230 返回 HTTP ${response.statusCode}', uri: uri);
      }
      _fpsStopwatch
        ..reset()
        ..start();
      _subscription = response.listen(
        _onData,
        onError: _onError,
        onDone: () {
          if (!_closed && mounted) {
            _onError(const HttpException('K230 视频流已断开'));
          }
        },
        cancelOnError: true,
      );
    } catch (error) {
      _onError(error);
    }
  }

  void _onData(List<int> chunk) {
    _buffer.addAll(chunk);
    _extractFrames();
  }

  void _extractFrames() {
    while (true) {
      final start = _findMarker(_buffer, _jpegStart);
      if (start < 0) {
        if (_buffer.length > 1) {
          _buffer = _buffer.sublist(_buffer.length - 1);
        }
        return;
      }
      if (start > 0) {
        _buffer = _buffer.sublist(start);
      }
      final end = _findMarker(_buffer, _jpegEnd, 2);
      if (end < 0) return;

      final nextFrame = Uint8List.fromList(_buffer.sublist(0, end + 2));
      _buffer = _buffer.sublist(end + 2);
      _fpsFrameCount++;
      widget.onJpegFrame?.call(nextFrame);
      if (_fpsStopwatch.elapsedMilliseconds >= 1000) {
        final fps = _fpsFrameCount * 1000 / _fpsStopwatch.elapsedMilliseconds;
        widget.onFpsChanged?.call(fps);
        _fpsFrameCount = 0;
        _fpsStopwatch
          ..reset()
          ..start();
      }
      if (mounted) setState(() => _frame = nextFrame);
    }
  }

  int _findMarker(List<int> bytes, List<int> marker, [int from = 0]) {
    for (var i = from; i <= bytes.length - marker.length; i++) {
      var match = true;
      for (var j = 0; j < marker.length; j++) {
        if (bytes[i + j] != marker[j]) {
          match = false;
          break;
        }
      }
      if (match) return i;
    }
    return -1;
  }

  void _onError(Object error) {
    if (!_closed && mounted) setState(() => _error = error);
  }

  void _disconnect() {
    _subscription?.cancel();
    _subscription = null;
    _client?.close(force: true);
    _client = null;
    _buffer = <int>[];
  }

  @override
  void dispose() {
    _closed = true;
    _disconnect();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null && _frame == null) {
      return widget.errorBuilder?.call(context, _error!) ??
          Center(child: Text('视频流错误: $_error'));
    }
    if (_frame == null) {
      return const Center(child: CircularProgressIndicator());
    }
    return Image.memory(_frame!, fit: widget.fit, gaplessPlayback: true);
  }
}
