import 'dart:typed_data';

/// 与 MCU pid_schedule_node_t 对应的速度自适应 PID 节点。
/// 二进制载荷固定 77 字节：index:u8 + 19 个 float32 little-endian。
class PidScheduleNode {
  final int index;
  double speedCmS;
  double speedKp;
  double speedKi;
  double speedKd;
  double speedKf;
  double lineKp;
  double lineKd;
  double lineMaxSteer;
  double curveKp;
  double curveKd;
  double curveMaxSteer;
  double curveErrorTrigger;
  double yawKp;
  double yawKd;
  double yawMaxSteer;
  double posKp;
  double decelPulses;
  double endSpeed;
  double distanceScale;

  PidScheduleNode({
    required this.index,
    required this.speedCmS,
    required this.speedKp,
    required this.speedKi,
    required this.speedKd,
    required this.speedKf,
    required this.lineKp,
    required this.lineKd,
    required this.lineMaxSteer,
    required this.curveKp,
    required this.curveKd,
    required this.curveMaxSteer,
    required this.curveErrorTrigger,
    required this.yawKp,
    required this.yawKd,
    required this.yawMaxSteer,
    required this.posKp,
    required this.decelPulses,
    required this.endSpeed,
    required this.distanceScale,
  });

  factory PidScheduleNode.fromPayload(Uint8List payload) {
    if (payload.length < 77) {
      throw ArgumentError('PID schedule payload too short: ${payload.length}');
    }
    final data = ByteData.sublistView(payload);
    return PidScheduleNode(
      index: payload[0],
      speedCmS: data.getFloat32(1, Endian.little),
      speedKp: data.getFloat32(5, Endian.little),
      speedKi: data.getFloat32(9, Endian.little),
      speedKd: data.getFloat32(13, Endian.little),
      speedKf: data.getFloat32(17, Endian.little),
      lineKp: data.getFloat32(21, Endian.little),
      lineKd: data.getFloat32(25, Endian.little),
      lineMaxSteer: data.getFloat32(29, Endian.little),
      curveKp: data.getFloat32(33, Endian.little),
      curveKd: data.getFloat32(37, Endian.little),
      curveMaxSteer: data.getFloat32(41, Endian.little),
      curveErrorTrigger: data.getFloat32(45, Endian.little),
      yawKp: data.getFloat32(49, Endian.little),
      yawKd: data.getFloat32(53, Endian.little),
      yawMaxSteer: data.getFloat32(57, Endian.little),
      posKp: data.getFloat32(61, Endian.little),
      decelPulses: data.getFloat32(65, Endian.little),
      endSpeed: data.getFloat32(69, Endian.little),
      distanceScale: data.getFloat32(73, Endian.little),
    );
  }

  Uint8List toPayload() {
    final bytes = Uint8List(77);
    final data = ByteData.sublistView(bytes);
    bytes[0] = index;
    data.setFloat32(1, speedCmS, Endian.little);
    data.setFloat32(5, speedKp, Endian.little);
    data.setFloat32(9, speedKi, Endian.little);
    data.setFloat32(13, speedKd, Endian.little);
    data.setFloat32(17, speedKf, Endian.little);
    data.setFloat32(21, lineKp, Endian.little);
    data.setFloat32(25, lineKd, Endian.little);
    data.setFloat32(29, lineMaxSteer, Endian.little);
    data.setFloat32(33, curveKp, Endian.little);
    data.setFloat32(37, curveKd, Endian.little);
    data.setFloat32(41, curveMaxSteer, Endian.little);
    data.setFloat32(45, curveErrorTrigger, Endian.little);
    data.setFloat32(49, yawKp, Endian.little);
    data.setFloat32(53, yawKd, Endian.little);
    data.setFloat32(57, yawMaxSteer, Endian.little);
    data.setFloat32(61, posKp, Endian.little);
    data.setFloat32(65, decelPulses, Endian.little);
    data.setFloat32(69, endSpeed, Endian.little);
    data.setFloat32(73, distanceScale, Endian.little);
    return bytes;
  }

  PidScheduleNode copy() => PidScheduleNode(
    index: index,
    speedCmS: speedCmS,
    speedKp: speedKp,
    speedKi: speedKi,
    speedKd: speedKd,
    speedKf: speedKf,
    lineKp: lineKp,
    lineKd: lineKd,
    lineMaxSteer: lineMaxSteer,
    curveKp: curveKp,
    curveKd: curveKd,
    curveMaxSteer: curveMaxSteer,
    curveErrorTrigger: curveErrorTrigger,
    yawKp: yawKp,
    yawKd: yawKd,
    yawMaxSteer: yawMaxSteer,
    posKp: posKp,
    decelPulses: decelPulses,
    endSpeed: endSpeed,
    distanceScale: distanceScale,
  );
}
