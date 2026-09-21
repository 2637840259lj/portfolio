import 'dart:io';

class K230H264Recording {
  const K230H264Recording({
    required this.file,
    required this.createdAt,
    required this.bytes,
  });

  final File file;
  final DateTime createdAt;
  final int bytes;

  String get id => file.path;
}
