import 'dart:typed_data';
import '../models/ble_packet.dart';

/// 协议层：帧封装 / 帧解析（状态机）/ XOR 校验
///
/// 与 MCU ble_protocol.c 严格一致:
///   帧: [0xAA] [LEN] [CMD] [PAYLOAD:0~250B] [XOR8]
///   XOR8 = LEN ^ CMD ^ PAYLOAD[0] ^ ... ^ PAYLOAD[N-1]
class BleProtocol {
  // ── 组帧 ──

  static Uint8List pack(int cmd, List<int> payload) {
    if (payload.length > frameMaxPayload) {
      throw ArgumentError(
          'Payload too long: ${payload.length} > $frameMaxPayload');
    }
    final len = payload.length;
    final frame = <int>[];
    frame.add(frameSyncByte);
    frame.add(len);
    frame.add(cmd);
    frame.addAll(payload);

    // XOR8 = LEN ^ CMD ^ PAYLOAD[0] ^ ... ^ PAYLOAD[N-1]  (不含 SYNC)
    int xor = len ^ cmd;
    for (final b in payload) {
      xor ^= b;
    }
    frame.add(xor);

    return Uint8List.fromList(frame);
  }

  static Uint8List packSimple(int cmd) => pack(cmd, []);

  // ── 解帧状态机 ──

  static const int _stIdle = 0;
  static const int _stLen = 1;
  static const int _stData = 2;

  int _state = _stIdle;
  int _len = 0;
  final List<int> _buf = [];

  List<BlePacket> feed(Uint8List data) {
    final packets = <BlePacket>[];
    for (final byte in data) {
      final pkt = _feedByte(byte);
      if (pkt != null) packets.add(pkt);
    }
    return packets;
  }

  BlePacket? _feedByte(int byte) {
    switch (_state) {
      case _stIdle:
        if (byte == frameSyncByte) {
          _state = _stLen;
        }
        return null;

      case _stLen:
        if (byte > frameMaxPayload) {
          _state = _stIdle;
          return null;
        }
        _len = byte;
        _buf.clear();
        _state = _stData;
        return null;

      case _stData:
        _buf.add(byte);
        // 需要: CMD(1) + PAYLOAD(_len) + XOR8(1) = _len + 2 字节
        if (_buf.length >= _len + 2) {
          return _tryParse();
        }
        return null;
    }
    return null;
  }

  BlePacket? _tryParse() {
    _state = _stIdle;
    final cmd = _buf[0];
    final payloadLen = _len;
    final dataEnd = payloadLen + 1;
    final payload = (payloadLen > 0) ? _buf.sublist(1, dataEnd) : <int>[];
    final rxXor = _buf[dataEnd];

    // XOR8 = LEN ^ CMD ^ PAYLOAD  (不含 SYNC)
    int calcXor = _len ^ cmd;
    for (final b in payload) {
      calcXor ^= b;
    }

    if (calcXor != rxXor) return null;
    return BlePacket(cmd: cmd, payload: payload);
  }

  void reset() {
    _state = _stIdle;
    _buf.clear();
    _len = 0;
  }

  static int xor8(List<int> data) =>
      data.fold<int>(0, (prev, b) => prev ^ b);
}
