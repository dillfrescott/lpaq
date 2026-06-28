FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y g++-mingw-w64-x86-64 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY lpaq.cpp .
RUN x86_64-w64-mingw32-g++ -O3 -s -static lpaq.cpp -o lpaq.exe

FROM scratch AS export
COPY --from=build /src/lpaq.exe /