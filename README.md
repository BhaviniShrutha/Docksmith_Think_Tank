# Docksmith

A simplified Docker-like container build and runtime system written in C++17.  
Docksmith parses a `Docksmithfile`, builds a layered image using libarchive tar layers with SHA-256 content addressing, and runs containers in isolated Linux namespaces.

> **Platform**: Linux only (WSL2 / native Linux). Uses `clone()`, `chroot()`, `CLONE_NEWPID`, `CLONE_NEWNS`.  
> **Architecture**: x86_64.

---

## Prerequisites

Install on Ubuntu/Debian (WSL2 or native):

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    libssl-dev \
    libarchive-dev
```

---

## Build

```bash
make
# Binary is at ./build/docksmith
```

To clean:

```bash
make clean
```

---

## One-Time Setup: Import a Base Image

Docksmith does not pull images from the internet during a build. You must import a base image once before your first build.

**Download Alpine Linux minimal rootfs (one-time):**

```bash
wget -O /tmp/alpine-rootfs.tar.gz \
  https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz

gunzip /tmp/alpine-rootfs.tar.gz
# Result: /tmp/alpine-rootfs.tar
```

**Import as `base:latest`:**

```bash
./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest
```

This will:
- Compute the SHA-256 digest of the tar
- Store the layer in `~/.docksmith/layers/sha256:<hex>.tar`
- Write the image manifest to `~/.docksmith/images/base_latest.json`

---

## Usage

### Build an image

```bash
./build/docksmith build -t myapp:latest ./sample_app/
```

With cache disabled:

```bash
./build/docksmith build -t myapp:latest --no-cache ./sample_app/
```

> **Note**: `build` steps that use `RUN` execute commands inside an isolated namespace. This requires **root** or `sudo`:
>
> ```bash
> sudo ./build/docksmith build -t myapp:latest ./sample_app/
> ```

### List images

```bash
./build/docksmith images
```

### Run a container

> **Note**: `run` always requires **sudo** (uses `clone()` + `chroot()`):

```bash
sudo ./build/docksmith run myapp:latest
```

Override environment variables at runtime:

```bash
sudo ./build/docksmith run -e GREETING=Goodbye -e TARGET=Universe myapp:latest
```

Override the command:

```bash
sudo ./build/docksmith run myapp:latest /bin/sh -c "echo custom command"
```

### Remove an image

```bash
./build/docksmith rmi myapp:latest
```

---

## Full Demo Sequence

```bash
# 1. Build
make

# 2. Import base (once only)
./scripts/import_base.sh /tmp/alpine-rootfs.tar base latest

# 3. First build (all cache misses)
sudo ./build/docksmith build -t myapp:latest ./sample_app/

# 4. Second build (verify cache hits)
sudo ./build/docksmith build -t myapp:latest ./sample_app/

# 5. List images
./build/docksmith images

# 6. Run with default ENV
sudo ./build/docksmith run myapp:latest

# 7. Run with ENV override
sudo ./build/docksmith run -e GREETING=Goodbye -e TARGET=Universe myapp:latest

# 8. Remove image
./build/docksmith rmi myapp:latest
```

---

## Docksmithfile Reference

```dockerfile
FROM base:latest           # Required first instruction — specifies base image
WORKDIR /app               # Set working directory (created if missing)
ENV GREETING=Hello         # Set environment variable
ENV TARGET=World           # Set another environment variable
COPY . .                   # Copy context dir to current WORKDIR
RUN /bin/sh app.sh         # Execute command in isolated namespace, capture delta
CMD ["/bin/sh", "app.sh"]  # Default command when running the image
```

**Supported instructions**: `FROM`, `WORKDIR`, `ENV`, `COPY`, `RUN`, `CMD`

**`CMD` format**: JSON array `["bin", "arg1"]` or shell form `command arg1 arg2` (wrapped as `/bin/sh -c`).

---

## How It Works

### Build Pipeline
1. **Parser** reads `Docksmithfile`, validates instructions, tracks line numbers.
2. **Build engine** processes each instruction in order, tracking: `currentWorkdir`, `currentEnv`, `collectedLayers`, `previousLayerDigest`, `cacheBroken`.
3. **Cache** — for `COPY` and `RUN`:
   - Cache key = SHA-256 of `(prevLayerDigest + instruction + workdir + envState + srcFileHashes)`
   - First cache miss sets `cacheBroken = true` — all subsequent steps skip cache lookup.
4. **COPY** — Files are staged and packed into a tar layer (only files being copied).
5. **RUN** — Delta capture:
   - Snapshot rootfs (all file hashes) **before** execution
   - Execute in isolated namespace via `clone(CLONE_NEWPID|CLONE_NEWNS|CLONE_NEWUTS) + chroot()`
   - Snapshot rootfs **after** execution
   - Compute diff: new and changed files only
   - Stage only the delta → create tar layer
6. **Manifest** — Written as JSON with name, tag, digest, config, and layers array.

### Storage Layout
```
~/.docksmith/
├── images/          # Image manifests: <name>_<tag>.json
├── layers/          # Content-addressed layer tars: sha256:<hex>.tar
└── cache/           # Cache index: <cacheKey> → <layerDigest>
```

### Isolation
`docksmith run` uses Linux namespaces:
- `CLONE_NEWPID` — new PID namespace (container process is PID 1)
- `CLONE_NEWNS` — new mount namespace
- `CLONE_NEWUTS` — new UTS/hostname namespace
- `chroot()` — filesystem root isolation

---

## Project Structure

```
Docksmith_Think_Tank/
├── Makefile
├── README.md
├── include/
│   ├── build_engine.h
│   ├── cache.h
│   ├── layer.h
│   ├── parser.h
│   └── runtime.h
├── src/
│   ├── build_engine.cpp   # Full build pipeline + delta capture
│   ├── cache.cpp          # Cache key computation + storage
│   ├── layer.cpp          # SHA-256, tar creation/extraction, manifest I/O
│   ├── main.cpp           # CLI argument parsing
│   ├── parser.cpp         # Docksmithfile parser
│   └── runtime.cpp        # Linux namespace isolation, image management
├── sample_app/
│   ├── Docksmithfile      # All 6 instruction types
│   └── app.sh             # Sample script using ENV vars
└── scripts/
    └── import_base.sh     # One-time base image import
```
