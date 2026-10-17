using Microsoft.UI.Xaml;

namespace Sillok.Windows;

public partial class App : Application
{
    private MainWindow? window;

    public App()
    {
        InitializeComponent();
    }

    public static MainWindow? MainWindow => (Current as App)?.window;

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        AppModel.Shared.Start();
        window = new MainWindow();
        window.Activate();
    }
}
