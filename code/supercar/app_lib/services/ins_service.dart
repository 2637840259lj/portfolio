import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/ble_packet.dart';
import 'app_state.dart';

/// 指令示教服务 — INS 系列命令
class InsService extends ChangeNotifier {
  final AppState app;

  bool _executing = false;
  bool _paused = false;
  int _pc = 0;
  int _count = 0;

  final List<Instruction> _insList = [];

  bool get executing => _executing;
  bool get paused => _paused;
  int get pc => _pc;
  int get count => _count;
  List<Instruction> get insList => List.unmodifiable(_insList);

  InsService({required this.app});

  // ── 缓冲区管理 ──
  Future<bool> clear() => _sendAndWaitAck(CmdCode.insClear, []);

  Future<bool> append(Instruction ins) async {
    final payload = <int>[ins.opcode];
    payload.addAll(_i16le(ins.p1));
    payload.addAll(_i16le(ins.p2));
    payload.addAll(_i16le(ins.p3));
    payload.addAll(_i16le(ins.p4));
    if (!await _sendAndWaitAck(CmdCode.insAppend, payload)) return false;
    _insList.add(ins);
    _count = _insList.length;
    app.addLog('INS APPEND: ${ins.label}');
    notifyListeners();
    return true;
  }

  Future<bool> insert(int index, Instruction ins) async {
    final payload = <int>[index, ins.opcode];
    payload.addAll(_i16le(ins.p1));
    payload.addAll(_i16le(ins.p2));
    payload.addAll(_i16le(ins.p3));
    payload.addAll(_i16le(ins.p4));
    if (!await _sendAndWaitAck(CmdCode.insInsert, payload)) return false;
    _insList.insert(index, ins);
    _count = _insList.length;
    notifyListeners();
    return true;
  }

  Future<bool> delete(int index) async {
    if (!await _sendAndWaitAck(CmdCode.insDelete, [index])) return false;
    if (index < _insList.length) {
      _insList.removeAt(index);
      _count = _insList.length;
    }
    notifyListeners();
    return true;
  }

  void removeLocal(int index) {
    if (index < 0 || index >= _insList.length) return;
    _insList.removeAt(index);
    _count = _insList.length;
    notifyListeners();
  }

  // ── 执行控制 ──
  Future<bool> exec() async {
    _executing = true;
    _paused = false;
    _pc = 0;
    app.setLocked(true);
    notifyListeners();
    app.addLog('INS EXEC ($_count 条指令)');
    final ok = await _sendAndWaitAck(CmdCode.insExec, []);
    if (ok) {
      // 后台监听完成事件, 自动清除执行状态
      _waitForInsEvent(timeoutSec: 120).then((code) {
        if (!_executing) return; // 已被 stop() 清除
        if (code == null) {
          app.addLog('INS EXEC 超时: 未收到完成事件');
        } else if (code == 1) {
          app.addLog('INS EXEC 完成');
        } else {
          app.addLog('INS EXEC 出错: 0x${code.toRadixString(16).toUpperCase()}');
        }
        _executing = false;
        _paused = false;
        app.setLocked(false);
        notifyListeners();
      });
    } else {
      _executing = false;
      app.setLocked(false);
      notifyListeners();
    }
    return ok;
  }

  Future<bool> stop() async {
    if (!await _sendAndWaitAck(CmdCode.insStop, [])) return false;
    _executing = false;
    _paused = false;
    app.setLocked(false);
    notifyListeners();
    return true;
  }

  Future<bool> pause() async {
    if (!await _sendAndWaitAck(CmdCode.insPause, [])) return false;
    _paused = true;
    notifyListeners();
    return true;
  }

  Future<bool> resume() async {
    if (!await _sendAndWaitAck(CmdCode.insResume, [])) return false;
    _paused = false;
    notifyListeners();
    return true;
  }

  Future<bool> step() async {
    _pc++;
    notifyListeners();
    return _sendAndWaitAck(CmdCode.insStep, []);
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
        sub?.cancel(); completer.complete(true);
      } else if (pkt.cmd == CmdCode.nack &&
                 pkt.payload.isNotEmpty &&
                 pkt.payload.first == cmd) {
        sub?.cancel(); completer.complete(false);
      }
    });
    Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) { sub?.cancel(); completer.complete(false); }
    });
    return completer.future;
  }

  // ── 单条指令执行 ──
  /// 保存当前缓冲区 → 清空 → 附加一条 → 执行 → 等待 INS_EVENT → 恢复
  Future<void> playSingle(Instruction ins) async {
    if (executing) return;
    app.addLog('INS 单步执行: ${ins.label}');
    app.setLocked(true);

    // 保存旧列表
    final saved = _insList.toList();

    // 清空并附加单条
    await _sendAndWaitAck(CmdCode.insClear, []);
    _insList.clear();
    await append(ins);

    // 执行
    _executing = true;
    _paused = false;
    notifyListeners();
    await _sendAndWaitAck(CmdCode.insExec, []);

    // 等待 MCU 发送 INS_EVENT (payload[0]: 1=完成, 0xFF=超时, 0xFE=编码器, 0xFD=IMU)
    final errCode = await _waitForInsEvent(timeoutSec: 15);

    if (errCode == null) {
      app.addLog('INS 单步超时: 未收到完成事件');
    } else if (errCode == 1) {
      app.addLog('INS 单步完成');
    } else {
      app.addLog('INS 单步出错: 0x${errCode.toRadixString(16).toUpperCase()}');
    }

    // 恢复原列表
    await _sendAndWaitAck(CmdCode.insClear, []);
    _insList.clear();
    for (final i in saved) {
      await append(i);
    }
    _executing = false;
    app.setLocked(false);
    notifyListeners();
  }

  /// 等待 MCU 的 INS_EVENT 通知
  /// 返回 payload[0]: 1=正常完成, 0xFF=超时, 0xFE=编码器卡死, 0xFD=IMU失联
  /// 返回 null: 等待超时未收到事件
  Future<int?> _waitForInsEvent({int timeoutSec = 15}) async {
    final completer = Completer<int?>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.insEvent && pkt.payload.isNotEmpty) {
        sub?.cancel();
        completer.complete(pkt.payload[0]);
      }
    });
    Timer(Duration(seconds: timeoutSec), () {
      if (!completer.isCompleted) {
        sub?.cancel();
        completer.complete(null);
      }
    });
    return completer.future;
  }

  /// 更新已本地存在的指令 (重新下发到 MCU)
  Future<void> updateAt(int index, Instruction ins) async {
    if (index < 0 || index >= _insList.length) return;
    if (!await _sendAndWaitAck(CmdCode.insDelete, [index])) return;
    if (!await _sendAndWaitAck(CmdCode.insInsert, [index, ins.opcode,
      ins.p1 & 0xFF, (ins.p1 >> 8) & 0xFF,
      ins.p2 & 0xFF, (ins.p2 >> 8) & 0xFF,
      ins.p3 & 0xFF, (ins.p3 >> 8) & 0xFF,
      ins.p4 & 0xFF, (ins.p4 >> 8) & 0xFF,
    ])) return;
    _insList[index] = ins;
    app.addLog('INS 编辑 #$index: ${ins.label}');
    notifyListeners();
  }

  static List<int> _i16le(int v) => [v & 0xFF, (v >> 8) & 0xFF];

  @override
  void dispose() => super.dispose();

  // ── Opcode 参考 ──
  static const opcodes = [
    (0x00, 'NOP', <String>[]),
    (0x01, 'MOVE_CM', <String>['dist×10', 'speed']),
    (0x02, 'MOVE_RAMP', <String>['dist×10']),
    (0x03, 'MOVE_TRANSITION', <String>[]),
    (0x05, 'MOVE_TIME', <String>['time_ms', 'speed']),
    (0x10, 'ROTATE_IMU', <String>['deg×10', 'speed']),
    (0x11, 'ROTATE_ENC', <String>['deg×10', 'speed']),
    (0x20, 'LINE_FOLLOW', <String>['timeout_ms']),
    (0x30, 'WAIT_BLACK', <String>['timeout_ms']),
    (0x31, 'WAIT_NOBLACK', <String>['timeout_ms']),
    (0x33, 'STOP_IF_BLACK', <String>[]),
    (0x40, 'SET_SPEED', <String>['speed']),
    (0x41, 'SET_SPEED_RAW', <String>['left', 'right']),
    (0x52, 'YAW_RESET', <String>[]),
    (0x53, 'ENC_RESET', <String>[]),
    (0x54, 'DELAY', <String>['time_ms']),
    (0x56, 'BRAKE', <String>[]),
    (0x60, 'LABEL', <String>['id']),
    (0x61, 'JUMP', <String>['label_id']),
    (0x63, 'IF', <String>['cond_type', 'cond_val']),
    (0x64, 'ELSE', <String>[]),
    (0x65, 'END_IF', <String>[]),
    (0x66, 'FOR', <String>['var_id', 'start', 'end', 'step']),
    (0x67, 'END_FOR', <String>[]),
    (0x68, 'WHILE', <String>['cond_type', 'cond_val']),
    (0x69, 'END_WHILE', <String>[]),
    (0x6A, 'BREAK', <String>[]),
    (0x6B, 'CONTINUE', <String>[]),
    (0x6F, 'END_SCRIPT', <String>[]),
  ];
}

/// 指令
class Instruction {
  int opcode;
  int p1, p2, p3, p4;

  Instruction({this.opcode = 0x00, this.p1 = 0, this.p2 = 0, this.p3 = 0, this.p4 = 0});

  String get label {
    final def = InsService.opcodes.where((o) => o.$1 == opcode).firstOrNull;
    if (def == null) return '0x${opcode.toRadixString(16)}';
    return def.$2;
  }

  List<String> get paramNames {
    final def = InsService.opcodes.where((o) => o.$1 == opcode).firstOrNull;
    return def?.$3.toList() ?? <String>[];
  }
}
