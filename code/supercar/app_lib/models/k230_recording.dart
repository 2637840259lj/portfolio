import 'dart:convert';

class K230FrameRecord {
  const K230FrameRecord({
    required this.index,
    required this.timestampUs,
    required this.file,
    required this.bytes,
  });

  final int index;
  final int timestampUs;
  final String file;
  final int bytes;

  Map<String, dynamic> toJson() => {
    'i': index,
    'tUs': timestampUs,
    'file': file,
    'bytes': bytes,
  };

  factory K230FrameRecord.fromJson(Map<String, dynamic> json) =>
      K230FrameRecord(
        index: json['i'] as int,
        timestampUs: json['tUs'] as int,
        file: json['file'] as String,
        bytes: json['bytes'] as int,
      );
}

class K230RecordingSession {
  const K230RecordingSession({
    required this.id,
    required this.streamUrl,
    required this.startedAtUtc,
    required this.durationUs,
    required this.frameCount,
    required this.droppedFrameCount,
  });

  final String id;
  final String streamUrl;
  final DateTime startedAtUtc;
  final int durationUs;
  final int frameCount;
  final int droppedFrameCount;

  Duration get duration => Duration(microseconds: durationUs);

  Map<String, dynamic> toJson() => {
    'version': 1,
    'status': 'completed',
    'source': {'type': 'k230-mjpeg', 'streamUrl': streamUrl},
    'startedAtUtc': startedAtUtc.toIso8601String(),
    'durationUs': durationUs,
    'frameCount': frameCount,
    'droppedFrameCount': droppedFrameCount,
    'format': 'jpeg-directory+ndjson',
  };

  String encode() => const JsonEncoder.withIndent('  ').convert(toJson());

  factory K230RecordingSession.fromJson(Map<String, dynamic> json, String id) {
    final source = json['source'] as Map<String, dynamic>? ?? const {};
    return K230RecordingSession(
      id: id,
      streamUrl: source['streamUrl'] as String? ?? '',
      startedAtUtc: DateTime.parse(json['startedAtUtc'] as String),
      durationUs: json['durationUs'] as int? ?? 0,
      frameCount: json['frameCount'] as int? ?? 0,
      droppedFrameCount: json['droppedFrameCount'] as int? ?? 0,
    );
  }
}
