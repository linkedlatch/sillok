using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Views;

/// <summary>The settings a job runs with: language, speakers, terms, model and device.</summary>
public sealed partial class SettingsFields : StackPanel
{
    private static readonly string[] Languages = ["auto", "ko", "en"];
    private readonly RadioButtons _language = new();
    private readonly ToggleSwitch _diarize = new();
    private readonly TextBox _glossary = new() { AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, MinHeight = 72 };
    private readonly ComboBox _model = new() { MinWidth = 280 };
    private readonly RadioButtons _device = new();
    private readonly IReadOnlyList<CatalogModel> _models;
    private bool _loading;

    /// <param name="models">Transcription models to choose from; one or none hides the choice.</param>
    public SettingsFields(JobSettings settings, IReadOnlyList<CatalogModel> models, IReadOnlyList<ComputeDevice> gpus)
    {
        Spacing = 12;
        _models = models;
        _loading = true;
        _language.Header = Strings.Get("SetupLanguage");
        foreach (var code in Languages) _language.Items.Add(Strings.Get("Language_" + code));
        _language.SelectedIndex = Math.Max(0, Array.IndexOf(Languages, settings.Language));
        _diarize.Header = Strings.Get("SetupDiarize");
        _diarize.IsOn = settings.Diarize;
        _glossary.Header = Strings.Get("SetupGlossary");
        _glossary.PlaceholderText = Strings.Get("SetupGlossaryHint");
        _glossary.Text = settings.Glossary;
        Children.Add(_language);
        Children.Add(_diarize);
        Children.Add(_glossary);
        if (models.Count > 1)
        {
            _model.Header = Strings.Get("SetupModel");
            foreach (var model in models) _model.Items.Add(Strings.ModelName(model));
            var current = settings.TranscriptionModel ?? models.FirstOrDefault(m => m.Recommended)?.Id;
            _model.SelectedIndex = Math.Max(0, models.ToList().FindIndex(m => m.Id == current));
            Children.Add(_model);
        }
        if (gpus.Count > 0)
        {
            // The CPU first, then each GPU in the library's order.
            _device.Header = Strings.Get("SetupDevice");
            _device.Items.Add(Strings.Get("DeviceCpu"));
            foreach (var gpu in gpus) _device.Items.Add(gpu.Description);
            _device.SelectedIndex = settings.GpuIndex is { } index ? index + 1 : 0;
            Children.Add(_device);
        }
        _loading = false;
        _language.SelectionChanged += (_, _) => Notify();
        _diarize.Toggled += (_, _) => Notify();
        _glossary.LostFocus += (_, _) => Notify();
        _model.SelectionChanged += (_, _) => Notify();
        _device.SelectionChanged += (_, _) => Notify();
        _initial = settings;
    }

    private readonly JobSettings _initial;

    /// <summary>The settings as the fields show them now.</summary>
    public JobSettings Settings => _initial with
    {
        Language = Languages[Math.Max(0, _language.SelectedIndex)],
        Diarize = _diarize.IsOn,
        Glossary = _glossary.Text,
        GpuIndex = _device.SelectedIndex > 0 ? _device.SelectedIndex - 1 : _device.Items.Count > 0 ? null : _initial.GpuIndex,
        TranscriptionModel = _models.Count > 1 && _model.SelectedIndex >= 0
            ? (_models[_model.SelectedIndex].Recommended ? null : _models[_model.SelectedIndex].Id)
            : _initial.TranscriptionModel,
    };

    /// <summary>Raised when a field changes, with the new settings.</summary>
    public event EventHandler<JobSettings>? Changed;

    private void Notify()
    {
        if (!_loading) Changed?.Invoke(this, Settings);
    }
}
