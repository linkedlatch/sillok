using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Sillok.App.Pages;

namespace Sillok.App;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
    }

    /// <summary>The app's state, shared by every page; made once the CPU can run the library.</summary>
    public static AppState State { get; private set; } = null!;

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // Nothing may load the library on a CPU it cannot run on.
        if (CpuSupport.Missing.Count > 0)
        {
            _window = Unsupported();
            _window.Activate();
            return;
        }
        State = new AppState();
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

    /// <summary>In place of the app on a CPU without the instructions the library needs.</summary>
    private static Window Unsupported() => new()
    {
        Title = Strings.Get("AppName"),
        SystemBackdrop = new MicaBackdrop(),
        Content = new StackPanel
        {
            MaxWidth = 640,
            Margin = new Thickness(32),
            Spacing = 16,
            Children =
            {
                new TextBlock { Text = Strings.Get("UnsupportedTitle"), Style = Theme.Style("TitleTextBlockStyle") },
                new TextBlock
                {
                    Text = Strings.Format("UnsupportedBody", string.Join(", ", CpuSupport.Missing)),
                    TextWrapping = TextWrapping.Wrap,
                },
            },
        },
    };
}
