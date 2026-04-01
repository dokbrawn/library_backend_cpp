# Library backend + frontend

В папке `library_backend_cpp` теперь связаны:
- **C++ backend** — PostgreSQL/CLI-сервис для хранения, сортировки, поиска и CRUD-операций;
- **Rust GUI frontend** — новый основной графический интерфейс;
- **Python/tkinter frontend** — legacy-версия интерфейса (временная обратная совместимость).

## Что сделано

### Backend (C++)
- хранит данные в PostgreSQL (через `LIBRARY_PG_CONN`);
- создает структуру `genres -> subgenres -> books` в PostgreSQL;
- при первом `init` автоматически заполняет пустую БД демонстрационным набором книг;
- поддерживает офлайн-режим и сетевые флаги через переменные окружения (`OFFLINE_MODE`, `LIBRARY_ENABLE_NET`);
- поддерживает команды:
  - `init`
  - `list`
  - `search <query>`
  - `lookup <query> [limit]` (поиск кандидатов в OpenLibrary, используется Rust GUI)
  - `sort <field> <asc|desc>`
  - `upsert <book_file> [--fetch-network]`
  - `remove <id>`
  - `obst`
- сохраняет поля из требований:
  - название, автор, жанр, поджанр;
  - цена, рейтинг, возрастной рейтинг;
  - ISBN;
  - издательство, год, формат;
  - общий тираж;
  - дата подписи в печать;
  - даты дополнительных тиражей;
  - путь к фото обложки;
  - путь к фото лицензии;
  - библиографическая ссылка.
- строит optimal BST по ISBN.

### Frontend (Python)
- больше не хранит собственную JSON-базу как основной источник данных;
- при запуске автоматически использует C++ backend;
- если backend еще не собран, пытается собрать его через CMake;
- загрузка/удаление/редактирование книг идет через C++ backend;
- сортировка в основном интерфейсе берется из backend;
- поиск в UI сохраняет требование: сначала по названию, потом по автору;
- в форме добавления книги можно:
  - либо вручную заполнить все поля;
  - либо ввести слово/фразу, получить несколько результатов из Open Library и выбрать нужную книгу для автозаполнения;
  - при выборе результата из Open Library приложение старается сразу скачать и показать обложку;
- при первом запуске GUI вызывает backend `init`, и демонстрационный набор появляется на стороне backend, а не через отдельную JSON-инициализацию во frontend.

## Что уже считается БД и СУБД в проекте
- **СУБД**: используется **PostgreSQL**.
- **БД**: PostgreSQL-база, задаваемая строкой подключения `LIBRARY_PG_CONN`.
- **library.db**: локальный marker-файл в `LIBRARY_DATA_PATH` (или в рабочей папке), чтобы соблюсти файловую структуру проекта рядом с `images/` и `library.log`.
- **Обложки и файлы**: картинки для книг сохраняются в локальную папку `images/`, а путь к ним хранится в БД.

## Почему такие решения
- **PostgreSQL**: соответствует целевому стеку ТЗ и дает строгую SQL-схему/ограничения.
- **C++ backend как CLI**: проще надежно связать с Python GUI без дополнительного web-сервера и сторонних фреймворков.
- **tkinter frontend**: уже был в проекте, поэтому дешевле и быстрее довести готовый GUI, чем переписывать интерфейс заново.
- **Локальные файлы изображений**: это соответствует требованию хранить картинки для каждой записи и не зависит от сети после загрузки.
- **Сетевое обогащение записи**: backend умеет пытаться подтягивать данные из Open Library при сохранении книги.
- **Легкий UI без тяжелых эффектов**: дизайн можно улучшать за счет цветов, карточек, типографики и информативности, не добавляя тяжелые web-движки — это важно для плавной работы даже на x86.

## Сборка backend

```bash
cd library_backend_cpp
cmake -S . -B build
cmake --build build
```

После сборки создаются 2 исполняемых файла:
- `library_backend` — CLI c командами (`list/search/sort/upsert/remove/...`);
- `library_backend_console` — интерактивное меню для работы с backend в консоли (в т.ч. PowerShell).

### Windows
- для multi-config генераторов Visual Studio backend теперь кладется прямо в `build/`, а не только в `build/Debug`;
- после сборки CMake пытается безопасно скопировать рядом с `library_backend.exe` нужные runtime DLL, включая зависимости PostgreSQL/libpq, если они есть в списке зависимостей текущей сборки.

## Переменные окружения backend

- `LIBRARY_PG_CONN` — строка подключения PostgreSQL (`host=... port=... dbname=... user=... password=...`).
- `LIBRARY_DATA_PATH` — путь к папке с локальными файлами (`library.db`, `library.db.backup`, `library.log`, `images/`).
- `LIBRARY_ENABLE_NET` — `false` полностью отключает API-запросы.
- `OFFLINE_MODE` — `true` принудительно включает офлайн-режим (API не вызывается).

## Запуск Rust GUI (новый основной UI)

```bash
cd rust_gui
cargo run
```

Rust GUI теперь напрямую управляет C++ backend:
- выполняет `init` (создание схемы PostgreSQL);
- показывает список (`list`);
- ищет (`search`);
- запрашивает кандидатов из OpenLibrary через backend (`lookup`);
- добавляет книги (`upsert`);
- удаляет книги (`remove`).

Перед запуском рекомендуется задать строку подключения:

```bash
export LIBRARY_PG_CONN="host=localhost port=5432 dbname=library user=postgres password=123"
```

или в PowerShell:

```powershell
$env:LIBRARY_PG_CONN = "host=localhost port=5432 dbname=library user=postgres password=123"
```

## Запуск Python GUI (legacy)

```bash
cd library_backend_cpp
python library_app.py
```

## Примеры backend-команд

```bash
./build/library_backend init
./build/library_backend list
./build/library_backend sort price desc
./build/library_backend search "Clean Code"
```

## Интерактивный backend-режим (консоль/PowerShell)

```bash
./build/library_backend_console
```

### Проверка в Windows PowerShell

```powershell
cd library_backend_cpp
cmake -S . -B build
cmake --build build --config Release
$env:LIBRARY_PG_CONN = "host=localhost port=5432 dbname=library user=postgres password=123"
.\build\library_backend_console.exe
```

В меню `library_backend_console` можно проверить все ключевые функции backend:
- просмотр всех книг;
- поиск;
- сортировка;
- добавление/обновление книги;
- удаление по ID;
- API-подгрузка;
- вывод OBST.

В меню доступны:
- вывод всех книг;
- поиск;
- сортировка;
- добавление и обновление книги;
- удаление по `id`;
- подгрузка книги из OpenLibrary API;
- вывод OBST.

## Запуск Java frontend (новый вариант)

Добавлен отдельный Java/Swing frontend в папке `java_frontend`.

Сборка:

```bash
cd library_backend_cpp/java_frontend
mvn package
```

Запуск:

```bash
cd /workspace/library_backend_cpp
java -jar java_frontend/target/java-frontend-1.0.0.jar
```

В UI можно:
- инициализировать backend (`init`);
- просматривать список книг (`list`) в карточном виде;
- выполнять поиск (`search`);
- сортировать (`sort`);
- видеть обложки книг (из `cover_image_path` или `cover_url`) и ключевые метаданные на карточке.

Поле `Backend binary path` позволяет задать путь к бинарнику `library_backend` вручную.

## Отдельная папка для ARM запуска

Создана папка `java_frontend_arm` для запуска Java frontend в ARM64-контейнере.

1. Соберите JAR:
```bash
cd /workspace/library_backend_cpp/java_frontend
mvn package
```

2. Соберите ARM-образ:
```bash
cd /workspace/library_backend_cpp
docker build -f java_frontend_arm/Dockerfile -t library-java-frontend:arm64 .
```

3. Запустите контейнер (при необходимости передайте backend для ARM):
```bash
docker run --rm -it \
  -e LIBRARY_PG_CONN="host=localhost port=5432 dbname=library user=postgres password=123" \
  -e BACKEND_PATH="/app/backend/library_backend" \
  -v /path/to/arm/backend:/app/backend \
  library-java-frontend:arm64
```

Это изолирует ARM-окружение в отдельной папке/образе, при том что основной C++ проект можно продолжать тестировать в Windows.

## Flutter frontend (новый основной GUI)

По вашему ТЗ добавлен новый фронтенд на Flutter с акцентом на современный карточный интерфейс и полный вызов backend-команд (логика в backend, интерфейс во frontend).

### Папка для Windows
- `flutter_frontend_windows`
- содержит Flutter приложение (`lib/main.dart`) и PowerShell-скрипт запуска `run-windows.ps1`.

Запуск в PowerShell:

```powershell
cd library_backend_cpp
cmake -S . -B build
cmake --build build --config Release

cd .\flutter_frontend_windows
.\run-windows.ps1 -BackendPath "..\build\Release\library_backend.exe" -PgConn "host=localhost port=5432 dbname=library user=postgres password=123"
```

### Папка для ARM
- `flutter_frontend_arm`
- содержит отдельный `Dockerfile` и скрипт `run-arm.sh`.

Запуск ARM-сценария:

```bash
cd /workspace/library_backend_cpp
./flutter_frontend_arm/run-arm.sh
```

### Что покрыто во Flutter UI
- Полный вызов backend-команд:
  - `init`, `list`, `search`, `sort`, `binary-search`, `obst`, `upsert`, `remove`, `lookup`, `lookup-google`.
- CRUD-форма книги со всеми полями, используемыми backend сериализацией.
- Подгрузка кандидатов из OpenLibrary / Google Books и перенос в форму книги.
- Карточная витрина книг с обложками (локальный путь или URL), выделением выбранной карточки и редактированием.
- Backend-логи в UI, чтобы видеть stderr/exit-коды каждой операции.

### Важные уточнения по алгоритмам в UI
- **QuickSort**: выполняется командой backend `sort` (в UI это кнопка `QuickSort`).
- **OBST**: кнопка `OBST` теперь открывает отдельное окно со списком узлов дерева (`key/book_id/left/right`), а не только пишет в лог.
- **Binary Search**: сначала вызывается backend `binary-search`; если backend не вернул результат (часто на смешанной локали/регистре), frontend автоматически делает fallback на `search`, чтобы кириллица и mixed-language запросы находились корректно.
- **Сортировка по фамилии**: выбирайте поле `author (surname)` — backend сортирует авторов по извлечённой фамилии.
- В сетевом поиске (`lookup`) теперь дополнительно подтягивается краткое описание (`first_sentence`, если доступно), а также жанр/рейтинг.
- Таймауты `curl` смягчены (увеличен лимит ожидания), а служебные PostgreSQL NOTICE-сообщения скрыты из пользовательского stderr, чтобы лог в UI был чище.

### Телефон / ARM / перенос
- Текущий Flutter UI рассчитан на desktop-сценарий, где запускается **локальный `library_backend` бинарник**.
- Для ARM (Linux ARM64) используйте папку `flutter_frontend_arm` (Docker/run-script).
- Для телефона (Android/iOS) сам UI можно собрать, но backend-CLI как отдельный процесс там не запускается так же, как на Windows/Linux desktop; нужен отдельный мобильный backend-слой (FFI/embedded service/API).

#### Пошагово: перенос на ARM (Linux ARM64, например Raspberry Pi)
1. На ARM-устройстве установите Docker.
2. Скопируйте проект на устройство.
3. Выполните:
   ```bash
   cd library_backend_cpp
   ./flutter_frontend_arm/run-arm.sh
   ```
4. Если хотите нативный ARM запуск без Docker:
   - соберите `library_backend` под ARM (`cmake -S . -B build && cmake --build build`);
   - в `flutter_frontend_windows` выполните `flutter pub get` и `flutter run -d linux`;
   - перед запуском задайте `LIBRARY_BACKEND_BIN` на ARM-бинарник backend.

#### Пошагово: перенос на телефон (Android)
На сегодня это **не просто “скопировать папку”**, потому что текущая архитектура запускает desktop CLI-процесс backend.
Рабочий путь для телефона:
1. Оставить Flutter UI.
2. Вынести backend в мобильный API/FFI-слой (например, C++ как библиотека через FFI или отдельный локальный сервис).
3. Заменить в Flutter вызовы `Process.start(...)` на вызовы этого слоя.
4. Собрать APK:
   ```bash
   cd flutter_frontend_windows
   flutter build apk --release
   ```
5. Установить APK на телефон (`adb install build/app/outputs/flutter-apk/app-release.apk`).
