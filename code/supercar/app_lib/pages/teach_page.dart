import 'package:flutter/material.dart';
import '../theme.dart';

/// Tab5: 示教编程
class TeachPage extends StatefulWidget {
  const TeachPage({super.key});
  @override
  State<TeachPage> createState() => _TeachPageState();
}

class _TeachPageState extends State<TeachPage> {
  @override
  Widget build(BuildContext context) {
    return Container(
      color: CyberpunkTheme.background,
      child: DefaultTabController(
      length: 2,
      child: Column(
        children: [
          const TabBar(
            labelColor: CyberpunkTheme.primary,
            unselectedLabelColor: CyberpunkTheme.dim,
            indicatorColor: CyberpunkTheme.primary,
            indicatorSize: TabBarIndicatorSize.label,
            tabs: [
              Tab(text: '路径示教'),
              Tab(text: '指令示教'),
            ],
          ),
          Expanded(
            child: TabBarView(
              children: [
                _buildPlaceholder('路径录制 / 回放'),
                _buildPlaceholder('指令序列编辑 / 执行'),
              ],
            ),
          ),
        ],
      ),
    ),
    );
  }

  Widget _buildPlaceholder(String label) {
    return Container(
      color: CyberpunkTheme.background,
      child: Center(
        child: TweenAnimationBuilder<double>(
          duration: CyberpunkTheme.durStandard,
          curve: CyberpunkTheme.easeOut,
          tween: Tween(begin: 0.0, end: 1.0),
          builder: (_, value, child) => Opacity(
            opacity: value,
            child: Transform.translate(
              offset: Offset(0, (1 - value) * 10),
              child: child,
            ),
          ),
          child: Container(
            margin: const EdgeInsets.symmetric(horizontal: 24),
            padding: const EdgeInsets.fromLTRB(20, 26, 20, 22),
            decoration: BoxDecoration(
              color: CyberpunkTheme.raisedSurface,
              borderRadius: BorderRadius.circular(CyberpunkTheme.radiusSheet),
              border: Border.all(
                color: CyberpunkTheme.primary.withAlpha(130),
                width: 1.1,
              ),
              boxShadow: CyberpunkTheme.raisedShadowsStrong,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Container(
                  width: 68,
                  height: 68,
                  decoration: BoxDecoration(
                    color: CyberpunkTheme.insetDeep,
                    shape: BoxShape.circle,
                    border: Border.all(
                      color: CyberpunkTheme.primary.withAlpha(150),
                      width: 1.2,
                    ),
                    boxShadow: CyberpunkTheme.insetShadows,
                  ),
                  child: const Icon(
                    Icons.construction,
                    size: 32,
                    color: CyberpunkTheme.primary,
                  ),
                ),
                const SizedBox(height: 16),
                Text(
                  label,
                  style: const TextStyle(
                    color: CyberpunkTheme.text,
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: 6),
                const Text(
                  '需 CH9141 支持 len>0 帧 — 待修复',
                  style: TextStyle(fontSize: 11, color: CyberpunkTheme.dim),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
