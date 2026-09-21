import 'dart:async';
import 'dart:collection';
import 'package:flutter/foundation.dart';
import '../models/ble_packet.dart';
import '../models/telem_frame.dart';
import 'app_state.dart';

/// 遥测数据服务 — TELEM_START/STOP + TELEM_FRAME 解析 + 数据缓冲
class TelemService extends ChangeNotifier {
  final AppState app;
  StreamSubscription<BlePacket>? _sub;

  bool _active = false;
  int _rateHz = 10;
  int _frameCount = 0;

  // 数据点缓冲 (每系列最多 200 点)
  final int _maxPoints = 200;
  final List<double> _timeSec = [];
  final List<double> _speedL = [];
  final List<double> _speedR = [];
  final List<double> _yaw = [];
  final List<int> _encL = [];
  final List<int> _encR = [];
  final List<double> _yawRate = [];
  final List<double> _roll = [];

  // IMU 联调只观察角度与角速度，不再由 App 自动下发 Z 轴零偏修正。
  bool _initialCalibrationReady = false;
  bool _hasGyroBiasZ = false;
  int _gyroBiasZ = 0;
  String _calibrationStatus = '等待监测数据';
  bool _operationBusy = false;
  bool _calibrationBusy = false;
  bool _disposed = false;
  int _operationToken = 0;
  final Set<StreamSubscription<BlePacket>> _ackSubscriptions = {};
  final Set<Timer> _ackTimers = {};
  final Set<Completer<bool>> _ackWaiters = {};

  bool get active => _active;
  int get rateHz => _rateHz;
  int get frameCount => _frameCount;

  UnmodifiableListView<double> get timeSec => UnmodifiableListView(_timeSec);
  UnmodifiableListView<double> get speedL => UnmodifiableListView(_speedL);
  UnmodifiableListView<double> get speedR => UnmodifiableListView(_speedR);
  UnmodifiableListView<double> get yaw => UnmodifiableListView(_yaw);
  UnmodifiableListView<int> get encL => UnmodifiableListView(_encL);
  UnmodifiableListView<int> get encR => UnmodifiableListView(_encR);
  UnmodifiableListView<double> get yawRate => UnmodifiableListView(_yawRate);
  UnmodifiableListView<double> get roll => UnmodifiableListView(_roll);
  double get latestYaw => _yaw.isEmpty ? 0 : _yaw.last;
  double get latestYawRate => _yawRate.isEmpty ? 0 : _yawRate.last;
  double get latestRoll => _roll.isEmpty ? 0 : _roll.last;
  double get driftDeg => _yaw.length < 2 ? 0 : _yaw.last - _yaw.first;
  bool get hasGyroBiasZ => _hasGyroBiasZ;
  int get gyroBiasZ => _gyroBiasZ;
  double get gyroBiasZDps => _gyroBiasZ / 16.4;
  int get lastBiasDelta => 0;
  String get calibrationStatus => _calibrationStatus;
  bool get initialCalibrationReady => _initialCalibrationReady;

  double _t0 = 0; // 首帧时间戳

  TelemService({required this.app});

  /// 监测开始前必须先完成一次 MCU 静止零偏校准。
  /// 自动闭环只处理之后的热漂余量，不能代替初始零偏校准。
  Future<bool> start({int rateHz = 10}) async {
    if (_active || _operationBusy) return _active;
    _operationBusy = true;
    final token = ++_operationToken;
    try {
      _rateHz = rateHz;
      _frameCount = 0;
      _t0 = 0;
      _clearData();
      final calibrated = await calibrateInitial();
      if (!calibrated || token != _operationToken) return false;
      _calibrationStatus = '初始角度校准完成，启动实时监测';
      _sub?.cancel();
      _sub = app.handshake.packetStream.listen(_onPacket);
      final telemAck = _waitAck(CmdCode.telemStart);
      final telemSent = await app.bleConn.sendAscii('TM+\n');
      final started = telemSent && await telemAck;
      if (started && token == _operationToken) {
        _active = true;
        app.addLog('IMU INITIAL CAL ACK; TELEM START ACK');
      } else {
        await app.bleConn.sendAscii('TM-\n');
        _sub?.cancel();
        _initialCalibrationReady = false;
        _calibrationStatus = '遥测启动未确认';
        app.addLog('TELEM START FAILED: MCU 未确认 TM+');
      }
      notifyListeners();
      return started && token == _operationToken;
    } finally {
      _operationBusy = false;
    }
  }

  /// 执行完整静止零偏校准。MCU 采样本身约 2.3 秒；校准期间暂停心跳，
  /// 防止 PNG 插入 CH9141 单通道，使 ACK 被延后或丢失。
  /// force=true 用于用户在长时间运行后主动重做零偏校准；此时必须真实下发 CAL，
  /// 不能只因已有一次成功记录就直接返回。
  Future<bool> calibrateInitial({bool force = false}) async {
    if (_calibrationBusy) return false;
    if (_initialCalibrationReady && !_operationBusy && !force) return true;
    if (!app.bleConn.isConnected || !app.handshake.linkAlive) {
      _calibrationStatus = '蓝牙未连接，无法校准';
      notifyListeners();
      return false;
    }
    _initialCalibrationReady = false;
    _calibrationStatus = '正在进行初始角度校准，请保持静止约 3 秒';
    _calibrationBusy = true;
    // start() 已经持有操作令牌；独立点击“校准”时才创建新令牌。
    final token = _operationBusy ? _operationToken : ++_operationToken;
    notifyListeners();

    // `CAL` 触发 MCU 的非阻塞采样：停止心跳后不再插入 PNG；完成后恢复链路保活。
    app.handshake.pauseHeartbeat();
    try {
      // 校准采样约3秒；入口不再被静止L/R续发占用后，保留8秒确认窗口即可。
      final calAck = _waitAck(
        CmdCode.imuCal,
        timeout: const Duration(seconds: 8),
      );
      final calSent = await app.bleConn.sendAscii('CAL\n');
      final calibrated = token == _operationToken && calSent && await calAck;
      if (calibrated) {
        _initialCalibrationReady = true;
        _calibrationStatus = '初始角度校准完成，可开始观察 Yaw 与角速度';
        app.markImuCalibrationValid();
        app.addLog('IMU INITIAL CAL ACK');
      } else {
        _calibrationStatus = '初始零偏校准超时：请保持小车静止，并检查蓝牙链路是否拥塞';
        app.addLog('IMU INITIAL CAL FAILED: CAL ACK timeout');
      }
      notifyListeners();
      return calibrated;
    } finally {
      _calibrationBusy = false;
      if (!_disposed && token == _operationToken) {
        app.handshake.resumeHeartbeat();
      }
    }
  }

  /// 停止遥测
  Future<void> stop() async {
    await _cancelAckWaiters();
    if (!_active) return;
    await app.bleConn.sendAscii('TM-\n');
    _active = false;
    _sub?.cancel();
    app.addLog('TELEM STOP (共 $_frameCount 帧)');
    notifyListeners();
  }

  Future<bool> _waitAck(
    int cmd, {
    Duration timeout = const Duration(seconds: 2),
  }) async {
    final completer = Completer<bool>();
    _ackWaiters.add(completer);
    StreamSubscription<BlePacket>? sub;
    Timer? timer;

    Future<void> finish(bool value) async {
      if (completer.isCompleted) return;
      if (sub != null) {
        _ackSubscriptions.remove(sub);
        await sub!.cancel();
      }
      if (timer != null) {
        timer!.cancel();
        _ackTimers.remove(timer);
      }
      _ackWaiters.remove(completer);
      completer.complete(value);
    }

    sub = app.handshake.packetStream.listen((pkt) {
      debugPrint(
        'TelemService: _waitAck received packet: '
        'cmd=0x${pkt.cmd.toRadixString(16)}, '
        'payload=${pkt.payload.map((e) => e.toRadixString(16).padLeft(2, '0')).join(' ')}',
      );
      if (pkt.cmd == CmdCode.ack &&
          pkt.payload.isNotEmpty &&
          pkt.payload.first == cmd) {
        unawaited(finish(true));
      }
    });
    _ackSubscriptions.add(sub);

    timer = Timer(timeout, () => finish(false));
    _ackTimers.add(timer);
    return completer.future;
  }

  Future<void> _cancelAckWaiters() async {
    for (final timer in List<Timer>.from(_ackTimers)) {
      timer.cancel();
    }
    _ackTimers.clear();
    for (final sub in List<StreamSubscription<BlePacket>>.from(
      _ackSubscriptions,
    )) {
      await sub.cancel();
    }
    _ackSubscriptions.clear();
    for (final waiter in List<Completer<bool>>.from(_ackWaiters)) {
      if (!waiter.isCompleted) waiter.complete(false);
    }
    _ackWaiters.clear();
  }

  void _onPacket(BlePacket pkt) {
    if (pkt.cmd != CmdCode.telemFrame) return;

    try {
      final frame = TelemFrame.parse(Uint8List.fromList(pkt.payload));
      _frameCount++;

      if (_t0 == 0) _t0 = frame.tMs / 1000.0;
      final t = frame.tMs / 1000.0 - _t0;

      _addPoint(_timeSec, t);
      _addPoint(_speedL, frame.speedL);
      _addPoint(_speedR, frame.speedR);
      _addPoint(_yaw, frame.yawDeg);
      _addPoint(_yawRate, frame.yawRateDps);
      _addPoint(_roll, frame.rollDeg);
      _addInt(_encL, frame.encL);
      _addInt(_encR, frame.encR);
      if (frame.hasGyroBiasZ) {
        _hasGyroBiasZ = true;
        _gyroBiasZ = frame.gyroBiasZ;
      }
      // 仅接收并显示角度相关数据；不再根据遥测自动发送 BZ/Z 轴修正命令。
      notifyListeners();
    } catch (e) {
      debugPrint('TelemService: parse error $e');
    }
  }

  void _addPoint(List<double> list, double v) {
    list.add(v);
    if (list.length > _maxPoints) list.removeAt(0);
  }

  void _addInt(List<int> list, int v) {
    list.add(v);
    if (list.length > _maxPoints) list.removeAt(0);
  }

  void _clearData() {
    _timeSec.clear();
    _speedL.clear();
    _speedR.clear();
    _yaw.clear();
    _yawRate.clear();
    _roll.clear();
    _hasGyroBiasZ = false;
    _gyroBiasZ = 0;
    _calibrationStatus = '等待初始角度校准';
    _encL.clear();
    _encR.clear();
  }

  Future<void> stopAndCancel() async {
    ++_operationToken;
    await _cancelAckWaiters();
    _operationBusy = false;
    _calibrationBusy = false;
    app.handshake.pauseHeartbeat();
    // 即使 _active 尚未置 true，也要发送 TM-，因为旧 start() 可能已经把 TM+
    // 写入队列但尚未收到 ACK。TM- 以 S0 优先级插入，可压过残留的 TM+。
    if (app.bleConn.isConnected) {
      await app.bleConn.sendAscii('TM-\n');
    }
    _active = false;
    _sub?.cancel();
    _sub = null;
    if (!_disposed) {
      notifyListeners();
      app.handshake.resumeHeartbeat();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    ++_operationToken;
    for (final timer in _ackTimers) {
      timer.cancel();
    }
    _ackTimers.clear();
    for (final sub in _ackSubscriptions) {
      sub.cancel();
    }
    _ackSubscriptions.clear();
    _operationBusy = false;
    _calibrationBusy = false;
    _sub?.cancel();
    // dispose 不能 await，但 TM- 会进入 S0 队列；同时禁止旧异步流程恢复心跳。
    if (app.bleConn.isConnected) {
      app.bleConn.sendAscii('TM-\n');
    }
    super.dispose();
  }
}
