param(
  [string]$BackendPath = "..\\build\\Release\\library_backend.exe",
  [string]$PgConn = "host=localhost port=5432 dbname=library user=postgres password=123"
)

$ErrorActionPreference = "Stop"
$env:LIBRARY_BACKEND_BIN = $BackendPath
$env:LIBRARY_PG_CONN = $PgConn

flutter pub get
flutter run -d windows
