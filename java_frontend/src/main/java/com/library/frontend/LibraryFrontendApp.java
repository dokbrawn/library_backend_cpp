package com.library.frontend;

import javax.imageio.ImageIO;
import javax.swing.*;
import javax.swing.border.EmptyBorder;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class LibraryFrontendApp extends JFrame {
    private final JTextField backendPathField = new JTextField(detectDefaultBackendPath());
    private final JTextField queryField = new JTextField();
    private final JComboBox<String> sortFieldBox = new JComboBox<>(new String[]{
            "title", "author", "genre", "subgenre", "publisher", "year", "rating", "price"
    });
    private final JComboBox<String> sortDirectionBox = new JComboBox<>(new String[]{"asc", "desc"});
    private final JTextArea logArea = new JTextArea();

    private final JPanel cardsPanel = new JPanel(new GridLayout(0, 4, 12, 12));
    private final JScrollPane cardsScrollPane = new JScrollPane(cardsPanel);
    private final JLabel statsLabel = new JLabel("Книг: 0");
    private final Map<String, ImageIcon> coverCache = new HashMap<>();

    public LibraryFrontendApp() {
        super("Library Java Frontend");
        setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
        setSize(1400, 860);
        setLocationRelativeTo(null);

        cardsPanel.setBorder(new EmptyBorder(12, 12, 12, 12));
        cardsPanel.setBackground(new Color(17, 24, 39));
        cardsScrollPane.getVerticalScrollBar().setUnitIncrement(16);

        JPanel top = new JPanel(new BorderLayout(8, 8));
        top.setBorder(new EmptyBorder(8, 8, 8, 8));

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
        controls.add(Box.createHorizontalStrut(12));
        controls.add(statsLabel);

        top.add(backendPanel, BorderLayout.NORTH);
        top.add(controls, BorderLayout.SOUTH);

        logArea.setEditable(false);
        JScrollPane logScroll = new JScrollPane(logArea);
        logScroll.setPreferredSize(new Dimension(1400, 170));

        JSplitPane splitPane = new JSplitPane(JSplitPane.VERTICAL_SPLIT, cardsScrollPane, logScroll);
        splitPane.setResizeWeight(0.8);

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

        cardsScrollPane.getViewport().addComponentListener(new java.awt.event.ComponentAdapter() {
            @Override
            public void componentResized(java.awt.event.ComponentEvent e) {
                updateGridColumns();
            }
        });
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
        renderBooks(books);
        appendLog("Loaded books: " + books.size() + "\n\n");
    }

    private void renderBooks(List<BookRow> books) {
        cardsPanel.removeAll();
        if (books.isEmpty()) {
            JLabel empty = new JLabel("📭 Книги не найдены");
            empty.setForeground(Color.WHITE);
            empty.setFont(empty.getFont().deriveFont(Font.BOLD, 18f));
            cardsPanel.setLayout(new GridLayout(1, 1));
            cardsPanel.add(empty);
            statsLabel.setText("Книг: 0");
            cardsPanel.revalidate();
            cardsPanel.repaint();
            return;
        }

        updateGridColumns();
        for (BookRow book : books) {
            cardsPanel.add(createBookCard(book));
        }
        statsLabel.setText("Книг: " + books.size());
        cardsPanel.revalidate();
        cardsPanel.repaint();
    }

    private void updateGridColumns() {
        int width = Math.max(cardsScrollPane.getViewport().getWidth(), 1);
        int columns = Math.max(1, width / 270);
        cardsPanel.setLayout(new GridLayout(0, columns, 12, 12));
        cardsPanel.revalidate();
    }

    private JPanel createBookCard(BookRow b) {
        JPanel card = new JPanel();
        card.setLayout(new BorderLayout());
        card.setBackground(new Color(31, 41, 55));
        card.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createLineBorder(new Color(55, 65, 81), 1),
                new EmptyBorder(8, 8, 8, 8)
        ));

        JLabel coverLabel = new JLabel("Загрузка обложки...", SwingConstants.CENTER);
        coverLabel.setPreferredSize(new Dimension(230, 160));
        coverLabel.setOpaque(true);
        coverLabel.setBackground(new Color(17, 24, 39));
        coverLabel.setForeground(new Color(209, 213, 219));
        card.add(coverLabel, BorderLayout.NORTH);
        loadCoverAsync(b, coverLabel);

        JPanel info = new JPanel();
        info.setLayout(new BoxLayout(info, BoxLayout.Y_AXIS));
        info.setBackground(card.getBackground());

        JLabel title = new JLabel("<html><b>" + escapeHtml(nonEmpty(b.title, "Без названия")) + "</b></html>");
        title.setForeground(Color.WHITE);
        title.setAlignmentX(Component.LEFT_ALIGNMENT);
        info.add(title);

        info.add(makeText("✍ " + nonEmpty(b.author, "Неизвестен")));
        info.add(makeText("📚 " + nonEmpty(b.genre, "—") + " / " + nonEmpty(b.subgenre, "—")));
        info.add(makeText("🏢 " + nonEmpty(b.publisher, "—")));
        info.add(makeText("📅 " + nonEmpty(b.year, "—") + "    ⭐ " + nonEmpty(b.rating, "0")));
        info.add(makeText("💰 " + nonEmpty(b.price, "0") + "    👶 " + nonEmpty(b.ageRating, "0+")));

        JTextArea biblio = new JTextArea(nonEmpty(b.bibliographicReference, ""));
        biblio.setLineWrap(true);
        biblio.setWrapStyleWord(true);
        biblio.setEditable(false);
        biblio.setRows(3);
        biblio.setFont(new Font(Font.SANS_SERIF, Font.PLAIN, 11));
        biblio.setBackground(new Color(17, 24, 39));
        biblio.setForeground(new Color(203, 213, 225));
        biblio.setBorder(new EmptyBorder(4, 4, 4, 4));
        info.add(Box.createVerticalStrut(8));
        info.add(biblio);

        card.add(info, BorderLayout.CENTER);

        return card;
    }

    private JLabel makeText(String text) {
        JLabel label = new JLabel(text);
        label.setForeground(new Color(209, 213, 219));
        label.setAlignmentX(Component.LEFT_ALIGNMENT);
        return label;
    }

    private void loadCoverAsync(BookRow b, JLabel coverLabel) {
        String coverKey = nonEmpty(b.coverImagePath, b.coverUrl);
        if (!coverKey.isBlank() && coverCache.containsKey(coverKey)) {
            coverLabel.setIcon(coverCache.get(coverKey));
            coverLabel.setText("");
            return;
        }

        SwingWorker<ImageIcon, Void> worker = new SwingWorker<>() {
            @Override
            protected ImageIcon doInBackground() {
                try {
                    BufferedImage image = null;
                    if (b.coverImagePath != null && !b.coverImagePath.isBlank()) {
                        File f = new File(b.coverImagePath);
                        if (!f.isAbsolute()) {
                            f = new File(new File("."), b.coverImagePath);
                        }
                        if (f.exists()) {
                            image = ImageIO.read(f);
                        }
                    }
                    if (image == null && b.coverUrl != null && !b.coverUrl.isBlank()) {
                        image = ImageIO.read(new URL(b.coverUrl));
                    }
                    if (image == null) {
                        return null;
                    }
                    Image scaled = image.getScaledInstance(230, 160, Image.SCALE_SMOOTH);
                    return new ImageIcon(scaled);
                } catch (Exception ignored) {
                    return null;
                }
            }

            @Override
            protected void done() {
                try {
                    ImageIcon icon = get();
                    if (icon != null) {
                        coverLabel.setIcon(icon);
                        coverLabel.setText("");
                        if (!coverKey.isBlank()) {
                            coverCache.put(coverKey, icon);
                        }
                    } else {
                        coverLabel.setText("📖 Нет обложки");
                    }
                } catch (Exception ex) {
                    coverLabel.setText("📖 Нет обложки");
                }
            }
        };
        worker.execute();
    }

    private CommandResult runBackend(String... args) {
        List<String> cmd = new ArrayList<>();
        cmd.add(backendPathField.getText().trim());
        for (String arg : args) {
            cmd.add(arg);
        }

        ProcessBuilder pb = new ProcessBuilder(cmd);
        pb.directory(new File("."));

        String pgConn = System.getenv("LIBRARY_PG_CONN");
        if (pgConn != null && !pgConn.isBlank()) {
            pb.environment().put("LIBRARY_PG_CONN", pgConn);
        }

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
            String value = unescapeBackendValue(line.substring(sep + 1));
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
                case "price" -> current.price = value;
                case "age_rating" -> current.ageRating = value;
                case "cover_image_path" -> current.coverImagePath = value;
                case "cover_url" -> current.coverUrl = value;
                case "bibliographic_reference" -> current.bibliographicReference = value;
                default -> {
                }
            }
        }
        return rows;
    }

    private String unescapeBackendValue(String value) {
        return value
                .replace("\\\\", "\\u0000")
                .replace("\\n", "\n")
                .replace("\\r", "\r")
                .replace("\\=", "=")
                .replace("\\u0000", "\\");
    }

    private void appendLog(String text) {
        logArea.append(text);
        logArea.setCaretPosition(logArea.getDocument().getLength());
    }

    private static String detectDefaultBackendPath() {
        String envPath = System.getenv("LIBRARY_BACKEND_BIN");
        if (envPath != null && !envPath.isBlank()) {
            return envPath;
        }

        String[] paths = {
                "build/library_backend",
                "build/library_backend.exe",
                "build/Release/library_backend.exe",
                "../build/library_backend",
                "../build/library_backend.exe",
                "../build/Release/library_backend.exe"
        };
        for (String p : paths) {
            if (new File(p).exists()) {
                return p;
            }
        }
        return "build/library_backend";
    }

    private static String nonEmpty(String value, String fallback) {
        return value == null || value.isBlank() ? fallback : value;
    }

    private static String escapeHtml(String s) {
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
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
        String price = "";
        String ageRating = "";
        String coverImagePath = "";
        String coverUrl = "";
        String bibliographicReference = "";
    }
}
