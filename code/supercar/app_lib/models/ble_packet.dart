// 通信协议常量 — 与 MCU ble_protocol.h 严格一致
//
// 帧格式: [0xAA] [LEN:1B] [CMD:1B] [PAYLOAD:0~250B] [XOR8:1B]
// XOR8 = LEN ^ CMD ^ PAYLOAD[0] ^ ... ^ PAYLOAD[N-1]

const int frameSyncByte = 0xAA;
const int frameMinLength = 4;
const int frameMaxPayload = 250;

// ── 命令码 — 与 MCU ble_protocol.h 完全一致 ──
class CmdCode {
  // 系统 (0x00~0x06)
  static const int ping = 0x00;
  static const int reset = 0x01;
  static const int getState = 0x02;
  static const int ack = 0x03;
  static const int nack = 0x04;
  static const int getHwStatus = 0x05;
  static const int getPageData = 0x06;

  // 运动控制 (0x10~0x17)
  static const int moveRaw = 0x10;
  static const int stop = 0x11;
  static const int emergency = 0x12;
  static const int turn = 0x13;
  static const int moveDist = 0x14;
  static const int setArcade = 0x15;
  static const int setBaseSpeed = 0x16;
  static const int lineFollow = 0x17;
  static const int throttleSet = 0x18;
  static const int brake = 0x19;

  // 参数管理 (0x30~0x34)
  static const int paramRead = 0x30;
  static const int paramWrite = 0x31;
  static const int paramSave = 0x32;
  static const int paramLoad = 0x33;
  static const int paramList = 0x34;
  static const int pidScheduleRead = 0x35;
  static const int pidScheduleWrite = 0x36;
  static const int pidScheduleCommit = 0x37;
  static const int pidScheduleAutotune = 0x38;
  static const int benchStart = 0x39;
  static const int benchAbort = 0x3A;
  static const int benchLineStraightStart = 0x3B;
  static const int benchLineCurveStart = 0x3C;

  // 路径示教 (0x50~0x5C)
  static const int teachStart = 0x50;
  static const int teachStop = 0x51;
  static const int teachPlay = 0x52;
  static const int teachPause = 0x53;
  static const int teachResume = 0x54;
  static const int teachAbort = 0x55;
  static const int teachWaypoint = 0x56;
  static const int teachClear = 0x57;
  static const int teachSave = 0x58;
  static const int teachLoad = 0x59;
  static const int teachList = 0x5A;
  static const int teachDelete = 0x5B;
  static const int teachRecordWp = 0x5C;

  // 指令示教 (0x60~0x6C)
  static const int insClear = 0x60;
  static const int insAppend = 0x61;
  static const int insInsert = 0x62;
  static const int insDelete = 0x63;
  static const int insExec = 0x64;
  static const int insStop = 0x65;
  static const int insPause = 0x66;
  static const int insResume = 0x67;
  static const int insStep = 0x68;
  static const int insGetPc = 0x69;
  static const int insEvent = 0x6A;
  static const int insSetBp = 0x6B;
  static const int insGetBuf = 0x6C;

  // 遥测 / IMU (0x70~0x74)
  static const int telemStart = 0x70;
  static const int telemStop = 0x71;
  static const int telemFrame = 0x72;
  static const int imuCal = 0x73;
  static const int yawZero = 0x74;
  static const int motorStatus = 0x75;
  static const int lineStatus = 0x76;
  static const int grayCal = 0x77;
  static const int pidAdjust = 0x78;
  static const int lineStart = 0x79;
  static const int lineStop = 0x7A;
  static const int speedTune = 0x7B;
  static const int benchResult = 0x7C;
  static const int h1DebugMode = 0x7D;
  static const int calCapture = 0x7E;
  static const int calResult = 0x7F;

  // 调试 (0x80~0x83)
  static const int debugMsg = 0x80;
  static const int getDebug = 0x82;
  static const int clearDebug = 0x83;

  // 单字节透传
  static const int raw = 0xF0;

  // ── 名称映射（调试用）──
  static String name(int code) {
    switch (code) {
      case ping: return 'PING';
      case reset: return 'RESET';
      case getState: return 'GET_STATE';
      case ack: return 'ACK';
      case nack: return 'NACK';
      case getHwStatus: return 'GET_HW_STATUS';
      case getPageData: return 'GET_PAGE_DATA';
      case moveRaw: return 'MOVE_RAW';
      case stop: return 'STOP';
      case emergency: return 'EMERGENCY';
      case turn: return 'TURN';
      case moveDist: return 'MOVE_DIST';
      case setArcade: return 'SET_ARCADE';
      case setBaseSpeed: return 'SET_BASE_SPEED';
      case lineFollow: return 'LINE_FOLLOW';
      case paramRead: return 'PARAM_READ';
      case paramWrite: return 'PARAM_WRITE';
      case paramSave: return 'PARAM_SAVE';
      case paramLoad: return 'PARAM_LOAD';
      case paramList: return 'PARAM_LIST';
      case pidScheduleRead: return 'PID_SCHEDULE_READ';
      case pidScheduleWrite: return 'PID_SCHEDULE_WRITE';
      case pidScheduleCommit: return 'PID_SCHEDULE_COMMIT';
      case pidScheduleAutotune: return 'PID_SCHEDULE_AUTOTUNE';
      case benchStart: return 'BENCH_START';
      case benchAbort: return 'BENCH_ABORT';
      case benchLineStraightStart: return 'BENCH_LINE_STRAIGHT_START';
      case benchLineCurveStart: return 'BENCH_LINE_CURVE_START';
      case teachStart: return 'TEACH_START';
      case teachStop: return 'TEACH_STOP';
      case teachPlay: return 'TEACH_PLAY';
      case teachPause: return 'TEACH_PAUSE';
      case teachResume: return 'TEACH_RESUME';
      case teachAbort: return 'TEACH_ABORT';
      case teachWaypoint: return 'TEACH_WAYPOINT';
      case teachClear: return 'TEACH_CLEAR';
      case teachSave: return 'TEACH_SAVE';
      case teachLoad: return 'TEACH_LOAD';
      case teachList: return 'TEACH_LIST';
      case teachDelete: return 'TEACH_DELETE';
      case teachRecordWp: return 'TEACH_RECORD_WP';
      case insClear: return 'INS_CLEAR';
      case insAppend: return 'INS_APPEND';
      case insInsert: return 'INS_INSERT';
      case insDelete: return 'INS_DELETE';
      case insExec: return 'INS_EXEC';
      case insStop: return 'INS_STOP';
      case insPause: return 'INS_PAUSE';
      case insResume: return 'INS_RESUME';
      case insStep: return 'INS_STEP';
      case insGetPc: return 'INS_GET_PC';
      case insEvent: return 'INS_EVENT';
      case insSetBp: return 'INS_SET_BP';
      case insGetBuf: return 'INS_GET_BUF';
      case telemStart: return 'TELEM_START';
      case telemStop: return 'TELEM_STOP';
      case telemFrame: return 'TELEM_FRAME';
      case imuCal: return 'IMU_CAL';
      case yawZero: return 'YAW_ZERO';
      case motorStatus: return 'MOTOR_STATUS';
      case lineStatus: return 'LINE_STATUS';
      case grayCal: return 'GRAY_CAL';
      case pidAdjust: return 'PID_ADJUST';
      case lineStart: return 'LINE_START';
      case lineStop: return 'LINE_STOP';
      case speedTune: return 'SPEED_TUNE';
      case benchResult: return 'BENCH_RESULT';
      case h1DebugMode: return 'H1_DEBUG_MODE';
      case calCapture: return 'CAL_CAPTURE';
      case calResult: return 'CAL_RESULT';
      case debugMsg: return 'DEBUG_MSG';
      case getDebug: return 'GET_DEBUG';
      case clearDebug: return 'CLEAR_DEBUG';
      case raw: return 'RAW';
      default: return 'UNKNOWN(0x${code.toRadixString(16).padLeft(2, '0')})';
    }
  }
}

/// 解析后的数据包
class BlePacket {
  final int cmd;
  final List<int> payload;
  final DateTime timestamp;

  BlePacket({required this.cmd, required this.payload})
      : timestamp = DateTime.now();

  @override
  String toString() {
    final hex = payload.length <= 8
        ? payload.map((b) => b.toRadixString(16).padLeft(2, '0')).join(' ')
        : '${payload.length}bytes';
    return '${CmdCode.name(cmd)} [$hex]';
  }
}
