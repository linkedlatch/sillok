using Microsoft.UI.Xaml;

namespace Sillok.App;

public partial class App : Application
{
    private MainWindow? _window;

    public App()
    {
        InitializeComponent();
    }

    /// <summary>The app's state, shared by every page.</summary>
    public static AppState State { get; } = new();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        State.Attach(_window);
        _window.Activate();
    }
}
