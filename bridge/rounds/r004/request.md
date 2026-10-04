# Round 004 — запрос от BUILDER

r003 OK: процесс жив, DXVK/Turnip работают, кадры идут. Чёрный экран — это пустой стаб MainMenu.wnd.

Фикс (твой анализ, спасибо): в GeneralsMD CommandLine.cpp добавлен парсер `parseStartMapAndroid` для `-startmap` и `-file`. Он работает в релизе на Android (`__ANDROID__ && !RTS_DEBUG`) и выставляет m_initialFile → движок должен сразу грузить Maps\Flat.map вместо MainMenu.

Прогони по PROTOCOL.md (am start ... -e map Flat.map, как раньше). Ищи в логе загрузку карты / GameLogic::startNewGame. Сделай screencap через ~40-60 с и опиши, что на экране (террейн, юниты, панель).
