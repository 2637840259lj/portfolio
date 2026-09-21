import 'package:flutter/services.dart';

/// Android 日志文件服务。
/// 日志保存到系统下载目录的 Aicar 文件夹；每次保存仅保留最新一份。
class LogFileService {
  static const MethodChannel _channel = MethodChannel('com.carble.car_ble_app/log_file');

  /// 保存并返回 Android content URI，供后续“打开”使用。
  static Future<String> saveLatest({
    required String fileName,
    required String content,
  }) async {
    final uri = await _channel.invokeMethod<String>('saveLatestLog', {
      'fileName': fileName,
      'content': content,
    });
    if (uri == null || uri.isEmpty) {
      throw PlatformException(
        code: 'empty_uri',
        message: '系统未返回已保存日志的位置',
      );
    }
    return uri;
  }

  /// H题单圈遥测直接保存到公共 Download 根目录，便于USB接电脑后取走。
  static Future<String> saveH1LineCsv({
    required String fileName,
    required String content,
  }) async {
    final uri = await _channel.invokeMethod<String>('saveH1LineCsv', {
      'fileName': fileName,
      'content': content,
    });
    if (uri == null || uri.isEmpty) {
      throw PlatformException(
        code: 'empty_uri',
        message: '系统未返回已保存CSV的位置',
      );
    }
    return uri;
  }

  /// 通过 Android content URI 交给系统中的表格或文本应用打开。
  static Future<bool> open(String contentUri) async {
    return await _channel.invokeMethod<bool>('openLog', {'uri': contentUri}) ?? false;
  }
}
