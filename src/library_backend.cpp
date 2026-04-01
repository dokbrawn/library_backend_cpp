#include "library_backend.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <codecvt>
#include <libpq-fe.h>
using namespace std;
namespace fs = filesystem;

static const char* LOG_FILE_NAME = "library.log";
static const char* IMAGES_DIR_NAME = "images";

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================
static string nowTimestamp() {
    const auto now = chrono::system_clock::now();
    const time_t t = chrono::system_clock::to_time_t(now);
    tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    ostringstream out;
    out << put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

static void logMessage(const string& level, const string& text) {
    ofstream log(LOG_FILE_NAME, ios::app);
    if (!log.is_open()) return;
    log << nowTimestamp() << " [" << level << "] " << text << endl;
}

static string getEnvOrDefault(const char* name, const string& fallback) {
    const char* value = getenv(name);
    if (value == nullptr || string(value).empty()) return fallback;
    return string(value);
}

static string urlEncode(const string& value) {
    static const char* HEX = "0123456789ABCDEF";
    string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(HEX[c >> 4]);
            out.push_back(HEX[c & 0x0F]);
        }
    }
    return out;
}

static string readCommandOutput(const string& command) {
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) return {};
    string output;
    char chunk[512];
    while (fgets(chunk, sizeof(chunk), pipe) != nullptr) output += chunk;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

static string withSilentStderr(string command) {
#ifdef _WIN32
    command += " 2>NUL";
#else
    command += " 2>/dev/null";
#endif
    return command;
}

static void silentPgNoticeProcessor(void* /*arg*/, const char* /*message*/) {
    // Intentionally suppress PostgreSQL NOTICE messages like
    // "relation already exists, skipping" from user-facing stderr.
}

// ==================== JSON ПАРСЕР ====================
static string jsonStringFromArray(const string& object, const string& key) {
    const string marker = "\"" + key + "\"";
    const size_t keyPos = object.find(marker);
    if (keyPos == string::npos) return {};
    const size_t open = object.find('[', keyPos);
    const size_t quote1 = object.find('"', open);
    const size_t quote2 = object.find('"', quote1 + 1);
    if (open == string::npos || quote1 == string::npos || quote2 == string::npos) return {};
    return object.substr(quote1 + 1, quote2 - quote1 - 1);
}

static string jsonStringField(const string& object, const string& key) {
    const string marker = "\"" + key + "\"";
    const size_t keyPos = object.find(marker);
    if (keyPos == string::npos) return {};
    const size_t quote1 = object.find('"', keyPos + marker.size());
    const size_t quote2 = object.find('"', quote1 + 1);
    if (quote1 == string::npos || quote2 == string::npos) return {};
    return object.substr(quote1 + 1, quote2 - quote1 - 1);
}

static string jsonFlexibleTextField(const string& object, const string& key) {
    string direct = jsonStringField(object, key);
    if (!direct.empty()) return direct;
    return jsonStringFromArray(object, key);
}

static int jsonIntField(const string& object, const string& key) {
    const string marker = "\"" + key + "\"";
    const size_t keyPos = object.find(marker);
    if (keyPos == string::npos) return 0;
    const size_t colon = object.find(':', keyPos + marker.size());
    if (colon == string::npos) return 0;
    size_t begin = colon + 1;
    while (begin < object.size() && isspace(static_cast<unsigned char>(object[begin]))) ++begin;
    size_t end = begin;
    while (end < object.size() && (isdigit(static_cast<unsigned char>(object[end])) || object[end] == '.' || object[end] == '-')) ++end;
    if (begin == end) return 0;
    return atoi(object.substr(begin, end - begin).c_str());
}

static double jsonDoubleField(const string& object, const string& key) {
    const string marker = "\"" + key + "\"";
    const size_t keyPos = object.find(marker);
    if (keyPos == string::npos) return 0.0;
    const size_t colon = object.find(':', keyPos + marker.size());
    if (colon == string::npos) return 0.0;
    size_t begin = colon + 1;
    while (begin < object.size() && isspace(static_cast<unsigned char>(object[begin]))) ++begin;
    size_t end = begin;
    while (end < object.size() && (isdigit(static_cast<unsigned char>(object[end])) || object[end] == '.' || object[end] == '-')) ++end;
    if (begin == end) return 0.0;
    return atof(object.substr(begin, end - begin).c_str());
}

static vector<string> extractDocObjects(const string& json) {
    vector<string> docs;
    const size_t docsKey = json.find("\"docs\"");
    if (docsKey == string::npos) return docs;
    const size_t arrStart = json.find('[', docsKey);
    if (arrStart == string::npos) return docs;
    int braces = 0;
    size_t objStart = string::npos;
    for (size_t i = arrStart; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '{') {
            if (braces == 0) objStart = i;
            ++braces;
        } else if (c == '}') {
            --braces;
            if (braces == 0 && objStart != string::npos) {
                docs.push_back(json.substr(objStart, i - objStart + 1));
                objStart = string::npos;
            }
        } else if (c == ']' && braces == 0) break;
    }
    return docs;
}

static string trim(const string& value) {
    size_t start = 0;
    while (start < value.size() && isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

static string normalizeLower(const string& value) {
    string out = trim(value);
    try {
        wstring_convert<codecvt_utf8<wchar_t>> conv;
        wstring ws = conv.from_bytes(out);
        for (auto& ch : ws) ch = towlower(ch);
        out = conv.to_bytes(ws);
    } catch (...) {
        transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });
    }
    return out;
}

// ==================== ИЗВЛЕЧЕНИЕ ФАМИЛИИ АВТОРА (ИСПРАВЛЕННОЕ) ====================
static string extractLastName(const string& author) {
    if (author.empty()) return "";
    
    string a = trim(author);
    
    // 1. Формат "Фамилия, Имя" или "Фамилия, И.О."
    size_t comma = a.find(',');
    if (comma != string::npos) {
        return trim(a.substr(0, comma));
    }
    
    // 2. Разделяем по пробелам на части
    vector<string> parts;
    stringstream ss(a);
    string part;
    while (ss >> part) {
        // Убираем точки после инициалов
        if (part.size() > 1 && part.back() == '.') {
            part = part.substr(0, part.size() - 1);
        }
        if (!part.empty() && part.size() > 1) {
            parts.push_back(part);
        }
    }
    
    if (parts.empty()) return a;
    
    // 3. Если 2 части - "Имя Фамилия" (русский и английский формат)
    if (parts.size() == 2) {
        return parts[1];  // Последнее слово = фамилия
    }
    
    // 4. Если 3+ части - "Имя Отчество Фамилия" или "First Middle Last"
    if (parts.size() >= 3) {
        return parts.back();  // Последнее слово = фамилия
    }
    
    return parts[0];
}

// ==================== ПОИСК ПО ВХОЖДЕНИЮ СТРОКИ ====================
static bool containsSubstring(const string& text, const string& pattern) {
    if (pattern.empty()) return true;
    if (text.empty()) return false;
    if (pattern.size() > text.size()) return false;
    string textLower = normalizeLower(text);
    string patternLower = normalizeLower(pattern);
    return textLower.find(patternLower) != string::npos;
}

static string sqlEscape(const string& s) {
    string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

static string escapeValue(const string& s) {
    string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '=') out += "\\=";
        else out += c;
    }
    return out;
}

static string unescapeValue(const string& s) {
    string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[i + 1];
            if (n == 'n') { out += '\n'; ++i; }
            else if (n == 'r') { out += '\r'; ++i; }
            else if (n == '=') { out += '='; ++i; }
            else if (n == '\\') { out += '\\'; ++i; }
            else out += s[i];
        } else out += s[i];
    }
    return out;
}

static bool fileExists(const string& path) {
    if (path.empty()) return false;
    error_code ec;
    return fs::exists(path, ec);
}

static bool deleteFileIfExists(const string& path) {
    if (path.empty()) return true;
    error_code ec;
    if (!fs::exists(path, ec)) return true;
    return fs::remove(path, ec);
}

static void ensureImagesDir() {
    error_code ec;
    if (!fs::exists(IMAGES_DIR_NAME, ec)) fs::create_directories(IMAGES_DIR_NAME, ec);
}

// ==================== РОТАЦИЯ ЛОГОВ ====================
void LibraryStorage::rotateLogFile(const string& logPath, int maxDays) const {
    if (!fileExists(logPath)) return;
    auto now = chrono::system_clock::now();
    auto cutoff = now - chrono::hours(24 * maxDays);
    ifstream in(logPath);
    if (!in) return;
    vector<string> validLines;
    string line;
    while (getline(in, line)) {
        if (line.length() >= 19) {
            tm tm = {};
            istringstream ss(line.substr(0, 19));
            ss >> get_time(&tm, "%Y-%m-%d %H:%M:%S");
            if (!ss.fail()) {
                auto lineTime = chrono::system_clock::from_time_t(mktime(&tm));
                if (lineTime >= cutoff) validLines.push_back(line);
            } else validLines.push_back(line);
        } else validLines.push_back(line);
    }
    in.close();
    if (validLines.size() < 50000) {
        ofstream out(logPath, ios::trunc);
        for (const auto& l : validLines) out << l << endl;
    }
}

// ==================== ЧТЕНИЕ КНИГИ (БЕЗ PRICE) ====================
Book LibraryStorage::readBookFromStatement(void* stmtPtr) {
    PGresult* result = static_cast<PGresult*>(stmtPtr);
    Book book;
    if (!result || PQntuples(result) <= 0) return book;
    
    auto getValue = [&](int row, const char* colName) -> string {
        const int col = PQfnumber(result, colName);
        if (col < 0 || PQgetisnull(result, row, col)) return "";
        return PQgetvalue(result, row, col);
    };
    
    book.id = atoi(getValue(0, "id").c_str());
    book.title = getValue(0, "title");
    book.author = getValue(0, "author");
    book.genre = getValue(0, "genre");
    book.subgenre = getValue(0, "subgenre");
    book.publisher = getValue(0, "publisher");
    book.year = atoi(getValue(0, "year").c_str());
    book.format = getValue(0, "format");
    if (book.format.empty()) book.format = "Электронная книга";
    book.rating = atof(getValue(0, "rating").c_str());
    // ❌ price удалено
    book.ageRating = getValue(0, "age_rating");
    book.isbn = getValue(0, "isbn");
    book.totalPrintRun = atoll(getValue(0, "total_circulation").c_str());
    book.signedToPrintDate = getValue(0, "print_sign_date");
    const string additional = getValue(0, "additional_prints");
    if (!additional.empty()) book.additionalPrintDates.push_back(additional);
    book.coverImagePath = getValue(0, "cover_image_path");
    book.licenseImagePath = getValue(0, "license_image_path");
    book.bibliographicReference = getValue(0, "bibliographic_ref");
    return book;
}

// ==================== КОНСТРУКТОРЫ / ДЕСТРУКТОР ====================
LibraryStorage::LibraryStorage(string connectionString)
    : connectionString_(move(connectionString)), db_(nullptr) {
    if (connectionString_.empty()) {
        connectionString_ = getEnvOrDefault("LIBRARY_PG_CONN",
            "host=localhost port=5432 dbname=library user=postgres password=123");
    }
}

LibraryStorage::~LibraryStorage() {
    if (db_ != nullptr) {
        PQfinish(static_cast<PGconn*>(db_));
        db_ = nullptr;
    }
}

bool LibraryStorage::open() {
    if (db_ != nullptr) return true;
    PGconn* conn = PQconnectdb(connectionString_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        logMessage("ERROR", string("DB connect failed: ") + PQerrorMessage(conn));
        PQfinish(conn);
        return false;
    }
    PQsetNoticeProcessor(conn, silentPgNoticeProcessor, nullptr);
    db_ = conn;
    ensureImagesDir();
    return true;
}

bool LibraryStorage::execute(const string& sql) const {
    if (db_ == nullptr) return false;
    PGresult* result = PQexec(static_cast<PGconn*>(db_), sql.c_str());
    const ExecStatusType status = PQresultStatus(result);
    const bool ok = status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
    if (!ok) logMessage("ERROR", "SQL failed: " + sql + " | " + PQerrorMessage(static_cast<PGconn*>(db_)));
    PQclear(result);
    return ok;
}

// ==================== МИГРАЦИИ ДАННЫХ (БЕЗ PRICE) ====================
static bool columnExists(PGconn* conn, const string& table, const string& column) {
    string sql = "SELECT 1 FROM information_schema.columns "
        "WHERE table_schema='public' AND table_name='" + sqlEscape(table) +
        "' AND column_name='" + sqlEscape(column) + "' LIMIT 1;";
    PGresult* result = PQexec(conn, sql.c_str());
    bool exists = (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0);
    PQclear(result);
    return exists;
}

bool LibraryStorage::runMigrations() {
    if (!open()) return false;
    PGconn* conn = static_cast<PGconn*>(db_);
    
    if (!execute("CREATE TABLE IF NOT EXISTS genres ("
        "id SERIAL PRIMARY KEY, name TEXT NOT NULL UNIQUE);")) return false;
    if (!execute("CREATE TABLE IF NOT EXISTS subgenres ("
        "id SERIAL PRIMARY KEY, genre_id INTEGER NOT NULL REFERENCES genres(id) ON DELETE CASCADE,"
        "name TEXT NOT NULL, UNIQUE(genre_id, name));")) return false;
    if (!execute("CREATE TABLE IF NOT EXISTS books ("
        "id SERIAL PRIMARY KEY, title TEXT NOT NULL, author TEXT NOT NULL,"
        "subgenre_id INTEGER REFERENCES subgenres(id) ON DELETE SET NULL,"
        "publisher TEXT DEFAULT '', year INTEGER DEFAULT 0, format TEXT DEFAULT 'Электронная книга',"
        "rating DOUBLE PRECISION DEFAULT 0, "
        "age_rating TEXT DEFAULT '0+', isbn TEXT UNIQUE, total_circulation BIGINT DEFAULT 0,"
        "print_sign_date TEXT DEFAULT '', additional_prints TEXT DEFAULT '[]',"
        "cover_image_path TEXT DEFAULT '', license_image_path TEXT DEFAULT '',"
        "bibliographic_ref TEXT DEFAULT '', created_at TIMESTAMP DEFAULT NOW());")) return false;
    
    const vector<pair<string, string>> migrations = {
        {"books", "publisher TEXT DEFAULT ''"},
        {"books", "format TEXT DEFAULT 'Электронная книга'"},
        {"books", "rating DOUBLE PRECISION DEFAULT 0"},
        {"books", "age_rating TEXT DEFAULT '0+'"},
        {"books", "isbn TEXT UNIQUE"},
        {"books", "total_circulation BIGINT DEFAULT 0"},
        {"books", "print_sign_date TEXT DEFAULT ''"},
        {"books", "additional_prints TEXT DEFAULT '[]'"},
        {"books", "cover_image_path TEXT DEFAULT ''"},
        {"books", "license_image_path TEXT DEFAULT ''"},
        {"books", "bibliographic_ref TEXT DEFAULT ''"},
        {"books", "created_at TIMESTAMP DEFAULT NOW()"}
    };
    
    for (const auto& migration : migrations) {
        const string table = migration.first;
        const string def = migration.second;
        const string column = trim(def.substr(0, def.find(' ')));
        if (!columnExists(conn, table, column)) {
            if (!execute("ALTER TABLE " + table + " ADD COLUMN " + def + ";")) {
                logMessage("WARNING", "Migration failed for column: " + column);
            } else {
                logMessage("INFO", "Migration applied: " + column);
            }
        }
    }
    
    execute("CREATE INDEX IF NOT EXISTS idx_books_title ON books(title);");
    execute("CREATE INDEX IF NOT EXISTS idx_books_author ON books(author);");
    execute("CREATE INDEX IF NOT EXISTS idx_books_isbn ON books(isbn);");
    execute("CREATE INDEX IF NOT EXISTS idx_books_subgenre_id ON books(subgenre_id);");
    logMessage("INFO", "Migrations completed");
    return true;
}

bool LibraryStorage::ensureSchema() { return runMigrations(); }

bool LibraryStorage::ensureGenreHierarchy(const Book& book) const {
    if (db_ == nullptr) return false;
    PGconn* conn = static_cast<PGconn*>(db_);
    const string genre = trim(book.genre);
    const string subgenre = trim(book.subgenre);
    if (genre.empty() || subgenre.empty()) return true;
    
    string genreSql = "INSERT INTO genres(name) VALUES('" + sqlEscape(genre) + "') ON CONFLICT(name) DO NOTHING;";
    PQclear(PQexec(conn, genreSql.c_str()));
    
    string subgenreSql = "INSERT INTO subgenres(genre_id, name) "
        "SELECT id, '" + sqlEscape(subgenre) + "' FROM genres WHERE name='" + sqlEscape(genre) + "' "
        "ON CONFLICT(genre_id, name) DO NOTHING;";
    PGresult* result = PQexec(conn, subgenreSql.c_str());
    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    PQclear(result);
    return ok;
}

bool LibraryStorage::isEmpty() const {
    if (db_ == nullptr) return true;
    PGresult* result = PQexec(static_cast<PGconn*>(db_), "SELECT COUNT(*) FROM books;");
    if (PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) == 0) {
        PQclear(result);
        return true;
    }
    const bool empty = atoi(PQgetvalue(result, 0, 0)) == 0;
    PQclear(result);
    return empty;
}

bool LibraryStorage::rotateBackupIfNeeded() const {
    const string stampFile = ".last_backup_stamp";
    const auto now = chrono::system_clock::now();
    bool needBackup = true;
    if (fs::exists(stampFile)) {
        ifstream in(stampFile);
        long long lastTs = 0;
        in >> lastTs;
        const auto last = chrono::system_clock::time_point(chrono::seconds(lastTs));
        const auto diff = chrono::duration_cast<chrono::hours>(now - last).count();
        needBackup = diff >= 24 * 7;
    }
    if (!needBackup) return true;
    
    fs::create_directories("backup");
    const string fileName = "backup/library_backup.sql";
    string command = "pg_dump \"" + connectionString_ + "\" > \"" + fileName + "\"";
    const int rc = system(command.c_str());
    if (rc != 0) {
        logMessage("WARNING", "Backup failed via pg_dump");
        return false;
    }
    const auto epoch = chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();
    ofstream out(stampFile, ios::trunc);
    out << epoch;
    logMessage("INFO", "Backup completed: " + fileName);
    return true;
}

// ==================== ЗАГРУЗКА ВСЕХ КНИГ (БЕЗ PRICE) ====================
static vector<Book> fetchBooksBySql(PGconn* conn, const string& sql) {
    vector<Book> books;
    PGresult* result = PQexec(conn, sql.c_str());
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        logMessage("ERROR", "Fetch failed: " + string(PQerrorMessage(conn)));
        PQclear(result);
        return books;
    }
    
    const int rows = PQntuples(result);
    auto getValue = [&](int row, const char* colName) -> string {
        const int col = PQfnumber(result, colName);
        if (col < 0 || PQgetisnull(result, row, col)) return "";
        return PQgetvalue(result, row, col);
    };
    
    for (int i = 0; i < rows; ++i) {
        Book book;
        book.id = atoi(getValue(i, "id").c_str());
        book.title = getValue(i, "title");
        book.author = getValue(i, "author");
        book.genre = getValue(i, "genre");
        book.subgenre = getValue(i, "subgenre");
        book.publisher = getValue(i, "publisher");
        book.year = atoi(getValue(i, "year").c_str());
        book.format = getValue(i, "format");
        if (book.format.empty()) book.format = "Электронная книга";
        book.rating = atof(getValue(i, "rating").c_str());
        // ❌ price удалено
        book.ageRating = getValue(i, "age_rating");
        book.isbn = getValue(i, "isbn");
        book.totalPrintRun = atoll(getValue(i, "total_circulation").c_str());
        book.signedToPrintDate = getValue(i, "print_sign_date");
        const string additional = getValue(i, "additional_prints");
        if (!additional.empty()) book.additionalPrintDates.push_back(additional);
        book.coverImagePath = getValue(i, "cover_image_path");
        book.licenseImagePath = getValue(i, "license_image_path");
        book.bibliographicReference = getValue(i, "bibliographic_ref");
        books.push_back(book);
    }
    PQclear(result);
    return books;
}

vector<Book> LibraryStorage::allBooks() const {
    if (db_ == nullptr) return {};
    return fetchBooksBySql(static_cast<PGconn*>(db_),
        "SELECT b.id, b.title, b.author, g.name AS genre, sg.name AS subgenre, "
        "b.publisher, b.year, b.format, b.rating, b.age_rating, b.isbn, "
        "b.total_circulation, b.print_sign_date, b.additional_prints, "
        "b.cover_image_path, b.license_image_path, b.bibliographic_ref "
        "FROM books b "
        "LEFT JOIN subgenres sg ON b.subgenre_id = sg.id "
        "LEFT JOIN genres g ON sg.genre_id = g.id;");
}

// ==================== СОРТИРОВКА С УЧЁТОМ ФАМИЛИИ ====================
static bool compareBooksByField(const Book& a, const Book& b, SortField field, bool ascending) {
    bool result = false;
    switch (field) {
    case SortField::Title: result = normalizeLower(a.title) <= normalizeLower(b.title); break;
    case SortField::Author: {
        // СОРТИРОВКА ПО ФАМИЛИИ
        string lastNameA = extractLastName(a.author);
        string lastNameB = extractLastName(b.author);
        result = normalizeLower(lastNameA) <= normalizeLower(lastNameB);
        break;
    }
    case SortField::Genre: result = normalizeLower(a.genre) <= normalizeLower(b.genre); break;
    case SortField::Subgenre: result = normalizeLower(a.subgenre) <= normalizeLower(b.subgenre); break;
    case SortField::Publisher: result = normalizeLower(a.publisher) <= normalizeLower(b.publisher); break;
    case SortField::Year: result = a.year <= b.year; break;
    case SortField::Format: result = normalizeLower(a.format) <= normalizeLower(b.format); break;
    case SortField::Rating: result = a.rating <= b.rating; break;
    case SortField::AgeRating: result = normalizeLower(a.ageRating) <= normalizeLower(b.ageRating); break;
    case SortField::Isbn: result = normalizeLower(a.isbn) <= normalizeLower(b.isbn); break;
    case SortField::TotalPrintRun: result = a.totalPrintRun <= b.totalPrintRun; break;
    case SortField::SignedToPrintDate: result = normalizeLower(a.signedToPrintDate) <= normalizeLower(b.signedToPrintDate); break;
    }
    return ascending ? result : !result;
}

static void quickSortBooks(vector<Book>& books, int left, int right, SortField field, bool ascending) {
    if (left >= right) return;
    int i = left, j = right;
    Book pivot = books[(left + right) / 2];
    while (i <= j) {
        while (compareBooksByField(books[i], pivot, field, ascending) &&
            !(compareBooksByField(pivot, books[i], field, ascending) &&
                compareBooksByField(books[i], pivot, field, ascending))) { ++i; if (i > right) break; }
        while (compareBooksByField(pivot, books[j], field, ascending) &&
            !(compareBooksByField(books[j], pivot, field, ascending) &&
                compareBooksByField(pivot, books[j], field, ascending))) { --j; if (j < left) break; }
        if (i <= j && i <= right && j >= left) { swap(books[i], books[j]); ++i; --j; }
    }
    if (left < j) quickSortBooks(books, left, j, field, ascending);
    if (i < right) quickSortBooks(books, i, right, field, ascending);
}

vector<Book> LibraryStorage::sortedBooks(SortField field, bool ascending) const {
    vector<Book> books = allBooks();
    if (!books.empty()) quickSortBooks(books, 0, static_cast<int>(books.size()) - 1, field, ascending);
    return books;
}

// ==================== БИНАРНЫЙ ПОИСК ====================
int LibraryBackendService::binarySearchByTitle(const vector<Book>& books, const string& query) const {
    if (books.empty() || query.empty()) return -1;
    string normalizedQuery = normalizeLower(query);
    for (size_t i = 0; i < books.size(); ++i) {
        string titleLower = normalizeLower(books[i].title);
        if (titleLower.find(normalizedQuery) != string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

vector<Book> LibraryStorage::searchBooks(const string& query) const {
    const string q = normalizeLower(query);
    if (q.empty()) return allBooks();
    vector<Book> result;
    set<int> usedIds;
    
    auto byTitle = allBooks();
    if (!byTitle.empty()) quickSortBooks(byTitle, 0, static_cast<int>(byTitle.size()) - 1, SortField::Title, true);
    
    for (const auto& book : byTitle) {
        if (containsSubstring(normalizeLower(book.title), q)) {
            if (usedIds.insert(book.id).second) result.push_back(book);
        }
    }
    
    for (const auto& book : allBooks()) {
        if (usedIds.count(book.id) == 0 && containsSubstring(normalizeLower(book.author), q)) {
            usedIds.insert(book.id);
            result.push_back(book);
        }
    }
    return result;
}

// ==================== UPSERT (БЕЗ PRICE, С ФОРМАТОМ) ====================
static int getSubgenreId(PGconn* conn, const string& genre, const string& subgenre) {
    if (genre.empty() || subgenre.empty()) return 0;
    string sql = "SELECT sg.id FROM subgenres sg JOIN genres g ON g.id = sg.genre_id "
        "WHERE g.name='" + sqlEscape(genre) + "' AND sg.name='" + sqlEscape(subgenre) + "' LIMIT 1;";
    PGresult* result = PQexec(conn, sql.c_str());
    int id = (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0)
        ? atoi(PQgetvalue(result, 0, 0)) : 0;
    PQclear(result);
    return id;
}

bool LibraryStorage::upsertBook(Book& book) {
    if (!open() || !ensureGenreHierarchy(book)) return false;
    PGconn* conn = static_cast<PGconn*>(db_);
    
    // ВСЕГДА устанавливаем формат "Электронная книга"
    if (book.format.empty()) {
        book.format = "Электронная книга";
    }
    
    const int subgenreId = getSubgenreId(conn, trim(book.genre), trim(book.subgenre));
    ostringstream sql;
    
    if (book.id > 0) {
        sql << "UPDATE books SET "
            << "title='" << sqlEscape(book.title) << "', "
            << "author='" << sqlEscape(book.author) << "', "
            << "subgenre_id=" << (subgenreId > 0 ? to_string(subgenreId) : "NULL") << ", "
            << "publisher='" << sqlEscape(book.publisher) << "', "
            << "year=" << book.year << ", "
            << "format='" << sqlEscape(book.format) << "', "
            << "rating=" << book.rating << ", "
            << "age_rating='" << sqlEscape(book.ageRating) << "', "
            << "isbn=" << (book.isbn.empty() ? "NULL" : "'" + sqlEscape(book.isbn) + "'") << ", "
            << "total_circulation=" << book.totalPrintRun << ", "
            << "print_sign_date='" << sqlEscape(book.signedToPrintDate) << "', "
            << "additional_prints='" << sqlEscape(book.additionalPrintDates.empty() ? "[]" : book.additionalPrintDates[0]) << "', "
            << "cover_image_path='" << sqlEscape(book.coverImagePath) << "', "
            << "license_image_path='" << sqlEscape(book.licenseImagePath) << "', "
            << "bibliographic_ref='" << sqlEscape(book.bibliographicReference) << "' "
            << "WHERE id=" << book.id << ";";
    } else {
        sql << "INSERT INTO books("
            << "title, author, subgenre_id, publisher, year, format, rating, age_rating, isbn, "
            << "total_circulation, print_sign_date, additional_prints, cover_image_path, license_image_path, bibliographic_ref"
            << ") VALUES ("
            << "'" << sqlEscape(book.title) << "', "
            << "'" << sqlEscape(book.author) << "', "
            << (subgenreId > 0 ? to_string(subgenreId) : "NULL") << ", "
            << "'" << sqlEscape(book.publisher) << "', "
            << book.year << ", "
            << "'" << sqlEscape(book.format) << "', "
            << book.rating << ", "
            << "'" << sqlEscape(book.ageRating) << "', "
            << (book.isbn.empty() ? "NULL" : "'" + sqlEscape(book.isbn) + "'") << ", "
            << book.totalPrintRun << ", "
            << "'" << sqlEscape(book.signedToPrintDate) << "', "
            << "'" << sqlEscape(book.additionalPrintDates.empty() ? "[]" : book.additionalPrintDates[0]) << "', "
            << "'" << sqlEscape(book.coverImagePath) << "', "
            << "'" << sqlEscape(book.licenseImagePath) << "', "
            << "'" << sqlEscape(book.bibliographicReference) << "'"
            << ") RETURNING id;";
    }
    
    PGresult* result = PQexec(conn, sql.str().c_str());
    const ExecStatusType status = PQresultStatus(result);
    bool ok = false;
    if (book.id > 0) {
        ok = status == PGRES_COMMAND_OK;
    } else {
        ok = status == PGRES_TUPLES_OK && PQntuples(result) > 0;
        if (ok) book.id = atoi(PQgetvalue(result, 0, 0));
    }
    if (!ok) logMessage("ERROR", string("Upsert failed: ") + PQerrorMessage(conn));
    else logMessage("INFO", "Book id=" + to_string(book.id) + " added/updated");
    PQclear(result);
    return ok;
}

// ==================== УДАЛЕНИЕ ====================
bool LibraryStorage::removeBookById(int id) {
    if (!open()) return false;
    PGconn* conn = static_cast<PGconn*>(db_);
    
    string checkSql = "SELECT id FROM books WHERE id=" + to_string(id) + " LIMIT 1;";
    PGresult* checkResult = PQexec(conn, checkSql.c_str());
    bool exists = (PQresultStatus(checkResult) == PGRES_TUPLES_OK && PQntuples(checkResult) > 0);
    PQclear(checkResult);
    
    if (!exists) {
        logMessage("WARNING", "Book id=" + to_string(id) + " not found for deletion");
        return false;
    }
    
    string query = "SELECT cover_image_path, license_image_path FROM books WHERE id=" + to_string(id) + " LIMIT 1;";
    PGresult* result = PQexec(conn, query.c_str());
    string coverPath, licensePath;
    if (PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) > 0) {
        if (!PQgetisnull(result, 0, 0)) coverPath = PQgetvalue(result, 0, 0);
        if (!PQgetisnull(result, 0, 1)) licensePath = PQgetvalue(result, 0, 1);
    }
    PQclear(result);
    
    string removeSql = "DELETE FROM books WHERE id=" + to_string(id) + ";";
    result = PQexec(conn, removeSql.c_str());
    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    PQclear(result);
    
    if (ok) {
        deleteFileIfExists(coverPath);
        deleteFileIfExists(licensePath);
        logMessage("INFO", "Book id=" + to_string(id) + " removed");
    } else {
        logMessage("ERROR", "Book removal failed for id=" + to_string(id));
    }
    return ok;
}

// ==================== СЕРВИС ====================
LibraryBackendService::LibraryBackendService(LibraryStorage storage) : storage_(move(storage)) {}

bool LibraryBackendService::initialize() {
    if (!storage_.open() || !storage_.ensureSchema()) return false;
    storage_.rotateLogFile(LOG_FILE_NAME, 30);
    storage_.rotateBackupIfNeeded();
    return true;
}

static string makeGostReference(const Book& book) {
    ostringstream out;
    out << book.author;
    if (!book.author.empty() && !book.title.empty()) out << " ";
    out << book.title;
    if (book.year > 0) out << ". — " << book.year;
    if (!book.publisher.empty()) out << ". — " << book.publisher;
    if (!book.isbn.empty()) out << ". — ISBN " << book.isbn;
    return out.str();
}

// ==================== АВТОПОДГРУЗКА ИЗ API ====================
bool LibraryBackendService::addOrUpdateBook(Book& book, bool fetchFromNetwork) {
    if (fetchFromNetwork && !book.title.empty()) {
        string query = book.title;
        if (!book.author.empty()) query += " " + book.author;
        
        auto candidates = lookupOpenLibrary(query, 3);
        
        if (candidates.empty() || (candidates[0].genre.empty() && book.genre.empty())) {
            auto google = lookupGoogleBooks(query, 1);
            if (!google.empty()) {
                if (candidates.empty()) candidates = google;
                else if (!google[0].genre.empty()) candidates[0].genre = google[0].genre;
                if (!google[0].subgenre.empty()) candidates[0].subgenre = google[0].subgenre;
                if (google[0].rating > 0) candidates[0].rating = google[0].rating;
            }
        }
        
        if (!candidates.empty()) {
            const auto& api = candidates[0];
            if (book.author.empty() && !api.author.empty()) book.author = api.author;
            if (book.year == 0 && api.year > 0) book.year = api.year;
            if (book.isbn.empty() && !api.isbn.empty()) book.isbn = api.isbn;
            if (book.publisher.empty() && !api.publisher.empty()) book.publisher = api.publisher;
            if (book.genre.empty() && !api.genre.empty()) book.genre = api.genre;
            if (book.subgenre.empty() && !api.subgenre.empty()) book.subgenre = api.subgenre;
            if (book.rating == 0.0 && api.rating > 0.0) book.rating = api.rating;
            
            if (book.coverImagePath.empty() && !api.coverUrl.empty()) {
                string key = book.isbn.empty() ? book.title : book.isbn;
                if (downloadCoverImage(api.coverUrl, key)) {
                    string filename = "cover_" + key;
                    for (char& c : filename) {
                        if (c == ':' || c == '/' || c == '\\' || c == '*' ||
                            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
                    }
                    book.coverImagePath = "images/" + filename.substr(0, 100) + ".jpg";
                }
            }
        }
    }
    
    // Значения по умолчанию
    if (book.rating == 0.0) book.rating = 3.0;
    if (book.format.empty()) book.format = "Электронная книга";
    if (book.bibliographicReference.empty()) {
        book.bibliographicReference = makeGostReference(book);
    }
    
    return storage_.upsertBook(book);
}

bool LibraryBackendService::removeBookById(int id) { return storage_.removeBookById(id); }
vector<Book> LibraryBackendService::searchBooks(const string& query) const { return storage_.searchBooks(query); }
vector<Book> LibraryBackendService::sortedBooks(SortField field, bool ascending) const { return storage_.sortedBooks(field, ascending); }
vector<Book> LibraryBackendService::allBooks() const { return storage_.allBooks(); }

// ==================== OBST ====================
vector<ObstNode> LibraryBackendService::buildOptimalSearchTreeByIsbn() const {
    vector<Book> books = storage_.sortedBooks(SortField::Isbn, true);
    vector<ObstNode> nodes;
    vector<pair<string, int>> isbnBooks;
    
    for (const auto& b : books) {
        if (!b.isbn.empty()) isbnBooks.emplace_back(b.isbn, b.id);
    }
    
    if (isbnBooks.empty()) return nodes;
    
    const int n = static_cast<int>(isbnBooks.size());
    vector<double> weights(n, 1.0);
    for (int i = 0; i < n; ++i) {
        for (const auto& b : books) {
            if (b.isbn == isbnBooks[i].first) {
                weights[i] = max(1.0, b.rating > 0 ? b.rating : 1.0);
                break;
            }
        }
    }
    
    function<int(int, int)> buildA1 = [&](int left, int right) -> int {
        if (left > right) return -1;
        int maxIdx = left;
        double maxWeight = weights[left];
        for (int i = left + 1; i <= right; ++i) {
            if (weights[i] > maxWeight) { maxWeight = weights[i]; maxIdx = i; }
        }
        ObstNode node;
        node.isbn = isbnBooks[maxIdx].first;
        node.bookId = isbnBooks[maxIdx].second;
        node.weight = weights[maxIdx];
        node.left = -1;
        node.right = -1;
        int currentIndex = static_cast<int>(nodes.size());
        nodes.push_back(node);
        nodes[currentIndex].left = buildA1(left, maxIdx - 1);
        nodes[currentIndex].right = buildA1(maxIdx + 1, right);
        return currentIndex;
    };
    
    buildA1(0, n - 1);
    return nodes;
}

// ==================== OPENLIBRARY LOOKUP (ИСПРАВЛЕННЫЙ РЕЙТИНГ И ЖАНР) ====================
vector<OpenLibraryCandidate> LibraryBackendService::lookupOpenLibrary(const string& query, int limit) const {
    const string normalized = trim(query);
    if (normalized.empty()) return {};
    limit = clamp(limit, 1, 50);
    
    auto now = chrono::system_clock::now();
    if (networkErrorCount_ >= CIRCUIT_BREAKER_THRESHOLD) {
        if (now < circuitBreakerUntil_) {
            logMessage("WARN", "Circuit breaker active");
            return {};
        }
        networkErrorCount_ = 0;
        circuitBreakerUntil_ = {};
    }
    
    const string url = "https://openlibrary.org/search.json?q=" + urlEncode(normalized) +
        "&limit=" + to_string(limit) +
        "&fields=title,author_name,first_publish_year,isbn,publisher,cover_i,language,subject,rating_average,first_sentence";
    
    string resp;
    bool ok = false;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        const string cmd = withSilentStderr(
            "curl -fsSL --max-time 12 -H \"User-Agent: LibraryCPP/1.0\" \"" + url + "\"");
        resp = readCommandOutput(cmd);
        if (!resp.empty() && resp.find("\"docs\"") != string::npos) { ok = true; break; }
        logMessage("WARN", "OpenLibrary attempt " + to_string(attempt+1) + " failed");
        if (attempt < MAX_RETRIES - 1) this_thread::sleep_for(chrono::milliseconds(RETRY_DELAY_MS));
    }
    
    if (!ok) {
        ++networkErrorCount_;
        if (networkErrorCount_ >= CIRCUIT_BREAKER_THRESHOLD) {
            circuitBreakerUntil_ = now + chrono::seconds(CIRCUIT_BREAKER_DURATION_SEC);
            logMessage("ERROR", "Circuit breaker activated");
        }
        return {};
    }
    
    networkErrorCount_ = 0;
    vector<OpenLibraryCandidate> results;
    const auto docs = extractDocObjects(resp);
    results.reserve(docs.size());
    
    for (const auto& doc : docs) {
        OpenLibraryCandidate candidate;
        candidate.title = jsonStringField(doc, "title");
        candidate.author = jsonStringFromArray(doc, "author_name");
        candidate.publisher = jsonStringFromArray(doc, "publisher");
        candidate.language = jsonStringFromArray(doc, "language");
        candidate.isbn = jsonStringFromArray(doc, "isbn");
        candidate.year = jsonIntField(doc, "first_publish_year");
        candidate.description = jsonFlexibleTextField(doc, "first_sentence");
        
        // ИСПРАВЛЕНО: Извлекаем рейтинг
        candidate.rating = jsonDoubleField(doc, "rating_average");
        if (candidate.rating <= 0) {
            candidate.rating = jsonDoubleField(doc, "ratings_average");
        }
        
        // ИСПРАВЛЕНО: Извлекаем жанр из subject
        candidate.genre = jsonStringFromArray(doc, "subject");
        if (!candidate.genre.empty()) {
            size_t slashPos = candidate.genre.find('/');
            if (slashPos != string::npos) {
                candidate.subgenre = trim(candidate.genre.substr(slashPos + 1));
                candidate.genre = trim(candidate.genre.substr(0, slashPos));
            }
        }
        
        const int coverId = jsonIntField(doc, "cover_i");
        if (coverId > 0) 
            candidate.coverUrl = "https://covers.openlibrary.org/b/id/" + to_string(coverId) + "-L.jpg";
        
        if (!candidate.title.empty() || !candidate.author.empty()) 
            results.push_back(move(candidate));
    }
    
    return results;
}

// ==================== GOOGLE BOOKS LOOKUP (ИСПРАВЛЕННЫЙ РЕЙТИНГ И ЖАНР) ====================
vector<OpenLibraryCandidate> LibraryBackendService::lookupGoogleBooks(const string& query, int limit) const {
    const string normalized = trim(query);
    if (normalized.empty()) return {};
    limit = clamp(limit, 1, 40);
    
    const string apiKey = getEnvOrDefault("GOOGLE_BOOKS_API_KEY", "");
    string url = "https://www.googleapis.com/books/v1/volumes?q=" + urlEncode(normalized) +
        "&maxResults=" + to_string(limit) + "&printType=books&projection=full";
    
    if (!apiKey.empty()) url += "&key=" + apiKey;
    logMessage("DEBUG", "Google Books query: " + url);
    
    string resp;
    bool ok = false;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        const string cmd = withSilentStderr(
            "curl -fsSL --max-time 12 -H \"User-Agent: LibraryCPP/1.0\" \"" + url + "\"");
        resp = readCommandOutput(cmd);
        logMessage("DEBUG", "Google Books response length: " + to_string(resp.length()));
        if (!resp.empty() && resp.find("\"items\"") != string::npos) {
            ok = true;
            break;
        }
        logMessage("WARN", "Google Books attempt " + to_string(attempt+1) + " failed");
        if (attempt < MAX_RETRIES - 1)
            this_thread::sleep_for(chrono::milliseconds(RETRY_DELAY_MS));
    }
    
    if (!ok) {
        logMessage("ERROR", "Google Books API failed - check API key and network");
        return {};
    }
    
    vector<OpenLibraryCandidate> results;
    const size_t itemsKey = resp.find("\"items\"");
    if (itemsKey == string::npos) return {};
    
    const size_t arrStart = resp.find('[', itemsKey);
    if (arrStart == string::npos) return {};
    
    vector<string> items;
    int braces = 0;
    size_t objStart = string::npos;
    for (size_t i = arrStart; i < resp.size(); ++i) {
        const char c = resp[i];
        if (c == '{') {
            if (braces == 0) objStart = i;
            ++braces;
        } else if (c == '}') {
            --braces;
            if (braces == 0 && objStart != string::npos) {
                items.push_back(resp.substr(objStart, i - objStart + 1));
                objStart = string::npos;
            }
        } else if (c == ']' && braces == 0) break;
    }
    
    for (const auto& item : items) {
        OpenLibraryCandidate candidate;
        size_t volStart = item.find("\"volumeInfo\"");
        if (volStart == string::npos) continue;
        
        size_t volBraces = 0;
        size_t volEnd = string::npos;
        for (size_t i = volStart; i < item.size(); ++i) {
            if (item[i] == '{') ++volBraces;
            else if (item[i] == '}') {
                --volBraces;
                if (volBraces == 0) { volEnd = i; break; }
            }
        }
        if (volEnd == string::npos) continue;
        
        string volInfo = item.substr(volStart, volEnd - volStart + 1);
        candidate.title = jsonStringField(volInfo, "title");
        candidate.author = jsonStringFromArray(volInfo, "authors");
        candidate.publisher = jsonStringField(volInfo, "publisher");
        candidate.isbn = jsonStringFromArray(volInfo, "isbn");
        
        string pubDate = jsonStringField(volInfo, "publishedDate");
        if (!pubDate.empty() && pubDate.length() >= 4) {
            candidate.year = atoi(pubDate.substr(0, 4).c_str());
        }
        
        // ИСПРАВЛЕНО: Извлекаем категории (жанры)
        const string categories = jsonStringFromArray(volInfo, "categories");
        if (!categories.empty()) {
            candidate.genre = categories;
            size_t slashPos = categories.find('/');
            if (slashPos != string::npos) {
                candidate.genre = trim(categories.substr(0, slashPos));
                candidate.subgenre = trim(categories.substr(slashPos + 1));
            }
        }
        
        // ИСПРАВЛЕНО: Извлекаем averageRating БЕЗ проверки ratingsCount
        candidate.rating = jsonDoubleField(volInfo, "averageRating");
        // Убрали проверку: if (ratingsCount < 5) candidate.rating = 0.0;
        
        // Извлекаем обложку
        size_t imgStart = volInfo.find("\"imageLinks\"");
        if (imgStart != string::npos) {
            candidate.coverUrl = jsonStringField(volInfo, "thumbnail");
            if (candidate.coverUrl.empty()) {
                candidate.coverUrl = jsonStringField(volInfo, "smallThumbnail");
            }
            if (!candidate.coverUrl.empty() && candidate.coverUrl.find("http://") == 0) {
                candidate.coverUrl = "https" + candidate.coverUrl.substr(4);
            }
        }
        
        if (!candidate.title.empty() || !candidate.author.empty()) {
            results.push_back(move(candidate));
        }
    }
    
    logMessage("INFO", "Google Books returned " + to_string(results.size()) + " results");
    return results;
}

// ==================== PARSE SORT FIELD (БЕЗ PRICE) ====================
SortField LibraryBackendService::parseSortField(const string& value) {
    const string v = normalize(value);
    if (v == "title") return SortField::Title;
    if (v == "author") return SortField::Author;
    if (v == "genre") return SortField::Genre;
    if (v == "subgenre") return SortField::Subgenre;
    if (v == "publisher") return SortField::Publisher;
    if (v == "year") return SortField::Year;
    if (v == "format") return SortField::Format;
    if (v == "rating") return SortField::Rating;
    if (v == "agerating") return SortField::AgeRating;
    if (v == "isbn") return SortField::Isbn;
    if (v == "totalprintrun") return SortField::TotalPrintRun;
    if (v == "signedtoprintdate") return SortField::SignedToPrintDate;
    return SortField::Title;
}

string LibraryBackendService::normalize(const string& value) {
    string out;
    for (char c : value) {
        if (!isspace(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// ==================== СЕРИАЛИЗАЦИЯ (БЕЗ PRICE) ====================
string serializeBookList(const vector<Book>& books) {
    ostringstream out;
    for (const auto& book : books) {
        out << "BEGIN_BOOK\n"
            << "id=" << book.id << "\n"
            << "title=" << escapeValue(book.title) << "\n"
            << "author=" << escapeValue(book.author) << "\n"
            << "genre=" << escapeValue(book.genre) << "\n"
            << "subgenre=" << escapeValue(book.subgenre) << "\n"
            << "publisher=" << escapeValue(book.publisher) << "\n"
            << "year=" << book.year << "\n"
            << "format=" << escapeValue(book.format) << "\n"
            << "rating=" << book.rating << "\n"
            << "age_rating=" << escapeValue(book.ageRating) << "\n"
            << "isbn=" << escapeValue(book.isbn) << "\n"
            << "total_circulation=" << book.totalPrintRun << "\n"
            << "print_sign_date=" << escapeValue(book.signedToPrintDate) << "\n";
        out << "additional_prints=[";
        for (size_t i = 0; i < book.additionalPrintDates.size(); ++i) {
            if (i) out << ",";
            out << "\"" << escapeValue(book.additionalPrintDates[i]) << "\"";
        }
        out << "]\n"
            << "cover_image_path=" << escapeValue(book.coverImagePath) << "\n"
            << "license_image_path=" << escapeValue(book.licenseImagePath) << "\n"
            << "bibliographic_ref=" << escapeValue(book.bibliographicReference) << "\n"
            << "END_BOOK\n";
    }
    return out.str();
}

string serializeOpenLibraryCandidates(const vector<OpenLibraryCandidate>& candidates) {
    ostringstream out;
    for (const auto& item : candidates) {
        out << "BEGIN_CANDIDATE\n"
            << "title=" << escapeValue(item.title) << "\n"
            << "author=" << escapeValue(item.author) << "\n"
            << "publisher=" << escapeValue(item.publisher) << "\n"
            << "isbn=" << escapeValue(item.isbn) << "\n"
            << "cover_url=" << escapeValue(item.coverUrl) << "\n"
            << "year=" << item.year << "\n"
            << "genre=" << escapeValue(item.genre) << "\n"
            << "subgenre=" << escapeValue(item.subgenre) << "\n"
            << "description=" << escapeValue(item.description) << "\n"
            << "rating=" << item.rating << "\n"
            << "END_CANDIDATE\n";
    }
    return out.str();
}

optional<Book> parseBookFile(const string& filePath) {
    ifstream in(filePath);
    if (!in.is_open()) return nullopt;
    Book book;
    bool inBook = false;
    string line;
    while (getline(in, line)) {
        if (line == "BEGIN_BOOK") { inBook = true; continue; }
        if (line == "END_BOOK") break;
        if (!inBook) continue;
        const auto pos = line.find('=');
        if (pos == string::npos) continue;
        const string key = line.substr(0, pos);
        const string value = unescapeValue(line.substr(pos + 1));
        if (key == "id") book.id = atoi(value.c_str());
        else if (key == "title") book.title = value;
        else if (key == "author") book.author = value;
        else if (key == "genre") book.genre = value;
        else if (key == "subgenre") book.subgenre = value;
        else if (key == "publisher") book.publisher = value;
        else if (key == "year") book.year = atoi(value.c_str());
        else if (key == "format") book.format = value;
        else if (key == "rating") book.rating = atof(value.c_str());
        else if (key == "age_rating") book.ageRating = value;
        else if (key == "isbn") book.isbn = value;
        else if (key == "total_circulation") book.totalPrintRun = atoll(value.c_str());
        else if (key == "print_sign_date") book.signedToPrintDate = value;
        else if (key == "additional_prints") {
            book.additionalPrintDates.clear();
            book.additionalPrintDates.push_back(value);
        }
        else if (key == "cover_image_path") book.coverImagePath = value;
        else if (key == "license_image_path") book.licenseImagePath = value;
        else if (key == "bibliographic_ref") book.bibliographicReference = value;
    }
    if (trim(book.title).empty() || trim(book.author).empty()) return nullopt;
    return book;
}

// ==================== СКАЧИВАНИЕ ОБЛОЖКИ ====================
bool downloadCoverImage(const string& coverUrl, const string& isbnOrTitle) {
    if (coverUrl.empty()) return false;
    ensureImagesDir();
    string filename = "cover_" + (isbnOrTitle.empty() ? "unknown" : isbnOrTitle);
    for (char& c : filename) {
        if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    filename = filename.substr(0, 100) + ".jpg";
    const string fullPath = string(IMAGES_DIR_NAME) + "/" + filename;
    string command = "curl -fsSL --max-time 10 -o \"" + fullPath + "\" \"" + coverUrl + "\"";
    logMessage("DEBUG", "Downloading cover: " + coverUrl + " -> " + fullPath);
    readCommandOutput(command);
    if (fileExists(fullPath)) {
        logMessage("INFO", "Cover downloaded: " + fullPath);
        return true;
    }
    logMessage("WARN", "Cover download failed");
    return false;
}

std::optional<Book> LibraryBackendService::getBookById(int id) const {
    if (id <= 0) return std::nullopt;
    auto books = allBooks();
    for (const auto& book : books) {
        if (book.id == id) return book;
    }
    return std::nullopt;
}
