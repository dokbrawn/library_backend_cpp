#include "library_backend.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <optional>
using namespace std;

namespace {
string trim(string s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

string readLine(const string& prompt) {
    cout << prompt;
    string s;
    getline(cin, s);
    return trim(s);
}

int readInt(const string& p, int def = 0) {
    string s = readLine(p);
    return s.empty() ? def : stoi(s);
}

double readDouble(const string& p, double def = 0.0) {
    string s = readLine(p);
    return s.empty() ? def : stod(s);
}

void printBook(const Book& b) {
    cout << "[ID " << b.id << "] " << b.title << " — " << b.author << "\n"
         << "  Жанр: " << (b.genre.empty() ? "-" : b.genre)
         << "/" << (b.subgenre.empty() ? "-" : b.subgenre) << "\n"
         << "  ISBN: " << (b.isbn.empty() ? "-" : b.isbn)
         << ", Год: " << b.year
         << ", Рейтинг: " << setprecision(1) << b.rating << "\n"
         << "  Издательство: " << (b.publisher.empty() ? "-" : b.publisher)
         << ", Формат: " << (b.format.empty() ? "Электронная книга" : b.format) << "\n"
         << "  Возрастной рейтинг: " << (b.ageRating.empty() ? "-" : b.ageRating)
         << ", Тираж: " << b.totalPrintRun << "\n"
         << "  Обложка: " << (b.coverImagePath.empty() ? "-" : b.coverImagePath) << "\n";
    if (!b.bibliographicReference.empty())
        cout << "  ГОСТ: " << b.bibliographicReference << "\n";
    cout << "----------------------------------------\n";
}

void printMenu() {
    cout << "\n=== Library Console ===\n"
         << "1. Все книги     2. Поиск          3. Сортировка     4. Добавить\n"
         << "5. Удалить       6. OpenLibrary    7. Google Books   8. OBST\n"
         << "9. Binary Search 10. Редактировать                  0. Выход\n";
}
}

int main() {
    LibraryStorage storage;
    LibraryBackendService service(std::move(storage));
    
    if (!service.initialize()) {
        cerr << "Ошибка инициализации БД.\n";
        return 1;
    }
    
    while (true) {
        printMenu();
        int act = readInt("Выбор: ", -1);
        
        switch (act) {
        case 1: {
            auto books = service.allBooks();
            cout << "\nНайдено книг: " << books.size() << "\n";
            for (const auto& b : books) printBook(b);
            break;
        }
        case 2: {
            string q = readLine("Поисковый запрос: ");
            auto res = service.searchBooks(q);
            cout << "\nРезультаты поиска: " << res.size() << "\n";
            for (const auto& b : res) printBook(b);
            break;
        }
        case 3: {
            string f = readLine("Поле сортировки (title/author/genre/year/rating/isbn/...): ");
            string d = readLine("Направление (asc/desc): ");
            bool asc = (d != "desc");
            auto res = service.sortedBooks(LibraryBackendService::parseSortField(f), asc);
            cout << "\nОтсортировано " << res.size() << " книг:\n";
            for (const auto& b : res) printBook(b);
            break;
        }
        case 4: {
            Book b;
            cout << "\n=== Добавление новой книги ===\n";
            b.title = readLine("Название: ");
            b.author = readLine("Автор: ");
            b.genre = readLine("Жанр: ");
            b.subgenre = readLine("Поджанр: ");
            b.publisher = readLine("Издательство: ");
            b.format = "Электронная книга";  // ВСЕГДА электронная книга
            b.ageRating = readLine("Возрастной рейтинг: ");
            b.isbn = readLine("ISBN: ");
            b.year = readInt("Год: ", 0);
            b.rating = readDouble("Рейтинг: ", 0.0);
            b.totalPrintRun = readInt("Тираж: ", 0);
            b.coverImagePath = readLine("Путь к обложке: ");
            b.bibliographicReference = readLine("ГОСТ / Библиографическая ссылка: ");
            string fetch = readLine("Подгрузить из API? (y/n): ");
            if (service.addOrUpdateBook(b, fetch == "y" || fetch == "Y")) {
                cout << "✓ Добавлена (ID=" << b.id << ")\n";
                printBook(b);
            } else {
                cout << "✗ Ошибка добавления\n";
            }
            break;
        }
        case 5: {
            int id = readInt("ID для удаления: ", 0);
            if (id <= 0 || !service.removeBookById(id)) {
                cout << "✗ Не удалось удалить книгу с ID=" << id << "\n";
            } else {
                cout << "✓ Книга с ID=" << id << " удалена\n";
            }
            break;
        }
        case 6: {
            string q = readLine("Поиск в OpenLibrary: ");
            auto cands = service.lookupOpenLibrary(q, 5);
            if (cands.empty()) { cout << "Ничего не найдено.\n"; break; }
            for (size_t i = 0; i < cands.size(); ++i) {
                cout << "[" << i+1 << "] " << cands[i].title << " — " << cands[i].author 
                     << " | Рейтинг: " << cands[i].rating << " | Жанр: " << cands[i].genre << "\n";
            }
            int ch = readInt("Выберите (0=отмена): ", 0);
            if (ch > 0 && static_cast<size_t>(ch) <= cands.size()) {
                Book b;
                const auto& c = cands[ch-1];
                b.title = c.title; b.author = c.author; b.year = c.year;
                b.isbn = c.isbn; b.publisher = c.publisher; b.genre = c.genre; b.subgenre = c.subgenre;
                b.rating = c.rating;
                b.format = "Электронная книга";
                if (!c.coverUrl.empty() && downloadCoverImage(c.coverUrl, c.isbn.empty()?c.title:c.isbn)) {
                    b.coverImagePath = "images/cover_" + (c.isbn.empty()?c.title:c.isbn) + ".jpg";
                }
                if (service.addOrUpdateBook(b, false))
                    cout << "✓ Добавлено из OpenLibrary (ID=" << b.id << ")\n";
            }
            break;
        }
        case 7: {
            string q = readLine("Поиск в Google Books: ");
            auto cands = service.lookupGoogleBooks(q, 5);
            if (cands.empty()) {
                cout << "Ничего не найдено.\n";
                cout << "Проверьте:\n";
                cout << "  1. API ключ: echo $GOOGLE_BOOKS_API_KEY\n";
                cout << "  2. Интернет соединение\n";
                cout << "  3. library.log для ошибок\n";
                break;
            }
            for (size_t i = 0; i < cands.size(); ++i) {
                cout << "[" << i+1 << "] " << cands[i].title << " — " << cands[i].author
                     << " | Рейтинг: " << cands[i].rating << " | Жанр: " << cands[i].genre << "\n";
            }
            int ch = readInt("Выберите (0=отмена): ", 0);
            if (ch > 0 && static_cast<size_t>(ch) <= cands.size()) {
                Book b;
                const auto& c = cands[ch-1];
                b.title = c.title; b.author = c.author; b.year = c.year;
                b.isbn = c.isbn; b.publisher = c.publisher;
                b.genre = c.genre; b.subgenre = c.subgenre; b.rating = c.rating;
                b.format = "Электронная книга";
                if (!c.coverUrl.empty() && downloadCoverImage(c.coverUrl, c.isbn.empty()?c.title:c.isbn)) {
                    b.coverImagePath = "images/cover_" + (c.isbn.empty()?c.title:c.isbn) + ".jpg";
                }
                if (service.addOrUpdateBook(b, false))
                    cout << "✓ Добавлено из Google Books (ID=" << b.id << ")\n";
            }
            break;
        }
        case 8: {
            auto nodes = service.buildOptimalSearchTreeByIsbn();
            cout << "\n=== OBST ===\nУзлов: " << nodes.size() << "\n";
            for (const auto& n : nodes) {
                cout << "key=" << n.isbn << ", book_id=" << n.bookId
                     << ", left=" << n.left << ", right=" << n.right << "\n";
            }
            break;
        }
        case 9: {
            string q = readLine("Запрос для бинарного поиска (по названию): ");
            auto books = service.sortedBooks(SortField::Title, true);
            if (books.empty()) { cout << "Библиотека пуста\n"; break; }
            int pos = service.binarySearchByTitle(books, q);
            if (pos >= 0) {
                cout << "✓ Найдено на позиции " << pos << "\n";
                printBook(books[pos]);
            } else {
                cout << "✗ Не найдено\n";
            }
            break;
        }
        case 10: {
            int id = readInt("ID книги для редактирования: ", 0);
            if (id <= 0) { cout << "✗ Неверный ID\n"; break; }
            auto optBook = service.getBookById(id);
            if (!optBook.has_value()) {
                cout << "✗ Книга с ID=" << id << " не найдена\n";
                break;
            }
            Book b = optBook.value();
            cout << "\n=== Редактирование книги ID=" << id << " ===\n"
                 << "(оставьте пустым, чтобы не менять)\n";
            string input = readLine("Название [" + b.title + "]: ");
            if (!input.empty()) b.title = input;
            input = readLine("Автор [" + b.author + "]: ");
            if (!input.empty()) b.author = input;
            input = readLine("Жанр [" + b.genre + "]: ");
            if (!input.empty()) b.genre = input;
            input = readLine("Поджанр [" + b.subgenre + "]: ");
            if (!input.empty()) b.subgenre = input;
            input = readLine("Издательство [" + b.publisher + "]: ");
            if (!input.empty()) b.publisher = input;
            b.format = "Электронная книга";  // ВСЕГДА электронная книга
            input = readLine("Возрастной рейтинг [" + b.ageRating + "]: ");
            if (!input.empty()) b.ageRating = input;
            input = readLine("ISBN [" + b.isbn + "]: ");
            if (!input.empty()) b.isbn = input;
            b.year = readInt("Год [" + to_string(b.year) + "]: ", b.year);
            b.rating = readDouble("Рейтинг [" + to_string(b.rating) + "]: ", b.rating);
            b.totalPrintRun = readInt("Тираж [" + to_string(b.totalPrintRun) + "]: ", static_cast<int>(b.totalPrintRun));
            input = readLine("Путь к обложке [" + b.coverImagePath + "]: ");
            if (!input.empty()) b.coverImagePath = input;
            input = readLine("ГОСТ [" + b.bibliographicReference + "]: ");
            if (!input.empty()) b.bibliographicReference = input;
            if (service.addOrUpdateBook(b, false)) {
                cout << "✓ Книга успешно обновлена (ID=" << b.id << ")\n";
                printBook(b);
            } else {
                cout << "✗ Ошибка обновления\n";
            }
            break;
        }
        case 0:
            cout << "Выход.\n";
            return 0;
        default:
            cout << "Неверный выбор.\n";
        }
    }
}