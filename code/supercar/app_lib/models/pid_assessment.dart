import 'dart:math' as math;

import 'bench_result.dart';
import 'bench_session.dart';

/// 对单个“闭环类型 × 速度节点”已采集样本的只读质量结论。
/// 分数只用于判断当前 PID 的可用性，不参与 PID 候选计算或写入 MCU。
enum PidAssessmentLevel {
  insufficient('样本不足'),
  failed('不合格'),
  needsTuning('待优化'),
  qualified('合格');

  const PidAssessmentLevel(this.label);
  final String label;
}

class PidAssessment {
  final BenchMode mode;
  final int nodeIndex;
  final int sampleCount;
  final PidAssessmentLevel level;
  final int totalScore;
  final int safetyScore;
  final int stabilityScore;
  final int accuracyScore;
  final List<String> reasons;
  final List<String> suggestions;

  const PidAssessment({
    required this.mode,
    required this.nodeIndex,
    required this.sampleCount,
    required this.level,
    required this.totalScore,
    required this.safetyScore,
    required this.stabilityScore,
    required this.accuracyScore,
    required this.reasons,
    required this.suggestions,
  });

  factory PidAssessment.fromSession(BenchMode mode, int nodeIndex, BenchSession? session) {
    final samples = session?.samples ?? const <BenchSample>[];
    if (samples.length < BenchSession.requiredSamples) {
      return PidAssessment(
        mode: mode,
        nodeIndex: nodeIndex,
        sampleCount: samples.length,
        level: PidAssessmentLevel.insufficient,
        totalScore: 0,
        safetyScore: 0,
        stabilityScore: 0,
        accuracyScore: 0,
        reasons: ['需至少完成 ${BenchSession.requiredSamples} 次有效样本；当前 ${samples.length} 次。'],
        suggestions: const ['每次结束后先保存样本；中止、超时和未回传结果不能计入。'],
      );
    }

    final reasons = <String>[];
    final suggestions = <String>[];
    final results = samples.map((sample) => sample.result).toList(growable: false);
    final safe = results.every((result) => _isSafeCompletion(mode, result));
    final safety = safe ? 40 : 0;
    if (!safe) {
      reasons.add('存在超时或未知结束原因，安全门槛未通过。');
      suggestions.add('先检查场地、供电、传感器标定与急停，再调整 PID。');
    } else {
      reasons.add('三次均以允许的安全原因结束。');
    }

    // 曲线测试允许黑线长期落在侧面探头；此时灰度质心的绝对位置描述的是路线几何，
    // 不是“偏离目标线”的误差。不能再用 meanAbsError 给曲线精度判零。
    final stabilityValues = mode == BenchMode.lineCurve
        ? results.map((result) => result.distanceCm).toList(growable: false)
        : samples.map((sample) => _accuracyError(mode, sample)).toList(growable: false);
    final spread = _stddev(stabilityValues);
    final medianError = _median(samples.map((sample) => _accuracyError(mode, sample)).toList(growable: false));
    final stability = _stabilityScore(mode, spread);
    final accuracy = mode == BenchMode.lineCurve
        ? _curveCompletionScore(session, results)
        : _accuracyScore(mode, medianError);

    if (stability < 20) {
      reasons.add(mode == BenchMode.lineCurve
          ? '三次曲线终点距离离散较大（标准差 ${spread.toStringAsFixed(2)} cm）。'
          : '三次结果离散较大（标准差 ${spread.toStringAsFixed(2)}）。');
      suggestions.add('复核电量、场地与起点一致性；确认后再微调该速度节点。');
    } else {
      reasons.add(mode == BenchMode.lineCurve
          ? '三次曲线终点距离重复性可接受（标准差 ${spread.toStringAsFixed(2)} cm）。'
          : '三次重复性处于可接受范围（标准差 ${spread.toStringAsFixed(2)}）。');
    }
    if (mode == BenchMode.lineCurve) {
      final utilization = _curveSteerUtilization(session, results) * 100.0;
      reasons.add(accuracy < 20
          ? '曲线平均转向接近限幅（中位利用率 ${utilization.toStringAsFixed(0)}%）。'
          : '曲线由侧面探头完成；曲线覆盖与转向余量正常（中位利用率 ${utilization.toStringAsFixed(0)}%）。');
      if (accuracy < 20) suggestions.add(_accuracyHint(mode));
    } else if (accuracy < 20) {
      reasons.add('典型误差偏大（中位数 ${medianError.toStringAsFixed(2)}）。');
      suggestions.add(_accuracyHint(mode));
    } else {
      reasons.add('典型误差符合当前经验阈值（中位数 ${medianError.toStringAsFixed(2)}）。');
    }
    if (mode == BenchMode.lineCurve && results.any((result) => result.curveSamplePercent < 80)) {
      reasons.add('曲线样本覆盖不足，不能代表真实弯道表现。');
      suggestions.add('使用连续弯道重测，确保大部分控制样本来自曲线段。');
    }

    final total = safety + stability + accuracy;
    final curveCoverageOk = mode != BenchMode.lineCurve || results.every((result) => result.curveSamplePercent >= 80);
    final level = !safe
        ? PidAssessmentLevel.failed
        : total >= 80 && curveCoverageOk
            ? PidAssessmentLevel.qualified
            : PidAssessmentLevel.needsTuning;
    return PidAssessment(
      mode: mode,
      nodeIndex: nodeIndex,
      sampleCount: samples.length,
      level: level,
      totalScore: total,
      safetyScore: safety,
      stabilityScore: stability,
      accuracyScore: accuracy,
      reasons: reasons,
      suggestions: suggestions,
    );
  }

  static bool _isSafeCompletion(BenchMode mode, BenchResult result) {
    if (result.reason == 2) return false;
    if (mode == BenchMode.lineStraight || mode == BenchMode.lineCurve) {
      return result.reason == 0 || result.reason == 1;
    }
    return result.reason == 0;
  }

  static double _accuracyError(BenchMode mode, BenchSample sample) {
    switch (mode) {
      case BenchMode.lineStraight:
      case BenchMode.lineCurve:
        return sample.result.meanAbsError.abs();
      case BenchMode.yaw90:
        return (sample.manualTurnErrorDeg ?? (sample.result.finalYawDeg - 90.0)).abs();
      case BenchMode.distance1m:
        return (((sample.actualDistanceCm ?? sample.result.distanceCm) - 100.0).abs());
    }
  }

  /// 曲线精度不能依据质心的绝对位置：侧面探头是正常曲线几何。
  /// 用“确实完成曲线段 + 未持续顶到转向限幅”评估当前可用性；真正横向偏差
  /// 需要 MCU 未来回传相对轨迹误差后再取代此经验指标。
  static int _curveCompletionScore(BenchSession? session, List<BenchResult> results) {
    if (session == null || results.any((result) => result.curveSamplePercent < 80)) return 0;
    final utilization = _curveSteerUtilization(session, results);
    if (utilization <= 0.80) return 30;
    if (utilization <= 0.95) return 15;
    return 0;
  }

  static double _curveSteerUtilization(BenchSession? session, List<BenchResult> results) {
    if (session == null || results.isEmpty) return 1.0;
    final limit = session.pidBefore.curveMaxSteer;
    if (limit <= 0.0) return 1.0;
    return _median(results.map((result) => result.meanAbsSteer.abs()).toList(growable: false)) / limit;
  }

  static int _accuracyScore(BenchMode mode, double error) {
    final limits = switch (mode) {
      BenchMode.lineStraight || BenchMode.lineCurve => (60.0, 120.0),
      BenchMode.yaw90 => (3.0, 6.0),
      BenchMode.distance1m => (3.0, 6.0),
    };
    if (error <= limits.$1) return 30;
    if (error <= limits.$2) return 15;
    return 0;
  }

  static int _stabilityScore(BenchMode mode, double spread) {
    final limits = switch (mode) {
      BenchMode.lineStraight || BenchMode.lineCurve => (25.0, 60.0),
      BenchMode.yaw90 => (2.0, 5.0),
      BenchMode.distance1m => (2.0, 5.0),
    };
    if (spread <= limits.$1) return 30;
    if (spread <= limits.$2) return 15;
    return 0;
  }

  static String _accuracyHint(BenchMode mode) => switch (mode) {
    BenchMode.lineStraight => '优先复核白底/黑线标定，再检查 line_kp、line_kd 与最大转向限幅。',
    BenchMode.lineCurve => '优先复核白底/黑线标定，再检查 curve_kp、curve_kd 与最大转向限幅。',
    BenchMode.yaw90 => '确认陀螺仪静止校准后，依据超转/少转方向调整 yaw_kp、yaw_kd 与最大转向限幅。',
    BenchMode.distance1m => '确认陀螺仪静止校准与卷尺数据后，检查 distance_scale、减速脉冲和终点速度。',
  };

  static double _median(List<double> values) {
    final sorted = [...values]..sort();
    final middle = sorted.length ~/ 2;
    return sorted.length.isOdd ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
  }

  static double _stddev(List<double> values) {
    if (values.length < 2) return 0;
    final mean = values.reduce((a, b) => a + b) / values.length;
    return math.sqrt(values.map((value) => math.pow(value - mean, 2)).reduce((a, b) => a + b) / values.length);
  }
}
