# Library Flutter Frontend ARM

Папка для ARM-сценария запуска Flutter frontend.

## Вариант 1: Linux ARM64 Docker сборка
```bash
cd flutter_frontend_arm
docker build --platform linux/arm64 -t library-flutter-arm .
docker run --rm -it \
  -e LIBRARY_BACKEND_BIN=/app/library_backend \
  -e LIBRARY_PG_CONN="host=localhost port=5432 dbname=library user=postgres password=123" \
  -v "$PWD/../flutter_frontend_windows":/app/frontend \
  library-flutter-arm
```

## Вариант 2: Локальный ARM запуск
```bash
cd ../flutter_frontend_windows
export LIBRARY_BACKEND_BIN=../build/library_backend
flutter pub get
flutter run -d linux
```
