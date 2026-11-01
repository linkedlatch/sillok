// Downloads model files into the models folder. Each file goes to
// <file>.part first and resumes from there (HTTP Range); a server that
// answers a resume with the whole file starts it over.
import 'dart:async';
import 'dart:io';

import '../engine/sillok.dart';

class DownloadProgress {
  const DownloadProgress(this.done, this.total, this.bytesPerSecond);

  final int done;
  final int total;
  final int bytesPerSecond;
}

class DownloadStopped implements Exception {}

class ModelDownloader {
  HttpClient? _client;

  /// Bytes of [files] already in [dir], finished or partly.
  static int downloaded(Directory dir, List<ModelFile> files) {
    var total = 0;
    for (final file in files) {
      final done = File(_path(dir, file.file));
      final part = File('${_path(dir, file.file)}.part');
      if (done.existsSync()) {
        total += done.lengthSync();
      } else if (part.existsSync()) {
        total += part.lengthSync();
      }
    }
    return total;
  }

  /// Downloads [files] that are not complete in [dir]. Throws
  /// [DownloadStopped] after [stop], or the network error.
  Future<void> download(Directory dir, List<ModelFile> files, void Function(DownloadProgress) onProgress) async {
    await dir.create(recursive: true);
    final total = files.fold(0, (sum, f) => sum + f.size);
    var done = downloaded(dir, files);
    final clock = Stopwatch()..start();
    final startedWith = done;
    final client = _client = HttpClient()..userAgent = 'sillok';
    try {
      for (final file in files) {
        final target = File(_path(dir, file.file));
        if (target.existsSync() && target.lengthSync() == file.size) continue;
        final part = File('${target.path}.part');
        var have = part.existsSync() ? part.lengthSync() : 0;
        final request = await client.getUrl(Uri.parse(file.url));
        if (have > 0) request.headers.set(HttpHeaders.rangeHeader, 'bytes=$have-');
        final response = await request.close();
        if (response.statusCode == HttpStatus.ok && have > 0) {
          done -= have;
          have = 0;
        } else if (response.statusCode != HttpStatus.partialContent && response.statusCode != HttpStatus.ok) {
          throw HttpException('HTTP ${response.statusCode}', uri: Uri.parse(file.url));
        }
        final sink = part.openWrite(mode: have > 0 ? FileMode.append : FileMode.write);
        try {
          await for (final chunk in response) {
            sink.add(chunk);
            done += chunk.length;
            final seconds = clock.elapsedMilliseconds / 1000;
            onProgress(DownloadProgress(done, total, seconds > 0 ? ((done - startedWith) / seconds).round() : 0));
          }
        } finally {
          await sink.close();
        }
        if (part.lengthSync() != file.size) throw HttpException('incomplete download', uri: Uri.parse(file.url));
        await part.rename(target.path);
      }
    } on Object {
      if (_client == null) throw DownloadStopped();
      rethrow;
    } finally {
      _client = null;
      client.close(force: true);
    }
  }

  /// Stops [download]; what was received stays for a resume.
  void stop() {
    final client = _client;
    _client = null;
    client?.close(force: true);
  }

  static String _path(Directory dir, String file) => '${dir.path}${Platform.pathSeparator}$file';
}
