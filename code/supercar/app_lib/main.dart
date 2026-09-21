import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import 'services/app_state.dart';
import 'services/pid_schedule_service.dart';
import 'pages/scan_page.dart';
import 'theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]);
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  runApp(const CarBleApp());
}

class CarBleApp extends StatelessWidget {
  const CarBleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => AppState(),
      child: Builder(
        builder: (context) => ChangeNotifierProvider(
          create: (_) => PidScheduleService(app: context.read<AppState>()),
          child: MaterialApp(
        title: '小车蓝牙上位机',
        debugShowCheckedModeBanner: false,
        theme: CyberpunkTheme.darkTheme,
            home: const ScanPage(),
          ),
        ),
      ),
    );
  }
}
