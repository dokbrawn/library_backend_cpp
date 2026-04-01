#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
using namespace std;

struct Book {
    int id = 0;
    string title;
    string author;
    string genre;
    string subgenre;
    string publisher;
    int year = 0;
    string format = "Электронная книга";  // ВСЕГДА электронная книга
    double rating = 0.0;
    // double price = 0.0;  // ❌ УДАЛЕНО - книги бесплатные
    string ageRating;
    string isbn;
    long long totalPrintRun = 0;
    string signedToPrintDate;
    vector<string> additionalPrintDates;
    string coverImagePath;
    string licenseImagePath;
    string bibliographicReference;
};

enum class SortField {
    Title, Author, Genre, Subgenre, Publisher, Year, Format,
    Rating, AgeRating, Isbn, TotalPrintRun, SignedToPrintDate
    // Price ❌ УДАЛЕНО
};

struct ObstNode {
    string isbn;
    int bookId = 0;
    int left = -1;
    int right = -1;
    double weight = 1.0;
};

struct OpenLibraryCandidate {
    string title;
    string author;
    string publisher;
    string language;
    string isbn;
    string coverUrl;
    string genre;
    string subgenre;
    int year = 0;
    double rating = 0.0;
    // double price = 0.0;  // ❌ УДАЛЕНО
};

class LibraryStorage {
public:
    explicit LibraryStorage(string connectionString = "");
    ~LibraryStorage();
    bool open();
    bool ensureSchema();
    bool runMigrations();
    bool upsertBook(Book& book);
    bool removeBookById(int id);
    vector<Book> allBooks() const;
    vector<Book> sortedBooks(SortField field, bool ascending) const;
    vector<Book> searchBooks(const string& query) const;
    bool isEmpty() const;
    bool rotateBackupIfNeeded() const;
    void rotateLogFile(const string& logPath, int maxDays = 30) const;
    static Book readBookFromStatement(void* stmtPtr);
private:
    bool execute(const string& sql) const;
    bool executeParams(const string& sql, const vector<string>& params) const;
    bool ensureGenreHierarchy(const Book& book) const;
    string connectionString_;
    void* db_;
};

class LibraryBackendService {
public:
    explicit LibraryBackendService(LibraryStorage storage = LibraryStorage());
    bool initialize();
    bool addOrUpdateBook(Book & book, bool fetchFromNetwork);
    bool removeBookById(int id);
    std::optional<Book> getBookById(int id) const;
    vector<Book> allBooks() const;
    vector<Book> sortedBooks(SortField field, bool ascending) const;
    vector<Book> searchBooks(const string & query) const;
    int binarySearchByTitle(const vector<Book> & books, const string & query) const;
    vector<ObstNode> buildOptimalSearchTreeByIsbn() const;
    vector<OpenLibraryCandidate> lookupOpenLibrary(const string & query, int limit = 15) const;
    vector<OpenLibraryCandidate> lookupGoogleBooks(const string & query, int limit = 15) const;
    static SortField parseSortField(const string & value);
    static string normalize(const string & value);
private:
    LibraryStorage storage_;
    mutable int networkErrorCount_ = 0;
    mutable chrono::system_clock::time_point circuitBreakerUntil_{};
    static constexpr int MAX_RETRIES = 3;
    static constexpr int RETRY_DELAY_MS = 1000;
    static constexpr int CIRCUIT_BREAKER_THRESHOLD = 5;
    static constexpr int CIRCUIT_BREAKER_DURATION_SEC = 60;
};

string serializeBookList(const vector<Book>& books);
string serializeOpenLibraryCandidates(const vector<OpenLibraryCandidate>& candidates);
optional<Book> parseBookFile(const string& filePath);
bool downloadCoverImage(const string& coverUrl, const string& isbnOrTitle);