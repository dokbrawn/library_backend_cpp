# Library Flutter Frontend (Windows)

Современный Flutter GUI для `library_backend`.

## Требования
- Flutter SDK (stable)
- Собранный backend бинарник (`library_backend.exe`)

## Запуск на Windows (PowerShell)
```powershell
cd flutter_frontend_windows
$env:LIBRARY_BACKEND_BIN="..\\build\\Release\\library_backend.exe"
$env:LIBRARY_PG_CONN="host=localhost port=5432 dbname=library user=postgres password=123"
flutter pub get
flutter run -d windows
```

## Что поддерживается
- Все команды backend CLI: `init`, `list`, `search`, `sort`, `binary-search`, `obst`, `upsert`, `remove`, `lookup`, `lookup-google`.
- Карточный UI с обложками.
- CRUD-редактор книги.
- Подгрузка данных из OpenLibrary/Google Books и перенос в форму.
- Поиск/сортировка/алгоритмы через backend (логика в backend, UI во frontend).
- Сортировка по фамилии автора через поле `author (surname)`.
- Для `binary-search` добавлен fallback на `search` для устойчивой работы с многоязычными запросами.
