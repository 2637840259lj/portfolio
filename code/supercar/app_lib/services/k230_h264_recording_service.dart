import 'dart:io';

import 'package:path_provider/path_provider.dart';

import '../models/k230_h264_recording.dart';

/// Storage location for libVLC-recorded H.264 files.
///
/// The player receives K230's compressed RTSP stream and libVLC writes it
/// directly; no JPEG extraction or Dart-side frame persistence is involved.
class K230H264RecordingService {
  static const _folderName = 'k230_h264_recordings';

  static Future<Directory> recordingDirectory() async {
    final root = await getApplicationDocumentsDirectory();
    final directory = Directory('${root.path}/$_folderName');
    if (!await directory.exists()) {
      await directory.create(recursive: true);
    }
    return directory;
  }

  static Future<List<K230H264Recording>> listRecordings() async {
    final directory = await recordingDirectory();
    final recordings = <K230H264Recording>[];
    await for (final entity in directory.list()) {
      if (entity is! File || !entity.path.toLowerCase().endsWith('.mp4')) {
        continue;
      }
      final stat = await entity.stat();
      if (stat.size <= 0) continue;
      recordings.add(
        K230H264Recording(
          file: entity,
          createdAt: stat.modified.toUtc(),
          bytes: stat.size,
        ),
      );
    }
    recordings.sort((a, b) => b.createdAt.compareTo(a.createdAt));
    return recordings;
  }

  static Future<void> delete(K230H264Recording recording) async {
    if (await recording.file.exists()) {
      await recording.file.delete();
    }
  }
}
