using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sillok.App.Pages;

/// <summary>The running job's steps: done, current with its progress and time left, waiting.</summary>
public sealed partial class RunningPage : Page
{
    private static AppState State => App.State;
    private readonly DispatcherTimer _clock = new() { Interval = TimeSpan.FromSeconds(1) };

    public RunningPage()
    {
        InitializeComponent();
        Heading.Text = Strings.Get("RunningTitle");
        BackButton.Content = Strings.Get("RunningBack");
        _clock.Tick += (_, _) => Update();
        Loaded += (_, _) =>
        {
            State.PropertyChanged += OnStateChanged;
            _clock.Start();
            Update();
        };
        Unloaded += (_, _) =>
        {
            State.PropertyChanged -= OnStateChanged;
            _clock.Stop();
        };
    }

    private void OnStateChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(AppState.Run)) Update();
    }

    private void Update()
    {
        if (State.Run is not { } run) return;
        FileName.Text = run.Name;
        CancelButton.Content = Strings.Get(run.Cancelling ? "RunningCancelling" : "Cancel");
        CancelButton.IsEnabled = !run.Cancelling;
        Steps.Children.Clear();
        var current = run.Steps.ToList().IndexOf(run.Step);
        for (var i = 0; i < run.Steps.Count; i++)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
            if (i < current) row.Children.Add(new FontIcon { Glyph = "", FontSize = 16 });  // check mark
            else if (i == current && !run.Cancelling) row.Children.Add(new ProgressRing { IsActive = true, Width = 16, Height = 16 });
            else row.Children.Add(new Border { Width = 16 });
            row.Children.Add(new TextBlock { Text = Strings.Step(run.Steps[i]) });
            Steps.Children.Add(row);
            if (i != current) continue;
            Steps.Children.Add(new ProgressBar { Maximum = 1, Value = run.Fraction });
            if (Remaining(run) is { } left)
            {
                Steps.Children.Add(new TextBlock
                {
                    Text = Strings.Format("RunningRemaining", Strings.Time(left)),
                    Style = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
                });
            }
        }
    }

    /// <summary>Time left in the current step, extrapolated once it has run long enough to tell.</summary>
    private static long? Remaining(RunInfo run)
    {
        var elapsed = State.Now - run.StepStartedMs;
        if (run.Cancelling || run.Fraction < 0.02 || elapsed < 5_000) return null;
        return (long)(elapsed * (1 - run.Fraction) / run.Fraction);
    }

    private void OnCancel(object sender, RoutedEventArgs e) => State.Cancel();

    private void OnBack(object sender, RoutedEventArgs e) => State.GoHome();
}
