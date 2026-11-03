// ignore: unused_import
import 'package:intl/intl.dart' as intl;

import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Korean (`ko`).
class AppLocalizationsKo extends AppLocalizations {
  AppLocalizationsKo([String locale = 'ko']) : super(locale);

  @override
  String get appName => '실록';

  @override
  String get ok => '확인';

  @override
  String get cancel => '취소';

  @override
  String get save => '저장';

  @override
  String get close => '닫기';

  @override
  String get delete => '삭제';

  @override
  String get rename => '이름 바꾸기';

  @override
  String get retry => '다시 시도';

  @override
  String get remove => '목록에서 빼기';

  @override
  String get settings => '설정';

  @override
  String get add => '파일 추가';

  @override
  String get hideList => '파일 목록 숨기기';

  @override
  String get showList => '파일 목록 보기';

  @override
  String get mediaFiles => '오디오 및 동영상';

  @override
  String get title => '제목';

  @override
  String get welcomeTitle => '이 기기에서 전사하기';

  @override
  String welcomeBody(String size) {
    return '실록은 녹음을 화자별 텍스트로 바꿉니다. 모든 처리는 이 기기 안에서 하므로 녹음이 밖으로 나가지 않습니다. 먼저 음성 모델 약 $size를 받아야 합니다.';
  }

  @override
  String get modelsDownload => '받기';

  @override
  String get modelsResume => '이어 받기';

  @override
  String get modelsStop => '일시 중지';

  @override
  String modelsProgress(String done, String total, String speed) {
    return '$total 중 $done · 초당 $speed';
  }

  @override
  String get modelsVerifying => '파일을 확인하는 중…';

  @override
  String get modelsDamaged => '손상된 모델 파일을 지웠습니다. 다시 받아 주세요.';

  @override
  String modelsDownloadFailed(String message) {
    return '다운로드가 멈췄습니다: $message';
  }

  @override
  String get modelsVerified => '모델 파일이 모두 정상입니다.';

  @override
  String modelsCheckFailed(String message) {
    return '모델 파일을 확인하지 못했습니다: $message';
  }

  @override
  String get homeEmpty => '오디오나 동영상 파일을 열어 전사하세요.';

  @override
  String get detailNone => '결과를 고르세요.';

  @override
  String libraryWaiting(int position) {
    return '대기 중 ($position번째)';
  }

  @override
  String libraryRunning(String step, int percent) {
    return '$step · $percent%';
  }

  @override
  String libraryRunningLeft(String step, int percent, String left) {
    return '$step · $percent% · 약 $left 남음';
  }

  @override
  String libraryFailed(String message) {
    return '실패: $message';
  }

  @override
  String get stepRead => '파일 읽기';

  @override
  String get stepCopy => '다시 듣기 사본 저장';

  @override
  String get stepAnalyze => '음성 구간 찾기';

  @override
  String get stepTranscribe => '받아쓰기';

  @override
  String get stepDiarize => '화자 구분';

  @override
  String addTitle(int count) {
    String _temp0 = intl.Intl.pluralLogic(count, locale: localeName, other: '파일 $count개 전사');
    return '$_temp0';
  }

  @override
  String get addOptions => '옵션';

  @override
  String get addStart => '시작';

  @override
  String addModelMissing(String model) {
    return '$model 모델이 이 기기에 없습니다. 설정에서 받거나 다른 모델을 고르세요.';
  }

  @override
  String get language => '언어';

  @override
  String get languageAuto => '자동';

  @override
  String get languageKo => '한국어';

  @override
  String get languageEn => '영어';

  @override
  String get diarize => '화자 구분';

  @override
  String get diarizeOff => '화자 구분 안 함';

  @override
  String get glossary => '용어';

  @override
  String get glossaryHint => '녹음에 나오는 이름과 용어. 한 줄에 하나씩 또는 쉼표로 구분';

  @override
  String get device => '연산 장치';

  @override
  String get deviceCpu => 'CPU';

  @override
  String get transcriptionModel => '받아쓰기 모델';

  @override
  String get runningCancel => '취소';

  @override
  String get runningCancelling => '취소하는 중…';

  @override
  String runningOverall(String step, int percent) {
    return '$step · $percent%';
  }

  @override
  String runningLeft(String left) {
    return '약 $left 남음';
  }

  @override
  String get runningPreviews => '지금까지 받아쓴 문장입니다. 끝나면 화자와 함께 정리됩니다.';

  @override
  String get runningDetails => '자세히';

  @override
  String stepTime(String step, String time) {
    return '$step: $time';
  }

  @override
  String detailWaiting(int position) {
    return '시작 대기 중: $position번째 순서입니다.';
  }

  @override
  String get follow => '따라가기';

  @override
  String get latest => '최신 문장';

  @override
  String get search => '검색';

  @override
  String get searchHint => '결과에서 검색';

  @override
  String get searchNone => '일치하는 문장이 없습니다.';

  @override
  String get segmentPlay => '여기서 재생';

  @override
  String get segmentCopy => '복사';

  @override
  String segmentInfo(String start, String end, String language) {
    return '$start – $end · $language';
  }

  @override
  String get copied => '복사했습니다';

  @override
  String get copyText => '텍스트 복사';

  @override
  String get speakers => '화자';

  @override
  String speakerName(String n) {
    return '화자 $n';
  }

  @override
  String get speakerListen => '들어 보기';

  @override
  String get play => '재생';

  @override
  String get pause => '일시 정지';

  @override
  String get info => '처리 정보';

  @override
  String infoLength(String time) {
    return '원본 길이: $time';
  }

  @override
  String infoTotal(String time, String ratio) {
    return '합계: $time (원본의 $ratio배)';
  }

  @override
  String infoDevice(String device) {
    return '전사 장치: $device';
  }

  @override
  String infoFellBack(String device) {
    return '$device (GPU를 쓸 수 없어 대체)';
  }

  @override
  String get resultMissing => '이 결과를 읽을 수 없습니다.';

  @override
  String get resultEmpty => '음성을 찾지 못했습니다.';

  @override
  String deleteConfirm(String title) {
    return '“$title”을(를) 삭제할까요?';
  }

  @override
  String errorUnsupportedFile(String message) {
    return '이 파일의 소리를 읽을 수 없습니다($message).';
  }

  @override
  String errorModel(String message) {
    return '모델을 쓸 수 없습니다: $message';
  }

  @override
  String errorEngine(String message) {
    return '전사하지 못했습니다: $message';
  }

  @override
  String get settingsDefaults => '새 파일 기본 설정';

  @override
  String get settingsModels => '받아쓰기 모델';

  @override
  String get settingsModelsHint => '큰 모델은 정확하지만 느리고 메모리를 더 씁니다. 대부분의 기기에는 권장 모델이 알맞습니다.';

  @override
  String get modelRecommended => '권장';

  @override
  String get modelInUse => '새 파일에 사용';

  @override
  String get modelUse => '새 파일에 사용하기';

  @override
  String modelSummary(String accuracy, String speed, String quantization, String size) {
    return '정확도 $accuracy · 속도 $speed · $quantization · $size';
  }

  @override
  String get accuracyHighest => '최상';

  @override
  String get accuracyHigh => '높음';

  @override
  String get accuracyMedium => '보통';

  @override
  String get accuracyLow => '낮음';

  @override
  String get accuracyVeryLow => '매우 낮음';

  @override
  String get speedVerySlow => '매우 느림';

  @override
  String get speedSlow => '느림';

  @override
  String get speedMedium => '보통';

  @override
  String get speedFast => '빠름';

  @override
  String get speedVeryFast => '매우 빠름';

  @override
  String get speedFastest => '가장 빠름';

  @override
  String get quantF16 => '압축 없음';

  @override
  String get quantQ8 => '약한 압축';

  @override
  String get quantQ5 => '압축';

  @override
  String get settingsStorage => '저장 공간';

  @override
  String storageModels(String size) {
    return '모델: $size';
  }

  @override
  String storageHistory(String size) {
    return '결과와 녹음 사본: $size';
  }

  @override
  String get settingsMore => '기타';

  @override
  String get verifyModels => '모델 파일 확인';

  @override
  String get about => '정보';

  @override
  String version(String version) {
    return '버전: $version';
  }

  @override
  String deviceWithNpu(String device) {
    return '$device + NPU';
  }

  @override
  String get npuReady => 'NPU용 인코더를 받았습니다';

  @override
  String get npu => '인코더를 NPU에서 실행';

  @override
  String get npuHint => 'CPU와 함께 쓰면 전사가 GPU를 쓰지 않습니다';

  @override
  String get npuUnpacking => '확인하고 압축을 푸는 중…';

  @override
  String npuDownload(String size) {
    return 'NPU용 인코더 받기($size)';
  }
}
