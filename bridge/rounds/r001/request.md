# Round 001 — запрос от BUILDER

APK собран из коммита 81b9bee (main).

Что изменено относительно aa2e69d:
- DX8VertexBufferClass / DX8IndexBufferClass: null-safe Release() в деструкторах.
- DX8Wrapper::Pillarbox_Process_Resize: на Android больше не делает Reset_Device (был краш на первом кадре).
- Ещё 15 голых `->Release()` в dx8*.cpp защищены проверкой на null.

Известно от прошлого тестировщика: вероятно следующий краш — InGameUI::update() → moneyWin->winHide().
Просьба: просто прогнать по PROTOCOL.md и прислать полный backtrace + последние строки GeneralsX перед ним.
Заглушки GeneralsZH/Window не менялись (версия aa2e69d уже на телефоне).
