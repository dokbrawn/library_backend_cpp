#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Book {
    int id = 0;
    std::string title;
    std::string author;
    std::string genre;
    std::string subgenre;
    std::string publisher;
    int year = 0;
    std::string format;
    double rating = 0.0;
    double price = 0.0;
    std::string ageRating;
    std::string isbn;
    std::int64_t totalPrintRun = 0;
    std::string signedToPrintDate;
    std::vector<std::string> additionalPrintDates;
    std::string coverImagePath;
    std::string licenseImagePath;
    std::string bibliographicReference;
    std::string coverUrl;
    double searchFrequency = 1.0;
};

enum class SortField {
    Title,
    Author,
    Genre,
    Subgenre,
    Publisher,
    Year,
    Format,
    Rating,
    Price,
    AgeRating,
    Isbn,
    TotalPrintRun,
    SignedToPrintDate
};

struct ObstNode {
    std::string key;
    int bookId = -1;
    int left = -1;
    int right = -1;
};

class NetworkMetadataClient {
public:
    std::optional<Book> fetchByQuery(const Book& draft) const;
};

class LibraryStorage {
public:
    explicit LibraryStorage(std::string filePath);
    ~LibraryStorage();

    bool open();
    bool ensureSchema();

    std::vector<Book> allBooks() const;
    std::vector<Book> searchBooks(const std::string& query) const;
    std::vector<Book> sortedBooks(SortField field, bool ascending) const;

    bool upsertBook(Book& book);
    bool removeBookById(int id);
    bool isEmpty() const;

private:
    std::string filePath_;
    void* db_ = nullptr;

    bool execute(const std::string& sql) const;
    bool ensureGenreHierarchy(const Book& book) const;
    static Book readBookFromStatement(void* statement);
};

class LibraryBackendService {
public:
    explicit LibraryBackendService(LibraryStorage storage);

    bool initialize();
    bool addOrUpdateBook(Book& book, bool fetchFromNetwork);
    bool removeBookById(int id);

    std::vector<Book> searchBooks(const std::string& query) const;
    std::vector<Book> sortedBooks(SortField field, bool ascending) const;
    std::vector<Book> allBooks() const;
    std::vector<ObstNode> buildOptimalSearchTreeByIsbn() const;

    static SortField parseSortField(const std::string& value);

private:
    LibraryStorage storage_;
    NetworkMetadataClient networkClient_;

    static std::string normalize(const std::string& value);
};

std::string serializeBookList(const std::vector<Book>& books);
std::optional<Book> parseBookFile(const std::string& filePath);
