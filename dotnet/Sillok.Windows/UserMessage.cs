using Sillok.Core;

namespace Sillok.Windows;

public static class UserMessage
{
    public const string SpeakerFormat = "화자 {speaker}";

    public static string Describe(Exception error) => error switch
    {
        ModelChecksumException checksum => $"{checksum.FileName} 파일이 손상되었습니다. 다시 받아 주세요",
        InsufficientDiskSpaceException space => $"저장 공간이 부족합니다. {space.Needed / 1_048_576}MB가 더 필요합니다",
        InvalidDataException => "이 파일을 읽을 수 없습니다",
        FileNotFoundException => "원본 파일을 찾을 수 없습니다",
        _ => error.Message,
    };
}
