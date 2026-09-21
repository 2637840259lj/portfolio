import 'dart:async';
import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:device_info_plus/device_info_plus.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

/// BLE 连接管理 — 与 CH9141 MCU 端对接
///
/// MCU 端 BLE 配置:
///   - 设备名: CH9141BLE2U
///   - 服务 UUID: 0000FFF0
///   - Write (App→MCU): 0000FFF2
///   - Notify (MCU→App): 0000FFF1 (须写 CCCD 0x2902 使能)
class BleConnection extends ChangeNotifier {
  static const String serviceUuid = '0000fff0-0000-1000-8000-00805f9b34fb';
  static const String writeCharUuid =
      '0000fff2-0000-1000-8000-00805f9b34fb'; // App→MCU
  static const String notifyCharUuid =
      '0000fff1-0000-1000-8000-00805f9b34fb'; // MCU→App

  // Guid.toString() 在不同平台返回的格式不一致 (有时短 UUID, 有时完整 128-bit)
  // 用 contains 做模糊匹配，兼容两种格式
  static const String _serviceFilter = 'fff0';
  static const String _writeCharFilter = 'fff2';
  static const String _notifyCharFilter = 'fff1';

  static const String deviceNameFilter = 'CH9141BLE2U';

  // ── 状态 ──
  BluetoothDevice? _device;
  BluetoothCharacteristic? _writeChar;

  bool _isScanning = false;
  bool _isConnecting = false;
  bool _isConnected = false;
  String _statusText = '未连接';
  int _rssi = 0;
  Timer? _rssiTimer;

  final List<ScanResult> _scanResults = [];
  final StreamController<List<int>> _dataController =
      StreamController<List<int>>.broadcast();
  StreamSubscription? _scanSub;
  StreamSubscription<List<int>>? _notifySub;
  StreamSubscription<BluetoothConnectionState>? _connSub;

  bool get isScanning => _isScanning;
  bool get isConnecting => _isConnecting;
  bool get isConnected => _isConnected;
  String get statusText => _statusText;
  List<ScanResult> get scanResults => List.unmodifiable(_scanResults);
  BluetoothDevice? get device => _device;
  Stream<List<int>> get dataStream => _dataController.stream;
  int get rssi => _rssi;

  /// ASCII 协议发送字节统计 + 回调 (供 AppState 累加 txBytes / 记日志)
  int asciiTxBytes = 0;
  void Function(String cmd)? onAsciiSent;

  void _startRssi() {
    _rssiTimer?.cancel();
    _rssiTimer = Timer.periodic(const Duration(seconds: 2), (_) async {
      if (_device != null && _isConnected) {
        try {
          _rssi = await _device!.readRssi();
          notifyListeners();
        } catch (_) {}
      }
    });
  }

  void _stopRssi() {
    _rssiTimer?.cancel();
    _rssiTimer = null;
  }

  // ── 扫描 ──
  Timer? _scanTimer;

  int? _androidSdkInt;

  Future<int> _getAndroidSdkInt() async {
    if (_androidSdkInt != null) return _androidSdkInt!;
    _androidSdkInt = (await DeviceInfoPlugin().androidInfo).version.sdkInt;
    return _androidSdkInt!;
  }

  /// Android 12+ 的 BLE 扫描属于“附近设备”运行时权限；Android 11 及以下
  /// 则依赖定位权限与系统位置服务。权限被拒绝时不能继续扫描，否则部分手机
  /// 会出现 startScan 成功但结果恒为 0 的假象。
  Future<bool> _ensureScanPermissions() async {
    if (kIsWeb || !Platform.isAndroid) return true;

    final sdkInt = await _getAndroidSdkInt();
    if (sdkInt >= 31) {
      final bluetooth = await <Permission>[
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
      ].request();
      final denied = bluetooth.entries
          .where((entry) => !entry.value.isGranted)
          .toList();
      if (denied.isNotEmpty) {
        final permanentlyDenied = denied.any(
          (entry) => entry.value.isPermanentlyDenied,
        );
        _statusText = permanentlyDenied
            ? '附近设备权限被永久拒绝：请到系统设置允许“附近设备”'
            : '请允许“附近设备”权限后重试';
        debugPrint('BleConnection: Android $sdkInt 附近设备权限未授权: $bluetooth');
        return false;
      }
      debugPrint('BleConnection: Android $sdkInt 附近设备权限已授权');
      return true;
    }

    final location = await Permission.locationWhenInUse.request();
    if (!location.isGranted) {
      _statusText = location.isPermanentlyDenied
          ? '定位权限被永久拒绝：请到系统设置允许定位后重试'
          : 'Android 11 及以下扫描 BLE 需要定位权限';
      debugPrint('BleConnection: Android $sdkInt 定位权限未授权: $location');
      return false;
    }
    _statusText = '正在扫描…';
    debugPrint('BleConnection: Android $sdkInt 定位权限已授权；请确认系统“位置信息”已开启');
    return true;
  }

  Future<void> startScan({
    Duration timeout = const Duration(seconds: 10),
  }) async {
    if (_isScanning) return;
    _scanResults.clear();
    _isScanning = true;
    _statusText = '正在扫描…';
    notifyListeners();

    try {
      if (!await _ensureScanPermissions()) {
        _isScanning = false;
        notifyListeners();
        return;
      }

      // 检查蓝牙是否开启 (加超时，不能无限等)
      final state = await FlutterBluePlus.adapterState.first.timeout(
        const Duration(seconds: 2),
        onTimeout: () => BluetoothAdapterState.off,
      );
      if (state != BluetoothAdapterState.on) {
        _statusText = '蓝牙未开启';
        _isScanning = false;
        notifyListeners();
        return;
      }
      debugPrint('BleConnection: 适配器已就绪');

      // 先确保没有残留的扫描
      if (FlutterBluePlus.isScanningNow) {
        debugPrint('BleConnection: 检测到残留扫描，先停止…');
        await FlutterBluePlus.stopScan();
      }

      // ★ 关键: 先订阅结果流，再启动扫描
      _scanSub?.cancel();
      _scanSub = FlutterBluePlus.scanResults.listen((results) {
        _scanResults
          ..clear()
          ..addAll(results);
        final names = results
            .map(
              (r) => r.advertisementData.advName.isEmpty
                  ? r.device.remoteId.str
                  : r.advertisementData.advName,
            )
            .join(', ');
        debugPrint(
          'BleConnection: 扫描结果 ${results.length} 个${names.isEmpty ? '' : ' — $names'}',
        );
        notifyListeners();
      });

      final androidUsesFineLocation =
          Platform.isAndroid && await _getAndroidSdkInt() <= 30;
      debugPrint(
        'BleConnection: 开始扫描 (timeout=${timeout.inSeconds}s, '
        'androidUsesFineLocation=$androidUsesFineLocation)',
      );
      await FlutterBluePlus.startScan(
        timeout: timeout,
        androidUsesFineLocation: androidUsesFineLocation,
      );
      debugPrint('BleConnection: startScan 完成');

      // 等待 timeout 后自动停止
      _scanTimer?.cancel();
      _scanTimer = Timer(timeout, () {
        debugPrint('BleConnection: 扫描超时，自动停止');
        _onScanTimeout();
      });
      _statusText = '正在扫描…';
      debugPrint('BleConnection: 扫描已启动，等待设备…');
    } catch (e, stack) {
      debugPrint('BleConnection: 扫描异常 — $e');
      debugPrint('BleConnection: 堆栈 — $stack');
      _statusText = '扫描失败: $e';
      _isScanning = false;
      notifyListeners();
    }
  }

  void _onScanTimeout() {
    // timeout 到了，通过 stopScan 停止扫描
    stopScan();
  }

  Future<void> stopScan() async {
    _scanTimer?.cancel();
    _scanTimer = null;
    try {
      await FlutterBluePlus.stopScan();
    } catch (_) {}
    _isScanning = false;
    _statusText = _scanResults.isEmpty
        ? '扫描结束：未发现 BLE 广播设备'
        : '扫描结束：发现 ${_scanResults.length} 个 BLE 设备';
    notifyListeners();
  }

  // ── 连接 ──
  Future<bool> connect(BluetoothDevice device) async {
    if (_isConnected || _isConnecting) return _isConnected;

    _device = device;
    _isConnecting = true;
    _statusText = '正在连接 ${device.platformName}…';
    debugPrint(
      'BleConnection: 开始连接 device=${device.platformName} id=${device.remoteId}',
    );
    notifyListeners();

    try {
      await device.connect(autoConnect: false);
      debugPrint('BleConnection: device.connect() 成功');
      _statusText = '正在发现服务…';
      notifyListeners();

      final services = await device.discoverServices();
      debugPrint(
        'BleConnection: discoverServices 完成, 发现 ${services.length} 个服务',
      );

      BluetoothCharacteristic? writeChar;
      BluetoothCharacteristic? notifyChar;

      for (final service in services) {
        final svcUuid = service.serviceUuid.toString().toLowerCase();
        debugPrint('BleConnection:   服务 UUID=$svcUuid');
        if (svcUuid.contains(_serviceFilter)) {
          debugPrint(
            'BleConnection:     → 匹配 FFF0, 遍历特征 (${service.characteristics.length} 个)',
          );
          for (final char in service.characteristics) {
            final uuid = char.characteristicUuid.toString().toLowerCase();
            debugPrint('BleConnection:     特征 UUID=$uuid');
            if (uuid.contains(_writeCharFilter)) {
              writeChar = char;
              debugPrint('BleConnection:       → 匹配 FFF2 (Write)');
            } else if (uuid.contains(_notifyCharFilter)) {
              notifyChar = char;
              debugPrint('BleConnection:       → 匹配 FFF1 (Notify)');
            }
          }
        }
      }

      if (writeChar == null || notifyChar == null) {
        debugPrint(
          'BleConnection: 未找到 FFF1/FFF2 特征! write=$writeChar notify=$notifyChar',
        );
        _resetTransportState(
          status: '未找到 CH9141 透传特征 (FFF1/FFF2)',
          releaseDevice: true,
        );
        try {
          await device.disconnect();
        } catch (_) {}
        return false;
      }

      _writeChar = writeChar;
      debugPrint('BleConnection: 找到 FFF1/FFF2, 使能 Notify…');

      // 订阅 Notify (FFF1) — MCU→App 数据
      await notifyChar.setNotifyValue(true);
      _notifySub?.cancel();
      _notifySub = notifyChar.lastValueStream.listen((value) {
        _dataController.add(value);
      });
      debugPrint('BleConnection: Notify 已使能');

      _isConnected = true;
      _isConnecting = false;
      _statusText = '已连接 ${device.platformName}';
      _startRssi();
      notifyListeners();

      // 监听原生 BLE 连接断开 (设备关机/超出范围)
      _connSub?.cancel();
      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected &&
            (_isConnected || _isConnecting)) {
          debugPrint('BleConnection: BLE 原生断开检测');
          _resetTransportState(status: '连接已断开', releaseDevice: true);
        }
      });

      debugPrint('BleConnection: 连接流程完成, isConnected=true');
      return true;
    } catch (e, stack) {
      debugPrint('BleConnection: 连接异常 — $e');
      debugPrint('BleConnection: 堆栈 — $stack');
      _resetTransportState(status: '连接失败: $e', releaseDevice: true);
      try {
        await device.disconnect();
      } catch (_) {}
      return false;
    }
  }

  // ── 发送队列 ──
  final List<_WriteJob> _writeQueue = [];
  bool _writeBusy = false;
  int _transportEpoch = 0;
  int _nextDriveGroup = 0;
  int? _inFlightDriveGroup;

  // 页面切换期间先封锁所有非安全上行，避免旧页面的 Timer 在 dispose 前
  // 又把 L/R、轮询或二进制查询写进队列；S0 停车命令仍可穿透。
  bool _nonSafetyCommandsSuspended = false;

  void suspendNonSafetyCommands() {
    _nonSafetyCommandsSuspended = true;
    _dropQueued((job) => job.priority > 0);
  }

  void resumeNonSafetyCommands() {
    _nonSafetyCommandsSuspended = false;
  }

  // ── ASCII 协议发送 (CH9141 兼容) ──
  /// 将旧的同类实时指令丢弃，防止点动/轮询把 CH9141 写队列塞满。
  /// 返回值为 false 的任务代表它已被更新的命令取代，而不是链路失败。
  void _dropQueued(bool Function(_WriteJob job) predicate) {
    final dropped = <_WriteJob>[];
    _writeQueue.removeWhere((job) {
      if (!predicate(job)) return false;
      dropped.add(job);
      return true;
    });
    for (final job in dropped) {
      if (!job.completer.isCompleted) job.completer.complete(false);
    }
  }

  static bool _isDriveCommand(String cmd) {
    if (cmd.length != 4 || (cmd[0] != 'L' && cmd[0] != 'R')) return false;
    final hex = RegExp(r'^[0-9A-Fa-f]{2}$');
    return hex.hasMatch(cmd.substring(1, 3));
  }

  static bool _isPollCommand(String cmd) =>
      cmd == 'MST\n' || cmd == 'LST\n' || cmd == 'HW?\n';

  /// 控制平面优先级：安全抢占 > 独占任务 > 实时控制 > 可丢弃查询。
  /// MCU 同样执行这一规则；双端配合避免 CH9141 的窄通道积压。
  static int _asciiPriority(String cmd) {
    if (cmd == 'EMG\n' ||
        cmd == 'BRK\n' ||
        cmd == 'STP\n' ||
        cmd == 'TM-\n' ||
        cmd == 'AT-\n' ||
        cmd == 'LF-\n')
      return 0;
    if (cmd == 'CAL\n' ||
        cmd == 'TM+\n' ||
        cmd == 'GCL\n' ||
        cmd == 'AT+\n' ||
        cmd == 'LF+\n')
      return 1;
    if (_isPollCommand(cmd) || cmd == 'PNG\n') return 3;
    return 2;
  }

  Future<bool> sendAscii(String cmd) async {
    if (!_isConnected || _writeChar == null) return false;
    assert(cmd.length == 4, 'ASCII cmd must be 4 bytes: $cmd');
    final priority = _asciiPriority(cmd);
    if (_nonSafetyCommandsSuspended && priority > 0) {
      return false;
    }

    if (priority == 0) {
      // S0: 清除尚未写入的旧动作，在队首插入安全指令。
      _dropQueued((_) => true);
    } else if (priority == 1) {
      // S1: 进入独占任务前丢弃查询、实时控制与旧任务，避免排队后才开始。
      _dropQueued((job) => job.priority >= 1);
    } else if (_isPollCommand(cmd)) {
      if (_writeQueue.any((job) => job.asciiCmd == cmd)) return true;
    }

    final completer = Completer<bool>();
    final job = _WriteJob(
      cmd.codeUnits,
      completer,
      asciiCmd: cmd,
      priority: priority,
    );
    // 当前正在写入的 GATT 请求不可中断。S0 必须置顶；S1 不能越过已排队的 S0，
    // 避免“先停车、后校准”在 App 队列里被反转成先校准。
    if (priority == 0) {
      _writeQueue.insert(0, job);
    } else if (priority == 1) {
      final firstNonSafety = _writeQueue.indexWhere(
        (queued) => queued.priority != 0,
      );
      _writeQueue.insert(
        firstNonSafety < 0 ? _writeQueue.length : firstNonSafety,
        job,
      );
    } else {
      _writeQueue.add(job);
    }
    if (!_writeBusy) _processWriteQueue();
    return completer.future;
  }

  /// 原子提交一组左右轮实时命令：丢弃旧的 L/R 待发项，再按 L→R 相邻入队。
  /// 不能用两次 sendAscii() 代替，否则第二次调用会把第一次刚入队的命令当旧命令丢掉。
  Future<bool> sendDrivePair(String leftCmd, String rightCmd) async {
    if (!_isConnected || _writeChar == null || _nonSafetyCommandsSuspended)
      return false;
    assert(_isDriveCommand(leftCmd) && _isDriveCommand(rightCmd));
    final group = _nextDriveGroup++;
    // 正在发送的控制组不能撤销，但其后最多只保留“最新的一组”待发命令。
    // 50ms 定时器快于 CH9141 的 L/R 两次写入（约60ms），若不限制待发组，
    // 旧组会不断堆积，最终让查询和回包失去时隙。
    _dropQueued(
      (job) =>
          job.asciiCmd != null &&
          _isDriveCommand(job.asciiCmd!) &&
          job.driveGroup != _inFlightDriveGroup,
    );
    final left = _WriteJob(
      leftCmd.codeUnits,
      Completer<bool>(),
      asciiCmd: leftCmd,
      priority: 2,
      driveGroup: group,
    );
    final right = _WriteJob(
      rightCmd.codeUnits,
      Completer<bool>(),
      asciiCmd: rightCmd,
      priority: 2,
      driveGroup: group,
    );
    _writeQueue.addAll([left, right]);
    if (!_writeBusy) _processWriteQueue();
    final results = await Future.wait([
      left.completer.future,
      right.completer.future,
    ]);
    return results.every((ok) => ok);
  }

  Future<bool> send(List<int> data) async {
    if (_nonSafetyCommandsSuspended || !_isConnected || _writeChar == null) {
      return false;
    }
    // CH9141 FFF2 是 "只写" 特征 (Write Only, 不支持 WriteWithoutResponse)
    // 必须用带响应写入。队列化避免并发写入导致 GATT_BUSY。
    debugPrint(
      'BleConnection: TX: Binary ${data.map((e) => e.toRadixString(16).padLeft(2, "0")).join(" ")}',
    );
    final completer = Completer<bool>();
    _writeQueue.add(_WriteJob(data, completer));
    if (!_writeBusy) _processWriteQueue();
    return completer.future;
  }

  Future<void> _processWriteQueue() async {
    _writeBusy = true;
    final epoch = _transportEpoch;
    try {
      while (_writeQueue.isNotEmpty && epoch == _transportEpoch) {
        final job = _writeQueue.removeAt(0);
        _inFlightDriveGroup = job.driveGroup;
        try {
          // 优先使用带响应写入获得 GATT 完成边界；若模块特征只支持无响应写入则自动兼容。
          // 每次写入设超时，避免单次平台调用卡死后让 S0 永远无法获得发送机会。
          if (_writeChar != null && _isConnected) {
            final char = _writeChar!;
            final useWithoutResponse =
                !char.properties.write && char.properties.writeWithoutResponse;
            await char
                .write(job.data, withoutResponse: useWithoutResponse)
                .timeout(const Duration(seconds: 2));
            if (job.asciiCmd != null) {
              asciiTxBytes += job.asciiCmd!.length;
              onAsciiSent?.call(job.asciiCmd!);
            }
            if (!job.completer.isCompleted) job.completer.complete(true);
          } else if (!job.completer.isCompleted) {
            job.completer.complete(false);
          }
        } catch (e) {
          debugPrint('BleConnection: 写入失败 $e');
          if (!job.completer.isCompleted) job.completer.complete(false);
          // GATT 写超时意味着本地写通道已不可信；继续发送 S0 也无法保证到车端。
          // 立即断链并清空待发命令，由 MCU 的 500ms 无命令失效保护进入停车状态。
          if (e is TimeoutException) {
            _dropQueued((_) => true);
            unawaited(disconnect());
          }
        }
        // 一个控制组包含 L、R 两次写入。只有该组的最后一个命令完成后，
        // 才允许下一次 sendDrivePair() 将旧组从队列中淘汰；否则下一次50ms
        // 更新会在左轮写完后误删尚未发送的右轮。
        if (_inFlightDriveGroup != null &&
            (_writeQueue.isEmpty ||
                _writeQueue.first.driveGroup != _inFlightDriveGroup)) {
          _inFlightDriveGroup = null;
        }
        // CH9141 的写入完成与 UART 转发需要约 20~30ms。
        // 不能以 5ms 连发，否则 App 日志虽显示“已发送”，模块端实际会积压或覆盖。
        if (_writeQueue.isNotEmpty && epoch == _transportEpoch) {
          await Future.delayed(const Duration(milliseconds: 30));
        }
      }
    } finally {
      // 旧连接在断开后可能仍从超时写入中返回；不得让它重置新连接的写队列状态。
      if (epoch == _transportEpoch) {
        _writeBusy = false;
        // finally 期间若又有任务进入，重新启动队列，不能遗留“队列非空但无人发送”的死角。
        if (_writeQueue.isNotEmpty && _isConnected) {
          Future<void>.microtask(_processWriteQueue);
        }
      }
    }
  }

  void _resetTransportState({
    required String status,
    required bool releaseDevice,
  }) {
    _transportEpoch++;
    for (final job in _writeQueue) {
      if (!job.completer.isCompleted) job.completer.complete(false);
    }
    _writeQueue.clear();
    _writeBusy = false;
    _inFlightDriveGroup = null;
    _nextDriveGroup = 0;
    _nonSafetyCommandsSuspended = false;
    unawaited(_notifySub?.cancel() ?? Future<void>.value());
    _notifySub = null;
    unawaited(_connSub?.cancel() ?? Future<void>.value());
    _connSub = null;
    _writeChar = null;
    _isConnected = false;
    _isConnecting = false;
    if (releaseDevice) _device = null;
    _stopRssi();
    _statusText = status;
    notifyListeners();
  }

  /// 断开旧 GATT、取消 Notify 与未发送写入；用于失败握手或用户主动恢复链路。
  Future<void> disconnect({String status = '已断开'}) async {
    final device = _device;
    _resetTransportState(status: status, releaseDevice: true);
    if (device != null) {
      try {
        await device.disconnect();
      } catch (_) {}
    }
  }

  /// 恢复链路前清除本地队列和旧 GATT 会话，再重新发现服务并握手。
  Future<bool> recoverAndReconnect(BluetoothDevice device) async {
    await disconnect(status: '正在清理旧链路…');
    await Future<void>.delayed(const Duration(milliseconds: 350));
    return connect(device);
  }

  /// 判断是否为 CH9141 设备
  static bool isCH9141Device(ScanResult r) {
    final name = r.advertisementData.advName.toUpperCase();
    if (name == deviceNameFilter) return true;
    final svcUuid = r.advertisementData.serviceUuids.any(
      (u) => u.toString().toLowerCase().contains(_serviceFilter),
    );
    return svcUuid;
  }

  @override
  void dispose() {
    _scanTimer?.cancel();
    _scanTimer = null;
    _stopRssi();
    disconnect();
    _dataController.close();
    super.dispose();
  }
}

class _WriteJob {
  final List<int> data;
  final Completer<bool> completer;
  final String? asciiCmd;
  final int priority;
  final int? driveGroup;
  _WriteJob(
    this.data,
    this.completer, {
    this.asciiCmd,
    this.priority = 2,
    this.driveGroup,
  });
}
