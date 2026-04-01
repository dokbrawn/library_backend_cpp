#include "library_backend.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr const char* kDefaultDbPath = "data/library.db";

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  library_backend init\n"
        << "  library_backend list\n"
        << "  library_backend search <query>\n"
        << "  library_backend sort <field> <asc|desc>\n"
        << "  library_backend upsert <book_file> [--fetch-network]\n"
        << "  library_backend remove <id>\n"
        << "  library_backend obst\n";
}

bool fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}
} // namespace

int main(int argc, char* argv[]) {
    LibraryStorage storage(kDefaultDbPath);
    LibraryBackendService service(std::move(storage));
    if (!service.initialize()) {
        std::cerr << "error=failed_to_initialize\n";
        return 1;
    }

    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];

    if (command == "init") {
        std::cout << "status=ok\n";
        return 0;
    }

    if (command == "list") {
        std::cout << serializeBookList(service.allBooks());
        return 0;
    }

    if (command == "search") {
        if (argc < 3) {
            std::cerr << "error=missing_query\n";
            return 1;
        }
        std::cout << serializeBookList(service.searchBooks(argv[2]));
        return 0;
    }

    if (command == "sort") {
        if (argc < 4) {
            std::cerr << "error=missing_sort_arguments\n";
            return 1;
        }
        const SortField field = LibraryBackendService::parseSortField(argv[2]);
        const std::string direction = argv[3];
        const bool ascending = direction != "desc" && direction != "DESC";
        std::cout << serializeBookList(service.sortedBooks(field, ascending));
        return 0;
    }

    if (command == "upsert") {
        if (argc < 3 || !fileExists(argv[2])) {
            std::cerr << "error=missing_book_file\n";
            return 1;
        }
        const auto book = parseBookFile(argv[2]);
        if (!book.has_value()) {
            std::cerr << "error=invalid_book_file\n";
            return 1;
        }
        bool fetchNetwork = false;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--fetch-network") {
                fetchNetwork = true;
            }
        }
        Book mutableBook = *book;
        if (!service.addOrUpdateBook(mutableBook, fetchNetwork)) {
            std::cerr << "error=upsert_failed\n";
            return 1;
        }
        std::cout << "status=ok\n" << serializeBookList({mutableBook});
        return 0;
    }

    if (command == "remove") {
        if (argc < 3) {
            std::cerr << "error=missing_id\n";
            return 1;
        }
        if (!service.removeBookById(std::stoi(argv[2]))) {
            std::cerr << "error=remove_failed\n";
            return 1;
        }
        std::cout << "status=ok\n";
        return 0;
    }

    if (command == "obst") {
        const auto nodes = service.buildOptimalSearchTreeByIsbn();
        for (const auto& node : nodes) {
            std::cout << "key=" << node.key << ",book_id=" << node.bookId
                      << ",left=" << node.left << ",right=" << node.right << "\n";
        }
        return 0;
    }

    printUsage();
    return 1;
}
