import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/ble_packet.dart';
import 'app_state.dart';

/// 页面数据模型 — GET_PAGE_DATA 响应解析

class PageData extends ChangeNotifier {
  // ── 遥控页 (0x01) 28 bytes ──
  int _hwFlags = 0;
  int _bleState = 0;
  int _encL = 0;
  int _encR = 0;
  double _yaw = 0;
  double _speedL = 0;
  double _speedR = 0;
  double _targetL = 0;
  double _targetR = 0;
  int _gray = 0;
  int _battery = 0;
  int _uptime = 0;
  int _errCode = 0;

  // ── 电机页 (0x02) 40 bytes ──
  int _mEncL = 0, _mEncR = 0;
  double _mSpeedL = 0, _mSpeedR = 0;
  double _mTargetL = 0, _mTargetR = 0;
  double _mKp = 0, _mKi = 0, _mKd = 0, _mKf = 0;

  // ── 陀螺仪页 (0x03) 36 bytes ──
  double _roll = 0, _pitch = 0, _imuYaw = 0;
  double _gx = 0, _gy = 0, _gz = 0;
  double _ax = 0, _ay = 0, _az = 0;

  // ── 循迹页 (0x04) 14 bytes ──
  int _lineGray = 0;
  double _lineDeviation = 0;
  double _lineKp = 0;
  double _lineKd = 0;

  // ── 转弯页 (0x05) 16 bytes ──
  double _turnInner = 0;
  double _turnOuter = 0;
  double _turnTarget = 0;
  int _encDiff = 0;

  // ── 硬件状态 (0x05 cmd) 8 bytes ──
  int _hwFlags2 = 0;
  int _imuWhoAmI = 0;
  int _encFlags = 0;
  int _hwGray = 0;
  int _motorFlags = 0;
  int _hwErr = 0;
  int _hwBattery = 0;
  int _hwUptime = 0;
  DateTime? _lastHwStatusAt;
  static const Duration _hwStatusMaxAge = Duration(seconds: 5);
  bool _hwQueryBusy = false;
  bool _disposed = false;
  final Set<_PendingQuery> _pendingQueries = <_PendingQuery>{};

  // Getters — 遥控页
  int get hwFlags => _hwFlags;
  int get bleState => _bleState;
  int get encL => _encL;
  int get encR => _encR;
  double get yaw => _yaw;
  double get speedL => _speedL;
  double get speedR => _speedR;
  double get targetL => _targetL;
  double get targetR => _targetR;
  int get gray => _gray;
  int get battery => _battery;
  int get uptime => _uptime;
  int get errCode => _errCode;

  // Getters — 电机页
  int get mEncL => _mEncL;
  int get mEncR => _mEncR;
  double get mSpeedL => _mSpeedL;
  double get mSpeedR => _mSpeedR;
  double get mTargetL => _mTargetL;
  double get mTargetR => _mTargetR;
  double get mKp => _mKp;
  double get mKi => _mKi;
  double get mKd => _mKd;
  double get mKf => _mKf;

  // Getters — 陀螺仪
  double get roll => _roll;
  double get pitch => _pitch;
  double get imuYaw => _imuYaw;
  double get gx => _gx;
  double get gy => _gy;
  double get gz => _gz;
  double get ax => _ax;
  double get ay => _ay;
  double get az => _az;

  // Getters — 循迹
  int get lineGray => _lineGray;
  double get lineDeviation => _lineDeviation;
  double get lineKp => _lineKp;
  double get lineKd => _lineKd;

  // Getters — 转弯
  double get turnInner => _turnInner;
  double get turnOuter => _turnOuter;
  double get turnTarget => _turnTarget;
  int get encDiff => _encDiff;

  // Getters — 硬件状态
  int get hwFlags2 => _hwFlags2;
  int get imuWhoAmI => _imuWhoAmI;
  int get encFlags => _encFlags;
  int get hwGray => _hwGray;
  int get motorFlags => _motorFlags;
  int get hwErr => _hwErr;
  int get hwBattery => _hwBattery;
  int get hwUptime => _hwUptime;
  DateTime? get lastHwStatusAt => _lastHwStatusAt;
  bool get hwStatusFresh => _lastHwStatusAt != null &&
      DateTime.now().difference(_lastHwStatusAt!) <= _hwStatusMaxAge;

  bool get imuOk => (_hwFlags & 0x01) != 0;
  bool get encOk => (_hwFlags & 0x02) != 0;
  bool get grayOk => (_hwFlags & 0x04) != 0;
  bool get motorOk => (_hwFlags & 0x08) != 0;

  // ── 查询方法 ──

  Future<void> queryHwStatus(AppState app) async {
    if (_disposed || !app.handshake.linkAlive || _hwQueryBusy) return;
    _hwQueryBusy = true;
    try {
      // 用 Completer 等待 MCU 的二进制 GET_HW_STATUS 应答
      final completer = Completer<Uint8List?>();
      final pending = _PendingQuery(completer);
      _pendingQueries.add(pending);
      pending.sub = app.handshake.packetStream.listen((pkt) {
        if (!_disposed && pkt.cmd == CmdCode.getHwStatus && !completer.isCompleted) {
          pending.finish(Uint8List.fromList(pkt.payload));
        }
      });
      // 先完成本地 GATT 写入，再开始 MCU 应答计时；发送队列繁忙时不能让
      // 尚未发出的 HW? 抢占 1 秒等待窗口。
      final sent = await app.bleConn.sendAscii('HW?\n');
      if (!sent) {
        pending.finish(null);
        _pendingQueries.remove(pending);
        return;
      }
      pending.timer = Timer(const Duration(seconds: 2), () => pending.finish(null));
      final data = await completer.future;
      _pendingQueries.remove(pending);
      if (_disposed || data == null || data.length < 8) return;
      _lastHwStatusAt = DateTime.now();
      _hwFlags = data[0];
      _hwFlags2 = data[0];
      _imuWhoAmI = data[1];
      _encFlags = data[2];
      _hwGray = data[3];
      _motorFlags = data[4];
      _hwErr = data[5];
      _hwBattery = data[6];
      _hwUptime = data[7];

      // 新版 MCU 在 GET_HW_STATUS 里附送了遥控页数据 (30 bytes, 省掉 GET_PAGE_DATA)
      if (data.length >= 30) {
        _bleState = data[8];
        _encL = _i32(data, 9);
        _encR = _i32(data, 13);
        _yaw = _f32(data, 17);
        _speedL = _i16(data, 21) / 10.0;
        _speedR = _i16(data, 23) / 10.0;
        _targetL = _i16(data, 25) / 10.0;
        _targetR = _i16(data, 27) / 10.0;
        _gray = data[29];
      }
      notifyListeners();
    } finally {
      _hwQueryBusy = false;
    }
  }

  Future<void> queryRemote(AppState app) async {
    final data = await _query(app, CmdCode.getPageData, [0x01]);
    if (data == null || data.length < 26) return;
    _hwFlags = data[0];
    _bleState = data[1];
    _encL = _i32(data, 2);
    _encR = _i32(data, 6);
    _yaw = _f32(data, 10);
    _speedL = _i16(data, 14) / 10.0;
    _speedR = _i16(data, 16) / 10.0;
    _targetL = _i16(data, 18) / 10.0;
    _targetR = _i16(data, 20) / 10.0;
    _gray = data[22];
    _battery = data[23];
    _uptime = data[24];
    _errCode = data[25];
    notifyListeners();
  }

  Future<void> queryMotor(AppState app) async {
    final data = await _query(app, CmdCode.getPageData, [0x02]);
    if (data == null || data.length < 40) return;
    _mEncL = _i32(data, 0);
    _mEncR = _i32(data, 4);
    _mSpeedL = _f32(data, 8);
    _mSpeedR = _f32(data, 12);
    _mTargetL = _f32(data, 16);
    _mTargetR = _f32(data, 20);
    _mKp = _f32(data, 24);
    _mKi = _f32(data, 28);
    _mKd = _f32(data, 32);
    _mKf = _f32(data, 36);
    notifyListeners();
  }

  Future<void> queryImu(AppState app) async {
    final data = await _query(app, CmdCode.getPageData, [0x03]);
    if (data == null || data.length < 36) return;
    _roll = _f32(data, 0);
    _pitch = _f32(data, 4);
    _imuYaw = _f32(data, 8);
    _gx = _f32(data, 12);
    _gy = _f32(data, 16);
    _gz = _f32(data, 20);
    _ax = _f32(data, 24);
    _ay = _f32(data, 28);
    _az = _f32(data, 32);
    notifyListeners();
  }

  Future<void> queryLine(AppState app) async {
    final data = await _query(app, CmdCode.getPageData, [0x04]);
    if (data == null || data.length < 14) return;
    _lineGray = data[0];
    _lineDeviation = _f32(data, 2);
    _lineKp = _f32(data, 6);
    _lineKd = _f32(data, 10);
    notifyListeners();
  }

  Future<void> queryTurn(AppState app) async {
    final data = await _query(app, CmdCode.getPageData, [0x05]);
    if (data == null || data.length < 16) return;
    _turnInner = _f32(data, 0);
    _turnOuter = _f32(data, 4);
    _turnTarget = _f32(data, 8);
    _encDiff = _i32(data, 12);
    notifyListeners();
  }

  // ── 底层查询 ──
  Future<Uint8List?> _query(AppState app, int cmd, List<int> payload) async {
    if (_disposed || !app.handshake.linkAlive) {
      debugPrint('PageData: _query 跳过 cmd=0x${cmd.toRadixString(16)} linkAlive=false');
      return null;
    }

    debugPrint('PageData: _query 发送 cmd=0x${cmd.toRadixString(16)}');
    final completer = Completer<Uint8List?>();
    final pending = _PendingQuery(completer);
    _pendingQueries.add(pending);
    pending.sub = app.handshake.packetStream.listen((pkt) {
      if (!_disposed && pkt.cmd == cmd && !completer.isCompleted) {
        debugPrint('PageData: _query 收到响应 cmd=0x${cmd.toRadixString(16)} len=${pkt.payload.length}');
        pending.finish(Uint8List.fromList(pkt.payload));
      }
    });

    // 必须在 GATT 写入完成后再开始应答计时；否则本地写队列繁忙时，
    // 查询命令还未离开手机就可能先被判定超时。
    final sent = await app.sendPacket(cmd, payload);
    if (!sent) {
      pending.finish(null);
      _pendingQueries.remove(pending);
      return null;
    }
    pending.timer = Timer(const Duration(seconds: 2), () {
      debugPrint('PageData: _query 超时 cmd=0x${cmd.toRadixString(16)}');
      pending.finish(null);
    });
    final result = await completer.future;
    _pendingQueries.remove(pending);
    return _disposed ? null : result;
  }

  @override
  void dispose() {
    _disposed = true;
    for (final pending in List<_PendingQuery>.from(_pendingQueries)) {
      pending.finish(null);
    }
    _pendingQueries.clear();
    super.dispose();
  }

  // ── 辅助解码 (LE) ──
  static int _i32(Uint8List d, int o) => (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)).toSigned(32);
  static int _i16(Uint8List d, int o) => (d[o] | (d[o + 1] << 8)).toSigned(16);
  static double _f32(Uint8List d, int o) {
    final bd = ByteData.sublistView(Uint8List.fromList([d[o], d[o + 1], d[o + 2], d[o + 3]]));
    return bd.getFloat32(0, Endian.little);
  }
}

class _PendingQuery {
  final Completer<Uint8List?> completer;
  StreamSubscription<BlePacket>? sub;
  Timer? timer;

  _PendingQuery(this.completer);

  void finish(Uint8List? value) {
    timer?.cancel();
    timer = null;
    sub?.cancel();
    sub = null;
    if (!completer.isCompleted) completer.complete(value);
  }
}
