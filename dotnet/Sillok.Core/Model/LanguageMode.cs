using System.Text.Json;
using System.Text.Json.Serialization;

namespace Sillok.Core;

public enum LanguageModeKind
{
    Auto,
    KoreanEnglish,
    Fixed,
}

public sealed record LanguageMode(LanguageModeKind Kind, string? Code = null)
{
    public static LanguageMode Auto { get; } = new(LanguageModeKind.Auto);
    public static LanguageMode KoreanEnglish { get; } = new(LanguageModeKind.KoreanEnglish);
    public static LanguageMode Fixed(string code) => new(LanguageModeKind.Fixed, code);
}

public sealed class LanguageModeConverter : JsonConverter<LanguageMode>
{
    public override LanguageMode Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        using var document = JsonDocument.ParseValue(ref reader);
        var property = document.RootElement.EnumerateObject().FirstOrDefault();
        return property.Name switch
        {
            "auto" => LanguageMode.Auto,
            "koreanEnglish" => LanguageMode.KoreanEnglish,
            "fixed" => LanguageMode.Fixed(property.Value.GetProperty("_0").GetString() ?? throw new JsonException("언어 코드가 없습니다")),
            _ => throw new JsonException($"알 수 없는 언어 모드: {property.Name}"),
        };
    }

    public override void Write(Utf8JsonWriter writer, LanguageMode value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        switch (value.Kind)
        {
            case LanguageModeKind.Auto:
                writer.WriteStartObject("auto");
                break;
            case LanguageModeKind.KoreanEnglish:
                writer.WriteStartObject("koreanEnglish");
                break;
            case LanguageModeKind.Fixed:
                writer.WriteStartObject("fixed");
                writer.WriteString("_0", value.Code);
                break;
        }
        writer.WriteEndObject();
        writer.WriteEndObject();
    }
}
