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
