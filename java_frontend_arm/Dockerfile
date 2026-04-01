FROM --platform=linux/arm64 eclipse-temurin:21-jre

WORKDIR /app

COPY java_frontend/target/java-frontend-1.0.0.jar /app/java-frontend.jar
COPY java_frontend_arm/run-arm.sh /app/run-arm.sh

RUN chmod +x /app/run-arm.sh

ENTRYPOINT ["/app/run-arm.sh"]
