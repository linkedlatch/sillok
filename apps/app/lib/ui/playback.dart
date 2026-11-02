// Playing a job's copy for listening back. The screens use [Playback], so
// that tests can stand in for the platform's player.
import 'package:audioplayers/audioplayers.dart';

abstract interface class Playback {
  Stream<Duration> get position;
  Stream<Duration> get duration;
  Stream<bool> get playing;

  Future<void> open(String path);
  Future<void> seek(Duration position);
  Future<void> play();
  Future<void> pause();
  Future<void> dispose();
}

/// The platform's player, through audioplayers.
class AudioPlayback implements Playback {
  final _player = AudioPlayer();

  @override
  Stream<Duration> get position => _player.onPositionChanged;

  @override
  Stream<Duration> get duration => _player.onDurationChanged;

  @override
  Stream<bool> get playing => _player.onPlayerStateChanged.map((s) => s == PlayerState.playing);

  @override
  Future<void> open(String path) => _player.setSource(DeviceFileSource(path));

  @override
  Future<void> seek(Duration position) => _player.seek(position);

  @override
  Future<void> play() => _player.resume();

  @override
  Future<void> pause() => _player.pause();

  @override
  Future<void> dispose() => _player.dispose();
}
