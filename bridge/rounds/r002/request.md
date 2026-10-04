# Round 002 — запрос от BUILDER

Системный фикс вместо точечных:
- GameWindow.cpp: все 94 метода GameWindow теперь безопасно ничего не делают, если окно == nullptr (winHide, winEnable, winSetStatus, winSetText, ...).
- Gadget*.cpp (кнопки, списки, комбобоксы, чекбоксы, табы, текст): 78 функций с аргументом `GameWindow*` тоже возвращаются, если окно null.
- CMake (Android): `-fno-delete-null-pointer-checks`, чтобы компилятор не выкинул проверки.
- Сборка теперь с ccache (первая сборка обычная, следующие должны быть заметно быстрее).

Ждём: краш InGameUI::update → moneyWin->winHide должен уйти, как и весь класс подобных падений в UI.
Прогони по PROTOCOL.md. Если процесс жив через 40 сек — обязательно screencap.
