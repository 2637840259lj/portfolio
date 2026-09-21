import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:path_provider/path_provider.dart';

import '../models/k230_recording.dart';

/// 将 K230 MJPEG 已解析出的 JPEG 帧异步保存为帧目录和 NDJSON 时间轴。
class K230RecordingService {
  static const int _maxPendingFrames = 6;

  final List<_PendingFrame> _pending = <_PendingFrame>[];
  final Stopwatch _clock = Stopwatch();

  Directory? _sessionDirectory;
  IOSink? _manifestSink;
  bool _writing = false;
  bool _stopping = false;
  int _frameIndex = 0;
  int _droppedFrameCount = 0;
  DateTime? _startedAtUtc;
  String _streamUrl = '';

  bool get isRecording => _sessionDirectory != null && !_stopping;
  int get frameCount => _frameIndex;
  int get droppedFrameCount => _droppedFrameCount;
  Duration get elapsed => _clock.elapsed;

  Future<void> start({required String streamUrl}) async {
    if (isRecording) return;

    final root = await getApplicationDocumentsDirectory();
    final id = _sessionId();
    final directory = Directory('${root.path}/k230_recordings/$id');
    await Directory('${directory.path}/frames').create(recursive: true);

    _sessionDirectory = directory;
    _manifestSink = File('${directory.path}/frames.ndjson').openWrite();
    _frameIndex = 0;
    _droppedFrameCount = 0;
    _pending.clear();
    _stopping = false;
    _streamUrl = streamUrl;
    _startedAtUtc = DateTime.now().toUtc();
    _clock
      ..reset()
      ..start();
  }

  void addFrame(Uint8List jpeg) {
    if (!isRecording) return;
    if (_pending.length >= _maxPendingFrames) {
      _droppedFrameCount++;
      return;
    }
    _pending.add(
      _PendingFrame(Uint8List.fromList(jpeg), _clock.elapsedMicroseconds),
    );
    unawaited(_drain());
  }

  Future<K230RecordingSession?> stop() async {
    if (_sessionDirectory == null || _stopping) return null;
    _stopping = true;
    _clock.stop();
    while (_writing || _pending.isNotEmpty) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }

    final directory = _sessionDirectory!;
    await _manifestSink?.flush();
    await _manifestSink?.close();

    final session = K230RecordingSession(
      id: directory.path.split(Platform.pathSeparator).last,
      streamUrl: _streamUrl,
      startedAtUtc: _startedAtUtc!,
      durationUs: _clock.elapsedMicroseconds,
      frameCount: _frameIndex,
      droppedFrameCount: _droppedFrameCount,
    );
    await File(
      '${directory.path}/session.json',
    ).writeAsString(session.encode());

    _sessionDirectory = null;
    _manifestSink = null;
    _stopping = false;
    return session;
  }

  Future<void> _drain() async {
    if (_writing || _sessionDirectory == null) return;
    _writing = true;
    try {
      while (_pending.isNotEmpty) {
        final pending = _pending.removeAt(0);
        final index = _frameIndex++;
        final relativeFile = 'frames/${index.toString().padLeft(6, '0')}.jpg';
        final file = File('${_sessionDirectory!.path}/$relativeFile');
        await file.writeAsBytes(pending.jpeg, flush: false);
        _manifestSink?.writeln(
          jsonEncode(
            K230FrameRecord(
              index: index,
              timestampUs: pending.timestampUs,
              file: relativeFile,
              bytes: pending.jpeg.length,
            ).toJson(),
          ),
        );
      }
      await _manifestSink?.flush();
    } finally {
      _writing = false;
    }
  }

  static Future<List<K230RecordingSession>> listSessions() async {
    final root = await getApplicationDocumentsDirectory();
    final directory = Directory('${root.path}/k230_recordings');
    if (!await directory.exists()) return <K230RecordingSession>[];

    final sessions = <K230RecordingSession>[];
    await for (final entity in directory.list()) {
      if (entity is! Directory) continue;
      final sessionFile = File('${entity.path}/session.json');
      if (!await sessionFile.exists()) continue;
      try {
        final json =
            jsonDecode(await sessionFile.readAsString())
                as Map<String, dynamic>;
        sessions.add(
          K230RecordingSession.fromJson(
            json,
            entity.path.split(Platform.pathSeparator).last,
          ),
        );
      } catch (_) {
        // 跳过未完成或损坏的会话，保证录像列表可用。
      }
    }
    sessions.sort((a, b) => b.startedAtUtc.compareTo(a.startedAtUtc));
    return sessions;
  }

  static Future<List<K230FrameRecord>> loadFrames(String sessionId) async {
    final root = await getApplicationDocumentsDirectory();
    final file = File('${root.path}/k230_recordings/$sessionId/frames.ndjson');
    if (!await file.exists()) return <K230FrameRecord>[];

    final frames = <K230FrameRecord>[];
    await for (final line
        in file
            .openRead()
            .transform(utf8.decoder)
            .transform(const LineSplitter())) {
      try {
        frames.add(
          K230FrameRecord.fromJson(jsonDecode(line) as Map<String, dynamic>),
        );
      } catch (_) {
        // NDJSON 最末行可能因异常退出而不完整，可安全忽略。
      }
    }
    return frames;
  }

  static Future<Directory> sessionDirectory(String sessionId) async {
    final root = await getApplicationDocumentsDirectory();
    return Directory('${root.path}/k230_recordings/$sessionId');
  }

  String _sessionId() {
    final now = DateTime.now();
    String two(int value) => value.toString().padLeft(2, '0');
    return '${now.year}${two(now.month)}${two(now.day)}_${two(now.hour)}${two(now.minute)}${two(now.second)}';
  }
}

class _PendingFrame {
  const _PendingFrame(this.jpeg, this.timestampUs);

  final Uint8List jpeg;
  final int timestampUs;
}
