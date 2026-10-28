package io.github.linkedlatch.sillok.ui

import android.content.Context
import android.text.format.DateUtils
import android.text.format.Formatter

fun formatBytes(context: Context, bytes: Long): String = Formatter.formatShortFileSize(context, bytes)

/** "1:02:03" or "2:03". */
fun formatTime(ms: Long): String {
    val seconds = ms.coerceAtLeast(0) / 1000
    val h = seconds / 3600
    val m = seconds % 3600 / 60
    val s = seconds % 60
    return if (h > 0) "%d:%02d:%02d".format(h, m, s) else "%d:%02d".format(m, s)
}

fun formatDate(context: Context, ms: Long): String =
    DateUtils.formatDateTime(context, ms, DateUtils.FORMAT_SHOW_DATE or DateUtils.FORMAT_SHOW_TIME or DateUtils.FORMAT_ABBREV_MONTH)
