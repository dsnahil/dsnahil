FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake ninja-build python3 ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build-docker -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-docker -j 2 && ctest --test-dir build-docker --output-on-failure

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 && rm -rf /var/lib/apt/lists/* && useradd --uid 10001 --create-home app
COPY --from=build /src/build-docker/timingforge/timingforge /usr/local/bin/timingforge
COPY --from=build /src/build-docker/logicscope/logicscope /usr/local/bin/logicscope
USER 10001
WORKDIR /data
CMD ["timingforge", "--help"]
