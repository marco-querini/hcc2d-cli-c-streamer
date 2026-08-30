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

CFLAGS=(-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror)
if [[ "${SANITIZE:-0}" == "1" ]]; then
    CFLAGS=(-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror
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
grep -q -- '--mode {qr,hcc2d4,hcc2d8}' <<<"${HELP}"
grep -q -- '--max-data-shards N' <<<"${HELP}"
grep -q -- '--parity-ratio R' <<<"${HELP}"
grep -q '2 MiB (2,097,152 bytes)' <<<"${HELP}"
grep -q 'HCC2DST v2 output' <<<"${HELP}"
grep -q 'HCC2D Decoder version 1.2.4 or later' <<<"${HELP}"
if grep -Eq -- '--colors|--k-max|--parity-num|--parity-den' <<<"${HELP}"; then
    echo 'Error: help exposes an obsolete option' >&2
    exit 1
fi
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
expect_rejected version45 --mode hcc2d8 --version 45 /dev/null
expect_rejected experimental --experimental --mode hcc2d8 /dev/null
expect_rejected invalid_integer --version 12x /dev/null
expect_rejected integer_overflow --version 999999999999999999999 /dev/null
expect_rejected unsupported_fps --fps 11 /dev/null
expect_rejected obsolete_colors --colors 8 /dev/null
expect_rejected obsolete_k_max --k-max 100 /dev/null
expect_rejected obsolete_parity_num --parity-num 70 /dev/null
expect_rejected obsolete_parity_den --parity-den 100 /dev/null
expect_rejected malformed_palette --mode hcc2d4 --palette-rgb \
    '0,,0;220,0,0;0,200,220;255,255,255' /dev/null
expect_rejected parity_negative --parity-ratio -0.1 /dev/null
expect_rejected parity_above_one --parity-ratio 1.1 /dev/null
expect_rejected parity_malformed --parity-ratio 0.7x /dev/null
expect_rejected parity_precision --parity-ratio 0.1234567 /dev/null
expect_rejected extra_argument /dev/null unexpected
expect_rejected empty_file /dev/null

for supported_fps in 10 12 15 20; do
    name="supported_fps_${supported_fps}"
    expect_rejected "${name}" --fps "${supported_fps}" /dev/null
    grep -Fqx 'Error: input file is empty' "${TEST_DIR}/${name}.err"
done

expect_rejected full_parity --max-data-shards 255 --parity-ratio 1 /dev/null
grep -Fqx 'Error: input file is empty' "${TEST_DIR}/full_parity.err"

expect_rejected valid_palette --mode hcc2d4 --palette-rgb \
    '0,0,0; 220,0,0; 0,200,220; 255,255,255' /dev/null
grep -Fqx 'Error: input file is empty' "${TEST_DIR}/valid_palette.err"

truncate -s 2097153 "${TEST_DIR}/too-large.bin"
expect_rejected file_too_large "${TEST_DIR}/too-large.bin"
grep -q '2 MiB' "${TEST_DIR}/file_too_large.err"

if grep -Eiq 'hcc2d16|version[_ -]?45|palette[_ -]?16|experimental' "${SOURCE}"; then
    echo 'Error: source still contains experimental HCC2D16/version-45 support' >&2
    exit 1
fi

cat >"${TEST_DIR}/encode_harness.c" <<'EOF'
#define main hcc2d_streamer_cli_main
#include "single_file_c_hcc2d_streamer_v0.9.0.c"
#undef main

static int encode_symbol(const char *mode, char ec_level, int version,
                         const uint8_t *payload, int payload_len,
                         EncodedSymbol *symbol, char error[256])
{
    if (strcmp(mode, "qr") == 0) {
        return encode_qr(payload, payload_len, ec_level, version,
                         1, 4, symbol, error);
    }
    return encode_hcc2d(payload, payload_len, mode, ec_level, version,
                        1, 4, symbol, error);
}

static int check_symbol_boundary(const char *mode, char ec_level, int version)
{
    const QRVersion *qrv = version_for_mode(version);
    int ec_idx = ec_level_index(ec_level);
    int planes = plane_count_for_mode(mode);
    ECEntry entry = strcmp(mode, "qr") == 0
                  ? qrv->ec[ec_idx]
                  : hcc2d_ec(&qrv->ec[ec_idx], planes);
    int data_bytes = ec_total_sym(&entry) - ec_total_cw(&entry);
    int max_payload = data_bytes - header_overhead_bytes(mode, version);
    if (max_payload < 1) return 1;

    uint8_t *payload = xmalloc((size_t)max_payload + 1u);
    for (int i = 0; i <= max_payload; i++)
        payload[i] = (uint8_t)(i * 37 + 11);

    EncodedSymbol symbol = {0};
    char error[256] = "";
    if (encode_symbol(mode, ec_level, version, payload, max_payload,
                      &symbol, error) != 0) {
        fprintf(stderr, "%s/%c/v%d boundary encode failed: %s\n",
                mode, ec_level, version, error);
        free(payload);
        return 1;
    }

    int expected_inner = 17 + 4 * version;
    int expected_width = expected_inner + 8;
    if (strcmp(mode, "qr") != 0) expected_width += 2;
    if (!symbol.pixels || symbol.width != expected_width ||
        symbol.height != expected_width || symbol.version_number != version) {
        fprintf(stderr, "%s/%c/v%d produced invalid dimensions\n",
                mode, ec_level, version);
        free(symbol.pixels);
        free(payload);
        return 1;
    }
    free(symbol.pixels);

    symbol = (EncodedSymbol){0};
    error[0] = '\0';
    if (encode_symbol(mode, ec_level, version, payload, max_payload + 1,
                      &symbol, error) == 0) {
        fprintf(stderr, "%s/%c/v%d accepted an oversized payload\n",
                mode, ec_level, version);
        free(symbol.pixels);
        free(payload);
        return 1;
    }
    free(payload);
    return 0;
}

static uint16_t get_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t get_le32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static int check_stream_header(void)
{
    const uint8_t shard[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };
    const uint8_t session[4] = {0x01, 0x23, 0x45, 0x67};
    size_t wrapped_len = 0;
    g_shard_data_bytes = sizeof(shard);
    uint8_t *wrapped = build_shard_payload(
        shard, 123456u, 0, "example.txt", session,
        2, 1, 3, 2, 4, 9, 0x89ABCDEFu, "frame.bin", &wrapped_len);

    const size_t envelope_size = 6u + 1u + 1u + 1u + strlen("frame.bin");
    if (wrapped_len != envelope_size + STREAM_CHUNK_SIZE + sizeof(shard) ||
        memcmp(wrapped, "HCC2DF", 6) != 0 || wrapped[6] != 1 ||
        wrapped[7] != 0 || wrapped[8] != strlen("frame.bin")) {
        free(wrapped);
        return 1;
    }

    const uint8_t *header = wrapped + envelope_size;
    const uint8_t *payload = header + STREAM_CHUNK_SIZE;
    if (memcmp(header, "HCC2DST\0", 8) != 0 || header[8] != 2 ||
        memcmp(header + 9, session, 4) != 0 ||
        get_le32(header + 13) != 123456u || header[17] != 0 ||
        header[18] != strlen("example.txt") ||
        memcmp(header + 19, "example.txt", strlen("example.txt")) != 0 ||
        get_le16(header + 51) != 2 || get_le16(header + 53) != 1 ||
        header[55] != 3 || header[56] != 2 || header[57] != 4 ||
        header[58] != 0 || get_le32(header + 59) != 9 ||
        get_le32(header + 63) != 0x89ABCDEFu ||
        memcmp(payload, shard, sizeof(shard)) != 0) {
        free(wrapped);
        return 1;
    }

    uint32_t stored_crc = get_le32(header + STREAM_CHUNK_CRC_OFFSET);
    uint32_t calculated_crc = stream_chunk_crc32(header, payload, sizeof(shard));
    free(wrapped);
    return stored_crc == calculated_crc ? 0 : 1;
}

static int check_capacity_rates(void)
{
    struct RateCase { const char *mode; char ec; int version; int fps; int bytes; };
    static const struct RateCase cases[] = {
        {"hcc2d8", 'M', 33, 12, 4893},
        {"hcc2d8", 'L', 40, 15, 8868},
        {"hcc2d4", 'M', 30, 12, 2746},
        {"qr",     'L', 20, 12,  861},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const struct RateCase *c = &cases[i];
        const QRVersion *qrv = version_for_mode(c->version);
        ECEntry entry = qrv->ec[ec_level_index(c->ec)];
        if (strcmp(c->mode, "qr") != 0)
            entry = hcc2d_ec(&entry, plane_count_for_mode(c->mode));
        int bytes = ec_total_sym(&entry) - ec_total_cw(&entry);
        if (bytes != c->bytes || bytes * 8 * c->fps <= 0) return 1;
    }
    return 0;
}

static int check_parity_ratio(void)
{
    uint32_t ratio = 0;
    if (parse_parity_ratio("0", &ratio) != 0 || ratio != 0u) return 1;
    if (parse_parity_ratio(".7", &ratio) != 0 || ratio != 700000u) return 1;
    if (parse_parity_ratio("0.70", &ratio) != 0 || ratio != 700000u) return 1;
    if (parse_parity_ratio("1.000000", &ratio) != 0 ||
        ratio != PARITY_RATIO_SCALE) return 1;
    if (parity_shard_count(150u, 700000u) != 105u) return 1;
    if (parity_shard_count(3u, 500000u) != 2u) return 1;
    if (effective_data_shard_limit(255u, 0u) != 255u) return 1;
    if (effective_data_shard_limit(150u, 700000u) != 150u) return 1;
    if (effective_data_shard_limit(255u, PARITY_RATIO_SCALE) != 127u) return 1;
    static const uint32_t ratios[] = {0u, 1u, 100000u, 500000u,
                                      700000u, 999999u, PARITY_RATIO_SCALE};
    for (size_t r = 0; r < sizeof(ratios) / sizeof(ratios[0]); r++) {
        for (uint32_t requested = 1; requested <= N_LIMIT; requested++) {
            uint32_t limit = effective_data_shard_limit(requested, ratios[r]);
            if (limit < 1u || limit > requested ||
                limit + parity_shard_count(limit, ratios[r]) > N_LIMIT) {
                return 1;
            }
        }
    }
    return 0;
}

static int check_filename_handling(void)
{
    static const char valid_utf8[] = "r\xC3\xA9sum\xC3\xA9.pdf";
    static const char invalid_utf8[] = {'b', 'a', 'd', (char)0xC3, '(', '\0'};
    static const char long_name[] =
        "very-long-r\xC3\xA9sum\xC3\xA9-document-name.pdf";

    if (!filename_is_receiver_safe(valid_utf8) ||
        filename_is_receiver_safe(invalid_utf8) ||
        filename_is_receiver_safe("   ") ||
        filename_is_receiver_safe("bad\nname")) {
        return 1;
    }

    char field[32];
    uint8_t field_len = 0;
    set_fname_field(field, &field_len, long_name);
    if (field_len == 0 || field_len > sizeof(field) || field_len < 4 ||
        memcmp(field + field_len - 4, ".pdf", 4) != 0) {
        return 1;
    }

    char terminated[33] = {0};
    memcpy(terminated, field, field_len);
    return utf8_is_valid(terminated) ? 0 : 1;
}

static int invert_gf_matrix(uint8_t input[4][4], uint8_t inverse[4][4])
{
    uint8_t augmented[4][8] = {{0}};
    for (int row = 0; row < 4; row++) {
        memcpy(augmented[row], input[row], 4);
        augmented[row][4 + row] = 1;
    }

    for (int col = 0; col < 4; col++) {
        int pivot = col;
        while (pivot < 4 && augmented[pivot][col] == 0) pivot++;
        if (pivot == 4) return 1;
        if (pivot != col) {
            for (int j = 0; j < 8; j++) {
                uint8_t value = augmented[col][j];
                augmented[col][j] = augmented[pivot][j];
                augmented[pivot][j] = value;
            }
        }

        uint8_t scale = (uint8_t)gf_inv(augmented[col][col]);
        for (int j = 0; j < 8; j++)
            augmented[col][j] = (uint8_t)gf_mul(augmented[col][j], scale);

        for (int row = 0; row < 4; row++) {
            if (row == col) continue;
            uint8_t factor = augmented[row][col];
            if (factor == 0) continue;
            for (int j = 0; j < 8; j++)
                augmented[row][j] ^= (uint8_t)gf_mul(factor, augmented[col][j]);
        }
    }

    for (int row = 0; row < 4; row++)
        memcpy(inverse[row], augmented[row] + 4, 4);
    return 0;
}

static int check_outer_erasure_recovery(void)
{
    enum { k = 4, m = 3, n = k + m, shard_bytes = 32 };
    uint8_t data[k * shard_bytes];
    uint8_t parity[m * shard_bytes];
    const uint8_t *shards[n];

    for (int i = 0; i < k * shard_bytes; i++)
        data[i] = (uint8_t)(i * 73 + 19);
    g_shard_data_bytes = shard_bytes;
    rs_encode_parity(data, parity, k, m);
    for (int i = 0; i < k; i++) shards[i] = data + i * shard_bytes;
    for (int i = 0; i < m; i++) shards[k + i] = parity + i * shard_bytes;

    int subset_count = 0;
    for (unsigned mask = 0; mask < (1u << n); mask++) {
        int selected_indices[k];
        int selected_count = 0;
        for (int i = 0; i < n; i++) {
            if ((mask & (1u << i)) != 0) {
                if (selected_count < k) selected_indices[selected_count] = i;
                selected_count++;
            }
        }
        if (selected_count != k) continue;
        subset_count++;

        uint8_t matrix[k][k];
        for (int row = 0; row < k; row++) {
            int shard_index = selected_indices[row];
            for (int col = 0; col < k; col++) {
                matrix[row][col] = shard_index < k
                    ? (uint8_t)(shard_index == col)
                    : (uint8_t)gf_inv((shard_index - k) ^ (m + col));
            }
        }

        uint8_t inverse[k][k];
        if (invert_gf_matrix(matrix, inverse) != 0) return 1;
        for (int original = 0; original < k; original++) {
            for (int byte = 0; byte < shard_bytes; byte++) {
                uint8_t recovered = 0;
                for (int row = 0; row < k; row++) {
                    recovered ^= (uint8_t)gf_mul(
                        inverse[original][row],
                        shards[selected_indices[row]][byte]);
                }
                if (recovered != data[original * shard_bytes + byte]) return 1;
            }
        }
    }
    return subset_count == 35 ? 0 : 1;
}

int main(void)
{
    gf256_init();
    static const char *modes[] = {"qr", "hcc2d4", "hcc2d8"};
    static const char levels[] = {'L', 'M', 'Q', 'H'};
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
        for (size_t e = 0; e < sizeof(levels) / sizeof(levels[0]); e++)
            for (int version = 1; version <= 40; version++)
                if (check_symbol_boundary(modes[m], levels[e], version) != 0)
                    return 1;
    if (check_stream_header() != 0) return 1;
    if (check_capacity_rates() != 0) return 1;
    if (check_parity_ratio() != 0) return 1;
    if (check_filename_handling() != 0) return 1;
    if (check_outer_erasure_recovery() != 0) return 1;

    g_shard_data_bytes = 32;
    uint8_t data[64] = {0};
    rs_encode_parity(data, NULL, 2, 0);
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

cat >"${TEST_DIR}/main_harness.c" <<'EOF'
#define main hcc2d_streamer_cli_main
#include "single_file_c_hcc2d_streamer_v0.9.0.c"
#undef main

static Uint32 request_quit(Uint32 interval, void *parameter)
{
    (void)parameter;
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_QUIT;
    return SDL_PushEvent(&event) == 1 ? 0 : interval;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) != 0) return 2;
    if (SDL_setenv("SDL_RENDER_DRIVER", "software", 1) != 0) return 2;
    if (SDL_Init(SDL_INIT_TIMER) != 0) return 2;
    if (SDL_AddTimer(500, request_quit, NULL) == 0) {
        SDL_Quit();
        return 2;
    }

    char *streamer_argv[] = {
        (char *)"hcc2d_streamer",
        argv[1], NULL
    };
    return hcc2d_streamer_cli_main(2, streamer_argv);
}
EOF

MAIN_HARNESS="${TEST_DIR}/main_harness"
"${CC_BIN}" "${CFLAGS[@]}" "${SDL_CFLAGS[@]}" -I"${ROOT_DIR}" \
    -o "${MAIN_HARNESS}" "${TEST_DIR}/main_harness.c" \
    "${SDL_LIBS[@]}" -lz -lm

printf 'HCC2D Streamer main-path smoke test\n' >"${TEST_DIR}/input.txt"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "${MAIN_HARNESS}" "${TEST_DIR}/input.txt" \
    >"${TEST_DIR}/main.out" 2>"${TEST_DIR}/main.err"
grep -q 'Symbol: hcc2d8, EC M, version 33, display fps: 12' "${TEST_DIR}/main.out"
grep -q 'Ready. Streaming at 12 fps.' "${TEST_DIR}/main.out"
test ! -s "${TEST_DIR}/main.err"

truncate -s 2097152 "${TEST_DIR}/max-size.bin"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "${MAIN_HARNESS}" "${TEST_DIR}/max-size.bin" \
    >"${TEST_DIR}/max-size.out" 2>"${TEST_DIR}/max-size.err"
grep -q 'Symbol: hcc2d8, EC M, version 33, display fps: 12' \
    "${TEST_DIR}/max-size.out"
grep -q 'Ready. Streaming at 12 fps.' "${TEST_DIR}/max-size.out"
test ! -s "${TEST_DIR}/max-size.err"

printf 'PASS: build=1 help=1 invalid_inputs=18 supported_fps=4 boundary_encodes=480 protocol=1 option_model=1 parity_ratio=1 palette=1 filenames=1 erasure_subsets=35 main=1 max_input=1 sanitizer=%s\n' \
    "${SANITIZE:-0}"
