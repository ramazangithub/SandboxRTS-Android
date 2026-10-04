# Round 004 — Результат от TESTER

## Статус: ПРОЦЕСС ЖИВ (>55 сек, без вылетов), ЗВУК РАБОТАЕТ, ЧЁРНЫЙ ЭКРАН

### 1. Что работает:
- **Процесс полностью стабилен**: PID 26348 работал всё время теста (>55 сек), крашей/бэктрейсов в logcat нет.
- **Звуковая подсистема OpenAL / OpenSL**:
  - `SDL3GameEngine::createAudioManager() -> OpenAL audio backend`
  - Инициализирован OpenSL ES, 44100 Hz, стерео, 256 голосов. Звуковые потоки открыты (`AudioTrack: start(2435)`).
- **Графический конвейер (DXVK 2.6.0 + Turnip Mesa 25.2.99 на Adreno 618)**:
  - `D3D9DeviceEx::ResetSwapChain`: 2340x1080, D3D9Format::A8R8G8B8, D24S8 depth-stencil.
  - Turnip Adreno 618 инициализирован, Vulkan 1.3 активен.
  - Кадры отправляются в окно (`onframeavailable`).

### 2. Что на экране:
- Верхняя системная полоса Android: `Command & Conquer Generals: Zero Hour`.
- Основное окно: **чёрный экран** (скриншот сохранён в `screen.png`).

### 3. Что обнаружено в логах (причины чёрного экрана):
1. **Отсутствуют скомпилированные шейдеры пикселей (.pso)**:
   ```text
   [ASSET_LOAD] Attempting to load shader (PIXEL): file='shaders\terrain.pso'
   [ASSET_FAIL] File not found in VFS: normalized_path='shaders/terrain.pso'
   ERROR: Could not find shader file: 'shaders\terrain.pso'
   ERROR: Could not find shader file: 'shaders\roadnoise2.pso'
   ERROR: Could not find shader file: 'shaders\fterrain.pso'
   ERROR: Could not find shader file: 'shaders\monochrome.pso'
   ```
2. **Движок всё ещё открывает `MainMenu.wnd`**:
   ```text
   DEBUG: Shell::push() called with filename='Menus/MainMenu.wnd'
   DEBUG: Shell::update() - Processing pending push: 'Menus/MainMenu.wnd'
   ```
   В `GameEngine.cpp` вызов `TheShell->push("Menus/MainMenu.wnd")` срабатывает до или параллельно с запуском карты, а сам `MainMenu.wnd` является пустой заглушкой.

### 4. Предложения для Round 005 (Билдеру):
1. **Тестовая заливка цветом (Clear Color / Red Screen)**:
   - В начале каждого кадра в `Render` делать `Clear(D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 0))` (ярко-красный), чтобы на 100% подтвердить, что буфер Turnip доходит до дисплея Android.
2. **Добавить шейдеры в APK / VFS**:
   - Положить базовые `.pso` шейдеры (`shaders/terrain.pso`, `shaders/fterrain.pso` и др.) в ассеты, чтобы террейн не отваливался на этапе отрисовки.
3. **Прямой старт карты в обход Shell**:
   - Если передан `-startmap` / `m_initialFile`, не делать `TheShell->push("Menus/MainMenu.wnd")`, либо сразу скрывать меню при переходе к карте.
