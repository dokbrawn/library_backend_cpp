package com.library.frontend;

import javax.swing.*;
import javax.swing.table.DefaultTableModel;
import java.awt.*;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class LibraryFrontendApp extends JFrame {
    private final JTextField backendPathField = new JTextField(detectDefaultBackendPath());
    private final JTextField queryField = new JTextField();
    private final JComboBox<String> sortFieldBox = new JComboBox<>(new String[]{
            "title", "author", "genre", "subgenre", "publisher", "year", "rating", "isbn", "total_circulation"
    });
    private final JComboBox<String> sortDirectionBox = new JComboBox<>(new String[]{"asc", "desc"});
    private final JTextArea logArea = new JTextArea();
    private final DefaultTableModel tableModel = new DefaultTableModel(
            new String[]{"ID", "Title", "Author", "Genre", "Subgenre", "Publisher", "Year", "Rating", "ISBN"}, 0);

    public LibraryFrontendApp() {
        super("Library Java Frontend");
        setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
        setSize(1200, 700);
        setLocationRelativeTo(null);

        JPanel top = new JPanel(new BorderLayout(8, 8));
        JPanel backendPanel = new JPanel(new BorderLayout(8, 8));
        backendPanel.add(new JLabel("Backend binary path:"), BorderLayout.WEST);
        backendPanel.add(backendPathField, BorderLayout.CENTER);

        JPanel controls = new JPanel(new FlowLayout(FlowLayout.LEFT));
        JButton initButton = new JButton("Init");
        JButton listButton = new JButton("List all");
        JButton searchButton = new JButton("Search");
        JButton sortButton = new JButton("Sort");

        controls.add(initButton);
        controls.add(listButton);
        controls.add(new JLabel("Query:"));
        queryField.setColumns(20);
        controls.add(queryField);
        controls.add(searchButton);
        controls.add(new JLabel("Sort:"));
        controls.add(sortFieldBox);
        controls.add(sortDirectionBox);
        controls.add(sortButton);

        top.add(backendPanel, BorderLayout.NORTH);
        top.add(controls, BorderLayout.SOUTH);

        JTable table = new JTable(tableModel);
        table.setAutoCreateRowSorter(true);
        JScrollPane tableScroll = new JScrollPane(table);

        logArea.setEditable(false);
        JScrollPane logScroll = new JScrollPane(logArea);
        logScroll.setPreferredSize(new Dimension(1200, 180));

        JSplitPane splitPane = new JSplitPane(JSplitPane.VERTICAL_SPLIT, tableScroll, logScroll);
        splitPane.setResizeWeight(0.75);

        add(top, BorderLayout.NORTH);
        add(splitPane, BorderLayout.CENTER);

        initButton.addActionListener(e -> executeAndLog("init"));
        listButton.addActionListener(e -> loadBooks("list"));
        searchButton.addActionListener(e -> {
            String q = queryField.getText().trim();
            if (q.isEmpty()) {
                appendLog("Введите запрос для поиска.\n");
                return;
            }
            loadBooks("search", q);
        });
        sortButton.addActionListener(e -> loadBooks("sort",
                String.valueOf(sortFieldBox.getSelectedItem()),
                String.valueOf(sortDirectionBox.getSelectedItem())));
    }

    private void executeAndLog(String... args) {
        CommandResult result = runBackend(args);
        appendLog("$ " + String.join(" ", args) + "\n");
        appendLog(result.stdout);
        if (!result.stderr.isBlank()) {
            appendLog("[stderr]\n" + result.stderr);
        }
        appendLog("exit=" + result.exitCode + "\n\n");
    }

    private void loadBooks(String... args) {
        CommandResult result = runBackend(args);
        appendLog("$ " + String.join(" ", args) + "\n");
        if (!result.stderr.isBlank()) {
            appendLog("[stderr]\n" + result.stderr);
        }
        appendLog("exit=" + result.exitCode + "\n\n");

        if (result.exitCode != 0) {
            return;
        }

        List<BookRow> books = parseBooks(result.stdout);
        tableModel.setRowCount(0);
        for (BookRow b : books) {
            tableModel.addRow(new Object[]{b.id, b.title, b.author, b.genre, b.subgenre, b.publisher, b.year, b.rating, b.isbn});
        }
        appendLog("Loaded books: " + books.size() + "\n\n");
    }

    private CommandResult runBackend(String... args) {
        List<String> cmd = new ArrayList<>();
        cmd.add(backendPathField.getText().trim());
        for (String arg : args) {
            cmd.add(arg);
        }

        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(new File("."));

        try {
            Process process = pb.start();
            String stdout;
            String stderr;
            try (BufferedReader out = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8));
                 BufferedReader err = new BufferedReader(new InputStreamReader(process.getErrorStream(), StandardCharsets.UTF_8))) {
                stdout = out.lines().reduce("", (a, b) -> a + b + "\n");
                stderr = err.lines().reduce("", (a, b) -> a + b + "\n");
            }
            int code = process.waitFor();
            return new CommandResult(stdout, stderr, code);
        } catch (Exception ex) {
            return new CommandResult("", "backend_run_error=" + ex.getMessage() + "\n", 1);
        }
    }

    private List<BookRow> parseBooks(String text) {
        List<BookRow> rows = new ArrayList<>();
        BookRow current = null;
        for (String line : text.split("\\R")) {
            if (line.equals("BEGIN_BOOK")) {
                current = new BookRow();
                continue;
            }
            if (line.equals("END_BOOK")) {
                if (current != null) {
                    rows.add(current);
                }
                current = null;
                continue;
            }
            if (current == null) {
                continue;
            }
            int sep = line.indexOf('=');
            if (sep <= 0) {
                continue;
            }
            String key = line.substring(0, sep);
            String value = line.substring(sep + 1);
            switch (key) {
                case "id" -> current.id = value;
                case "title" -> current.title = value;
                case "author" -> current.author = value;
                case "genre" -> current.genre = value;
                case "subgenre" -> current.subgenre = value;
                case "publisher" -> current.publisher = value;
                case "year" -> current.year = value;
                case "rating" -> current.rating = value;
                case "isbn" -> current.isbn = value;
                default -> {
                }
            }
        }
        return rows;
    }

    private void appendLog(String text) {
        logArea.append(text);
        logArea.setCaretPosition(logArea.getDocument().getLength());
    }

    private static String detectDefaultBackendPath() {
        String[] paths = {
                "build/library_backend",
                "build/library_backend.exe",
                "../build/library_backend",
                "../build/library_backend.exe"
        };
        for (String p : paths) {
            if (new File(p).exists()) {
                return p;
            }
        }
        return "build/library_backend";
    }

    public static void main(String[] args) {
        SwingUtilities.invokeLater(() -> new LibraryFrontendApp().setVisible(true));
    }

    private record CommandResult(String stdout, String stderr, int exitCode) {
    }

    private static class BookRow {
        String id = "";
        String title = "";
        String author = "";
        String genre = "";
        String subgenre = "";
        String publisher = "";
        String year = "";
        String rating = "";
        String isbn = "";
    }
}
