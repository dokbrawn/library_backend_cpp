FROM ghcr.io/cirruslabs/flutter:stable

WORKDIR /app/frontend

# Ожидается, что frontend примонтирован как volume в /app/frontend
CMD ["bash", "-lc", "flutter pub get && flutter build linux --release"]
