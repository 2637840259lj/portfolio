import 'dart:typed_data';

/// 遥测帧解析 — 与 MCU AppBLE_TelemTrySend() 严格一致（基础 19 字节 + IMU 扩展）。
///
/// TELEM_FRAME payload（基础 19 bytes，全部小端）：
///   [0..3]   t_ms:u32       运行时间 ms
///   [4..7]   encL:i32       左编码器累计脉冲
///   [8..11]  encR:i32       右编码器累计脉冲
///   [12]     gray:u8        8 路灰度数字量
///   [13..14] yaw:i16        Yaw 角 (度 × 10)
///   [15..16] speedL:i16     左轮速度 (cm/s × 10)
///   [17..18] speedR:i16     右轮速度 (cm/s × 10)
/// 可选扩展（新固件 31 bytes）：
///   [19..20] gx:i16         X 角速度 (°/s × 100)
///   [21..22] gy:i16         Y 角速度 (°/s × 100)
///   [23..24] yawRate:i16    Z 角速度 (°/s × 100)
///   [25..26] roll:i16       横滚角 (° × 10)
///   [27..30] gyroBiasZ:i32  Z 轴原始零偏 (LSB)
/// 可选 PID 调试扩展（新固件 45 bytes）：
///   [31] mode:u8；[32..35] targetL/R:i16 (cm/s×10)；
///   [36..39] speedErrorL/R:i16 (cm/s×10)；[40..43] PID outputL/R:i16；[44] lineLost:u8。
/// H题循迹扩展（新固件53 bytes）：
///   [45..46] lineError:i16 (×10)；[47..48] lineSteer:i16 (×10)；
///   [49..50] lineBaseSpeed:i16 (cm/s×10)；[51] lineDebugValid:u8；[52] h1DebugMode:u8。
/// H题本圈结果扩展（新固件67 bytes）：
///   [53] h1RunState:u8；[54..57] elapsedMs:u32；[58..61] lapLeft:i32；
///   [62..65] lapRight:i32；[66] maxBlackBits:u8。
class TelemFrame {
  final int tMs;
  final int encL;
  final int encR;
  final double yawDeg;
  final int gray;
  final double speedL;
  final double speedR;
  final double gxDps;
  final double gyDps;
  final double yawRateDps;
  final double rollDeg;
  final int gyroBiasZ;
  final bool hasGyroBiasZ;
  final int mode;
  final double targetLeft;
  final double targetRight;
  final double speedErrorLeft;
  final double speedErrorRight;
  final int pidOutputLeft;
  final int pidOutputRight;
  final bool lineLost;
  final double lineError;
  final double lineSteer;
  final double lineBaseSpeed;
  final bool lineDebugValid;
  final bool h1DebugMode;
  final bool hasH1Result;
  final int h1RunState;
  final int h1ElapsedMs;
  final int h1LapLeftPulses;
  final int h1LapRightPulses;
  final int h1MaxBlackBits;
  final DateTime timestamp;

  TelemFrame({
    required this.tMs,
    required this.encL,
    required this.encR,
    required this.yawDeg,
    required this.gray,
    required this.speedL,
    required this.speedR,
    this.gxDps = 0,
    this.gyDps = 0,
    this.yawRateDps = 0,
    this.rollDeg = 0,
    this.gyroBiasZ = 0,
    this.hasGyroBiasZ = false,
    this.mode = 0,
    this.targetLeft = 0,
    this.targetRight = 0,
    this.speedErrorLeft = 0,
    this.speedErrorRight = 0,
    this.pidOutputLeft = 0,
    this.pidOutputRight = 0,
    this.lineLost = false,
    this.lineError = 0,
    this.lineSteer = 0,
    this.lineBaseSpeed = 0,
    this.lineDebugValid = false,
    this.h1DebugMode = false,
    this.hasH1Result = false,
    this.h1RunState = 0,
    this.h1ElapsedMs = 0,
    this.h1LapLeftPulses = 0,
    this.h1LapRightPulses = 0,
    this.h1MaxBlackBits = 0,
  }) : timestamp = DateTime.now();

  factory TelemFrame.parse(Uint8List payload) {
    if (payload.length < 19) {
      throw ArgumentError('TelemFrame payload too short: ${payload.length}');
    }
    final tMs = readU32LE(payload, 0);
    final encL = readI32LE(payload, 4);
    final encR = readI32LE(payload, 8);
    final gray = payload[12];
    final yawRaw = readI16LE(payload, 13);
    final speedL = readI16LE(payload, 15) / 10.0;
    final speedR = readI16LE(payload, 17) / 10.0;
    final gxDps = payload.length >= 21 ? readI16LE(payload, 19) / 100.0 : 0.0;
    final gyDps = payload.length >= 23 ? readI16LE(payload, 21) / 100.0 : 0.0;
    final yawRateDps = payload.length >= 25 ? readI16LE(payload, 23) / 100.0 : 0.0;
    final rollDeg = payload.length >= 27 ? readI16LE(payload, 25) / 10.0 : 0.0;
    final hasGyroBiasZ = payload.length >= 31;
    final gyroBiasZ = hasGyroBiasZ ? readI32LE(payload, 27) : 0;
    final hasPidDebug = payload.length >= 45;
    final mode = hasPidDebug ? payload[31] : 0;
    final targetLeft = hasPidDebug ? readI16LE(payload, 32) / 10.0 : 0.0;
    final targetRight = hasPidDebug ? readI16LE(payload, 34) / 10.0 : 0.0;
    final speedErrorLeft = hasPidDebug ? readI16LE(payload, 36) / 10.0 : 0.0;
    final speedErrorRight = hasPidDebug ? readI16LE(payload, 38) / 10.0 : 0.0;
    final pidOutputLeft = hasPidDebug ? readI16LE(payload, 40) : 0;
    final pidOutputRight = hasPidDebug ? readI16LE(payload, 42) : 0;
    final lineLost = hasPidDebug && payload[44] != 0;
    final hasH1LineDebug = payload.length >= 53;
    final lineError = hasH1LineDebug ? readI16LE(payload, 45) / 10.0 : 0.0;
    final lineSteer = hasH1LineDebug ? readI16LE(payload, 47) / 10.0 : 0.0;
    final lineBaseSpeed = hasH1LineDebug ? readI16LE(payload, 49) / 10.0 : 0.0;
    final lineDebugValid = hasH1LineDebug && payload[51] != 0;
    final h1DebugMode = hasH1LineDebug && payload[52] != 0;
    final hasH1Result = payload.length >= 67;
    final h1RunState = hasH1Result ? payload[53] : 0;
    final h1ElapsedMs = hasH1Result ? readU32LE(payload, 54) : 0;
    final h1LapLeftPulses = hasH1Result ? readI32LE(payload, 58) : 0;
    final h1LapRightPulses = hasH1Result ? readI32LE(payload, 62) : 0;
    final h1MaxBlackBits = hasH1Result ? payload[66] : 0;

    return TelemFrame(
      tMs: tMs,
      encL: encL,
      encR: encR,
      yawDeg: yawRaw / 10.0,
      gray: gray,
      speedL: speedL,
      speedR: speedR,
      gxDps: gxDps,
      gyDps: gyDps,
      yawRateDps: yawRateDps,
      rollDeg: rollDeg,
      gyroBiasZ: gyroBiasZ,
      hasGyroBiasZ: hasGyroBiasZ,
      mode: mode,
      targetLeft: targetLeft,
      targetRight: targetRight,
      speedErrorLeft: speedErrorLeft,
      speedErrorRight: speedErrorRight,
      pidOutputLeft: pidOutputLeft,
      pidOutputRight: pidOutputRight,
      lineLost: lineLost,
      lineError: lineError,
      lineSteer: lineSteer,
      lineBaseSpeed: lineBaseSpeed,
      lineDebugValid: lineDebugValid,
      h1DebugMode: h1DebugMode,
      hasH1Result: hasH1Result,
      h1RunState: h1RunState,
      h1ElapsedMs: h1ElapsedMs,
      h1LapLeftPulses: h1LapLeftPulses,
      h1LapRightPulses: h1LapRightPulses,
      h1MaxBlackBits: h1MaxBlackBits,
    );
  }

  static int readU32LE(Uint8List d, int off) =>
      d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) | (d[off + 3] << 24);

  static int readI32LE(Uint8List d, int off) =>
      readU32LE(d, off).toSigned(32);

  static int readI16LE(Uint8List d, int off) =>
      (d[off] | (d[off + 1] << 8)).toSigned(16);
}
