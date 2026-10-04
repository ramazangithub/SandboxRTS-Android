# Round 001 — Результат теста (TESTER)

## Статус: CRASH

### До чего дошло:
Движок успешно прошёл всю инициализацию и зашёл в игровой цикл обновления клиента (`GameEngine::execute()` -> `GameEngine::update()` -> `GameClient::update()` -> `InGameUI::update()`).
Предыдущие фиксы буферов DX8 и отключение Reset_Device сработали идеально.
Вылет произошёл в `InGameUI::update()` на вызове `moneyWin->winHide(TRUE)` / `powerWin->winHide(TRUE)` из-за не найденных окон `ControlBar.wnd:MoneyDisplay` / `PowerWindow` (разыменование null-указателя `moneyWin` в `GameWindow::winHide`).

### Backtrace:
```text
#00 pc 0000000000e231b0  libmain.so (GameWindow::winHide(bool))
#01 pc 0000000000c3015c  libmain.so (InGameUI::update()+1872)
#02 pc 0000000000b90124  libmain.so (GameClient::update()+1320)
#03 pc 0000000000b13110  libmain.so (GameEngine::update()+92)
#04 pc 0000000000b13324  libmain.so (GameEngine::execute()+88)
#05 pc 0000000000ee3764  libmain.so (SDL3GameEngine::execute()+52)
#06 pc 0000000000b0f4dc  libmain.so (GameMain()+212)
#07 pc 0000000000b0b608  libmain.so (SDL_main+2396)
```

### На экране:
Чёрный экран / мгновенное падение процесса при входе в `InGameUI::update()`. Скриншот пустой (процесс завершился с SIGSEGV).
Рекомендация: добавить проверку `if (this == nullptr) return;` в `GameWindow::winHide(bool)` и аналогичные методы `GameWindow` (или загардить `moneyWin`/`powerWin` в `InGameUI.cpp`).
