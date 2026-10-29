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
        try
        {
            // Clicking a notification brings this window back.
            Microsoft.Windows.AppNotifications.AppNotificationManager.Default.NotificationInvoked +=
                (_, _) => _window?.DispatcherQueue.TryEnqueue(() => _window.Activate());
            Microsoft.Windows.AppNotifications.AppNotificationManager.Default.Register();
        }
        catch (Exception e) when (e is InvalidOperationException or System.Runtime.InteropServices.COMException)
        {
            // Without notifications the app works the same.
        }
        _window = new MainWindow();
        State.Attach(_window);
        _window.Activate();
    }
}
