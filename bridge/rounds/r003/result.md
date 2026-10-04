# Round 003 — Результат от TESTER

## Статус: OK (ИГРА НЕ ВЫЛЕТАЕТ! Процесс жив >40 секунд, Vulkan/DXVK запущен)

### До чего дошло:
1. Системный фикс `WindowLayout` и `ControlBar::update()` сработал на 100%!
2. Успешно инициализированы все подсистемы движка:
   - `WindowManager`
   - `Shell`
   - `InGameUI`
   - `ChallengeGenerals`
   - `TerrainVisual`
   - `Mouse`, `VideoPlayer`, `Eva`, `SnowManager`
3. Успешно поднят графический конвейер **DXVK + Turnip (Vulkan)** на Adreno 618:
   - Swapchain: `1080x2256`, 3 буфера, режим `VK_PRESENT_MODE_MAILBOX_KHR`
   - Формат цвета: `VK_FORMAT_A2B10G10R10_UNORM_PACK32`
   - Компиляция шейдеров: 8 потоков DXVK
4. Движок успешно вошёл в игровой цикл: `SDL3GameEngine::execute() - entering main loop`.
5. Идут кадры на дисплей (`DispPerfService: onframeavailable pid=29632`).
6. Сенсорные нажатия пальцем по экрану обрабатываются штатно (`dispatchPointerEvent handled=true`).

### Что на экране:
- Верхняя строка Android: `Command & Conquer Generals: Zero Hour`.
- Основное окно: чёрный экран (скриншот в `screen.png`).
- Причина: загружен пустой стаб интерфейса `Menus/MainMenu.wnd`.

### Почему загрузилось MainMenu вместо Flat.map:
В Java-активности `GeneralsXZHActivity.java` передаётся:
`-win -nologo -noshellmap -quickstart -startmap Maps\Flat.map`

Однако в `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp` (и `Generals/...`):
- Функция `parseFile` (строки 723–731) и табличные бинды `{ "-startmap", parseFile }`, `{ "-map", parseFile }` (строки 1314–1316) обёрнуты в `#if defined(RTS_DEBUG)`.
- В релизной сборке (`CMAKE_BUILD_TYPE=Release`) макрос `RTS_DEBUG` не определён, поэтому движок просто игнорирует ключ `-startmap` и аргумент `Maps\Flat.map`!
- Соответственно, `m_initialFile` остаётся пустым, и `GameEngine::execute()` переходит к загрузке `TheShell->push("Menus/MainMenu.wnd")`.

### Предложение для Round 004:
Вынести `parseFile` и бинды `{ "-startmap", parseFile }`, `{ "-map", parseFile }` из `#if defined(RTS_DEBUG)` в общий доступ (или `#if defined(RTS_DEBUG) || defined(__ANDROID__)`) в:
- `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp`
- `Generals/Code/GameEngine/Source/Common/CommandLine.cpp`

После этого `-startmap Maps\Flat.map` сработает и движок напрямую загрузит карту `Flat.map` (`/sdcard/GeneralsZH/Maps/Flat/Flat.map` уже проверена на устройстве и готова).

### Скриншот и логи:
- Скриншот: `bridge/rounds/r003/screen.png`
- Лог: `bridge/rounds/r003/logcat.txt`
