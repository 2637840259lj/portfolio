import 'dart:typed_data';

/// MCU 通用台架闭环测试的最终结果。
/// 不表示竞赛流程，仅用于循迹、90度航向和1m停车的分速度参数评估。
enum BenchMode {
  lineStraight(1, '直线循迹测试'),
  yaw90(2, '90°航向测试'),
  distance1m(3, '1 m 距离停车测试'),
  lineCurve(4, '曲线循迹测试');

  const BenchMode(this.code, this.label);
  final int code;
  final String label;
}

class BenchResult {
  final BenchMode mode;
  final int nodeIndex;
  final int reason;
  final int curveSamplePercent;
  final Duration elapsed;
  final double distanceCm;
  final double finalYawDeg;
  final double peakYawDeg;
  final double meanAbsError;
  final double meanAbsSteer;
  final int samples;

  const BenchResult({
    required this.mode,
    required this.nodeIndex,
    required this.reason,
    required this.curveSamplePercent,
    required this.elapsed,
    required this.distanceCm,
    required this.finalYawDeg,
    required this.peakYawDeg,
    required this.meanAbsError,
    required this.meanAbsSteer,
    required this.samples,
  });

  factory BenchResult.fromPayload(Uint8List d) {
    if (d.length < 26) throw ArgumentError('台架结果长度错误: ${d.length}');
    final mode = BenchMode.values.firstWhere(
      (value) => value.code == d[0],
      orElse: () => throw ArgumentError('未知台架测试类型: ${d[0]}'),
    );
    int i16(int o) => (d[o] | (d[o + 1] << 8)).toSigned(16);
    int i32(int o) => (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)).toSigned(32);
    return BenchResult(
      mode: mode,
      nodeIndex: d[1],
      reason: d[2],
      curveSamplePercent: d[3],
      elapsed: Duration(milliseconds: i32(4)),
      distanceCm: i32(8) / 10.0,
      finalYawDeg: i16(12) / 100.0,
      peakYawDeg: i16(14) / 100.0,
      meanAbsError: i16(16) / 100.0,
      meanAbsSteer: i16(18) / 10.0,
      samples: i16(20),
    );
  }

  String get reasonLabel => switch (reason) {
    0 => '达到目标',
    1 => '检测到出线并自动停车',
    2 => '安全超时并自动停车',
    _ => '未知结束原因',
  };
}
