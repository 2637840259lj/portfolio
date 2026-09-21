import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_vlc_player/flutter_vlc_player.dart';

import '../models/k230_h264_recording.dart';
import '../services/k230_h264_recording_service.dart';
import '../theme.dart';

class K230PlaybackPage extends StatefulWidget {
  const K230PlaybackPage({super.key});

  @override
  State<K230PlaybackPage> createState() => _K230PlaybackPageState();
}

class _K230PlaybackPageState extends State<K230PlaybackPage> {
  late Future<List<K230H264Recording>> _recordingsFuture;

  @override
  void initState() {
    super.initState();
    _recordingsFuture = K230H264RecordingService.listRecordings();
  }

  void _reload() {
    setState(() {
      _recordingsFuture = K230H264RecordingService.listRecordings();
    });
  }

  Future<void> _delete(K230H264Recording recording) async {
    await K230H264RecordingService.delete(recording);
    if (mounted) _reload();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: CyberpunkTheme.background,
      appBar: AppBar(
        title: const Text('K230 H.264 录像'),
        actions: [
          IconButton(
            tooltip: '刷新录像列表',
            onPressed: _reload,
            icon: const Icon(Icons.refresh_rounded),
          ),
        ],
      ),
      body: FutureBuilder<List<K230H264Recording>>(
        future: _recordingsFuture,
        builder: (_, snapshot) {
          if (snapshot.connectionState != ConnectionState.done) {
            return const Center(child: CircularProgressIndicator());
          }
          final recordings = snapshot.data ?? <K230H264Recording>[];
          if (recordings.isEmpty) {
            return const Center(
              child: Padding(
                padding: EdgeInsets.all(28),
                child: Text(
                  '尚无 H.264 录像\n请在实时预览画面开始后，点击“开始录制”。',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: CyberpunkTheme.dim, height: 1.5),
                ),
              ),
            );
          }
          return ListView.separated(
            padding: const EdgeInsets.all(16),
            itemCount: recordings.length,
            separatorBuilder: (_, _) => const SizedBox(height: 10),
            itemBuilder: (_, index) {
              final recording = recordings[index];
              return ListTile(
                tileColor: CyberpunkTheme.raisedSurface,
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12),
                ),
                leading: const Icon(
                  Icons.movie_creation_outlined,
                  color: CyberpunkTheme.primary,
                ),
                title: Text(_dateLabel(recording.createdAt)),
                subtitle: Text('${_sizeLabel(recording.bytes)} · H.264 / MP4'),
                trailing: IconButton(
                  tooltip: '删除录像',
                  icon: const Icon(Icons.delete_outline_rounded),
                  onPressed: () => _delete(recording),
                ),
                onTap: () => Navigator.of(context).push(
                  MaterialPageRoute(
                    builder: (_) => K230H264PlayerPage(recording: recording),
                  ),
                ),
              );
            },
          );
        },
      ),
    );
  }

  static String _dateLabel(DateTime value) =>
      value.toLocal().toString().substring(0, 19);

  static String _sizeLabel(int bytes) {
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(0)} KB';
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }
}

class K230H264PlayerPage extends StatefulWidget {
  const K230H264PlayerPage({super.key, required this.recording});

  final K230H264Recording recording;

  @override
  State<K230H264PlayerPage> createState() => _K230H264PlayerPageState();
}

class _K230H264PlayerPageState extends State<K230H264PlayerPage> {
  late final VlcPlayerController _controller;
  bool _ready = false;
  bool _seekable = false;
  bool _metadataRequested = false;

  @override
  void initState() {
    super.initState();
    _controller = VlcPlayerController.file(
      File(widget.recording.file.path),
      hwAcc: HwAcc.full,
      // A recording must never start playing merely because its detail page
      // opened. Playback begins only after the user taps the play button.
      autoPlay: false,
    );
    _controller.addListener(_onValueChanged);
    _controller.addOnInitListener(_loadMetadata);
  }

  void _onValueChanged() {
    final ready = _controller.value.isInitialized;
    if (mounted && ready != _ready) {
      setState(() => _ready = ready);
      if (ready) _loadMetadata();
    }
  }

  Future<void> _loadMetadata() async {
    if (_metadataRequested || !_controller.value.isInitialized) return;
    _metadataRequested = true;
    try {
      final results = await Future.wait<Object?>([
        _controller.getDuration(),
        _controller.isSeekable(),
      ]);
      if (!mounted) return;
      setState(() => _seekable = results[1] == true);
    } catch (_) {
      if (mounted) setState(() => _seekable = false);
    }
  }

  @override
  void dispose() {
    _controller
      ..removeOnInitListener(_loadMetadata)
      ..removeListener(_onValueChanged)
      ..dispose();
    super.dispose();
  }

  Future<void> _togglePlayback() async {
    if (!_ready) return;
    if (_controller.value.isPlaying) {
      await _controller.pause();
    } else {
      await _controller.play();
    }
  }

  Future<void> _seek(double milliseconds) async {
    await _controller.setTime(milliseconds.toInt());
  }

  @override
  Widget build(BuildContext context) {
    final value = _controller.value;
    final durationMs = value.duration.inMilliseconds;
    final positionMs = value.position.inMilliseconds.clamp(0, durationMs);
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(title: const Text('H.264 录像回放')),
      body: Column(
        children: [
          Expanded(
            child: VlcPlayer(
              controller: _controller,
              aspectRatio: 2.0,
              placeholder: const Center(child: CircularProgressIndicator()),
            ),
          ),
          Container(
            color: CyberpunkTheme.raisedSurface,
            padding: const EdgeInsets.fromLTRB(16, 8, 16, 16),
            child: Row(
              children: [
                IconButton(
                  iconSize: 36,
                  onPressed: _ready ? _togglePlayback : null,
                  icon: Icon(
                    value.isPlaying
                        ? Icons.pause_circle_filled_rounded
                        : Icons.play_circle_fill_rounded,
                  ),
                ),
                Expanded(
                  child: Slider(
                    value: positionMs.toDouble(),
                    min: 0,
                    max: durationMs > 0 ? durationMs.toDouble() : 1,
                    onChanged: _ready && _seekable && durationMs > 0
                        ? _seek
                        : null,
                  ),
                ),
                Text(
                  '${_format(positionMs)} / ${_format(durationMs)}',
                  style: const TextStyle(
                    color: CyberpunkTheme.text,
                    fontFamily: 'monospace',
                    fontSize: 11,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  static String _format(int milliseconds) {
    final totalSeconds = milliseconds ~/ 1000;
    final minutes = totalSeconds ~/ 60;
    final seconds = totalSeconds % 60;
    return '${minutes.toString().padLeft(2, '0')}:${seconds.toString().padLeft(2, '0')}';
  }
}
