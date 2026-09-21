import 'package:flutter/material.dart';

/// App 全局常量 — 颜色、尺寸、超时
class AppColors {
  static const primary = Color(0xFF1565C0);
  static const accent = Color(0xFF00B0FF);
  static const danger = Colors.red;
  static const success = Colors.green;
  static const warning = Colors.orange;

  static Color dangerBg = Colors.red.shade50;
  static Color successBg = Colors.green.shade50;
  static Color accentBg = const Color(0xFF00B0FF).withAlpha(20);
}

class AppDurations {
  static const ackTimeout = Duration(seconds: 2);
  static const handshakeTimeout = Duration(seconds: 5);
  static const heartbeatInterval = Duration(seconds: 2);
  static const pingTimeout = Duration(seconds: 2);
  static const rssiInterval = Duration(seconds: 2);
  static const pageQueryTimeout = Duration(seconds: 1);
  static const moveRepeat = Duration(milliseconds: 100);
  static const joystickSend = Duration(milliseconds: 50);
}

class AppLayout {
  static const joystickMaxSize = 210.0;
  static const joystickMinSize = 140.0;
  static const dirBtnWidth = 56.0;
  static const dirBtnHeight = 48.0;
  static const throttleRange = 80.0;
}
