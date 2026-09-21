import 'dart:async';
import 'package:flutter/foundation.dart';
import 'ble_connection.dart';
import 'ble_protocol.dart';
import '../models/ble_packet.dart';

/// PING 链路握手 & 心跳管理
///
/// MCU 端 PING 响应 (app_ble.c):
///   App → MCU: PING (cmd=0x00, no payload)
///   MCU → App: PING (cmd=0x00, payload=[ver_major, ver_minor, state, 0])
///
/// 握手成功判定: 收到 PING 回复且 payload[0]==ver_major
///
/// 心跳 (文档 §4.5):
///   - 每 2s 发 PING 空包
///   - 连续 3 次无 PING 回复 → 判定断开
class BleHandshake extends ChangeNotifier {
  final BleConnection connection;
  final BleProtocol protocol = BleProtocol();

  bool _handshakeDone = false;
  bool _linkAlive = false;
  int _pingSeq = 0;
  int _missCount = 0;
  Timer? _heartbeatTimer;
  Timer? _pingTimeoutTimer;

  static const int _heartbeatIntervalSec = 2;  // 心跳间隔
  static const int _pingTimeoutSec = 2;         // 单次 PING 超时
  static const int _maxMissCount = 3;           // 连续丢失次数→断线

  VoidCallback? onHandshakeSuccess;
  VoidCallback? onLinkLost;

  BleHandshake({required this.connection}) {
    connection.dataStream.listen(_onRawData);
  }

  bool get handshakeDone => _handshakeDone;
  bool get linkAlive => _linkAlive;

  // ── 握手 ──
  Future<bool> performHandshake({Duration timeout = const Duration(seconds: 5)}) async {
    if (!connection.isConnected) return false;

    _handshakeDone = false;
    _linkAlive = false;

    final completer = Completer<bool>();
    StreamSubscription<BlePacket>? sub;

    sub = packetStream.listen((pkt) {
      if (_isPingReply(pkt)) {
        _handshakeDone = true;
        _linkAlive = true;
        sub?.cancel();
        completer.complete(true);
        onHandshakeSuccess?.call();
        _startHeartbeat();
        notifyListeners();
      }
    });

    // CH9141 会丢弃连接后首个 BLE write, 发 3 次 PING 确保至少 1 个到达
    _sendPing();
    Timer(const Duration(seconds: 1), () { if (!completer.isCompleted) _sendPing(); });
    Timer(const Duration(seconds: 2), () { if (!completer.isCompleted) _sendPing(); });

    Timer(timeout, () {
      if (!completer.isCompleted) {
        sub?.cancel();
        completer.complete(false);
      }
    });

    return completer.future;
  }

  /// PING 回复判定: cmd==0x00 且 payload[0]==0x01
  static bool _isPingReply(BlePacket pkt) {
    return pkt.cmd == CmdCode.ping &&
        pkt.payload.isNotEmpty &&
        pkt.payload[0] == 0x01;
  }

  // ── 心跳 ──
  void _startHeartbeat() {
    _heartbeatTimer?.cancel();
    _missCount = 0;
    _heartbeatTimer = Timer.periodic(
      Duration(seconds: _heartbeatIntervalSec),
      (_) {
        // BLE 已断开 → 自停
        if (!connection.isConnected) {
          debugPrint('Heartbeat: BLE 已断开，停止心跳');
          _heartbeatTimer?.cancel();
          _linkAlive = false;
          notifyListeners();
          return;
        }
        _sendPing();
        _pingTimeoutTimer?.cancel();
        _pingTimeoutTimer = Timer(
          Duration(seconds: _pingTimeoutSec),
          () {
            _missCount++;
            debugPrint('Heartbeat: PING 超时 ($_missCount/$_maxMissCount)');
            if (_missCount >= _maxMissCount) {
              debugPrint('Heartbeat: 连续 $_maxMissCount 次超时，判定断开');
              _linkAlive = false;
              onLinkLost?.call();
              notifyListeners();
            }
          },
        );
      },
    );
  }

  void _sendPing() {
    if (!connection.isConnected) return;
    _pingSeq++;
    connection.sendAscii('PNG\n');
    debugPrint('Heartbeat: 发送 PING seq=$_pingSeq');
  }

  /// 暂停周期 PING，但保持既有握手状态；用于 CAL 这类 MCU 阻塞任务，
  /// 避免心跳插入 CH9141 的单通道命令流。
  void pauseHeartbeat() {
    _heartbeatTimer?.cancel();
    _heartbeatTimer = null;
    _pingTimeoutTimer?.cancel();
    _pingTimeoutTimer = null;
  }

  /// 恢复心跳并立即验证链路。只在已完成握手且连接仍存在时生效。
  void resumeHeartbeat() {
    if (_handshakeDone && connection.isConnected) {
      _startHeartbeat();
      _sendPing();
    }
  }

  void stopHeartbeat() {
    pauseHeartbeat();
    _linkAlive = false;
    _handshakeDone = false;
    notifyListeners();
  }

  // ── 接收数据 → 解析协议包 ──
  final StreamController<BlePacket> _packetController =
      StreamController<BlePacket>.broadcast();

  Stream<BlePacket> get packetStream => _packetController.stream;

  void _onRawData(List<int> data) {
    final packets = protocol.feed(Uint8List.fromList(data));
    for (final pkt in packets) {
      // PING 回复 → 刷新心跳超时
      if (_isPingReply(pkt)) {
        _pingTimeoutTimer?.cancel();
        _missCount = 0;
        _linkAlive = true;
        debugPrint('Heartbeat: 收到 PING 应答');
      }
      _packetController.add(pkt);
    }
  }

  @override
  void dispose() {
    stopHeartbeat();
    _packetController.close();
    super.dispose();
  }
}
