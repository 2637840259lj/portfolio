import 'dart:async';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:flutter_vlc_player/flutter_vlc_player.dart';

import '../pages/k230_playback_page.dart';
import '../pages/k230_rtsp_fullscreen_page.dart';
import '../services/k230_h264_recording_service.dart';
import '../theme.dart';
import '../widgets/k230_rtsp_view.dart';

/// 比赛调参的第一阶段网络入口。
///
/// 当前先验证手机 App 可通过 WiFi 直达 K230 HTTP 服务；后续将该页面的
/// 服务状态区替换为 MJPEG 视频组件，并将 BLE 视觉扩展遥测填入球位置卡片。
class CompetitionTuningPage extends StatefulWidget {
  const CompetitionTuningPage({super.key});

  @override
  State<CompetitionTuningPage> createState() => _CompetitionTuningPageState();
}

class _CompetitionTuningPageState extends State<CompetitionTuningPage> {
  final _ipController = TextEditingController(text: '192.168.43.22');
  bool _checking = false;
  String _networkStatus = '未检测';
  String _detail = '请先在 K230 运行 k230_h264_rtsp.py';
  DateTime? _lastSuccess;
  bool _previewEnabled = false;
  bool _rtspReady = false;
  VlcPlayerController? _previewController;
  bool _recording = false;
  bool _recordingActionPending = false;

  @override
  void dispose() {
    // K230RtspView owns and disposes the controller. Stop any active recording
    // first so libVLC finalizes the MP4 container before the view is removed.
    if (_recording && _previewController != null) {
      _previewController!.stopRecording();
    }
    _ipController.dispose();
    super.dispose();
  }

  String get _baseUrl => 'http://${_ipController.text.trim()}:8081';
  String get _rtspUrl => 'rtsp://${_ipController.text.trim()}:8554/car';

  void _startPreview() {
    setState(() {
      _rtspReady = false;
      _previewEnabled = true;
    });
  }

  Future<void> _stopPreview() async {
    if (_recording) await _toggleRecording();
    if (!mounted) return;
    setState(() {
      _previewEnabled = false;
      _rtspReady = false;
      _previewController = null;
    });
  }

  Future<void> _toggleRecording() async {
    final controller = _previewController;
    if (controller == null || !_rtspReady || _recordingActionPending) return;

    setState(() => _recordingActionPending = true);
    try {
      if (!_recording) {
        final directory = await K230H264RecordingService.recordingDirectory();
        final started = await controller.startRecording(directory.path);
        if (!mounted) return;
        if (started == true) {
          setState(() {
            _recording = true;
            _detail = '正在将当前 RTSP H.264 码流直接录制为 MP4。';
          });
        } else {
          setState(() => _detail = '无法启动 H.264 录像，请重新进入预览后再试。');
        }
      } else {
        final stopped = await controller.stopRecording();
        if (!mounted) return;
        setState(() {
          _recording = false;
          _detail = stopped == true
              ? '录像已停止，MP4 已保存到“查看 H.264 录像”。'
              : '录像停止请求未确认，请到录像列表检查文件。';
        });
      }
    } catch (error) {
      if (mounted) {
        setState(() => _detail = 'H.264 录像失败：$error');
      }
    } finally {
      if (mounted) setState(() => _recordingActionPending = false);
    }
  }

  void _openFullscreen() {
    if (!_previewEnabled) return;
    Navigator.of(context).push(
      MaterialPageRoute(
        fullscreenDialog: true,
        builder: (_) => K230RtspFullscreenPage(streamUrl: _rtspUrl),
      ),
    );
  }

  Future<void> _checkK230() async {
    final ip = _ipController.text.trim();
    if (ip.isEmpty) {
      setState(() {
        _networkStatus = 'IP 为空';
        _detail = '请输入 K230 在同一 WiFi 下的 IPv4 地址。';
      });
      return;
    }

    setState(() {
      _checking = true;
      _networkStatus = '正在连接';
      _detail = '请求 $_baseUrl/health';
    });

    try {
      final response = await http
          .get(Uri.parse('$_baseUrl/health'))
          .timeout(const Duration(seconds: 4));
      if (!mounted) return;
      final body = response.body.trim();
      if (response.statusCode == 200 && body == 'K230_H264_OK') {
        setState(() {
          _networkStatus = 'K230 H.264 已连通';
          _detail = '硬件编码服务已就绪。可启动 RTSP 实时预览。';
          _lastSuccess = DateTime.now();
        });
      } else {
        setState(() {
          _networkStatus = '服务响应异常';
          _detail = 'HTTP ${response.statusCode}: $body';
        });
      }
    } on TimeoutException {
      if (!mounted) return;
      setState(() {
        _networkStatus = '连接超时';
        _detail = '确认手机与 K230 都接入同一 2.4GHz 热点，且 K230 脚本正在运行。';
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _networkStatus = '无法连接 K230';
        _detail = '$e\n检查 IP、热点隔离设置和 K230 服务端口。';
      });
    } finally {
      if (mounted) setState(() => _checking = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final online = _networkStatus == 'K230 H.264 已连通';
    final color = online
        ? const Color(0xFF4ADE80)
        : _checking
        ? const Color(0xFFA78BFA)
        : const Color(0xFFFBBF24);

    return Container(
      color: CyberpunkTheme.background,
      padding: const EdgeInsets.all(16),
      child: Row(
        children: [
          Expanded(
            flex: 7,
            child: Container(
              decoration: BoxDecoration(
                color: CyberpunkTheme.raisedSurface,
                borderRadius: BorderRadius.circular(CyberpunkTheme.radiusSheet),
                border: Border.all(color: CyberpunkTheme.edgeHighlight),
                boxShadow: CyberpunkTheme.raisedShadowsStrong,
              ),
              child: _previewEnabled
                  ? ClipRRect(
                      borderRadius: BorderRadius.circular(
                        CyberpunkTheme.radiusSheet,
                      ),
                      child: Stack(
                        fit: StackFit.expand,
                        children: [
                          K230RtspView(
                            streamUrl: _rtspUrl,
                            fit: BoxFit.contain,
                            onReadyChanged: (ready) {
                              if (mounted) setState(() => _rtspReady = ready);
                            },
                            onControllerCreated: (controller) {
                              _previewController = controller;
                            },
                            onControllerDisposed: (controller) {
                              if (identical(_previewController, controller)) {
                                _previewController = null;
                                _recording = false;
                              }
                            },
                            onError: (error) {
                              if (mounted) {
                                setState(() => _detail = 'RTSP 播放失败：$error');
                              }
                            },
                          ),
                          Positioned(
                            right: 8,
                            top: 8,
                            child: IconButton.filledTonal(
                              tooltip: '全屏预览',
                              onPressed: _openFullscreen,
                              icon: const Icon(Icons.fullscreen_rounded),
                            ),
                          ),
                          if (_recording)
                            Positioned(
                              right: 12,
                              bottom: 12,
                              child: Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 8,
                                  vertical: 4,
                                ),
                                decoration: BoxDecoration(
                                  color: const Color(0xCCE11D48),
                                  borderRadius: BorderRadius.circular(8),
                                ),
                                child: const Text(
                                  'REC · H.264',
                                  style: TextStyle(
                                    color: Colors.white,
                                    fontFamily: 'monospace',
                                    fontSize: 11,
                                    fontWeight: FontWeight.w700,
                                  ),
                                ),
                              ),
                            ),
                          Positioned(
                            left: 12,
                            top: 12,
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 8,
                                vertical: 4,
                              ),
                              decoration: BoxDecoration(
                                color: const Color(0xCC122017),
                                borderRadius: BorderRadius.circular(8),
                              ),
                              child: Text(
                                _rtspReady
                                    ? 'K230 H.264 · RTSP · LIVE'
                                    : 'K230 H.264 · CONNECTING',
                                style: const TextStyle(
                                  color: Color(0xFF4ADE80),
                                  fontSize: 11,
                                  fontFamily: 'monospace',
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    )
                  : Center(
                      child: Padding(
                        padding: const EdgeInsets.all(24),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(
                              online
                                  ? Icons.videocam_outlined
                                  : Icons.wifi_find_rounded,
                              size: 56,
                              color: color,
                            ),
                            const SizedBox(height: 16),
                            Text(
                              online ? 'K230 相机服务已就绪' : '等待 K230 相机服务',
                              style: const TextStyle(
                                color: CyberpunkTheme.text,
                                fontSize: 18,
                                fontWeight: FontWeight.w700,
                              ),
                            ),
                            const SizedBox(height: 10),
                            const Text(
                              '运行 k230_h264_rtsp.py 后，\n检测连通性并启动 RTSP 实时预览。',
                              textAlign: TextAlign.center,
                              style: TextStyle(
                                color: CyberpunkTheme.dim,
                                fontSize: 12,
                                height: 1.5,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
            ),
          ),
          const SizedBox(width: 14),
          SizedBox(
            width: 300,
            child: SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const Text(
                    '比赛调参 · K230',
                    style: TextStyle(
                      color: CyberpunkTheme.primary,
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  const SizedBox(height: 12),
                  _card(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          'K230 IPv4',
                          style: TextStyle(
                            color: CyberpunkTheme.dim,
                            fontSize: 11,
                          ),
                        ),
                        const SizedBox(height: 6),
                        TextField(
                          controller: _ipController,
                          keyboardType: TextInputType.number,
                          style: const TextStyle(
                            color: CyberpunkTheme.text,
                            fontFamily: 'monospace',
                          ),
                          decoration: const InputDecoration(
                            isDense: true,
                            hintText: '192.168.43.22',
                            border: OutlineInputBorder(),
                          ),
                        ),
                        const SizedBox(height: 10),
                        SizedBox(
                          width: double.infinity,
                          child: ElevatedButton.icon(
                            onPressed: _checking ? null : _checkK230,
                            icon: _checking
                                ? const SizedBox(
                                    width: 16,
                                    height: 16,
                                    child: CircularProgressIndicator(
                                      strokeWidth: 2,
                                    ),
                                  )
                                : const Icon(
                                    Icons.network_ping_rounded,
                                    size: 18,
                                  ),
                            label: Text(_checking ? '检测中…' : '检测 K230 连通性'),
                          ),
                        ),
                        const SizedBox(height: 8),
                        SizedBox(
                          width: double.infinity,
                          child: OutlinedButton.icon(
                            onPressed: online
                                ? (_previewEnabled
                                      ? _stopPreview
                                      : _startPreview)
                                : null,
                            icon: Icon(
                              _previewEnabled
                                  ? Icons.stop_circle_outlined
                                  : Icons.play_circle_outline,
                            ),
                            label: Text(_previewEnabled ? '停止相机预览' : '启动相机预览'),
                          ),
                        ),
                        const SizedBox(height: 8),
                        SizedBox(
                          width: double.infinity,
                          child: FilledButton.icon(
                            onPressed: _rtspReady && !_recordingActionPending
                                ? _toggleRecording
                                : null,
                            icon: _recordingActionPending
                                ? const SizedBox(
                                    width: 16,
                                    height: 16,
                                    child: CircularProgressIndicator(
                                      strokeWidth: 2,
                                    ),
                                  )
                                : Icon(
                                    _recording
                                        ? Icons.stop_circle_outlined
                                        : Icons.fiber_manual_record_rounded,
                                  ),
                            label: Text(
                              _recording ? '停止 H.264 录制' : '开始 H.264 录制',
                            ),
                          ),
                        ),
                        const SizedBox(height: 8),
                        const Text(
                          '录像直接保存当前 RTSP 的 H.264 码流为 MP4，不再保存 JPEG 帧目录。',
                          style: TextStyle(
                            color: CyberpunkTheme.dim,
                            fontSize: 11,
                            height: 1.35,
                          ),
                        ),
                        const SizedBox(height: 4),
                        SizedBox(
                          width: double.infinity,
                          child: TextButton.icon(
                            onPressed: () => Navigator.of(context).push(
                              MaterialPageRoute(
                                builder: (_) => const K230PlaybackPage(),
                              ),
                            ),
                            icon: const Icon(Icons.video_library_outlined),
                            label: const Text('查看 H.264 录像'),
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 10),
                  _card(
                    borderColor: color.withAlpha(140),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(
                              online ? Icons.check_circle : Icons.info_outline,
                              color: color,
                              size: 17,
                            ),
                            const SizedBox(width: 7),
                            Expanded(
                              child: Text(
                                _networkStatus,
                                style: TextStyle(
                                  color: color,
                                  fontWeight: FontWeight.w700,
                                ),
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 9),
                        Text(
                          _detail,
                          style: const TextStyle(
                            color: CyberpunkTheme.dim,
                            fontSize: 11,
                            height: 1.4,
                          ),
                        ),
                        if (_lastSuccess != null) ...[
                          const SizedBox(height: 8),
                          Text(
                            '最近成功: ${_lastSuccess!.toLocal().toString().substring(11, 19)}',
                            style: const TextStyle(
                              color: CyberpunkTheme.dim,
                              fontSize: 10,
                            ),
                          ),
                        ],
                      ],
                    ),
                  ),
                  const SizedBox(height: 10),
                  _card(
                    child: const Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '视觉遥测（下一步接入 BLE）',
                          style: TextStyle(
                            color: CyberpunkTheme.dim,
                            fontSize: 11,
                          ),
                        ),
                        SizedBox(height: 10),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Text(
                              '球位置 X / Y',
                              style: TextStyle(color: CyberpunkTheme.text),
                            ),
                            Text(
                              '-- / --',
                              style: TextStyle(
                                color: CyberpunkTheme.primary,
                                fontFamily: 'monospace',
                              ),
                            ),
                          ],
                        ),
                        SizedBox(height: 7),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Text(
                              '视觉链路',
                              style: TextStyle(color: CyberpunkTheme.text),
                            ),
                            Text(
                              '待接入',
                              style: TextStyle(color: CyberpunkTheme.dim),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _card({required Widget child, Color? borderColor}) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: CyberpunkTheme.insetDeep,
        borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
        border: Border.all(color: borderColor ?? CyberpunkTheme.edgeHighlight),
        boxShadow: CyberpunkTheme.insetShadows,
      ),
      child: child,
    );
  }
}
