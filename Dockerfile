FROM python:3.11-slim-bookworm AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir pyarrow==21.0.0
WORKDIR /src
COPY cpp cpp
COPY build.sh .
RUN sh build.sh && mkdir /runtime && cp build/shopping /runtime && cp /usr/local/lib/python3.11/site-packages/pyarrow/libarrow.so.2100 /usr/local/lib/python3.11/site-packages/pyarrow/libparquet.so.2100 /runtime/
FROM debian:bookworm-slim
COPY --from=build /runtime /app
ENV LD_LIBRARY_PATH=/app
USER 1000:1000
CMD ["/app/shopping"]
