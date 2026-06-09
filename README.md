# xash3d-net-patch

Патч-репо для tyabus/xash3d: добавляет 100% сетевую функциональность.

## Как получить APK

1. Создай пустой репозиторий на GitHub
2. Залей все файлы из этого архива (`git push`)
3. Actions → **Build xash3d Android APK** → Run workflow
4. Скачай APK из Artifacts через ~20 минут

## Что добавлено

| Файл | Изменение |
|---|---|
| `masterlist.c` | + `NET_MasterQuery()` с ключом безопасности |
| `cl_serverlist.c` | НОВЫЙ: избранное + история |
| `common.h` | + объявление `NET_MasterQuery()` |
| `client.h` | + `internetservers_key`, `internetservers_nat` |
| `cl_main.c` | + валидация ключа мастера, GoldSrc A2S_INFO, история |
| `Android.mk` | + `cl_serverlist.c` в сборку |

## Консольные команды

```
internetservers          — обновить список серверов
favorites_add ip:port    — добавить в избранное
favorites_remove 1       — удалить
favorites_list           — показать избранное
history_list             — история подключений
history_clear            — очистить историю
```

## Что НЕ тронуто

Рендер, NanoGL, OpenGL ES, звук — всё оригинальное от 0.19.4.
