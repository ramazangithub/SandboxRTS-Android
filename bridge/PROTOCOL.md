# AI Bridge — протокол работы двух ИИ

Ветка `ai-bridge`. **Не мержить в main.** Все коммиты в этой ветке с `[skip ci]`.

## Роли
- **BUILDER** (Notion AI, облако): правит код движка в `main`, запускает сборку на GitHub, выкладывает APK в GitHub Release `bridge-rNNN`.
- **TESTER** (Antigravity, ПК пользователя): ставит APK на телефон (Realme X2, Android 11, wireless adb), запускает, снимает лог, отдаёт его сюда.

TESTER **не правит** код движка и ветку `main` — только файлы в `bridge/` в ветке `ai-bridge`. Так не будет конфликтов и двойных фиксов.

## Файлы
- `bridge/state.json` — чей сейчас ход. Единственный источник правды.
- `bridge/rounds/rNNN/request.md` — что BUILDER поменял и что проверить.
- `bridge/rounds/rNNN/result.md` — краткий итог от TESTER.
- `bridge/rounds/rNNN/logcat.txt` — лог.
- `bridge/rounds/rNNN/screen.png` — скриншот экрана (если процесс жив).

## state.json
```
{
  "round": 1,
  "turn": "tester",          // "tester" | "builder" | "paused"
  "commit": "81b9bee",
  "release_tag": "bridge-r001",
  "apk": "GeneralsZH-Android-r001.apk",
  "stubs_changed": false,    // true = заново залить GeneralsZH/Window из main на телефон
  "status": "ready",         // tester пишет: "crash" | "ok" | "blocked"
  "updated_by": "builder",
  "updated_at": "ISO time"
}
```

## Цикл TESTER (Antigravity)
Таймер: раз в 60 секунд `git fetch origin ai-bridge` и читать `bridge/state.json` из `origin/ai-bridge`.
Если `turn == "tester"` и этот `round` ещё не обработан:

1. Скачать APK из релиза:
   `gh release download <release_tag> -R ramazangithub/SandboxRTS-Android -p "*.apk" -D ./bridge_apk --clobber`
   (без gh: GitHub API `GET /repos/ramazangithub/SandboxRTS-Android/releases/tags/<tag>` → `assets[0].url` → скачать с заголовком `Accept: application/octet-stream`).
2. Если `stubs_changed == true`: взять свежий `main` и `adb push GeneralsZH/Window/. /sdcard/GeneralsZH/Window/`.
3. Установка и запуск (порт adb меняется — сначала `adb devices`):
   ```
   adb uninstall com.generalsx.generalszh
   adb install -r ./bridge_apk/<apk>
   adb shell pm grant com.generalsx.generalszh android.permission.READ_EXTERNAL_STORAGE
   adb shell pm grant com.generalsx.generalszh android.permission.WRITE_EXTERNAL_STORAGE
   adb shell appops set com.generalsx.generalszh MANAGE_EXTERNAL_STORAGE allow
   adb logcat -c
   adb shell am start -n com.generalsx.generalszh/.GeneralsXZHActivity -e map Flat.map
   ```
4. Подождать 40 секунд. Если процесс жив (`adb shell pidof com.generalsx.generalszh`) — `adb exec-out screencap -p > screen.png`.
5. Лог: `adb logcat -d -v threadtime > full.txt`. В `logcat.txt` положить:
   - последние 1500 строк с тегами `GeneralsX`, `DEBUG`, `libc`, `AndroidRuntime`, `SDL`, `vulkan`, `adreno`;
   - блок краша `*** *** ***` … `backtrace:` целиком, если был.
   Не больше ~2 МБ.
6. `result.md`: 3–10 строк — до какой функции дошло, первые 15 строк backtrace, что на экране.
7. `state.json`: `turn: "builder"`, `status: crash|ok|blocked`, `updated_by: "tester"`, время.
8. `git pull --rebase origin ai-bridge && git add bridge && git commit -m "[skip ci] tester: round N result" && git push origin ai-bridge`.

Если сломалось не в игре (adb не видит телефон, APK не скачался и т.п.) — `status: "blocked"` и объяснение в `result.md`. Движок не чинить.

## Цикл BUILDER (Notion AI)
BUILDER не работает фоном: он активен, только пока пользователь с ним в чате. Внутри одной сессии он сам опрашивает ветку раз в 1–2 минуты и может сделать несколько кругов подряд.

1. Ждёт `turn == "builder"`, читает `result.md` и `logcat.txt`.
2. Правит код в `main`, пушит, запускает `release-android.yml`.
3. Скачивает APK, создаёт релиз `bridge-rNNN` с APK.
4. Пишет `rounds/rNNN/request.md`, обновляет `state.json` (`round+1`, `turn: "tester"`), пушит с `[skip ci]`.

Один круг ≈ сборка 15–20 мин + тест 2 мин.

## Правила
- Пишет в `state.json` только тот, чей ход. Перед пушем — `git pull --rebase origin ai-bridge`.
- Никаких токенов и паролей в файлах репозитория.
- Пользователь может поставить `turn: "paused"` — тогда оба ИИ ничего не делают.
