import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:presentation_displays/displays_manager.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('presentation_displays_plugin');
  final calls = <MethodCall>[];

  setUp(() {
    calls.clear();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
      calls.add(call);
      switch (call.method) {
        case 'listDisplay':
          return '[{"displayId":0,"flags":0,"rotation":0,'
              '"name":"Primary"},'
              '{"displayId":1,"flags":0,"rotation":1,'
              '"name":"Customer display"}]';
        case 'showPresentation':
        case 'hidePresentation':
        case 'transferDataToPresentation':
          return true;
      }
      return null;
    });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('getDisplays decodes the native display list', () async {
    final displays = await DisplayManager().getDisplays();

    expect(displays, hasLength(2));
    expect(displays![1].displayId, 1);
    expect(displays[1].name, 'Customer display');
    expect(displays[1].rotation, 1);
  });

  test('showSecondaryDisplay sends typed arguments', () async {
    final shown = await DisplayManager().showSecondaryDisplay(
      displayId: 1,
      routerName: 'presentation/checkout',
    );

    expect(shown, isTrue);
    expect(calls.single.method, 'showPresentation');
    expect(calls.single.arguments, <String, Object>{
      'displayId': 1,
      'routerName': 'presentation/checkout',
    });
  });

  test('hideSecondaryDisplay sends the display id', () async {
    final hidden = await DisplayManager().hideSecondaryDisplay(displayId: 1);

    expect(hidden, isTrue);
    expect(calls.single.method, 'hidePresentation');
    expect(calls.single.arguments, <String, Object>{'displayId': 1});
  });

  test('getNameByIndex rejects an index equal to the list length', () async {
    final manager = DisplayManager();

    expect(await manager.getNameByIndex(1), 'Customer display');
    expect(await manager.getNameByIndex(2), isNull);
  });

  test('transferDataToPresentation preserves encodable data', () async {
    final manager = DisplayManager();
    final payload = <String, Object>{'order': 42};

    expect(await manager.transferDataToPresentation(payload), isTrue);
    expect(calls.single.method, 'transferDataToPresentation');
    expect(calls.single.arguments, payload);
  });
}
