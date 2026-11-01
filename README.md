# INFINITY FPS

Мод для Bearite / Super Bear Adventure (Android, IL2CPP).

## Функции
- Настройка целевого FPS через Unity `Application.targetFrameRate`.
- Отключение VSync при включённой настройке целевого FPS.
- Отдельная подмена числа в FPS-счётчике (если совпадают ожидаемые UI-методы/поля).

## Сборка
Workflow: **Actions → Build INFINITY FPS → Run workflow**.

Для сборки требуется полный Bearite SDK в каталоге `sdk/`, включая:
- `sdk/build-mod.sh`
- заголовки API Bearite, используемые исходником мода.

После добавления SDK workflow соберёт и загрузит artifact `infinity-fps`.

**Важно:** установка целевого FPS не гарантирует такую частоту кадров: результат ограничен экраном, устройством, Unity и настройками игры. Значение 0 или меньше ставит игровое время на паузу, а не создаёт нулевую частоту кадров.
