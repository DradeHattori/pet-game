# Создаём образ на основе сло gcc (ОС и компилятор).
# 11.3 - используемая версия gcc
# Имя контейнера build
FROM gcc:11.3 as build 

RUN apt update && \
    apt install -y \
      python3-pip \
      cmake \
    && \
    pip install conan==1.* 

# копируем conanfile.txt в контейнер и запускаем conan install
COPY conanfile.txt /app/
RUN mkdir /app/build && cd /app/build && \
    conan install .. --build=missing -s build_type=Release -s compiler.libcxx=libstdc++11

# копируем файлы проекта и CMakeLists.txt
COPY ./src /app/src
COPY CMakeLists.txt /app/

# Запускаем сборку проекта
RUN cd /app/build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build .

# Контейнер, унаследованный от ubuntu:22.04.
# Имя контейнера run
FROM ubuntu:22.04 as run 

# Создадим пользователя www
RUN groupadd -r www && useradd -r -g www www
USER www

# Копируем приложение со сборочного контейнера в директорию /app.
# Еопируем папки data и static.
COPY --from=build /app/build/bin/game_server /app/
COPY ./data /app/data
COPY ./static /app/static

# Запускаем игровой сервер
ENTRYPOINT ["/app/game_server", "-c", "/app/data/config.json", "-w", "/app/static"]