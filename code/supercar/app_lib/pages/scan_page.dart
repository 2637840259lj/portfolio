import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:provider/provider.dart';
import '../services/app_state.dart';
import '../services/ble_connection.dart';
import 'control_page.dart';
import 'param_page.dart';
import 'telem_page.dart';
import 'teach_page.dart';
import 'line_page.dart';
import 'h1_line_debug_page.dart';
import 'pid_assessment_page.dart';
import 'competition_tuning_page.dart';
import '../widgets/debug_panel.dart';
import '../widgets/emergency_fab.dart';
import '../theme.dart';

/// BLE 设备扫描页面
class ScanPage extends StatefulWidget {
  /// 仅冷启动进入扫描页时自动回连；用户主动断开后保留在扫描页供其自行选择。
  final bool autoReconnect;

  const ScanPage({super.key, this.autoReconnect = true});

  @override
  State<ScanPage> createState() => _ScanPageState();
}

class _ScanPageState extends State<ScanPage> {
  Timer? _autoReconnectTimer;
  String? _rememberedDeviceId;
  bool _autoReconnectCancelled = false;
  bool _autoReconnectAttempted = false;

  @override
  void initState() {
    super.initState();
    // Web Bluetooth 要求扫描必须由用户手势触发，不能自动调用
    if (!kIsWeb) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        _startScan();
        if (widget.autoReconnect) _prepareAutoReconnect();
      });
    }
  }

  Future<void> _prepareAutoReconnect() async {
    final app = context.read<AppState>();
    final id = await app.lastDeviceId();
    if (!mounted || id == null || id.isEmpty) return;
    setState(() => _rememberedDeviceId = id);
    // 等扫描结果中出现“上次成功连接”的设备再连接；不会尝试未知设备。
    _autoReconnectTimer = Timer.periodic(
      const Duration(milliseconds: 350),
      (_) => _tryAutoReconnect(),
    );
  }

  Future<void> _tryAutoReconnect() async {
    if (!mounted ||
        _autoReconnectCancelled ||
        _autoReconnectAttempted ||
        _rememberedDeviceId == null)
      return;
    final app = context.read<AppState>();
    for (final result in app.bleConn.scanResults) {
      if (result.device.remoteId.str != _rememberedDeviceId) continue;
      _autoReconnectAttempted = true;
      _autoReconnectTimer?.cancel();
      final ok = await app.autoConnectIfRemembered(result.device);
      if (ok && mounted) {
        await app.bleConn.stopScan();
        if (!mounted) return;
        Navigator.pushReplacement(
          context,
          MaterialPageRoute(builder: (_) => const MainPage()),
        );
      } else if (mounted) {
        // 本次自动回连失败不循环重试，保留扫描页给用户自主选择。
        setState(() {});
      }
      return;
    }
  }

  @override
  void dispose() {
    _autoReconnectTimer?.cancel();
    super.dispose();
  }

  Future<void> _startScan() async {
    final app = context.read<AppState>();
    await app.bleConn.startScan(timeout: const Duration(seconds: 15));
  }

  Future<void> _enterOffline() async {
    _autoReconnectCancelled = true;
    _autoReconnectTimer?.cancel();
    final app = context.read<AppState>();
    await app.bleConn.stopScan();
    if (!mounted) return;
    Navigator.pushReplacement(
      context,
      MaterialPageRoute(builder: (_) => const MainPage(offlineMode: true)),
    );
  }

  Future<void> _connect(BluetoothDevice device) async {
    // 用户主动选择设备即覆盖自动回连流程与已记忆设备。
    _autoReconnectCancelled = true;
    _autoReconnectTimer?.cancel();
    debugPrint(
      'ScanPage: 用户点击连接 device=${device.platformName} id=${device.remoteId}',
    );
    final app = context.read<AppState>();
    try {
      final ok = await app.connectAndHandshake(device);
      debugPrint('ScanPage: connectAndHandshake 返回 $ok, mounted=$mounted');
      if (ok && mounted) {
        Navigator.pushReplacement(
          context,
          MaterialPageRoute(builder: (_) => const MainPage()),
        );
      } else if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('连接失败: ${app.bleConn.statusText}')),
        );
      }
    } catch (e, stack) {
      debugPrint('ScanPage: 连接异常 — $e');
      debugPrint('ScanPage: 堆栈 — $stack');
      if (mounted) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('连接异常: $e')));
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        toolbarHeight: 44,
        backgroundColor: CyberpunkTheme.background,
        elevation: 0,
        title: const Text(
          'CH9141 蓝牙扫描',
          style: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.2,
          ),
        ),
        actions: [
          TextButton.icon(
            onPressed: _enterOffline,
            icon: const Icon(Icons.skip_next_rounded, size: 16),
            label: const Text('暂不连接'),
            style: TextButton.styleFrom(
              foregroundColor: const Color(0xFFA78BFA),
              textStyle: const TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
          const SizedBox(width: 4),
        ],
      ),
      body: Column(
        children: [
          _buildStatusBar(),
          Expanded(child: _buildDeviceList()),
        ],
      ),
    );
  }

  Widget _buildStatusBar() {
    return Consumer<AppState>(
      builder: (ctx, app, _) {
        final isScanning = app.bleConn.isScanning;
        final status = app.bleConn.statusText;
        String displayText;
        Color textColor;
        Color bgColor;

        if (app.bleConn.isConnecting &&
            _rememberedDeviceId != null &&
            !_autoReconnectCancelled) {
          displayText = '正在自动连接上次设备…';
          textColor = const Color(0xFF4ADE80);
          bgColor = const Color(0xFF1A2E1A);
        } else if (isScanning) {
          displayText = '正在扫描 BLE 设备…';
          textColor = const Color(0xFFA78BFA);
          bgColor = const Color(0xFF27273B);
        } else if (status == '蓝牙未开启') {
          displayText = '⚠ 蓝牙未开��� — 请打开手机蓝牙后重试';
          textColor = const Color(0xFFFCA5A5);
          bgColor = const Color(0xFF3B1A1A);
        } else if (kIsWeb && !isScanning) {
          displayText = 'Web 模式 — 点击下方按钮开始扫描';
          textColor = const Color(0xFFFBBF24);
          bgColor = const Color(0xFF2D2412);
        } else {
          displayText = status;
          textColor = const Color(0xFF94A3B8);
          bgColor = const Color(0xFF1A1A35);
        }

        return AnimatedContainer(
          duration: CyberpunkTheme.durFast,
          curve: CyberpunkTheme.easeOut,
          margin: const EdgeInsets.fromLTRB(12, 8, 12, 4),
          decoration: BoxDecoration(
            color: bgColor,
            borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
            border: Border.all(color: textColor.withAlpha(115), width: 1.1),
            boxShadow: CyberpunkTheme.raisedShadows,
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
            child: Row(
              children: [
                if (isScanning)
                  const SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  ),
                const SizedBox(width: 10),
                Expanded(
                  child: AnimatedDefaultTextStyle(
                    duration: CyberpunkTheme.durFast,
                    curve: CyberpunkTheme.easeOut,
                    style: TextStyle(fontSize: 14, color: textColor),
                    child: Text(displayText),
                  ),
                ),
                TextButton.icon(
                  onPressed: isScanning ? null : _startScan,
                  icon: const Icon(Icons.refresh, size: 18),
                  label: const Text('重新扫描'),
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildDeviceList() {
    return Consumer<AppState>(
      builder: (ctx, app, _) {
        final results = app.bleConn.scanResults.toList()
          ..sort(
            (a, b) =>
                (BleConnection.isCH9141Device(b) ? 1 : 0) -
                (BleConnection.isCH9141Device(a) ? 1 : 0),
          );
        final status = app.bleConn.statusText;
        final hint = status.contains('权限')
            ? status
            : status == '蓝牙未开启'
            ? '请打开系统蓝牙后重新扫描'
            : '请确认目标正在广播 BLE；传统蓝牙耳机不一定会出现在此列表';
        if (results.isEmpty) {
          return Center(
            child: Container(
              margin: const EdgeInsets.symmetric(horizontal: 32),
              padding: const EdgeInsets.fromLTRB(24, 28, 24, 24),
              decoration: BoxDecoration(
                color: CyberpunkTheme.raisedSurface,
                borderRadius: BorderRadius.circular(CyberpunkTheme.radiusSheet),
                border: Border.all(color: CyberpunkTheme.edgeHighlight),
                boxShadow: CyberpunkTheme.raisedShadowsStrong,
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Container(
                    width: 56,
                    height: 56,
                    decoration: BoxDecoration(
                      color: CyberpunkTheme.insetDeep,
                      shape: BoxShape.circle,
                      border: Border.all(
                        color: CyberpunkTheme.dim.withAlpha(120),
                      ),
                      boxShadow: CyberpunkTheme.insetShadows,
                    ),
                    child: const Icon(
                      Icons.bluetooth_disabled,
                      size: 28,
                      color: CyberpunkTheme.dim,
                    ),
                  ),
                  const SizedBox(height: 16),
                  const Text(
                    '未发现蓝牙设备',
                    style: TextStyle(
                      color: CyberpunkTheme.text,
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Text(
                    hint,
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                      fontSize: 11,
                      color: CyberpunkTheme.dim,
                    ),
                  ),
                ],
              ),
            ),
          );
        }

        return ListView.builder(
          padding: const EdgeInsets.only(top: 4),
          itemCount: results.length,
          itemBuilder: (ctx, i) {
            final r = results[i];
            final device = r.device;
            final name = r.advertisementData.advName;
            final rssi = r.rssi;
            final isCH9141 = BleConnection.isCH9141Device(r);

            return Container(
              margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
              decoration: BoxDecoration(
                color: isCH9141
                    ? const Color(0xFF1A2E1A)
                    : CyberpunkTheme.raisedSurface,
                borderRadius: BorderRadius.circular(CyberpunkTheme.radiusCard),
                border: Border.all(
                  color: isCH9141
                      ? CyberpunkTheme.success.withAlpha(135)
                      : CyberpunkTheme.darkBorder.withAlpha(180),
                ),
                boxShadow: CyberpunkTheme.raisedShadows,
              ),
              child: ListTile(
                leading: CircleAvatar(
                  backgroundColor: isCH9141
                      ? const Color(0xFF1B4D1B)
                      : const Color(0xFF2D2D4A),
                  child: Icon(
                    isCH9141 ? Icons.bluetooth_connected : Icons.bluetooth,
                    color: isCH9141
                        ? const Color(0xFF4ADE80)
                        : const Color(0xFF94A3B8),
                  ),
                ),
                title: Text(
                  name,
                  style: TextStyle(
                    fontWeight: isCH9141 ? FontWeight.w600 : FontWeight.normal,
                  ),
                ),
                subtitle: Text(
                  '${device.remoteId.str}\nRSSI: $rssi dBm',
                  style: const TextStyle(fontSize: 12),
                ),
                trailing: app.bleConn.isConnecting
                    ? const SizedBox(
                        width: 24,
                        height: 24,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : ElevatedButton(
                        onPressed: () => _connect(device),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: const Color(0xFF7C3AED),
                          foregroundColor: Colors.white,
                        ),
                        child: const Text('连接'),
                      ),
                isThreeLine: true,
              ),
            );
          },
        );
      },
    );
  }
}

/// 主页面（连接成功后的界面）
class MainPage extends StatefulWidget {
  /// 从扫描页选择“暂不连接”进入时为 true；不伪造连接状态。
  final bool offlineMode;

  const MainPage({super.key, this.offlineMode = false});

  @override
  State<MainPage> createState() => _MainPageState();
}

class _MainPageState extends State<MainPage> {
  int _tabIndex = 0;
  Timer? _bgScanTimer;
  StreamSubscription<List<ScanResult>>? _bgScanSub;

  bool get _offlineMode => widget.offlineMode;
  List<ScanResult> _nearbyDevices = [];

  static const _tabs = [
    ('遥控', Icons.gamepad),
    ('电机', Icons.settings),
    ('陀螺仪', Icons.sensors),
    ('循迹', Icons.explore),
    ('H题调试', Icons.route_rounded),
    ('PID测评', Icons.fact_check_rounded),
    ('比赛调参', Icons.tune_rounded),
    ('示教', Icons.auto_stories),
  ];

  @override
  void initState() {
    super.initState();
    _startBgScan();
  }

  @override
  void dispose() {
    _bgScanTimer?.cancel();
    _bgScanSub?.cancel();
    FlutterBluePlus.stopScan().catchError((_) {});
    super.dispose();
  }

  Future<void> _recoverLink() async {
    final app = context.read<AppState>();
    final device = app.bleConn.device;
    if (device == null) return;
    final ok = await app.recoverLink(device);
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(ok ? '链路已重新建立' : '恢复失败：请确认模块已上电且在蓝牙范围内')),
    );
  }

  Future<void> _switchTab(int nextIndex) async {
    if (nextIndex == _tabIndex) return;
    final app = context.read<AppState>();
    final previousIndex = _tabIndex;
    final from = _tabs[previousIndex].$1;
    final to = _tabs[nextIndex].$1;

    // 先建立全局发送栅栏并完成 S0 停车。旧页在本帧稍后 dispose 前，
    // 其 Timer 即使再触发，也不能把轮询、L/R 或旧独占命令重新塞入队列。
    await app.switchWorkPage(from: from, to: to);
    if (!mounted) {
      await app.finishWorkPageSwitch();
      return;
    }
    setState(() => _tabIndex = nextIndex);

    // 旧页 dispose 与新页 initState 都在本次重建中同步完成；下一事件循环再发送最终停车并放行新页命令。
    await Future<void>.delayed(Duration.zero);
    await app.finishWorkPageSwitch();
  }

  void _startBgScan() {
    // 离线浏览时复用同一条权限校验与扫描路径，避免另一路扫描绕过权限检查。
    _bgScanSub = FlutterBluePlus.scanResults.listen((results) {
      if (mounted) setState(() => _nearbyDevices = results);
    });
    _bgScanTimer = Timer.periodic(const Duration(seconds: 5), (_) async {
      if (!mounted) return;
      final ble = context.read<AppState>().bleConn;
      // 已连接或前台扫描中不抢占 BLE 时隙。
      if (ble.isConnected || ble.isScanning) return;
      await ble.startScan(timeout: const Duration(seconds: 3));
    });
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        Scaffold(
          appBar: AppBar(
            toolbarHeight: 42,
            backgroundColor: CyberpunkTheme.background,
            foregroundColor: CyberpunkTheme.text,
            elevation: 0,
            title: Consumer<AppState>(
              builder: (ctx, app, _) => Row(
                children: [
                  // 左侧: 页面名称
                  Text(
                    _tabs[_tabIndex].$1,
                    style: const TextStyle(
                      fontSize: 15,
                      color: CyberpunkTheme.primary,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  const Spacer(),
                  // 中间: 蓝牙下拉
                  PopupMenuButton<ScanResult>(
                    offset: const Offset(0, 32),
                    color: const Color(0xFF1A1A35),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        _buildLinkDot(app),
                        const SizedBox(width: 6),
                        Text(
                          app.bleConn.isConnected
                              ? (app.handshake.linkAlive ? '在线' : '握手…')
                              : (_offlineMode ? '离线浏览' : '未连接'),
                          style: const TextStyle(
                            fontSize: 13,
                            color: Colors.white,
                          ),
                        ),
                        const Icon(
                          Icons.arrow_drop_down,
                          color: Color(0xFF94A3B8),
                          size: 16,
                        ),
                      ],
                    ),
                    itemBuilder: (_) {
                      final ch9141 = _nearbyDevices
                          .where((r) => BleConnection.isCH9141Device(r))
                          .toList();
                      if (ch9141.isEmpty) {
                        return [
                          const PopupMenuItem(
                            value: null,
                            enabled: false,
                            child: Text(
                              '未发现设备',
                              style: TextStyle(
                                color: Color(0xFF64748B),
                                fontSize: 12,
                              ),
                            ),
                          ),
                        ];
                      }
                      return ch9141
                          .map(
                            (r) => PopupMenuItem(
                              value: r,
                              child: Row(
                                children: [
                                  const Icon(
                                    Icons.bluetooth_connected,
                                    size: 14,
                                    color: Color(0xFF4ADE80),
                                  ),
                                  const SizedBox(width: 8),
                                  Text(
                                    r.advertisementData.advName.isNotEmpty
                                        ? r.advertisementData.advName
                                        : r.device.id.toString(),
                                    style: const TextStyle(
                                      fontSize: 12,
                                      color: Color(0xFFE2E8F0),
                                    ),
                                  ),
                                  const Spacer(),
                                  Text(
                                    '${r.rssi}dBm',
                                    style: const TextStyle(
                                      fontSize: 10,
                                      color: Color(0xFF94A3B8),
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          )
                          .toList();
                    },
                    onSelected: (r) async {
                      final a = context.read<AppState>();
                      await a.bleConn.disconnect();
                      final ok = await a.connectAndHandshake(r.device);
                      if (ok && mounted) {
                        setState(() => _tabIndex = 0);
                      }
                    },
                  ),
                ],
              ),
            ),
            actions: [
              Consumer<AppState>(
                builder: (_, app, __) => Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 6),
                  child: Center(
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 8,
                        vertical: 4,
                      ),
                      decoration: BoxDecoration(
                        color: CyberpunkTheme.insetDeep,
                        borderRadius: BorderRadius.circular(
                          CyberpunkTheme.radiusControl,
                        ),
                        border: Border.all(color: CyberpunkTheme.edgeHighlight),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          if (app.bleConn.isConnected) ...[
                            Icon(
                              _rssiIcon(app.bleConn.rssi),
                              size: 12,
                              color: _rssiColor(app.bleConn.rssi),
                            ),
                            const SizedBox(width: 3),
                            Text(
                              '${app.bleConn.rssi}',
                              style: TextStyle(
                                fontSize: 10,
                                color: _rssiColor(app.bleConn.rssi),
                                fontFamily: 'monospace',
                              ),
                            ),
                            const SizedBox(width: 7),
                          ] else
                            const Text(
                              '--',
                              style: TextStyle(
                                fontSize: 10,
                                color: CyberpunkTheme.danger,
                                fontFamily: 'monospace',
                              ),
                            ),
                          Text(
                            'TX:${app.txBytes}',
                            style: const TextStyle(
                              fontSize: 10,
                              color: CyberpunkTheme.dim,
                              fontFamily: 'monospace',
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
              IconButton(
                icon: const Icon(Icons.bug_report, size: 18),
                tooltip: '调试',
                onPressed: () => DebugPanel.show(context),
              ),
              Consumer<AppState>(
                builder: (_, app, __) => IconButton(
                  icon: const Icon(Icons.sync, size: 18),
                  tooltip: '恢复链路：清队列并重新握手',
                  onPressed:
                      app.bleConn.isConnected && !app.bleConn.isConnecting
                      ? _recoverLink
                      : null,
                ),
              ),
              Padding(
                padding: const EdgeInsets.only(right: 30),
                child: IconButton(
                  icon: const Icon(Icons.bluetooth_disabled, size: 18),
                  tooltip: '断开连接',
                  onPressed: () async {
                    final app = context.read<AppState>();
                    await app.requestSafetyStop(reason: '主动断开蓝牙');
                    await app.bleConn.disconnect();
                    if (context.mounted) {
                      Navigator.pushReplacement(
                        context,
                        MaterialPageRoute(
                          builder: (_) => const ScanPage(autoReconnect: false),
                        ),
                      );
                    }
                  },
                ),
              ),
            ],
          ),
          body: Row(
            children: [
              // ──── 竖排图标侧边栏 ────
              Consumer<AppState>(
                builder: (_, app, __) => Container(
                  width: 64,
                  margin: const EdgeInsets.fromLTRB(8, 12, 8, 12),
                  decoration: BoxDecoration(
                    color: CyberpunkTheme.raisedSurface,
                    borderRadius: BorderRadius.circular(
                      CyberpunkTheme.radiusSheet,
                    ),
                    border: Border.all(color: CyberpunkTheme.edgeHighlight),
                    boxShadow: CyberpunkTheme.raisedShadowsStrong,
                  ),
                  child: Scrollbar(
                    thumbVisibility: true,
                    child: ListView.builder(
                      padding: const EdgeInsets.symmetric(vertical: 6),
                      itemCount: _tabs.length,
                      itemBuilder: (_, i) {
                        final active = _tabIndex == i;
                        return GestureDetector(
                          onTap: app.isLocked ? null : () => _switchTab(i),
                          child: AnimatedContainer(
                            duration: CyberpunkTheme.durStandard,
                            curve: CyberpunkTheme.easeOut,
                            width: 48,
                            height: 48,
                            margin: const EdgeInsets.symmetric(vertical: 3),
                            decoration: BoxDecoration(
                              gradient: active
                                  ? const LinearGradient(
                                      begin: Alignment.topLeft,
                                      end: Alignment.bottomRight,
                                      colors: [
                                        Color(0xFF8B5CF6),
                                        Color(0xFF6D28D9),
                                      ],
                                    )
                                  : null,
                              color: active ? null : Colors.transparent,
                              borderRadius: BorderRadius.circular(16),
                              border: Border.all(
                                color: active
                                    ? Colors.white.withAlpha(80)
                                    : Colors.transparent,
                              ),
                              boxShadow: active
                                  ? [
                                      BoxShadow(
                                        color: CyberpunkTheme.primary.withAlpha(
                                          110,
                                        ),
                                        blurRadius: 12,
                                      ),
                                      const BoxShadow(
                                        color: CyberpunkTheme.shadowDark,
                                        offset: Offset(3, 4),
                                        blurRadius: 7,
                                      ),
                                    ]
                                  : null,
                            ),
                            child: AnimatedScale(
                              duration: CyberpunkTheme.durFast,
                              curve: CyberpunkTheme.easeOut,
                              scale: active ? 1 : 0.9,
                              child: Center(
                                child: i == 4
                                    ? Text(
                                        'H',
                                        textAlign: TextAlign.center,
                                        style: TextStyle(
                                          height: 1.0,
                                          fontSize: 19,
                                          fontWeight: FontWeight.w900,
                                          color: active
                                              ? Colors.white
                                              : app.isLocked
                                              ? const Color(0xFF475569)
                                              : CyberpunkTheme.primary,
                                        ),
                                      )
                                    : Icon(
                                        _tabs[i].$2,
                                        size: 21,
                                        color: active
                                            ? Colors.white
                                            : app.isLocked
                                            ? const Color(0xFF475569)
                                            : CyberpunkTheme.dim,
                                      ),
                              ),
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                ),
              ),
              // ──── 内容区 ────
              Expanded(child: _buildTabContent()),
            ],
          ),
        ),
        Consumer<AppState>(
          builder: (_, app, __) => app.bleConn.isConnected
              ? EmergencyFab(
                  onTap: () {
                    app.requestSafetyStop(emergency: true, reason: '用户急停');
                  },
                  enabled: true,
                )
              : const SizedBox.shrink(),
        ),
      ],
    );
  }

  IconData _rssiIcon(int rssi) {
    if (rssi > -50) return Icons.signal_cellular_alt;
    if (rssi > -65) return Icons.signal_cellular_alt;
    return Icons.signal_cellular_connected_no_internet_4_bar;
  }

  Color _rssiColor(int rssi) {
    if (rssi > -50) return const Color(0xFF4ADE80);
    if (rssi > -70) return const Color(0xFFF59E0B);
    return const Color(0xFFEF4444);
  }

  Widget _buildLinkDot(AppState app) {
    final connected = app.bleConn.isConnected;
    final alive = app.handshake.linkAlive;
    final color = connected
        ? (alive ? const Color(0xFF4ADE80) : const Color(0xFFF59E0B))
        : const Color(0xFFEF4444);
    return Container(
      width: 7,
      height: 7,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        color: color,
        boxShadow: connected && alive
            ? [
                BoxShadow(
                  color: const Color(0xFF4ADE80).withAlpha(80),
                  blurRadius: 4,
                ),
              ]
            : null,
      ),
    );
  }

  Widget _buildTabContent() {
    switch (_tabIndex) {
      case 0:
        return const ControlPage();
      case 1:
        return const ParamPage();
      case 2:
        return const TelemPage();
      case 3:
        return const LinePage();
      case 4:
        return const H1LineDebugPage();
      case 5:
        return const PidAssessmentPage();
      case 6:
        return const CompetitionTuningPage();
      case 7:
        return const TeachPage();
      default:
        return const ControlPage();
    }
  }
}
