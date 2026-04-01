import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';

void main() {
  runApp(const LibraryApp());
}

class LibraryApp extends StatelessWidget {
  const LibraryApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'LIBRARY',
      theme: ThemeData(
        brightness: Brightness.dark,
        colorSchemeSeed: const Color(0xFF5B8CFF),
        useMaterial3: true,
      ),
      home: const LibraryHomePage(),
    );
  }
}

class LibraryHomePage extends StatefulWidget {
  const LibraryHomePage({super.key});

  @override
  State<LibraryHomePage> createState() => _LibraryHomePageState();
}

class _LibraryHomePageState extends State<LibraryHomePage> {
  final backendPath = TextEditingController(text: _detectBackendPath());
  final searchCtrl = TextEditingController();
  final lookupCtrl = TextEditingController();
  final logCtrl = TextEditingController();

  final idCtrl = TextEditingController();
  final titleCtrl = TextEditingController();
  final authorCtrl = TextEditingController();
  final genreCtrl = TextEditingController();
  final subgenreCtrl = TextEditingController();
  final publisherCtrl = TextEditingController();
  final yearCtrl = TextEditingController();
  final formatCtrl = TextEditingController(text: 'Электронная книга');
  final ratingCtrl = TextEditingController();
  final ageCtrl = TextEditingController();
  final isbnCtrl = TextEditingController();
  final runCtrl = TextEditingController();
  final signDateCtrl = TextEditingController();
  final addPrintsCtrl = TextEditingController();
  final coverPathCtrl = TextEditingController();
  final licensePathCtrl = TextEditingController();
  final biblioCtrl = TextEditingController();

  final sortFields = const [
    'title', 'author', 'genre', 'subgenre', 'publisher', 'year', 'format',
    'rating', 'age_rating', 'isbn', 'total_circulation', 'print_sign_date'
  ];

  String sortField = 'title';
  String sortDirection = 'asc';
  String lookupSource = 'openlibrary';
  bool fetchNetworkOnSave = true;
  bool loading = false;
  bool editorVisible = false;

  List<BookRow> books = [];
  List<LookupCandidate> candidates = [];
  int? selectedBookId;

  @override
  void initState() {
    super.initState();
    _loadBooks(['list']);
  }

  @override
  void dispose() {
    for (final c in [
      backendPath, searchCtrl, lookupCtrl, logCtrl, idCtrl, titleCtrl, authorCtrl,
      genreCtrl, subgenreCtrl, publisherCtrl, yearCtrl, formatCtrl, ratingCtrl,
      ageCtrl, isbnCtrl, runCtrl, signDateCtrl, addPrintsCtrl, coverPathCtrl,
      licensePathCtrl, biblioCtrl,
    ]) {
      c.dispose();
    }
    super.dispose();
  }

  Future<void> _loadBooks(List<String> args) async {
    final result = await _runBackend(args);
    if (result.exitCode == 0) {
      setState(() => books = parseBooks(result.stdout));
    }
  }

  Future<void> _runLookup() async {
    if (lookupCtrl.text.trim().isEmpty) return;
    final cmd = lookupSource == 'openlibrary' ? 'lookup' : 'lookup-google';
    final result = await _runBackend([cmd, lookupCtrl.text.trim(), '15']);
    if (result.exitCode == 0) {
      final parsed = parseCandidates(result.stdout);
      if (parsed.isEmpty && cmd == 'lookup') {
        final fallback = await _runBackend(['lookup-google', lookupCtrl.text.trim(), '15']);
        if (fallback.exitCode == 0) {
          setState(() => candidates = parseCandidates(fallback.stdout));
        }
      } else {
        setState(() => candidates = parsed);
      }
    }
  }

  Future<void> _initBackend() => _runBackend(['init']);

  Future<void> _search() async {
    final q = searchCtrl.text.trim();
    if (q.isEmpty) return;
    await _loadBooks(['search', q]);
  }

  Future<void> _sort() => _loadBooks(['sort', sortField, sortDirection]);

  Future<void> _binarySearch() async {
    final q = searchCtrl.text.trim();
    if (q.isEmpty) return;
    final result = await _runBackend(['binary-search', q]);
    if (!mounted) return;
    final lower = q.toLowerCase();
    if (result.exitCode == 0 && result.stdout.contains('BEGIN_BOOK')) {
      final rows = parseBooks(result.stdout);
      if (rows.isNotEmpty) {
        showDialog(
          context: context,
          builder: (_) => AlertDialog(
            title: const Text('Бинарный поиск: найдено'),
            content: Text('${rows.first.title} — ${rows.first.author}\nISBN: ${rows.first.isbn}'),
            actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('OK'))],
          ),
        );
      }
      return;
    }
    // Backend binary-search может быть чувствителен к локали/регистру для некоторых языков.
    // Делаем fallback на search, чтобы кириллица и mixed-language запросы работали стабильно.
    await _loadBooks(['search', lower]);
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Бинарный поиск fallback: показаны результаты через обычный поиск.')),
    );
  }

  Future<void> _obst() async {
    final result = await _runBackend(['obst']);
    if (!mounted) return;
    final lines = const LineSplitter()
        .convert(result.stdout)
        .where((e) => e.trim().isNotEmpty)
        .toList();
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: Text('Узлы OBST (${lines.length})'),
        content: SizedBox(
          width: 680,
          child: lines.isEmpty
              ? const Text('OBST пуст (возможно нет ISBN в данных).')
              : ListView.builder(
                  shrinkWrap: true,
                  itemCount: lines.length,
                  itemBuilder: (_, i) => Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Text(lines[i], style: const TextStyle(fontFamily: 'monospace')),
                  ),
                ),
        ),
        actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('Закрыть'))],
      ),
    );
  }

  Future<void> _removeSelected() async {
    final id = int.tryParse(idCtrl.text.trim());
    if (id == null || id <= 0) return;
    final result = await _runBackend(['remove', '$id']);
    if (result.exitCode == 0) {
      _clearForm();
      await _loadBooks(['list']);
    }
  }

  Future<void> _saveUpsert() async {
    final temp = await _buildTempBookFile();
    final args = ['upsert', temp.path];
    if (fetchNetworkOnSave) args.add('--fetch-network');
    final result = await _runBackend(args);
    await temp.delete();
    if (result.exitCode == 0) {
      await _loadBooks(['list']);
    }
  }

  Future<File> _buildTempBookFile() async {
    String esc(String s) => s
        .replaceAll('\\', '\\\\')
        .replaceAll('\n', '\\n')
        .replaceAll('\r', '\\r')
        .replaceAll('=', '\\=');

    final b = StringBuffer()
      ..writeln('BEGIN_BOOK')
      ..writeln('id=${esc(idCtrl.text.trim())}')
      ..writeln('title=${esc(titleCtrl.text.trim())}')
      ..writeln('author=${esc(authorCtrl.text.trim())}')
      ..writeln('genre=${esc(genreCtrl.text.trim())}')
      ..writeln('subgenre=${esc(subgenreCtrl.text.trim())}')
      ..writeln('publisher=${esc(publisherCtrl.text.trim())}')
      ..writeln('year=${esc(yearCtrl.text.trim())}')
      ..writeln('format=${esc(formatCtrl.text.trim().isEmpty ? 'Электронная книга' : formatCtrl.text.trim())}')
      ..writeln('rating=${esc(ratingCtrl.text.trim())}')
      ..writeln('age_rating=${esc(ageCtrl.text.trim())}')
      ..writeln('isbn=${esc(isbnCtrl.text.trim())}')
      ..writeln('total_circulation=${esc(runCtrl.text.trim())}')
      ..writeln('print_sign_date=${esc(signDateCtrl.text.trim())}')
      ..writeln('additional_prints=${esc(addPrintsCtrl.text.trim())}')
      ..writeln('cover_image_path=${esc(coverPathCtrl.text.trim())}')
      ..writeln('license_image_path=${esc(licensePathCtrl.text.trim())}')
      ..writeln('bibliographic_ref=${esc(biblioCtrl.text.trim())}')
      ..writeln('END_BOOK');

    final f = File('${Directory.systemTemp.path}/library_${DateTime.now().microsecondsSinceEpoch}.book');
    return f.writeAsString(b.toString(), encoding: utf8);
  }

  Future<CommandResult> _runBackend(List<String> args) async {
    final exe = backendPath.text.trim();
    if (exe.isEmpty) {
      _appendLog('backend_path_empty\n');
      return const CommandResult('', 'backend_path_empty', 1);
    }

    setState(() => loading = true);
    try {
      final env = Map<String, String>.from(Platform.environment);
      final conn = env['LIBRARY_PG_CONN'];
      if (conn != null && conn.isNotEmpty) {
        env['LIBRARY_PG_CONN'] = conn;
      }

      final proc = await Process.start(exe, args, environment: env, runInShell: true);
      final out = await proc.stdout.transform(utf8.decoder).join();
      final err = await proc.stderr.transform(utf8.decoder).join();
      final code = await proc.exitCode;

      _appendLog('\$ ${[exe, ...args].join(' ')}\n$out${err.isEmpty ? '' : '[stderr]\n$err'}exit=$code\n\n');
      return CommandResult(out, err, code);
    } catch (e) {
      _appendLog('backend_run_error=$e\n');
      return CommandResult('', '$e', 1);
    } finally {
      if (mounted) setState(() => loading = false);
    }
  }

  void _appendLog(String text) {
    logCtrl.text += text;
  }

  void _fillForm(BookRow b) {
    idCtrl.text = b.id;
    titleCtrl.text = b.title;
    authorCtrl.text = b.author;
    genreCtrl.text = b.genre;
    subgenreCtrl.text = b.subgenre;
    publisherCtrl.text = b.publisher;
    yearCtrl.text = b.year;
    formatCtrl.text = b.format.isEmpty ? 'Электронная книга' : b.format;
    ratingCtrl.text = b.rating;
    ageCtrl.text = b.ageRating;
    isbnCtrl.text = b.isbn;
    runCtrl.text = b.totalCirculation;
    signDateCtrl.text = b.printSignDate;
    addPrintsCtrl.text = b.additionalPrints;
    coverPathCtrl.text = b.coverImagePath;
    licensePathCtrl.text = b.licenseImagePath;
    biblioCtrl.text = b.bibliographicRef;
  }

  void _fillFromCandidate(LookupCandidate c) {
    titleCtrl.text = c.title;
    authorCtrl.text = c.author;
    publisherCtrl.text = c.publisher;
    yearCtrl.text = c.year;
    genreCtrl.text = c.genre;
    subgenreCtrl.text = c.subgenre;
    ratingCtrl.text = c.rating;
    isbnCtrl.text = c.isbn;
    coverPathCtrl.text = c.coverUrl;
  }

  void _clearForm() {
    for (final c in [
      idCtrl, titleCtrl, authorCtrl, genreCtrl, subgenreCtrl, publisherCtrl,
      yearCtrl, ratingCtrl, ageCtrl, isbnCtrl, runCtrl, signDateCtrl,
      addPrintsCtrl, coverPathCtrl, licensePathCtrl, biblioCtrl,
    ]) {
      c.clear();
    }
    formatCtrl.text = 'Электронная книга';
    setState(() => selectedBookId = null);
  }

  void _startCreateNewBook() {
    _clearForm();
    setState(() => editorVisible = true);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('LIBRARY • Modern Flutter Frontend'),
        actions: [
          IconButton(onPressed: loading ? null : () => _loadBooks(['list']), icon: const Icon(Icons.refresh)),
        ],
      ),
      body: Row(
        children: [
          Expanded(flex: editorVisible ? 5 : 1, child: _buildCatalogPane()),
          if (editorVisible) ...[
            const VerticalDivider(width: 1),
            Expanded(flex: 4, child: _buildEditorPane()),
          ]
        ],
      ),
    );
  }

  Widget _buildCatalogPane() {
    return Padding(
      padding: const EdgeInsets.all(12),
      child: Column(
        children: [
          Row(
            children: [
              Expanded(child: TextField(controller: backendPath, decoration: const InputDecoration(labelText: 'Путь к backend бинарнику'))),
              const SizedBox(width: 8),
              FilledButton(onPressed: loading ? null : _initBackend, child: const Text('Init')),
              const SizedBox(width: 8),
              FilledButton.tonal(onPressed: loading ? null : () => _loadBooks(['list']), child: const Text('List')),
            ],
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              SizedBox(width: 220, child: TextField(controller: searchCtrl, decoration: const InputDecoration(labelText: 'Поиск по названию/автору'))),
              FilledButton(onPressed: loading ? null : _search, child: const Text('Поиск')),
              FilledButton.tonal(onPressed: loading ? null : _startCreateNewBook, child: const Text('Добавить книгу')),
              DropdownButton<String>(
                value: sortField,
                items: sortFields
                    .map((e) => DropdownMenuItem(
                          value: e,
                          child: Text(e == 'author' ? 'автор (по фамилии)' : e),
                        ))
                    .toList(),
                onChanged: (v) => setState(() => sortField = v!),
              ),
              DropdownButton<String>(value: sortDirection, items: const [DropdownMenuItem(value: 'asc', child: Text('asc')), DropdownMenuItem(value: 'desc', child: Text('desc'))], onChanged: (v) => setState(() => sortDirection = v!)),
              FilledButton.tonal(
                onPressed: loading
                    ? null
                    : () async {
                        await _sort();
                        if (!mounted) return;
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(content: Text('QuickSort выполнен на стороне backend.')),
                        );
                      },
                child: const Text('Быстрая сортировка'),
              ),
              FilledButton.tonal(onPressed: loading ? null : _binarySearch, child: const Text('Бинарный поиск')),
              FilledButton.tonal(onPressed: loading ? null : _obst, child: const Text('OBST')),
              Chip(label: Text('Книг: ${books.length}')),
            ],
          ),
          const SizedBox(height: 10),
          Expanded(
            child: GridView.builder(
              gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                maxCrossAxisExtent: 280,
                childAspectRatio: 0.62,
                crossAxisSpacing: 12,
                mainAxisSpacing: 12,
              ),
              itemCount: books.length,
              itemBuilder: (_, i) {
                final b = books[i];
                final selected = selectedBookId != null && selectedBookId.toString() == b.id;
                return InkWell(
                  onTap: () {
                    setState(() {
                      selectedBookId = int.tryParse(b.id);
                      editorVisible = true;
                    });
                    _fillForm(b);
                  },
                  child: Card(
                    elevation: selected ? 8 : 2,
                    shape: RoundedRectangleBorder(
                      side: BorderSide(color: selected ? Colors.blueAccent : Colors.transparent, width: 2),
                      borderRadius: BorderRadius.circular(16),
                    ),
                    child: Padding(
                      padding: const EdgeInsets.all(10),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Expanded(child: _coverWidget(b)),
                          const SizedBox(height: 8),
                          Text(b.title.isEmpty ? 'Без названия' : b.title, maxLines: 2, overflow: TextOverflow.ellipsis, style: const TextStyle(fontWeight: FontWeight.bold)),
                          Text(b.author.isEmpty ? 'Неизвестен' : b.author, maxLines: 1, overflow: TextOverflow.ellipsis),
                          Text('${b.genre}/${b.subgenre}', maxLines: 1, overflow: TextOverflow.ellipsis),
                          Text('ISBN: ${b.isbn.isEmpty ? '-' : b.isbn}', maxLines: 1, overflow: TextOverflow.ellipsis),
                          Text('Год: ${b.year}  ★ ${b.rating}'),
                        ],
                      ),
                    ),
                  ),
                );
              },
            ),
          ),
          const SizedBox(height: 8),
          TextField(controller: logCtrl, minLines: 7, maxLines: 7, readOnly: true, decoration: const InputDecoration(labelText: 'Лог backend')),
        ],
      ),
    );
  }

  Widget _coverWidget(BookRow b) {
    final path = b.coverImagePath;
    if (path.startsWith('http://') || path.startsWith('https://')) {
      return ClipRRect(
        borderRadius: BorderRadius.circular(12),
        child: Image.network(path, fit: BoxFit.cover, errorBuilder: (_, __, ___) => _coverPlaceholder()),
      );
    }
    if (path.isNotEmpty && File(path).existsSync()) {
      return ClipRRect(
        borderRadius: BorderRadius.circular(12),
        child: Image.file(File(path), fit: BoxFit.cover, errorBuilder: (_, __, ___) => _coverPlaceholder()),
      );
    }
    return _coverPlaceholder();
  }

  Widget _coverPlaceholder() => Container(
    decoration: BoxDecoration(borderRadius: BorderRadius.circular(12), color: Colors.black26),
    alignment: Alignment.center,
    child: const Icon(Icons.menu_book, size: 46, color: Colors.white54),
  );

  Widget _buildEditorPane() {
    Widget tf(String label, TextEditingController c) => TextField(controller: c, decoration: InputDecoration(labelText: label));

    return Padding(
      padding: const EdgeInsets.all(12),
      child: SingleChildScrollView(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('Редактирование / Добавление', style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
            const SizedBox(height: 8),
            Wrap(spacing: 8, runSpacing: 8, children: [
              SizedBox(width: 100, child: tf('ID', idCtrl)),
              SizedBox(width: 230, child: tf('Название*', titleCtrl)),
              SizedBox(width: 230, child: tf('Автор*', authorCtrl)),
              SizedBox(width: 150, child: tf('Год', yearCtrl)),
              SizedBox(width: 150, child: tf('Рейтинг', ratingCtrl)),
              SizedBox(width: 230, child: tf('Жанр', genreCtrl)),
              SizedBox(width: 230, child: tf('Поджанр', subgenreCtrl)),
              SizedBox(width: 230, child: tf('Издательство', publisherCtrl)),
              SizedBox(width: 230, child: tf('Формат', formatCtrl)),
              SizedBox(width: 160, child: tf('Возрастной рейтинг', ageCtrl)),
              SizedBox(width: 260, child: tf('ISBN', isbnCtrl)),
              SizedBox(width: 180, child: tf('Тираж', runCtrl)),
              SizedBox(width: 200, child: tf('Дата подписи в печать', signDateCtrl)),
              SizedBox(width: 280, child: tf('Доп. тиражи', addPrintsCtrl)),
              SizedBox(width: 420, child: tf('Путь/URL обложки', coverPathCtrl)),
              SizedBox(width: 420, child: tf('Путь лицензии', licensePathCtrl)),
            ]),
            const SizedBox(height: 8),
            tf('Библиографическая ссылка (ГОСТ)', biblioCtrl),
            const SizedBox(height: 8),
            Row(children: [
              Checkbox(value: fetchNetworkOnSave, onChanged: (v) => setState(() => fetchNetworkOnSave = v ?? true)),
              const Text('Подтянуть метаданные из сети при сохранении'),
            ]),
            Wrap(spacing: 8, runSpacing: 8, children: [
              FilledButton(onPressed: loading ? null : _saveUpsert, child: const Text('Сохранить / Обновить')),
              FilledButton.tonal(onPressed: loading ? null : _removeSelected, child: const Text('Удалить')),
              OutlinedButton(onPressed: _clearForm, child: const Text('Очистить форму')),
              OutlinedButton.icon(
                onPressed: () {
                  setState(() {
                    selectedBookId = null;
                    editorVisible = false;
                  });
                  FocusScope.of(context).unfocus();
                },
                icon: const Icon(Icons.exit_to_app),
                label: const Text('Закрыть панель редактирования'),
              ),
            ]),
            const Divider(height: 28),
            const Text('Подгрузка из сети', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
            const SizedBox(height: 8),
            Row(children: [
                  Expanded(child: TextField(controller: lookupCtrl, decoration: const InputDecoration(labelText: 'Ключевое слово (например: мастер и маргарита)'))),
              const SizedBox(width: 8),
              DropdownButton<String>(value: lookupSource, items: const [
                DropdownMenuItem(value: 'openlibrary', child: Text('OpenLibrary')),
                DropdownMenuItem(value: 'google', child: Text('Google Books')),
              ], onChanged: (v) => setState(() => lookupSource = v!)),
              const SizedBox(width: 8),
              FilledButton(onPressed: loading ? null : _runLookup, child: const Text('Подгрузить из сети')),
            ]),
            const SizedBox(height: 8),
            SizedBox(
              height: 220,
              child: ListView.builder(
                itemCount: candidates.length,
                itemBuilder: (_, i) {
                  final c = candidates[i];
                  return Card(
                    child: ListTile(
                      title: Text('${c.title} — ${c.author}'),
                      subtitle: Text(
                        'ISBN: ${c.isbn} | ${c.publisher} | ${c.year}\n'
                        'Жанр: ${c.genre.isEmpty ? "-" : c.genre} | Рейтинг: ${c.rating.isEmpty ? "-" : c.rating}\n'
                        '${c.description.isEmpty ? "Описание не найдено" : c.description}',
                        maxLines: 4,
                        overflow: TextOverflow.ellipsis,
                      ),
                      trailing: const Icon(Icons.download_for_offline),
                      onTap: () => _fillFromCandidate(c),
                    ),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class CommandResult {
  final String stdout;
  final String stderr;
  final int exitCode;
  const CommandResult(this.stdout, this.stderr, this.exitCode);
}

class BookRow {
  String id = '';
  String title = '';
  String author = '';
  String genre = '';
  String subgenre = '';
  String publisher = '';
  String year = '';
  String format = '';
  String rating = '';
  String ageRating = '';
  String isbn = '';
  String totalCirculation = '';
  String printSignDate = '';
  String additionalPrints = '';
  String coverImagePath = '';
  String licenseImagePath = '';
  String bibliographicRef = '';
}

class LookupCandidate {
  String title = '';
  String author = '';
  String publisher = '';
  String isbn = '';
  String coverUrl = '';
  String year = '';
  String genre = '';
  String subgenre = '';
  String description = '';
  String rating = '';
}

List<BookRow> parseBooks(String text) {
  final rows = <BookRow>[];
  BookRow? cur;
  for (final line in const LineSplitter().convert(text)) {
    if (line == 'BEGIN_BOOK') {
      cur = BookRow();
      continue;
    }
    if (line == 'END_BOOK') {
      if (cur != null) rows.add(cur);
      cur = null;
      continue;
    }
    if (cur == null) continue;
    final p = line.indexOf('=');
    if (p < 1) continue;
    final k = line.substring(0, p);
    final v = _unescape(line.substring(p + 1));
    switch (k) {
      case 'id': cur.id = v; break;
      case 'title': cur.title = v; break;
      case 'author': cur.author = v; break;
      case 'genre': cur.genre = v; break;
      case 'subgenre': cur.subgenre = v; break;
      case 'publisher': cur.publisher = v; break;
      case 'year': cur.year = v; break;
      case 'format': cur.format = v; break;
      case 'rating': cur.rating = v; break;
      case 'age_rating': cur.ageRating = v; break;
      case 'isbn': cur.isbn = v; break;
      case 'total_circulation': cur.totalCirculation = v; break;
      case 'print_sign_date': cur.printSignDate = v; break;
      case 'additional_prints': cur.additionalPrints = v; break;
      case 'cover_image_path': cur.coverImagePath = v; break;
      case 'license_image_path': cur.licenseImagePath = v; break;
      case 'bibliographic_ref': cur.bibliographicRef = v; break;
    }
  }
  return rows;
}

List<LookupCandidate> parseCandidates(String text) {
  final rows = <LookupCandidate>[];
  LookupCandidate? cur;
  for (final line in const LineSplitter().convert(text)) {
    if (line == 'BEGIN_CANDIDATE') {
      cur = LookupCandidate();
      continue;
    }
    if (line == 'END_CANDIDATE') {
      if (cur != null) rows.add(cur);
      cur = null;
      continue;
    }
    if (cur == null) continue;
    final p = line.indexOf('=');
    if (p < 1) continue;
    final k = line.substring(0, p);
    final v = _unescape(line.substring(p + 1));
    switch (k) {
      case 'title': cur.title = v; break;
      case 'author': cur.author = v; break;
      case 'publisher': cur.publisher = v; break;
      case 'isbn': cur.isbn = v; break;
      case 'cover_url': cur.coverUrl = v; break;
      case 'year': cur.year = v; break;
      case 'genre': cur.genre = v; break;
      case 'subgenre': cur.subgenre = v; break;
      case 'description': cur.description = v; break;
      case 'rating': cur.rating = v; break;
    }
  }
  return rows;
}

String _unescape(String value) {
  return value
      .replaceAll('\\\\', '\u0000')
      .replaceAll('\\n', '\n')
      .replaceAll('\\r', '\r')
      .replaceAll('\\=', '=')
      .replaceAll('\u0000', '\\');
}

String _detectBackendPath() {
  final env = Platform.environment['LIBRARY_BACKEND_BIN'];
  if (env != null && env.isNotEmpty) return env;
  final paths = [
    'build/library_backend.exe',
    'build/Release/library_backend.exe',
    '../build/library_backend.exe',
    '../build/Release/library_backend.exe',
    'build/library_backend',
    '../build/library_backend',
  ];
  for (final p in paths) {
    if (File(p).existsSync()) return p;
  }
  return 'build/Release/library_backend.exe';
}
