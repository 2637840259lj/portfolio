import 'package:flutter/material.dart';

class CyberpunkTheme {
  static const Color background = Color(0xFF0F0F23);
  static const Color surface = Color(0xFF1A1A35);
  static const Color primary = Color(0xFF7C3AED);
  static const Color success = Color(0xFF10B981);
  static const Color warn = Color(0xFFF59E0B);
  static const Color danger = Color(0xFFEF4444);
  static const Color text = Color(0xFFE2E8F0);
  static const Color dim = Color(0xFF94A3B8);
  static const Color cyan = Color(0xFF22D3EE);
  static const Color green = Color(0xFF4ADE80);
  static const Color amber = Color(0xFFF59E0B);
  static const Color red = Color(0xFFEF4444);
  static const Color lightBlue = Color(0xFF67E8F9);
  static const Color lightGray = Color(0xFF64748B);
  static const Color darkSurface = Color(0xFF172033);
  static const Color darkGray = Color(0xFF26304A);
  static const Color darkBorder = Color(0xFF334155);
  static const Color activeBorder = Color(0xFF2D2D4A);

  // Neo-Apple Control：仅用于纯展示层的统一表面、空间和新拟态层级。
  static const Color insetSurface = Color(0xFF172033);
  static const Color insetDeep = Color(0xFF101827);
  static const Color raisedSurface = Color(0xFF202044);
  static const Color shadowDark = Color(0x66080816);
  static const Color shadowDeeper = Color(0x99050812);
  static const Color shadowLight = Color(0x1AFFFFFF);
  static const Color edgeHighlight = Color(0x26FFFFFF);
  static const double radiusControl = 12;
  static const double radiusCard = 16;
  static const double radiusSheet = 20;
  static const double space1 = 4;
  static const double space2 = 8;
  static const double space3 = 12;
  static const double space4 = 16;
  static const double space5 = 24;
  static const List<BoxShadow> raisedShadows = [
    BoxShadow(color: shadowDark, offset: Offset(5, 5), blurRadius: 12),
    BoxShadow(color: shadowLight, offset: Offset(-2, -2), blurRadius: 6),
  ];
  static const List<BoxShadow> insetShadows = [
    BoxShadow(color: shadowDark, offset: Offset(3, 3), blurRadius: 7),
    BoxShadow(color: shadowLight, offset: Offset(-1, -1), blurRadius: 4),
  ];
  static const List<BoxShadow> raisedShadowsStrong = [
    BoxShadow(color: shadowDeeper, offset: Offset(7, 8), blurRadius: 16),
    BoxShadow(color: shadowLight, offset: Offset(-2, -2), blurRadius: 7),
  ];
  static const List<BoxShadow> insetShadowsStrong = [
    BoxShadow(color: shadowDeeper, offset: Offset(4, 5), blurRadius: 10),
    BoxShadow(color: shadowLight, offset: Offset(-1, -1), blurRadius: 5),
  ];

  // UI 动画 token：仅用于视觉反馈，不参与通信或控制状态。
  static const Curve easeOut = Cubic(0.23, 1, 0.32, 1);
  static const Curve easeInOut = Cubic(0.77, 0, 0.175, 1);
  static const Duration durPress = Duration(milliseconds: 120);
  static const Duration durFast = Duration(milliseconds: 150);
  static const Duration durStandard = Duration(milliseconds: 200);

  static ThemeData darkTheme = ThemeData(
    brightness: Brightness.dark,
    scaffoldBackgroundColor: background,
    colorScheme: const ColorScheme.dark(
      primary: primary,
      secondary: cyan,
      surface: surface,
      background: background,
      error: danger,
      onPrimary: text,
      onSecondary: text,
      onSurface: text,
      onBackground: text,
      onError: text,
    ),
    textTheme: const TextTheme(
      bodyLarge: TextStyle(color: text),
      bodyMedium: TextStyle(color: text),
      titleLarge: TextStyle(color: text),
      titleMedium: TextStyle(color: text),
      titleSmall: TextStyle(color: text),
    ),
    appBarTheme: const AppBarTheme(
      backgroundColor: surface,
      foregroundColor: text,
    ),
    bottomNavigationBarTheme: const BottomNavigationBarThemeData(
      backgroundColor: surface,
      selectedItemColor: primary,
      unselectedItemColor: dim,
    ),
    // Add more theme properties as needed
  );
}
