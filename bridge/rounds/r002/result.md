# Round 002 — Результат от TESTER

## Статус: CRASH (Огромный прогресс: все 94 метода GameWindow и Gadget сработали!)

### До чего дошло:
Системный фикс null-safe `GameWindow` и `Gadget` в коммите `d754dd9` полностью сработал! Все вызовы `moneyWin->winHide` и другие обращения к отсутствующим окнам интерфейса прошли **на 100% чисто** без вылетов.
Движок дошёл до обновления командной панели `TheControlBar->update()` внутри `InGameUI::update()`.

Вылет произошёл в `ControlBar::update()` на строке 1456 (`m_buildToolTipLayout->isHidden()`), так как `m_buildToolTipLayout` является объектом `WindowLayout*` (а не `GameWindow*`) и оказался `nullptr`.

### Backtrace:
```text
#00 pc 0000000000b96340  libmain.so (ControlBar::update()+332)
#01 pc 0000000000c30d90  libmain.so (InGameUI::update()+1908)
#02 pc 0000000000b90b20  libmain.so (GameClient::update()+1320)
#03 pc 0000000000b13510  libmain.so (GameEngine::update()+92)
#04 pc 0000000000b13724  libmain.so (GameEngine::execute()+88)
#05 pc 0000000000ee7094  libmain.so (SDL3GameEngine::execute()+52)
#06 pc 0000000000b0f8dc  libmain.so (GameMain()+212)
#07 pc 0000000000b0b9b8  libmain.so (SDL_main+2396)
```

### Точное место и фикс:
Файл: `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/ControlBar/ControlBar.cpp` (строка 1456) и `Generals/Code/GameEngine/Source/GameClient/GUI/ControlBar/ControlBar.cpp` (строка 1440):
```cpp
// БЫЛО:
if( !m_buildToolTipLayout->isHidden())

// НАДО:
if( m_buildToolTipLayout && !m_buildToolTipLayout->isHidden())
```
Все остальные места в этом файле (строки 1041, 1292, 1343, 1462) уже проверяют `if (m_buildToolTipLayout)`, только в строке 1456 проверка отсутствовала.

### Скриншот:
Сохранён в `screen.png`.
