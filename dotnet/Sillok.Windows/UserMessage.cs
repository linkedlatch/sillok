using Sillok.Core;

namespace Sillok.Windows;

public static class UserMessage
{
    public const string SpeakerFormat = "화자 {speaker}";

    public static string Describe(Exception error) => error switch
    {
        ModelChecksumException checksum => $"{checksum.FileName} 파일이 손상되었습니다. 다시 받아 주세요",
        InsufficientDiskSpaceException space => $"저장 공간이 부족합니다. {space.Needed / 1_048_576}MB가 더 필요합니다",
        HttpRequestException http => http.StatusCode is { } status
            ? $"모델 서버에 연결하지 못했습니다 (HTTP {(int)status})"
            : $"네트워크 연결을 확인해 주세요 ({Detail(http)})",
        InvalidDataException => "이 파일을 읽을 수 없습니다",
        FileNotFoundException => "원본 파일을 찾을 수 없습니다",
        _ => Detail(error),
    };

    private static string Detail(Exception error) =>
        string.IsNullOrWhiteSpace(error.Message) ? $"{error.GetType().Name} (0x{error.HResult:X8})" : error.Message;
}
