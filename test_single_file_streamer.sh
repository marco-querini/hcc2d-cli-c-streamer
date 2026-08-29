#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE="${ROOT_DIR}/single_file_c_hcc2d_streamer_v0.9.0.c"
CC_BIN="${CC:-cc}"
TMP_BASE="${TMPDIR:-/tmp}"
TEST_DIR="$(mktemp -d "${TMP_BASE%/}/hcc2d-streamer-0.9.0-test.XXXXXX")"

case "${TEST_DIR}" in
    "${TMP_BASE%/}"/hcc2d-streamer-0.9.0-test.*) ;;
    *) echo "Error: unexpected temporary directory: ${TEST_DIR}" >&2; exit 1 ;;
esac

cleanup() {
    case "${TEST_DIR}" in
        "${TMP_BASE%/}"/hcc2d-streamer-0.9.0-test.*) rm -rf -- "${TEST_DIR}" ;;
        *) echo "Error: refusing to remove unexpected path: ${TEST_DIR}" >&2 ;;
    esac
}
trap cleanup EXIT

if [[ ! -f "${SOURCE}" ]]; then
    echo "Error: source not found: ${SOURCE}" >&2
    exit 1
fi

SDL_CFLAGS_TEXT="$(pkg-config --cflags sdl2)"
SDL_LIBS_TEXT="$(pkg-config --libs sdl2)"
read -r -a SDL_CFLAGS <<<"${SDL_CFLAGS_TEXT}"
read -r -a SDL_LIBS <<<"${SDL_LIBS_TEXT}"

CFLAGS=(-std=c11 -O2 -Wall -Wextra)
if [[ "${SANITIZE:-0}" == "1" ]]; then
    CFLAGS=(-std=c11 -O1 -g -Wall -Wextra
            -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

STREAMER="${TEST_DIR}/hcc2d_streamer"
"${CC_BIN}" "${CFLAGS[@]}" "${SDL_CFLAGS[@]}" -o "${STREAMER}" \
    "${SOURCE}" "${SDL_LIBS[@]}" -lz -lm

run_streamer() {
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1 \
        "${STREAMER}" "$@"
}

HELP="$(run_streamer --help)"
grep -q 'Version 0.9.0' <<<"${HELP}"
grep -q -- '--colors {2,4,8}' <<<"${HELP}"
grep -q -- '--mode {qr,hcc2d4,hcc2d8}' <<<"${HELP}"
if grep -Eiq 'hcc2d16|version 45|experimental' <<<"${HELP}"; then
    echo 'Error: help exposes an experimental symbol family' >&2
    exit 1
fi

expect_rejected() {
    local name=$1
    shift
    if run_streamer "$@" >"${TEST_DIR}/${name}.out" 2>"${TEST_DIR}/${name}.err"; then
        echo "Error: invalid case '${name}' unexpectedly succeeded" >&2
        exit 1
    fi
}

expect_rejected hcc2d16 --mode hcc2d16 --version 40 /dev/null
expect_rejected colors16 --colors 16 --version 40 /dev/null
expect_rejected version45 --mode hcc2d8 --version 45 /dev/null
expect_rejected experimental --experimental --mode hcc2d8 /dev/null

if grep -Eiq 'hcc2d16|version[_ -]?45|palette[_ -]?16|experimental' "${SOURCE}"; then
    echo 'Error: source still contains experimental HCC2D16/version-45 support' >&2
    exit 1
fi

cat >"${TEST_DIR}/encode_harness.c" <<'EOF'
#define main hcc2d_streamer_cli_main
#include "single_file_c_hcc2d_streamer_v0.9.0.c"
#undef main

static int check_symbol(const char *mode)
{
    const uint8_t payload[] = {'t', 'e', 's', 't'};
    EncodedSymbol symbol = {0};
    char error[256] = "";
    int status;

    if (strcmp(mode, "qr") == 0) {
        status = encode_qr(payload, (int)sizeof(payload), 'M', 10,
                           1024, 4, &symbol, error);
    } else {
        status = encode_hcc2d(payload, (int)sizeof(payload), mode, 'M', 10,
                              1024, 4, &symbol, error);
    }
    if (status != 0) {
        fprintf(stderr, "%s encode failed: %s\n", mode, error);
        return 1;
    }
    if (!symbol.pixels || symbol.width <= 0 || symbol.height <= 0 ||
        symbol.version_number != 10) {
        fprintf(stderr, "%s produced an invalid symbol\n", mode);
        free(symbol.pixels);
        return 1;
    }
    free(symbol.pixels);
    return 0;
}

int main(void)
{
    gf256_init();
    if (check_symbol("qr") != 0) return 1;
    if (check_symbol("hcc2d4") != 0) return 1;
    if (check_symbol("hcc2d8") != 0) return 1;
    return 0;
}
EOF

HARNESS="${TEST_DIR}/encode_harness"
"${CC_BIN}" "${CFLAGS[@]}" "${SDL_CFLAGS[@]}" -I"${ROOT_DIR}" \
    -o "${HARNESS}" "${TEST_DIR}/encode_harness.c" \
    "${SDL_LIBS[@]}" -lz -lm

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "${HARNESS}"

printf 'PASS: build=1 help=1 rejected_modes=4 encoded_modes=3 sanitizer=%s\n' \
    "${SANITIZE:-0}"
