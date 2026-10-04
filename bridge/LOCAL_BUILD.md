# LOCAL_BUILD — сборка APK на ПК пользователя (делает TESTER / Antigravity)

GitHub Actions заблокирован (биллинг). Теперь APK собирает TESTER на ПК пользователя.
BUILDER (Notion AI) по-прежнему правит код и пушит в `main`. Релизы больше не нужны.

## Разовая установка (один раз)
1. WSL2 + Ubuntu 24.04 (PowerShell от админа): `wsl --install -d Ubuntu-24.04`.
   Если WSL уже есть, этот шаг пропусти. Нужно ~60 ГБ свободного места на диске.
2. Внутри Ubuntu склонируй репо в домашнюю папку Linux (не в /mnt/c — так в 5-10 раз медленнее):
   ```
   cd ~ && git clone https://github.com/ramazangithub/SandboxRTS-Android.git
   ```
   Для приватного репо используй токен пользователя через `git credential` или `gh auth login`.
   Токен НЕ коммитить и НЕ писать в файлы bridge/.
3. Первая сборка (долго: vcpkg собирает FFmpeg/OpenAL, ~1-2 ч):
   ```
   cd ~/SandboxRTS-Android && git fetch origin ai-bridge && git show origin/ai-bridge:bridge/local_build.sh > ~/local_build.sh
   bash ~/local_build.sh 2>&1 | tee ~/build.log
   ```
   В конце должна быть строка `BUILD_OK <commit>`. APK будет в `C:\Users\lbx95\GeneralsZH-Android.apk`.

## Новый цикл раунда
- BUILDER пушит фикс в `main` и ставит в `bridge/state.json`: `turn: "pc_build"`, `commit: <sha>`.
- TESTER видит `turn == "pc_build"` и делает следующее:
  1. `bash ~/local_build.sh --fast 2>&1 | tee ~/build.log`
  2. Если сборка упала: в `bridge/rounds/rNNN/build_error.txt` положи последние ~200 строк `~/build.log`
     и строки с `error:`. Затем `status: "build_failed"`, `turn: "builder"`, commit + push в ai-bridge.
  3. Если `BUILD_OK`: установи APK на телефон и протестируй по PROTOCOL.md (uninstall/install, pm grant,
     appops, am start, logcat, screencap). Результат и логи сохрани в `bridge/rounds/rNNN/`,
     затем `turn: "builder"`.
- Опрос `bridge/state.json` в ветке `ai-bridge` — каждые 60 с, как раньше.
- `turn: "tester"` (старый режим с APK из релиза) тоже ещё поддерживается — на случай, если CI оживёт.

## Если первая сборка на ПК падает
Это тоже ход BUILDER: положи лог в `bridge/rounds/setup/build_error.txt`, затем
`status: "setup_failed"`, `turn: "builder"`, push. BUILDER поправит скрипт или код.
