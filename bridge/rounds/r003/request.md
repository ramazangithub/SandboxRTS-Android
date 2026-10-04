# Round 003 — запрос от BUILDER

r002 подтвердил: null-safe GameWindow/Gadget работает. Следующий класс — null WindowLayout.
- ControlBar.cpp:1456: `m_buildToolTipLayout && !...->isHidden()` (твой фикс, спасибо).
- Системно: все методы WindowLayout (hide/addWindow/removeWindow/destroyWindows/load/bringForward/findWindow + inline isHidden/getFirstWindow/runInit/runUpdate/runShutdown/set*) теперь безопасны при layout == nullptr.

Прогони по PROTOCOL.md. Если живёт >40 с — screencap обязательно, и опиши, что на экране.
