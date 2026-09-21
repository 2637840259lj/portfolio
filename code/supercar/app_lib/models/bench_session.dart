import 'bench_result.dart';
import 'pid_schedule_node.dart';

/// 单一闭环类型、单一速度节点的连续调参会话。
/// 会话仅在 App 侧管理；MCU 每次测试均独立、本地实时闭环执行。
class BenchSample {
  final BenchResult result;
  final DateTime receivedAt;
  final double? manualTurnErrorDeg;
  final double? actualDistanceCm;
  final double? manualHeadingErrorDeg;

  const BenchSample({
    required this.result,
    required this.receivedAt,
    this.manualTurnErrorDeg,
    this.actualDistanceCm,
    this.manualHeadingErrorDeg,
  });
}

class BenchSession {
  static const int requiredSamples = 3;

  final BenchMode mode;
  final int nodeIndex;
  final double speedCmS;
  final PidScheduleNode pidBefore;
  final DateTime startedAt;
  final List<BenchSample> samples = [];

  PidScheduleNode? pidAfter;
  DateTime? finalizedAt;
  String? csvUri;

  BenchSession({
    required this.mode,
    required this.nodeIndex,
    required this.speedCmS,
    required this.pidBefore,
    required this.startedAt,
  });

  String get key => '${mode.code}:$nodeIndex';
  int get remainingSamples => (requiredSamples - samples.length).clamp(0, requiredSamples);
  bool get isReady => samples.length >= requiredSamples;
  bool get isFinalized => pidAfter != null && finalizedAt != null;
}
