import 'dart:typed_data';

/// MCU ble_param.h 参数条目
/// 所有多字节为 LITTLE-ENDIAN

class ParamEntry {
  final int id;
  String name;
  double value;
  double defaultValue;
  double min;
  double max;

  ParamEntry({
    required this.id,
    required this.name,
    required this.value,
    required this.defaultValue,
    required this.min,
    required this.max,
  });

  /// 从 PARAM_LIST 响应帧的 payload 中解析单条参数
  /// payload: [total(u8), index(u8), name_len(u8), name(N), val(f32), def(f32), min(f32), max(f32)]
  factory ParamEntry.fromListPayload(Uint8List payload) {
    int off = 0;
    off++; // skip total
    final index = payload[off]; off++;
    final nameLen = payload[off]; off++;
    final name = String.fromCharCodes(payload.sublist(off, off + nameLen));
    off += nameLen;
    final val = readF32LE(payload, off); off += 4;
    final def = readF32LE(payload, off); off += 4;
    final min = readF32LE(payload, off); off += 4;
    final max = readF32LE(payload, off);

    return ParamEntry(
      id: _indexToId(index),
      name: name,
      value: val,
      defaultValue: def,
      min: min,
      max: max,
    );
  }

  /// 将 index(0-based) 映射到 param ID
  /// MCU 参数注册表: ID 0x00~0x0E 连续, 0x10~0x12 陀螺仪
  static int _indexToId(int index) {
    if (index <= 14) return index;       // 0x00~0x0E
    return index + 1;                     // 0x10~0x12 (跳 0x0F)
  }

  /// 参数 ID 列表 (与 MCU g_param_table 注册顺序一致)
  static const paramDefs = [
    (0x00, 'PID Speed KP'),
    (0x01, 'PID Speed KI'),
    (0x02, 'PID Speed KD'),
    (0x03, 'PID Speed FF'),
    (0x04, 'PID Line KP'),
    (0x05, 'PID Line KD'),
    (0x06, 'PID Angle KP'),
    (0x07, 'PID Angle KD'),
    (0x08, 'Base Speed'),
    (0x09, 'Turn Inner'),
    (0x0A, 'Turn Outer'),
    (0x0B, 'Turn Target'),
    (0x0C, 'Soft Start'),
    (0x0D, 'Laps'),
    (0x0E, 'Gray Thresh'),
    (0x10, 'Gyro Bias X'),
    (0x11, 'Gyro Bias Y'),
    (0x12, 'Gyro Bias Z'),
  ];

  /// 参数 ID → 名称快查
  static String nameById(int id) {
    for (final p in paramDefs) {
      if (p.$1 == id) return p.$2;
    }
    return 'Param 0x${id.toRadixString(16)}';
  }

  /// float32 小端解码
  static double readF32LE(Uint8List data, int offset) {
    final bytes = ByteData.sublistView(Uint8List.fromList(
        [data[offset], data[offset + 1], data[offset + 2], data[offset + 3]]));
    return bytes.getFloat32(0, Endian.little);
  }

  /// float32 小端编码
  static Uint8List writeF32LE(double v) {
    final bd = ByteData(4);
    bd.setFloat32(0, v, Endian.little);
    return bd.buffer.asUint8List();
  }
}
