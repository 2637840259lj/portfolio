import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/ble_packet.dart';
import '../models/param_entry.dart';
import 'app_state.dart';

/// 参数读写服务 — 发送 PARAM 命令并收集回复
class ParamService extends ChangeNotifier {
  final AppState app;

  List<ParamEntry> _entries = [];
  bool _loading = false;
  String _status = '';

  List<ParamEntry> get entries => _entries;
  bool get loading => _loading;
  String get status => _status;

  StreamSubscription<BlePacket>? _sub;

  ParamService({required this.app});

  /// 加载参数列表 (PARAM_LIST)
  Future<void> loadParams() async {
    _loading = true;
    _status = '正在加载参数...';
    _entries.clear();
    notifyListeners();

    final entries = <ParamEntry>[];

    // 监听 PARAM_LIST 回复
    _sub?.cancel();
    _sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.paramList) {
        try {
          final entry = ParamEntry.fromListPayload(Uint8List.fromList(pkt.payload));
          entries.add(entry);
        } catch (e) {
          debugPrint('ParamService: 解析失败 $e');
        }
      }
    });

    // 发送 PARAM_LIST 请求
    await app.sendPacket(CmdCode.paramList, []);

    // 等待 2 秒收集所有条目
    await Future.delayed(const Duration(milliseconds: 800));
    _sub?.cancel();

    _entries = entries;
    _loading = false;
    _status = '共 ${_entries.length} 个参数';
    notifyListeners();
  }

  /// 读取单个参数 (PARAM_READ)
  Future<double?> readParam(int id) async {
    if (!app.handshake.linkAlive) return null;
    if (!await app.sendPacket(CmdCode.paramRead, [id])) return null;

    final completer = Completer<double?>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.paramRead && pkt.payload.isNotEmpty && pkt.payload[0] == id) {
        if (pkt.payload.length >= 5) {
          final val = ParamEntry.readF32LE(Uint8List.fromList(pkt.payload), 1);
          sub?.cancel();
          completer.complete(val);
        }
      }
    });
    Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) { sub?.cancel(); completer.complete(null); }
    });
    return completer.future;
  }

  /// 写入单个参数 (PARAM_WRITE)
  /// 返回: 0=ACK, 1=NACK, null=超时
  Future<int?> writeParam(int id, double value) async {
    if (!app.handshake.linkAlive) return null;
    // PARAM_WRITE payload: [id:u8, value:f32 LE]
    final payload = <int>[id];
    payload.addAll(ParamEntry.writeF32LE(value));
    if (!await app.sendPacket(CmdCode.paramWrite, payload)) return null;

    final completer = Completer<int?>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.ack &&
          pkt.payload.isNotEmpty &&
          pkt.payload.first == CmdCode.paramWrite) {
        sub?.cancel();
        completer.complete(0);
      } else if (pkt.cmd == CmdCode.nack &&
                 pkt.payload.isNotEmpty &&
                 pkt.payload.first == CmdCode.paramWrite) {
        sub?.cancel();
        completer.complete(1);
      }
    });
    Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) { sub?.cancel(); completer.complete(null); }
    });
    return completer.future;
  }

  /// 保存参数到 Flash (PARAM_SAVE)
  Future<bool> saveToFlash() async {
    if (!app.handshake.linkAlive) return false;
    if (!await app.sendPacket(CmdCode.paramSave, [])) return false;

    final completer = Completer<bool>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.ack &&
          pkt.payload.isNotEmpty &&
          pkt.payload.first == CmdCode.paramSave) {
        sub?.cancel();
        completer.complete(true);
      } else if (pkt.cmd == CmdCode.nack &&
                 pkt.payload.isNotEmpty &&
                 pkt.payload.first == CmdCode.paramSave) {
        sub?.cancel();
        completer.complete(false);
      }
    });
    Timer(const Duration(seconds: 3), () {
      if (!completer.isCompleted) { sub?.cancel(); completer.complete(false); }
    });
    return completer.future;
  }

  /// 恢复默认参数 (PARAM_LOAD)
  Future<bool> loadDefaults() async {
    if (!app.handshake.linkAlive) return false;
    if (!await app.sendPacket(CmdCode.paramLoad, [])) return false;

    final completer = Completer<bool>();
    StreamSubscription<BlePacket>? sub;
    sub = app.handshake.packetStream.listen((pkt) {
      if (pkt.cmd == CmdCode.ack &&
          pkt.payload.isNotEmpty &&
          pkt.payload.first == CmdCode.paramLoad) {
        sub?.cancel();
        completer.complete(true);
      } else if (pkt.cmd == CmdCode.nack &&
                 pkt.payload.isNotEmpty &&
                 pkt.payload.first == CmdCode.paramLoad) {
        sub?.cancel();
        completer.complete(false);
      }
    });
    Timer(const Duration(seconds: 2), () {
      if (!completer.isCompleted) { sub?.cancel(); completer.complete(false); }
    });
    return completer.future;
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}
