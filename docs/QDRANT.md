### Generate the Qdrant Qt client

```bash
scripts/generate-qdrant-client.sh
scripts/generate-qdrant-client.sh v1.17.0
```

By default the script pulls the OpenAPI spec from:
`https://raw.githubusercontent.com/qdrant/qdrant/refs/heads/master/docs/redoc/master/openapi.json`

Pass an optional tag as the first argument, for example `v1.17.0`, to switch the git
ref to `refs/tags/<tag>` while keeping the same `docs/redoc/master/openapi.json` path.
If needed, you can still override the spec URL/path explicitly with `QDRANT_OPENAPI_SPEC=...`.

### Install local Qdrant support files

```bash
scripts/qdrant/install.sh
scripts/qdrant/install.sh --enable --start
```

This installer prepares a local user-level Qdrant setup. It creates the expected
directories, installs the bundled config/service files and generates certificate.

### Generate local Qdrant TLS certificates

```bash
mkdir -p /tmp/qdrant-tls && cd /tmp/qdrant-tls
/path/to/repo/scripts/qdrant/cert.sh --ca --pass ca
/path/to/repo/scripts/qdrant/cert.sh --node server
/path/to/repo/scripts/qdrant/cert.sh --node client
```

This generates `ca.pem` / `ca.key` for the local CA, then
`server.pem` / `server.key` and `client.pem` / `client.key`
for node certificates in the current working directory. The first positional argument
after the flags is the base name used for those output files. The script generates
only `prime256v1` EC keys. When `--pass` is used, OpenSSL encrypts the private key
with AES-256 and asks for the password interactively instead of receiving it through
command-line arguments.
