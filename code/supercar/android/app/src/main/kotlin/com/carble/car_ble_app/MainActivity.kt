package com.carble.car_ble_app

import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.annotation.NonNull
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val channelName = "com.carble.car_ble_app/log_file"
    private val logRelativePath = "Download/Aicar/"

    override fun configureFlutterEngine(@NonNull flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, channelName)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "saveLatestLog" -> saveLatestLog(call, result)
                    "saveH1LineCsv" -> saveH1LineCsv(call, result)
                    "openLog" -> openLog(call, result)
                    else -> result.notImplemented()
                }
            }
    }

    private fun saveLatestLog(call: MethodCall, result: MethodChannel.Result) {
        val fileName = call.argument<String>("fileName")
        val content = call.argument<String>("content")
        if (fileName.isNullOrBlank() || content == null) {
            result.error("invalid_arguments", "缺少日志文件名或内容", null)
            return
        }

        try {
            val resolver = contentResolver
            val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI

            // 闭环 CSV 按固定“测试类型 + 速度节点”文件名覆盖保存。
            // 仅删除 Download/Aicar 内同名旧记录，不影响其它闭环或其它速度节点的结果。
            resolver.query(
                collection,
                arrayOf(MediaStore.Downloads._ID),
                "${MediaStore.Downloads.RELATIVE_PATH} = ? AND ${MediaStore.Downloads.DISPLAY_NAME} = ?",
                arrayOf(logRelativePath, fileName),
                null,
            )?.use { cursor ->
                val idIndex = cursor.getColumnIndexOrThrow(MediaStore.Downloads._ID)
                while (cursor.moveToNext()) {
                    val oldUri = Uri.withAppendedPath(collection, cursor.getLong(idIndex).toString())
                    resolver.delete(oldUri, null, null)
                }
            }

            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, fileName)
                put(MediaStore.Downloads.MIME_TYPE, "text/csv")
                put(MediaStore.Downloads.RELATIVE_PATH, logRelativePath)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.Downloads.IS_PENDING, 1)
                }
            }
            val uri = resolver.insert(collection, values)
                ?: throw IllegalStateException("无法创建日志文件")

            resolver.openOutputStream(uri, "w")?.bufferedWriter(Charsets.UTF_8).use { writer ->
                if (writer == null) throw IllegalStateException("无法写入日志文件")
                writer.write(content)
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val published = ContentValues().apply {
                    put(MediaStore.Downloads.IS_PENDING, 0)
                }
                resolver.update(uri, published, null, null)
            }
            result.success(uri.toString())
        } catch (e: Exception) {
            result.error("save_failed", e.message, null)
        }
    }

    /** H题单圈循迹原始数据：直接写入公共 Download 根目录，方便USB连接电脑取走。 */
    private fun saveH1LineCsv(call: MethodCall, result: MethodChannel.Result) {
        val fileName = call.argument<String>("fileName")
        val content = call.argument<String>("content")
        if (fileName.isNullOrBlank() || content == null) {
            result.error("invalid_arguments", "缺少CSV文件名或内容", null)
            return
        }

        try {
            val resolver = contentResolver
            val values = ContentValues().apply {
                put(MediaStore.Downloads.DISPLAY_NAME, fileName)
                put(MediaStore.Downloads.MIME_TYPE, "text/csv")
                put(MediaStore.Downloads.RELATIVE_PATH, "Download/")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.Downloads.IS_PENDING, 1)
                }
            }
            val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                ?: throw IllegalStateException("无法创建Download中的CSV文件")

            resolver.openOutputStream(uri, "w")?.bufferedWriter(Charsets.UTF_8).use { writer ->
                if (writer == null) throw IllegalStateException("无法写入CSV文件")
                writer.write(content)
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                resolver.update(uri, ContentValues().apply {
                    put(MediaStore.Downloads.IS_PENDING, 0)
                }, null, null)
            }
            result.success(uri.toString())
        } catch (e: Exception) {
            result.error("save_failed", e.message, null)
        }
    }

    private fun openLog(call: MethodCall, result: MethodChannel.Result) {
        val uriText = call.argument<String>("uri")
        if (uriText.isNullOrBlank()) {
            result.error("invalid_uri", "缺少日志文件位置", null)
            return
        }
        try {
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(Uri.parse(uriText), "text/csv")
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            if (intent.resolveActivity(packageManager) == null) {
                result.success(false)
                return
            }
            startActivity(intent)
            result.success(true)
        } catch (e: Exception) {
            result.error("open_failed", e.message, null)
        }
    }
}
