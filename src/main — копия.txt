#include "library_backend.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
using namespace std;

namespace {
string defaultConn() {
    const char* c = getenv("LIBRARY_PG_CONN");
    return (c && string(c).size()) ? c :
        "host=localhost port=5432 dbname=library user=postgres password=123";
}

void usage() {
    cout << "Usage:\n"
         << "  library_backend init\n"
         << "  library_backend list\n"
         << "  library_backend search <query>\n"
         << "  library_backend lookup <query> [limit]         # OpenLibrary\n"
         << "  library_backend lookup-google <query> [limit]  # Google Books\n"
         << "  library_backend sort <field> <asc|desc>\n"
         << "  library_backend upsert <file> [--fetch-network]\n"
         << "  library_backend remove <id>\n"
         << "  library_backend obst\n"
         << "  library_backend binary-search <query>\n";
}

bool fileExists(const string& p) { return filesystem::exists(p); }
}

int main(int argc, char* argv[]) {
    LibraryStorage storage(defaultConn());
    LibraryBackendService service(move(storage));
    
    if (!service.initialize()) {
        cerr << "error=init_failed\n";
        cerr << "info=database_connection_error_check_credentials\n";
        return 1;
    }
    
    if (argc < 2) { usage(); return 1; }
    
    string cmd = argv[1];
    
    if (cmd == "init") { cout << "status=ok\n"; return 0; }
    
    if (cmd == "list") { cout << serializeBookList(service.allBooks()); return 0; }
    
    if (cmd == "search") {
        if (argc < 3) { cerr << "error=missing_query\n"; return 1; }
        cout << serializeBookList(service.searchBooks(argv[2]));
        return 0;
    }
    
    if (cmd == "lookup") {
        if (argc < 3) { cerr << "error=missing_query\n"; return 1; }
        int lim = (argc >= 4) ? max(1, stoi(argv[3])) : 15;
        cout << serializeOpenLibraryCandidates(service.lookupOpenLibrary(argv[2], lim));
        return 0;
    }
    
    if (cmd == "lookup-google") {
        if (argc < 3) { cerr << "error=missing_query\n"; return 1; }
        int lim = (argc >= 4) ? max(1, stoi(argv[3])) : 15;
        cout << serializeOpenLibraryCandidates(service.lookupGoogleBooks(argv[2], lim));
        return 0;
    }
    
    if (cmd == "sort") {
        if (argc < 4) { cerr << "error=missing_args\n"; return 1; }
        SortField f = LibraryBackendService::parseSortField(argv[2]);
        bool asc = string(argv[3]) != "desc";
        cout << serializeBookList(service.sortedBooks(f, asc));
        return 0;
    }
    
    if (cmd == "upsert") {
        if (argc < 3 || !fileExists(argv[2])) {
            cerr << "error=missing_file\n";
            cerr << "info=file_not_found_or_path_invalid\n";
            return 1;
        }
        auto book = parseBookFile(argv[2]);
        if (!book) {
            cerr << "error=invalid_file\n";
            cerr << "info=file_format_invalid_missing_required_fields\n";
            return 1;
        }
        bool fetch = false;
        for (int i = 3; i < argc; ++i)
            if (string(argv[i]) == "--fetch-network")
                fetch = true;
        
        if (fetch) {
            cout << "status=fetching_metadata\n";
            cerr << "info=connecting_to_external_apis\n";
        }
        
        Book b = *book;
        if (!service.addOrUpdateBook(b, fetch)) {
            cerr << "error=upsert_failed\n";
            cerr << "info=database_error_or_network_timeout_check_logs\n";
            return 1;
        }
        cout << "status=ok\n" << serializeBookList({b});
        return 0;
    }
    
    if (cmd == "remove") {
        if (argc < 3) { cerr << "error=missing_id\n"; return 1; }
        if (!service.removeBookById(stoi(argv[2]))) {
            cerr << "error=remove_failed\n";
            cerr << "info=book_not_found_or_database_error\n";
            return 1;
        }
        cout << "status=ok\n";
        return 0;
    }
    
    if (cmd == "obst") {
        for (const auto& n : service.buildOptimalSearchTreeByIsbn()) {
            cout << "key=" << n.isbn << ",book_id=" << n.bookId
                 << ",left=" << n.left << ",right=" << n.right << "\n";
        }
        return 0;
    }
    
    if (cmd == "binary-search") {
        if (argc < 3) { cerr << "error=missing_query\n"; return 1; }
        auto books = service.sortedBooks(SortField::Title, true);
        int pos = service.binarySearchByTitle(books, argv[2]);
        if (pos >= 0) {
            cout << "Found at index " << pos << " (of " << books.size() << " books)\n";
            cout << serializeBookList({books[pos]});
        } else {
            cout << "Not found\n";
        }
        return 0;
    }
    
    usage(); return 1;
}