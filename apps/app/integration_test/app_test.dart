// The app as built for a device or desktop, with the library and FFmpeg
// bundled by the build hook: it loads them and shows its first screen.
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sillok/app_state.dart';
import 'package:sillok/engine/sillok.dart';
import 'package:sillok/main.dart';
import 'package:sillok/ui/home.dart';
import 'package:sillok/ui/welcome.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('the app starts on the bundled library', (tester) async {
    // Loading the library also loads FFmpeg's, which it links.
    expect(Sillok.version, startsWith('sillok '));
    expect(Sillok.catalog(), isNotEmpty);
    final state = AppState();
    await state.start();
    await tester.pumpWidget(SillokApp(state: state));
    await tester.pumpAndSettle();
    // A fresh install shows the first start; one with models, the library.
    expect(find.byType(WelcomePage).evaluate().length + find.byType(HomePage).evaluate().length, 1);
  });
}
