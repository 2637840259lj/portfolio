import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/ble_packet.dart';
import 'app_state.dart';

/// 示教编程服务 — TEACH 系列命令
class TeachService extends ChangeNotifier {
  final AppState app;
  StreamSubscription<BlePacket>? _sub;

  bool _recording = false;
  bool _playing = false;
  bool _paused = false;
  int _wpCount = 0;
  int _progress = 0;
  int _seqCount = 0;

  final List<Waypoint> _waypoints = [];

  bool get recording => _recording;
  bool get playing => _playing;
  bool get paused => _paused;
  int get wpCount => _wpCount;
  int get progress => _progress;
  int get seqCount => _seqCount;
  List<Waypoint> get waypoints => List.unmodifiable(_waypoints);

  TeachService({required this.app});

  // ── 录制 ──
  Future<bool> startRecording({int rateHz = 10}) async {
    _listenPackets();
    if (!await _sendAndWaitAck(CmdCode.teachStart, [rateHz])) return false;
    _recording = true;
    _waypoints.clear();
    _wpCount = 0;
    app.addLog('TEACH 开始录制 @ ${rateHz}Hz');
    notifyListeners();
    return true;
  }

  Future<bool> stopRecording() async {
    if (!await _sendAndWaitAck(CmdCode.teachStop, [])) return false;
    _recording = false;
    _sub?.cancel();
    app.addLog('TEACH 停止录制 (共 $_wpCount 航点)');
    notifyListeners();
    return true;
  }

  Future<bool> recordWaypoint() async {
    if (!_recording) return false;
    if (!await _sendAndWaitAck(CmdCode.teachRecordWp, [])) return false;
    _wpCount++;
    app.addLog('TEACH 记录航点 #$_wpCount');
    notifyListeners();
    return true;
  }

  // ── 回放 ──
  Future<bool> startPlayback({int speedScale = 10}) async {
    _listenPackets();
    if (!await _sendAndWaitAck(CmdCode.teachPlay, [speedScale, 0])) return false;
    _playing = true;
    _paused = false;
    _progress = 0;
    app.addLog('TEACH 开始回放 (speed=$speedScale)');
    notifyListeners();
    return true;
  }

  Future<bool> pausePlayback() async {
    if (!await _sendAndWaitAck(CmdCode.teachPause, [])) return false;
    _paused = true;
    app.addLog('TEACH 暂停');
    notifyListeners();
    return true;
  }

  Future<bool> resumePlayback() async {
    if (!await _sendAndWaitAck(CmdCode.teachResume, [])) return false;
    _paused = false;
    app.addLog('TEACH 恢复');
    notifyListeners();
    return true;
  }

  Future<bool> abortPlayback() async {
    if (!await _sendAndWaitAck(CmdCode.teachAbort, [])) return false;
    _playing = false;
    _paused = false;
    _sub?.cancel();
    app.addLog('TEACH 中止回放');
    notifyListeners();
    return true;
  }

  // ── 航点管理 ──
  Future<bool> addWaypoint(Waypoint wp) async {
    final payload = <int>[];
    payload.addAll(_i32le(wp.encL));
    payload.addAll(_i32le(wp.encR));
    payload.add(wp.speed);
    payload.add(wp.action);
    payload.addAll(_i16le(wp.timeoutMs));
    if (!await _sendAndWaitAck(CmdCode.teachWaypoint, payload)) return false;
    _waypoints.add(wp);
    _wpCount = _waypoints.length;
    app.addLog('TEACH 下发航点: ${wp.label}');
    notifyListeners();
    return true;
  }

  void removeWaypoint(int index) {
    if (index < 0 || index >= _waypoints.length) return;
    _waypoints.removeAt(index);
    _wpCount = _waypoints.length;
    notifyListeners();
  }

  Future<bool> clearSequence() async {
    if (!await _sendAndWaitAck(CmdCode.teachClear, [])) return false;
    _waypoints.clear();
    _wpCount = 0;
    app.addLog('TEACH 清空序列');
    notifyListeners();
    return true;
  }

  // ── 序列管理 ──
  Future<int?> listSequences() async {
    final completer = Completer<int?>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.teachList && pkt.payload.isNotEmpty) {
        sub?.cancel();
        completer.complete(pkt.payload[0]);
      }
    });
    Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) {
        sub?.cancel();
        completer.complete(null);
      }
    });
    await app.sendPacket(CmdCode.teachList, []);
    final count = await completer.future;
    if (count != null) {
      _seqCount = count;
      app.addLog('TEACH_LIST: $count 个序列');
      notifyListeners();
    }
    return count;
  }

  Future<bool> saveSequence() async {
    if (!await _sendAndWaitAck(CmdCode.teachSave, [])) return false;
    app.addLog('TEACH SAVE 已保存');
    return true;
  }

  Future<bool> loadSequence() async {
    if (!await _sendAndWaitAck(CmdCode.teachLoad, [])) return false;
    app.addLog('TEACH LOAD 已加载');
    return true;
  }

  Future<bool> deleteSequence(int index) async {
    if (!await _sendAndWaitAck(CmdCode.teachDelete, [index])) return false;
    app.addLog('TEACH DELETE seq=$index');
    return true;
  }

  // ── 底层 ──
  Future<bool> _sendAndWaitAck(int cmd, List<int> payload) async {
    if (!app.handshake.linkAlive) return false;
    if (!await app.sendPacket(cmd, payload)) return false;
    final completer = Completer<bool>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.ack &&
          pkt.payload.isNotEmpty &&
          pkt.payload.first == cmd) {
        sub?.cancel();
        completer.complete(true);
      } else if (pkt.cmd == CmdCode.nack &&
                 pkt.payload.isNotEmpty &&
                 pkt.payload.first == cmd) {
        sub?.cancel();
        completer.complete(false);
      }
    });
    Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) {
        sub?.cancel();
        completer.complete(false);
      }
    });
    return completer.future;
  }

  void _listenPackets() {
    _sub?.cancel();
    _sub = app.handshake.packetStream.listen((pkt) {
      // TODO: 监听回放进度/完成事件
    });
  }

  static List<int> _i32le(int v) => [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF];
  static List<int> _i16le(int v) => [v & 0xFF, (v >> 8) & 0xFF];

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}

/// 航点
class Waypoint {
  int encL;
  int encR;
  int speed; // cm/s
  int action; // 0x00 MOVE_TO, 0x01 TURN_L90, ...
  int timeoutMs;

  Waypoint({
    required this.encL,
    required this.encR,
    this.speed = 20,
    this.action = 0x00,
    this.timeoutMs = 5000,
  });

  String get label {
    switch (action) {
      case 0x00: return 'MOVE_TO (${speed}cm/s)';
      case 0x01: return 'TURN_L90';
      case 0x02: return 'TURN_R90';
      case 0x03: return 'WAIT ${timeoutMs}ms';
      case 0x04: return 'LINE_TO';
      case 0x05: return 'END';
      default: return 'ACT_0x${action.toRadixString(16)}';
    }
  }
}
