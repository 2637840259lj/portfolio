import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'ble_connection.dart';
import 'ble_handshake.dart';
import 'ble_protocol.dart';
import '../models/ble_packet.dart';

/// 全局 App 状态 — 组合 BLE 连接 + 握手 + 调试日志
class AppState extends ChangeNotifier {
  final BleConnection bleConn = BleConnection();
  late final BleHandshake handshake;

  // 协议解析日志 (持久化, 供调试面板 MCU 标签使用)
  final List<String> _protoLogs = [];
  List<String> get protoLogs => _protoLogs;

  /// 调试日志（轮转缓冲，最多 2000 条）
  final List<String> _logs = [];
  final int _maxLogs = 2000;

  /// 原始数据字节数统计
  int txBytes = 0;
  int rxBytes = 0;

  /// 停车请求序号 (急停/刹车时 ++, ControlPage 监听以归零速度滑块)
  int stopSeq = 0;
  void requestStop() { stopSeq++; notifyListeners(); }

  /// S0 安全抢占：用于急停、切换工作页面与主动中止任务。
  /// BleConnection 会把它插到当前 GATT 写之后的队首；MCU 回 ACK 后日志可确认已执行。
  Future<bool> requestSafetyStop({bool emergency = false, String reason = '安全停止'}) async {
    if (!bleConn.isConnected) return false;
    addLog('S0 抢占: $reason');
    requestStop();
    return bleConn.sendAscii(emergency ? 'EMG\n' : 'STP\n');
  }

  bool _pageSwitching = false;
  bool get isPageSwitching => _pageSwitching;

  /// 页面切换的传输栅栏：先取消待发的非安全指令，再让旧页面释放定时器。
  /// 运行中的单次 GATT 写不可撤销，但切换期间后续旧命令均会被拒绝。
  Future<void> switchWorkPage({required String from, required String to}) async {
    if (from == to) return;
    _pageSwitching = true;
    bleConn.suspendNonSafetyCommands();
    requestStop();
  }

  /// 旧页面 dispose 后再发送最终 STP。这样旧页的 TM-/AT-/LF- 等清理命令
  /// 不会把停车命令从 S0 队列中覆盖掉。
  Future<void> finishWorkPageSwitch() async {
    if (bleConn.isConnected) {
      await bleConn.sendAscii('STP\n');
    }
    bleConn.resumeNonSafetyCommands();
    _pageSwitching = false;
    notifyListeners();
  }

  /// 执行锁: 指令/示教运行时禁止其他操作
  bool _locked = false;
  bool get isLocked => _locked;

  void setLocked(bool v) {
    _locked = v;
    notifyListeners();
  }

  // lock 期间禁发的命令
  static const _lockedCmds = {
    CmdCode.moveRaw, CmdCode.setArcade, CmdCode.throttleSet,
    CmdCode.stop, CmdCode.emergency, CmdCode.brake,
    CmdCode.teachStart, CmdCode.teachPlay, CmdCode.teachPause,
    CmdCode.teachResume, CmdCode.teachAbort,
    CmdCode.telemStart, CmdCode.telemStop,
    CmdCode.insExec, CmdCode.insStop, CmdCode.insStep,
    CmdCode.benchStart, CmdCode.benchLineStraightStart, CmdCode.benchLineCurveStart,
  };
  // lock 期间允许的命令 (暂停/急停/状态查询)
  static const _allowCmds = {
    CmdCode.insPause, CmdCode.insResume, CmdCode.emergency,
    CmdCode.ping, CmdCode.getState, CmdCode.getHwStatus,
    CmdCode.paramRead, CmdCode.paramList, CmdCode.getPageData,
    CmdCode.pidScheduleRead,
    CmdCode.benchAbort,
  };

  // ── 台架测评的校准有效状态 ──
  // 只在当前连接设备和本次 App 会话内有效；断开后必须重新完成校准，不能把旧设备的 ACK 当作新设备可用。
  DateTime? _imuCalibrationAt;
  DateTime? _grayCalibrationAt;
  String? _calibrationDeviceId;
  DateTime? get imuCalibrationAt => _isCalibrationForConnectedDevice ? _imuCalibrationAt : null;
  DateTime? get grayCalibrationAt => _isCalibrationForConnectedDevice ? _grayCalibrationAt : null;
  bool get imuCalibrationValid => imuCalibrationAt != null;
  bool get grayCalibrationValid => grayCalibrationAt != null;
  bool get _isCalibrationForConnectedDevice {
    final device = bleConn.device;
    return device != null && _calibrationDeviceId == device.remoteId.str;
  }

  void markImuCalibrationValid() {
    final device = bleConn.device;
    if (device == null) return;
    _calibrationDeviceId = device.remoteId.str;
    _imuCalibrationAt = DateTime.now();
    addLog('测评校准状态：陀螺仪静止校准已确认');
  }

  void markGrayCalibrationValid() {
    final device = bleConn.device;
    if (device == null) return;
    _calibrationDeviceId = device.remoteId.str;
    _grayCalibrationAt = DateTime.now();
    addLog('测评校准状态：白底灰度标定已确认');
  }

  void _clearCalibrationState() {
    _imuCalibrationAt = null;
    _grayCalibrationAt = null;
    _calibrationDeviceId = null;
  }

  /// 当前连接/握手状态 (优先 BLE 原生连接状态, 其次心跳)
  String get status {
    if (!bleConn.isConnected) return '未连接';
    if (!handshake.handshakeDone) return '握手中…';
    return handshake.linkAlive ? '在线' : '断线';
  }

  /// 日志摘要
  List<String> get logs => List.unmodifiable(_logs);

  AppState() {
    handshake = BleHandshake(connection: bleConn);
    handshake.addListener(_onHandshakeChanged);

    // 让 AppState 感知 BleConnection 的状态变化 (扫描/连接/蓝牙关闭等)
    bleConn.addListener(_onConnectionChanged);

    // 监听 ASCII 命令发送 → 累加 txBytes + 记日志 (sendAscii 绕过 sendPacket, 需单独统计)
    bleConn.onAsciiSent = (cmd) {
      txBytes += cmd.length;
      final tag = 'TX: $cmd';
      if (_logs.isEmpty || _logs.last != tag) {
        addLog(tag);
      }
    };

    // 监听解析后的数据包，记录日志
    handshake.packetStream.listen(_onPacket);
  }

  void _onHandshakeChanged() => notifyListeners();
  void _onConnectionChanged() {
    // BLE 断开时停止心跳并清除本设备会话的校准确认，避免重连后误用旧 ACK。
    if (!bleConn.isConnected) {
      handshake.stopHeartbeat();
      _clearCalibrationState();
    }
    notifyListeners();
  }

  String get ts => '${DateTime.now().hour.toString().padLeft(2,'0')}:${DateTime.now().minute.toString().padLeft(2,'0')}:${DateTime.now().second.toString().padLeft(2,'0')}';

  void _onPacket(BlePacket pkt) {
    rxBytes += 1 + (pkt.payload.isNotEmpty ? pkt.payload.length + 4 : 4);
    debugPrint('AppState RX: cmd=0x${pkt.cmd.toRadixString(16)} ($pkt)');
    addLog('RX: $pkt');
    _protoLogs.add('[$ts] $pkt');
    if (_protoLogs.length > 1000) _protoLogs.removeAt(0);
  }

  // ── 发送数据包 ──
  Future<bool> sendPacket(int cmd, List<int> payload) async {
    // 执行锁: 静默拒绝冲突命令
    if (_locked && !_allowCmds.contains(cmd)) {
      if (_lockedCmds.contains(cmd)) {
        addLog('[锁] 已拒绝 ${CmdCode.name(cmd)} (执行中)');
      }
      return false;
    }
    final frame = BleProtocol.pack(cmd, payload);
    txBytes += frame.length;
    final result = await bleConn.send(frame);
    if (_logs.length < 5 || _logs.last != 'TX: ${CmdCode.name(cmd)} len=${payload.length}') {
      addLog('TX: ${CmdCode.name(cmd)} len=${payload.length}');
    }
    return result;
  }

  // ── 日志 ──
  void addLog(String msg) {
    _logs.add(
        '[${DateTime.now().hour.toString().padLeft(2, '0')}:${DateTime.now().minute.toString().padLeft(2, '0')}:${DateTime.now().second.toString().padLeft(2, '0')}] $msg');
    if (_logs.length > _maxLogs) {
      _logs.removeAt(0);
    }
    notifyListeners();
  }

  void clearLogs() {
    _logs.clear();
    notifyListeners();
  }

  // ── 最近连接设备记忆 / 自动回连 ──
  static const _lastDeviceIdKey = 'last_ble_device_id';
  static const _lastDeviceNameKey = 'last_ble_device_name';

  Future<void> rememberDevice(BluetoothDevice device) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_lastDeviceIdKey, device.remoteId.str);
    await prefs.setString(_lastDeviceNameKey, device.platformName);
  }

  Future<String?> lastDeviceId() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_lastDeviceIdKey);
  }

  Future<String?> lastDeviceName() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_lastDeviceNameKey);
  }

  /// 仅连接已被用户成功连接过的设备；扫描发现该设备时由页面调用。
  Future<bool> autoConnectIfRemembered(BluetoothDevice device) async {
    final id = await lastDeviceId();
    if (id == null || id != device.remoteId.str || bleConn.isConnected || bleConn.isConnecting) return false;
    debugPrint('AppState: 自动回连已记忆设备 ${device.platformName} ($id)');
    return connectAndHandshake(device, remember: false);
  }

  // ── 连接流程 ──
  // 扫描页自动回连与用户点击连接可能在同一帧交错；整个连接+握手期间只允许一个事务。
  Future<bool>? _connectAndHandshakeFuture;

  Future<bool> connectAndHandshake(BluetoothDevice device, {bool remember = true}) async {
    final active = _connectAndHandshakeFuture;
    if (active != null) return active;
    final task = _connectAndHandshakeImpl(device, remember: remember);
    _connectAndHandshakeFuture = task;
    try {
      return await task;
    } finally {
      if (identical(_connectAndHandshakeFuture, task)) {
        _connectAndHandshakeFuture = null;
      }
    }
  }

  /// 用户触发的链路恢复：先发停车（若仍可写），再彻底释放旧 GATT/Notify/写队列，最后重新连接握手。
  Future<bool> recoverLink(BluetoothDevice device) async {
    addLog('开始恢复链路：清理旧 GATT 与发送队列');
    handshake.stopHeartbeat();
    if (bleConn.isConnected) {
      await requestSafetyStop(reason: '恢复链路前停车');
    }
    final connected = await bleConn.recoverAndReconnect(device);
    if (!connected) {
      addLog('恢复链路失败：无法重新建立 GATT');
      return false;
    }
    final ok = await handshake.performHandshake();
    if (ok) {
      await rememberDevice(device);
      addLog('恢复链路成功：已重新握手');
    } else {
      addLog('恢复链路失败：握手未响应，已释放会话');
      await bleConn.disconnect(status: '恢复握手失败，已断开');
    }
    notifyListeners();
    return ok;
  }

  Future<bool> _connectAndHandshakeImpl(BluetoothDevice device, {required bool remember}) async {
    debugPrint('AppState: connectAndHandshake 开始 device=${device.platformName}');
    addLog('正在连接 ${device.platformName}…');
    final connected = await bleConn.connect(device);
    debugPrint('AppState: bleConn.connect 返回 $connected');
    if (!connected) {
      addLog('连接失败: ${bleConn.statusText}');
      debugPrint('AppState: 连接失败, statusText=${bleConn.statusText}');
      return false;
    }
    addLog('GATT 服务发现完成，开始握手…');
    debugPrint('AppState: 开始 PING 握手…');
    final hsOk = await handshake.performHandshake();
    debugPrint('AppState: 握手结果=$hsOk');
    if (hsOk) {
      if (remember) await rememberDevice(device);
      addLog('握手成功！链路已建立');
    } else {
      // 握手失败不能保留“GATT 已连、Notify/写队列可能失效”的半连接会话。
      // 否则下一次 connect() 会因 isConnected 为真直接返回，反复在旧链路上写 PNG。
      addLog('握手超时/失败，已释放旧蓝牙会话');
      handshake.stopHeartbeat();
      await bleConn.disconnect(status: '握手失败，已断开');
    }
    notifyListeners();
    return hsOk;
  }

  @override
  void dispose() {
    handshake.removeListener(_onHandshakeChanged);
    handshake.dispose();
    bleConn.dispose();
    super.dispose();
  }
}
