# Round 005 — Результат от TESTER

## Статус: ПРОЦЕСС ЖИВ (>55 сек, без вылетов), НАЙДЕНА ТОЧНАЯ ПРИЧИНА ЧЁРНОГО ЭКРАНА!

### Ответы на вопросы Builder:

1. **Экран: red / terrain visible / still black?**
   - **Всё ещё чёрный** (ни красного, ни террейна). Скриншот в `screen.png`.
   - **ПРИЧИНА НАЙДЕНА**: `TheGlobalData->m_breakTheMovie` остаётся равен `TRUE`!
     - В `GeneralsMD/Code/GameEngine/Source/GameClient/GameClient.cpp` (строки 569, 601) при старте без роликов выставляется:
       `TheWritableGlobalData->m_breakTheMovie = TRUE;`
     - В `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` (строка 2134) условие главного цикла рендера:
       ```cpp
       if ((TheGlobalData->m_breakTheMovie == FALSE) && (TheGlobalData->m_disableRender == false) && WW3D::Begin_Render(...))
       ```
     - Из-за `m_breakTheMovie == TRUE` метод `WW3D::Begin_Render(...)` **ВООБЩЕ НЕ ВЫЗЫВАЕТСЯ** в игровом цикле! Соответственно, ни красная заливка, ни сцена физически не отправляются в D3D/Vulkan!
     - *Примечание из нашего desktop патча (engine_sandbox.patch):* там в `LoadScreen::init()` или при прямом старте специально добавляли сброс:
       `TheWritableGlobalData->m_breakTheMovie = FALSE;`

2. **Control bar внизу виден?**
   - Нет, потому что весь вызов `Begin_Render` / `drawViews` / GUI пропускается из-за `m_breakTheMovie`.

3. **Ушли ли `Shell::push ... MainMenu.wnd` и ошибки шейдеров?**
   - **`Shell::push ... MainMenu.wnd` УШЁЛ НА 100%!** Твой фикс в `GameEngine.cpp` сработал отлично, меню больше не накладывается.
   - Ошибки шейдеров: при инициализации `TerrainVisual` есть попытка загрузки `shaders\Trees.vso`, но в `W3DShaderManager.cpp` (строка 1985) для `terrain.pso` уже есть встроенный fallback:
     `DEBUG_LOG(("W3DShaderManager: terrain.pso not found, using 2-stage fallback"));`

4. **Ошибки Vulkan / DXVK / present в logcat:**
   - **0 ошибок!** Графический стек работает идеально:
     - Turnip Mesa 25.2.99 на Adreno 618
     - Swapchain `2340x1080` (D3D9Format::A8R8G8B8, D24S8 depth-stencil)
     - D3D8 compatibility mode активен
     - Процесс живёт стабильно всё время без утечек и крашей.

---

### Точный фикс для Round 006:
В `GameEngine.cpp` (там, где обрабатывается прямой запуск карты `m_initialFile`), либо в `GameClient.cpp` (после пропуска интро):
```cpp
TheWritableGlobalData->m_breakTheMovie = FALSE;
```
Либо в `W3DDisplay.cpp` (строка 2134) не блокировать рендер по `m_breakTheMovie`, если `m_initialFile` задан.
Как только `m_breakTheMovie` станет `FALSE`, `WW3D::Begin_Render(...)` начнёт выполняться каждый кадр, и картинка (красная заливка / террейн) сразу пойдёт на экран!
