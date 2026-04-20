# Docksmith 4-Part Explanation Report

This report splits the project into 4 explainable parts so that 4 different people can present it clearly. Each part has:

1. A title
2. A detailed explanation
3. Commands to run
4. What the output means
5. A matching script that prints `cmd`, `op`, and `inference` block by block

The scripts are designed for presentation use. They run grouped command blocks and print:

- `title`: which sub-topic is being demonstrated
- `cmd`: the exact command block being run
- `op`: the output produced by that block
- `inference`: one-line meaning of that block, with the exit code

## Before Running Any Part

Run the base-setup helper first:

```bash
bash ./scripts/report_prepare_base.sh
```

What it does:

- Checks whether `base:latest` exists in `/root/.docksmith/images/`
- Checks whether the referenced base layer tar exists
- Re-imports the base image if `/tmp/alpine-rootfs.tar` is available

If `/tmp/alpine-rootfs.tar` is missing, restore it first:

```bash
wget -O /tmp/alpine-rootfs.tar.gz https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz
gunzip -f /tmp/alpine-rootfs.tar.gz
sudo ./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest
```

## Part 1: CLI and Command Routing

### What this part covers

This part explains the user-facing command-line interface. It shows how the binary is built, how the system exposes commands like `build`, `images`, `run`, and `rmi`, and how the project expects to be operated from the terminal.

Main files:

- `src/main.cpp`
- `include/build_engine.h`
- `include/runtime.h`
- `Makefile`

### Core explanation

The CLI is the single entry point of the project. There is no daemon. The user interacts only through the `docksmith` binary, which parses terminal arguments and dispatches work to either the build engine or the runtime functions.

This part is important because it establishes the architecture: all features are reachable through one binary, and state is stored on disk rather than managed by a background service.

### Commands to run manually

```bash
pwd
uname -a
id
sudo -v
make
ls -l ./build/docksmith
./build/docksmith 2>&1 || true
sudo ./build/docksmith images
```

### What the output means

- If `make` succeeds, the project can be compiled from source.
- If `./build/docksmith` prints usage, the CLI entry point is functioning.
- If `docksmith images` runs, the binary is correctly routing a real command into the image-management code.

### Matching script

```bash
bash ./scripts/report_part1_cli.sh
```

## Part 2: Docksmithfile Parsing and Instruction Handling

### What this part covers

This part explains how Docksmith reads a `Docksmithfile`, validates supported instructions, and handles instruction-specific behavior such as `CMD` JSON parsing and `COPY` glob expansion.

Main files:

- `src/parser.cpp`
- `include/parser.h`
- `src/build_engine.cpp`

### Core explanation

The parser reads the `Docksmithfile` line by line, strips whitespace, skips comments, validates that only the supported instructions are used, and stores the original line text plus a line number for better error handling.

The build engine then interprets those parsed instructions and applies instruction-specific logic:

- `FROM` selects the base image
- `WORKDIR` changes the current working directory state
- `ENV` updates the environment state
- `COPY` resolves sources and destinations, including glob support
- `RUN` executes inside the isolated filesystem
- `CMD` stores the default runtime command in JSON-array form

For presentation, this section is strongest when you show that all six instructions exist in the sample app, that `CMD` with commas is parsed correctly, and that `COPY src/**/*.txt` selects the expected files.

### Commands to run manually

```bash
cat ./sample_app/Docksmithfile
```

```bash
rm -rf /tmp/docksmith_report_cmd
mkdir -p /tmp/docksmith_report_cmd
cat > /tmp/docksmith_report_cmd/Docksmithfile <<'EOF'
FROM base:latest
CMD ["/bin/sh","-c","echo a,b"]
EOF
sudo ./build/docksmith build -t report-cmdverify:latest /tmp/docksmith_report_cmd
sudo cat /root/.docksmith/images/report-cmdverify_latest.json
```

```bash
rm -rf /tmp/docksmith_report_glob
mkdir -p /tmp/docksmith_report_glob/src/sub
printf 'top\n' > /tmp/docksmith_report_glob/src/a.txt
printf 'nested\n' > /tmp/docksmith_report_glob/src/sub/b.txt
printf 'skip\n' > /tmp/docksmith_report_glob/src/sub/c.log
cat > /tmp/docksmith_report_glob/Docksmithfile <<'EOF'
FROM base:latest
COPY src/**/*.txt /app/
CMD ["/bin/sh"]
EOF
sudo ./build/docksmith build -t report-globverify:latest /tmp/docksmith_report_glob
```

### What the output means

- If the sample `Docksmithfile` shows all 6 instructions, the project satisfies the basic language coverage for the demo app.
- If the `report-cmdverify` manifest stores `["/bin/sh", "-c", "echo a,b"]`, `CMD` JSON parsing is working correctly.
- If the glob test builds and the produced layer contains `/app/a.txt` and `/app/sub/b.txt`, the `COPY` glob logic is working for the tested case.

### Matching script

```bash
bash ./scripts/report_part2_parser.sh
```

## Part 3: Build Engine, Cache, Layers, and Manifest

### What this part covers

This part explains how an image is built from a `Docksmithfile`, how cache keys are used, how `COPY` and `RUN` produce layers, and how the final image manifest is written.

Main files:

- `src/build_engine.cpp`
- `src/cache.cpp`
- `include/cache.h`
- `src/layer.cpp`
- `include/layer.h`

### Core explanation

The build engine is the core of Docksmith. It:

- loads the base image from local storage
- tracks current `WORKDIR`, `ENV`, and `CMD`
- computes cache keys before `COPY` and `RUN`
- reuses cached layers on a hit
- creates a new content-addressed tar layer on a miss
- updates the final image manifest with config and layer metadata

This part is best explained through a four-step sequence:

1. A build with `--no-cache` to force real layer creation without using the cache
2. A normal rebuild that writes cache entries
3. A warm rebuild that shows `[CACHE HIT]`
4. A modified source file that invalidates `COPY` and downstream `RUN`

### Commands to run manually

```bash
rm -rf /tmp/docksmith_report_build
cp -r ./sample_app /tmp/docksmith_report_build
printf 'nonce:%s\n' "$(date +%s%N)" > /tmp/docksmith_report_build/.report_nonce
sudo ./build/docksmith build -t report-myapp:latest --no-cache /tmp/docksmith_report_build
sudo ./build/docksmith build -t report-myapp:latest /tmp/docksmith_report_build
sudo ./build/docksmith build -t report-myapp:latest /tmp/docksmith_report_build
printf '\necho "report edit"\n' >> /tmp/docksmith_report_build/app.sh
sudo ./build/docksmith build -t report-myapp:latest /tmp/docksmith_report_build
sudo ./build/docksmith images
sudo cat /root/.docksmith/images/report-myapp_latest.json
```

### What the output means

- `[CACHE MISS]` on `COPY` and `RUN` shows actual layer creation.
- The first normal rebuild after `--no-cache` may still show misses, because `--no-cache` skips cache writes.
- `[CACHE HIT]` on the next warm rebuild shows deterministic reuse of existing layers.
- After editing `app.sh`, the affected `COPY` and downstream `RUN` should become misses again.
- The image manifest proves that the final image stores config plus ordered layer metadata.

### Matching script

```bash
bash ./scripts/report_part3_build_cache.sh
```

## Part 4: Runtime, Isolation, and the Sample App

### What this part covers

This part explains how Docksmith assembles a container filesystem from layers, applies image configuration, runs a process in isolation, and demonstrates the runtime behavior of the sample app.

Main files:

- `src/runtime.cpp`
- `sample_app/Docksmithfile`
- `sample_app/app.sh`
- `scripts/import_base.sh`

### Core explanation

At runtime, Docksmith extracts the image layers into a temporary root filesystem, applies the image environment and working directory, then executes the requested command inside an isolated root using Linux process-isolation primitives.

This is the most demo-visible part of the project because it shows:

- the sample app running from the built image
- runtime `-e KEY=VALUE` environment overrides
- isolation proof by writing a file inside the container and checking that it does not appear on the host

### Commands to run manually

```bash
rm -rf /tmp/docksmith_report_runtime
cp -r ./sample_app /tmp/docksmith_report_runtime
sudo ./build/docksmith build -t report-runtime:latest --no-cache /tmp/docksmith_report_runtime
sudo ./build/docksmith run report-runtime:latest
sudo ./build/docksmith run -e GREETING=Goodbye -e TARGET=Universe report-runtime:latest
sudo rm -f /proof.txt
sudo ./build/docksmith run report-runtime:latest /bin/sh -c 'echo isolated > /proof.txt'
sudo test ! -e /proof.txt && echo PASS || echo FAIL
```

### What the output means

- If the sample app prints greeting text and exits with code `0`, the runtime path is working.
- If `-e GREETING=... -e TARGET=...` changes the printed values, runtime environment overrides are applied correctly.
- If the host-side check prints `PASS`, the container write stayed inside the isolated filesystem.

### Matching script

```bash
bash ./scripts/report_part4_runtime.sh
```

## Run Everything Sequentially

If you want one command that runs all 4 parts:

```bash
bash ./scripts/report_run_all.sh
```
